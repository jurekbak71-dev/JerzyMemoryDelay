// Jerzy MultiFX 0.2 - JACK modular engine.
// All module loading, graph editing and deletion happen off JACK's audio thread.
#include "api.h"
#include <jack/jack.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t quit=0;
void signalHandler(int){quit=1;}
constexpr unsigned MAX=JFX_MAX_SLOTS;
constexpr unsigned PARAMS=JFX_MAX_PARAMS;
bool validKey(const std::string& s){
 if(s.empty()||s.size()>48)return false;
 for(char c:s)if(!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='_')return false;
 return true;
}
bool finite(float f){return std::isfinite(f);}
struct Slot {
 int id=0;
 void* library=nullptr;
 const JfxDescriptor* plugin=nullptr;
 void* instance=nullptr;
 std::array<std::atomic<float>,PARAMS> params{};
 std::atomic<bool> enabled{true};
 // Only read/written by JACK callback.
 float fade=1.f;
 ~Slot(){
  if(instance&&plugin&&plugin->destroy)plugin->destroy(instance);
  if(library)dlclose(library);
 }
};
struct Graph {
 unsigned count=0;
 std::array<Slot*,MAX> slots{};
};
struct Engine {
 jack_client_t* jack=nullptr;
 jack_port_t *in=nullptr,*left=nullptr,*right=nullptr;
 std::string moduleDir;
 std::atomic<Graph*> current{nullptr};
 std::atomic<bool> processing{false};
 std::atomic<bool> jackLost{false};
 std::atomic<double> bpm{120.};
 std::atomic<float> master{.7f};
 float level=0.7f; // JACK-thread only smooth master value
 std::vector<std::unique_ptr<Slot>> owned;
 int nextId=1;
 Engine(const std::string& directory):moduleDir(directory){
  current.store(new Graph);
 }
 ~Engine() {
  delete current.exchange(nullptr);
  owned.clear();
 }
 static void shutdown(void* opaque){
  static_cast<Engine*>(opaque)->jackLost.store(true);
 }
 static int audio(jack_nframes_t frames,void* opaque) {
  auto& e=*static_cast<Engine*>(opaque);
  e.processing.store(true); // SC: controller never frees a graph while in-use.
  const Graph* graph=e.current.load();
  auto* input=static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(e.in,frames));
  auto* l=static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(e.left,frames));
  auto* r=static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(e.right,frames));
  if(!l||!r||!graph){e.processing.store(false);return 0;}
  struct Local {Slot* slot; std::array<float,PARAMS> p;bool enabled;};
  std::array<Local,MAX> block{};
  for(unsigned k=0;k<graph->count;++k) {
   block[k].slot=graph->slots[k];
   block[k].enabled=block[k].slot->enabled.load(std::memory_order_relaxed);
   for(unsigned j=0;j<block[k].slot->plugin->parameter_count;++j)
    block[k].p[j]=block[k].slot->params[j].load(std::memory_order_relaxed);
  }
  const double tempo=e.bpm.load(std::memory_order_relaxed);
  const float targetMaster=e.master.load(std::memory_order_relaxed);
  const float stepMaster=1.f/(.010f*float(jack_get_sample_rate(e.jack)));
  const float step=stepMaster;
  for(jack_nframes_t n=0;n<frames;++n){
   const float mono=input&&finite(input[n])?input[n]:0.f;
   float y[2]{mono,mono};
   for(unsigned k=0;k<graph->count;++k){
    auto& item=block[k];Slot* slot=item.slot;
    float dry[2]{y[0],y[1]},wet[2]{y[0],y[1]};
    // Process while bypassed so reactivation preserves effect history.
    slot->plugin->process(slot->instance,dry,wet,item.p.data(),tempo);
    const float target=item.enabled?1.f:0.f;
    slot->fade+=std::clamp(target-slot->fade,-step,step);
    const float mix=slot->fade;
    for(unsigned c=0;c<2;++c){
     const float value=dry[c]*(1.f-mix)+wet[c]*mix;
     y[c]=finite(value)?std::clamp(value,-8.f,8.f):0.f;
    }
   }
   e.level+=std::clamp(targetMaster-e.level,-stepMaster,stepMaster);
   const float vol=1.4f*e.level;
   l[n]=std::clamp(y[0]*vol,-1.f,1.f);r[n]=std::clamp(y[1]*vol,-1.f,1.f);
  }
  e.processing.store(false);
  return 0;
 }
 void replaceGraph(std::unique_ptr<Graph> next) {
  Graph* old=current.exchange(next.release());
  // Once the callback is idle after swap, old Graph is unreachable.
  // The JACK callback sets processing=true BEFORE reading current.
  while(processing.load())usleep(1000);
  delete old;
 }
 std::unique_ptr<Slot> openModule(const std::string& key,std::string& error) {
  if(!validKey(key)){error="Invalid effect name";return nullptr;}
  const auto path=std::filesystem::path(moduleDir)/(key+".so");
  if(!std::filesystem::is_regular_file(path)){error="Module file unavailable";return nullptr;}
  void* lib=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
  if(!lib){const char* info=dlerror();error=info?info:"dlopen failed";return nullptr;}
  auto entry=reinterpret_cast<JfxEntryPoint>(dlsym(lib,"jfx_plugin"));
  const JfxDescriptor* d=entry?entry():nullptr;
  if(!d||d->abi_version!=JFX_ABI_VERSION||!d->key||key!=d->key||
    d->parameter_count>PARAMS||!d->create||!d->destroy||!d->process||
    (d->parameter_count&&!d->parameters)){
   dlclose(lib);error="Invalid or incompatible module ABI";return nullptr;
  }
  const double sr=jack?jack_get_sample_rate(jack):44100.;
  void* ctx=d->create(sr);
  if(!ctx){dlclose(lib);error="Cannot create effect";return nullptr;}
  auto slot=std::make_unique<Slot>();
  slot->id=nextId++;slot->library=lib;slot->plugin=d;slot->instance=ctx;
  for(unsigned i=0;i<d->parameter_count;++i){
   float v=d->parameters[i].default_value;
   slot->params[i].store(finite(v)?std::clamp(v,0.f,1.f):0.f);
  }
  return slot;
 }
 Slot* find(int id){
  for(auto& s:owned)if(s->id==id)return s.get();
  return nullptr;
 }
 std::string add(const std::string& key) {
  Graph* prev=current.load();
  if(prev->count>=MAX)return "ERR Chain full";
  std::string error;
  auto slot=openModule(key,error);
  if(!slot)return "ERR "+error;
  auto next=std::make_unique<Graph>(*prev);
  const int id=slot->id;
  next->slots[next->count++]=slot.get();
  owned.push_back(std::move(slot));
  replaceGraph(std::move(next));
  return "OK "+std::to_string(id);
 }
 std::string remove(int id) {
  Graph* prev=current.load();
  auto next=std::make_unique<Graph>();
  bool found=false;
  for(unsigned i=0;i<prev->count;++i) {
   if(prev->slots[i]->id==id)found=true;
   else next->slots[next->count++]=prev->slots[i];
  }
  if(!found)return "ERR Unknown instance";
  replaceGraph(std::move(next));
  owned.erase(std::remove_if(owned.begin(),owned.end(),[id](const auto& s){return s->id==id;}),owned.end());
  return "OK";
 }
 std::string move(int id,int position) {
  Graph* prev=current.load();
  if(position<0||unsigned(position)>=prev->count)return "ERR Invalid position";
  auto next=std::make_unique<Graph>();
  Slot* target=nullptr;
  for(unsigned i=0;i<prev->count;++i)if(prev->slots[i]->id==id)target=prev->slots[i];
  if(!target)return "ERR Unknown instance";
  for(unsigned i=0;i<prev->count;++i){
   Slot* s=prev->slots[i];
   if(s!=target)next->slots[next->count++]=s;
  }
  for(int i=int(next->count);i>position;--i)next->slots[i]=next->slots[i-1];
  next->slots[position]=target;++next->count;
  replaceGraph(std::move(next));
  return "OK";
 }
 std::string reset(){
  replaceGraph(std::make_unique<Graph>());
  owned.clear();
  return "OK";
 }
 std::string command(const std::string& line){
  std::istringstream ss(line);std::string type,key,extra;
  int id,index,pos;float value;double tempo;
  if(!(ss>>type))return "ERR Empty command";
  if(type=="PING"&&!(ss>>extra))return "OK";
  if(type=="CLEAR"&&!(ss>>extra))return reset();
  if(type=="ADD"&&(ss>>key)&&!(ss>>extra))return add(key);
  if(type=="REMOVE"&&(ss>>id)&&!(ss>>extra))return remove(id);
  if(type=="MOVE"&&(ss>>id>>pos)&&!(ss>>extra))return move(id,pos);
  if(type=="ENABLE"&&(ss>>id>>index)&&!(ss>>extra)){
   Slot* slot=find(id);
   if(!slot||index<0||index>1)return "ERR Bad toggle";
   slot->enabled.store(index!=0);return "OK";
  }
  if(type=="SET"&&(ss>>id>>index>>value)&&!(ss>>extra)){
   Slot* slot=find(id);
   if(!slot||index<0||unsigned(index)>=slot->plugin->parameter_count||!finite(value)||value<0||value>1)
    return "ERR Bad parameter";
   slot->params[index].store(value);return "OK";
  }
  if(type=="MASTER"&&(ss>>value)&&!(ss>>extra)){
   if(!finite(value)||value<0||value>1)return "ERR Bad master";
   master.store(value);return "OK";
  }
  if(type=="BPM"&&(ss>>tempo)&&!(ss>>extra)){
   if(!std::isfinite(tempo)||tempo<20||tempo>300)return "ERR Bad tempo";
   bpm.store(tempo);return "OK";
  }
  return "ERR Invalid command";
 }
};
int control(Engine& engine){
 int fd=socket(AF_INET,SOCK_STREAM,0);
 if(fd<0)return 1;
 int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
 sockaddr_in addr{};addr.sin_family=AF_INET;
 addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(9811);
 if(bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))||listen(fd,4)){
  std::perror("Control socket 127.0.0.1:9811");close(fd);return 1;
 }
 timeval timeout{};timeout.tv_sec=1;timeout.tv_usec=0;
 setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 std::cout<<"Jerzy MultiFX v0.2 JACK / TCP 127.0.0.1:9811\n"<<std::flush;
 while(!quit&&!engine.jackLost.load()){
  sockaddr_in peer{};socklen_t peerSize=sizeof(peer);
  int client=accept(fd,reinterpret_cast<sockaddr*>(&peer),&peerSize);
  if(client<0){if(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)continue;break;}
  timeval receiveTimeout{};receiveTimeout.tv_sec=2;
  setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&receiveTimeout,sizeof(receiveTimeout));
  char buf[256]{};
  const ssize_t n=recv(client,buf,sizeof(buf)-1,0);
  std::string response="ERR Bad read";
  if(n>0){
   buf[n]=0;
   std::string line(buf,size_t(n));
   const auto i=line.find('\n');
   if(i!=std::string::npos)line.resize(i);
   response=engine.command(line);
  }
  response+="\n";
  send(client,response.data(),response.size(),MSG_NOSIGNAL);
  close(client);
 }
 close(fd);return 0;
}
}
int main(int argc,char** argv){
 std::string moduleDir="./build-multifx2/modules";
 if(argc==3&&std::strcmp(argv[1],"--modules")==0)moduleDir=argv[2];
 else if(argc!=1){std::cerr<<"Usage: JerzyMultiFX2JACK [--modules MODULE_DIR]\n";return 2;}
 signal(SIGINT,signalHandler);signal(SIGTERM,signalHandler);
 Engine engine(moduleDir);
 jack_status_t status{};
 engine.jack=jack_client_open("JerzyMultiFX2",static_cast<jack_options_t>(JackNoStartServer|JackUseExactName),&status);
 if(!engine.jack){std::cerr<<"No JACK server available\n";return 1;}
 engine.in=jack_port_register(engine.jack,"in_1",JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput,0);
 engine.left=jack_port_register(engine.jack,"out_1",JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
 engine.right=jack_port_register(engine.jack,"out_2",JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
 if(!engine.in||!engine.left||!engine.right){jack_client_close(engine.jack);return 1;}
 jack_set_process_callback(engine.jack,Engine::audio,&engine);
 jack_on_shutdown(engine.jack,Engine::shutdown,&engine);
 if(jack_activate(engine.jack)){jack_client_close(engine.jack);return 1;}
 const int result=control(engine);
 jack_deactivate(engine.jack);
 jack_client_close(engine.jack);
 engine.jack=nullptr;
 return result;
}
