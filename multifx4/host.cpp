// Jerzy MultiFX 0.2 - JACK modular engine.
// All module loading, graph editing and deletion happen off JACK's audio thread.
#include "../multifx2/api.h"
#include "bass_ambient.h"
#include "ir_cab.h"
#include <jack/midiport.h>
#include <mutex>
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
 std::atomic<int> route{0}; // 0 guitar, 1 BassAmbient; duplicate module instances if needed
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
 jack_port_t *in=nullptr,*left=nullptr,*right=nullptr,*midi=nullptr;
 std::string moduleDir;
 std::atomic<Graph*> current{nullptr};
 std::atomic<bool> processing{false};
 std::atomic<bool> jackLost{false};
 std::atomic<double> bpm{120.};
 std::atomic<float> master{.7f};
 float level=0.7f; // JACK-thread only smooth master value

 // Parallel generative synthesizer (reuses original BassAmbient DSP).
 mfx4::BassAmbient bass;
 std::atomic<bool> bassOn{false},padOn{false};
 std::atomic<int> root{40},bassStyle{5},bassScale{1},padMode{2},bassModel{0},padChord{1},padEngine1{0},padEngine2{1};
 std::atomic<bool> synthSync{true};
 std::atomic<float> freeBpm{120.f},evolution{.5f},bassFx{.15f},padFx{.4f};
 std::atomic<float> bassVolume{.6f},padVolume{.35f},density{.75f},synthVolume{.35f},synthPan{0.f};
 // CAB/IR runs exclusively on guitar path, BEFORE summing BassAmbient.
 std::array<std::atomic<mfx4::CabinetIR*>,2> cabs{};
 std::array<std::array<mfx4::CabinetFilter,2>,2> cabFilters{};
 std::atomic<bool> cabOn{false};
 std::atomic<float> cabMix{1.f},cabBlend{0.f},cabLevel{.5f},cabLowCut{.12f},cabHighCut{.85f};
 float cabFade=0.f;
 float synthFade=0.f;
 struct MidiBinding {
  std::atomic<int> channel{-1},cc{-1},kind{-1},id{-1},index{-1};
 };
 std::array<MidiBinding,128> bindings{};
 std::atomic<uint64_t> midiSequence{0};
 std::atomic<int> midiChannel{0},midiController{0},midiValue{0};
 std::string irRoot="/home/jerzy/.config/jerzy-multifx4/irs";

 std::vector<std::unique_ptr<Slot>> owned;
 int nextId=1;
 Engine(const std::string& directory):moduleDir(directory){
  current.store(new Graph);
 }
 ~Engine() {
  delete current.exchange(nullptr);
  owned.clear();
  for(auto& c:cabs){delete c.exchange(nullptr);}
 }
 static void shutdown(void* opaque){
  static_cast<Engine*>(opaque)->jackLost.store(true);
 }
 static int audio(jack_nframes_t frames,void* opaque) {
  auto& e=*static_cast<Engine*>(opaque);
  e.processing.store(true); // SC: controller never frees a graph while in-use.
  const Graph* graph=e.current.load();
  auto* input=static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(e.in,frames));
  void* midiBuffer=e.midi?jack_port_get_buffer(e.midi,frames):nullptr;
  auto* l=static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(e.left,frames));
  auto* r=static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(e.right,frames));
  if(!l||!r||!graph){e.processing.store(false);return 0;}
  struct Local {Slot* slot; std::array<float,PARAMS> p;bool enabled;int route;};
  std::array<Local,MAX> block{};
  for(unsigned k=0;k<graph->count;++k) {
   block[k].slot=graph->slots[k];
   block[k].enabled=block[k].slot->enabled.load(std::memory_order_relaxed);
   block[k].route=block[k].slot->route.load(std::memory_order_relaxed);
   for(unsigned j=0;j<block[k].slot->plugin->parameter_count;++j)
    block[k].p[j]=block[k].slot->params[j].load(std::memory_order_relaxed);
  }
  const double tempo=e.bpm.load(std::memory_order_relaxed);
  const float targetMaster=e.master.load(std::memory_order_relaxed);
  const float stepMaster=1.f/(.010f*float(jack_get_sample_rate(e.jack)));
  const float step=stepMaster;
  // Snapshot coherent synth parameters once per callback.
  e.bass.bassOn=e.bassOn.load();e.bass.padOn=e.padOn.load();
  e.bass.settings.style=e.bassStyle.load();e.bass.settings.scale=e.bassScale.load();
  e.bass.settings.model=e.bassModel.load();e.bass.settings.chord=e.padChord.load();
  e.bass.settings.engine1=e.padEngine1.load();e.bass.settings.engine2=e.padEngine2.load();
  e.bass.settings.evolution=1.+59.*e.evolution.load();
  e.bass.settings.bassFX=e.bassFx.load();e.bass.settings.padFX=e.padFx.load();
  e.bass.settings.padMode=e.padMode.load();
  e.bass.settings.density=e.density.load();
  e.bass.settings.bassLevel=e.bassVolume.load();
  e.bass.settings.padLevel=e.padVolume.load();
  e.bass.gain=e.synthVolume.load();e.bass.pan=e.synthPan.load();
  e.bass.setRoot(e.root.load());
  const bool irEnabled=e.cabOn.load();
  const float wet=std::clamp(e.cabMix.load(),0.f,1.f);
  const float blend=std::clamp(e.cabBlend.load(),0.f,1.f);
  const float cabinetGain=std::pow(10.f,(e.cabLevel.load()*36.f-18.f)/20.f);
  const float lowCut=30.f*std::pow(10.f,e.cabLowCut.load()*1.6f);
  const float highCut=1000.f*std::pow(10.f,e.cabHighCut.load()*1.45f);
  const double sr=jack_get_sample_rate(e.jack);
  const uint32_t eventCount=midiBuffer?jack_midi_get_event_count(midiBuffer):0;
  uint32_t midiIndex=0;
  for(jack_nframes_t n=0;n<frames;++n){
   while(midiIndex<eventCount){
    jack_midi_event_t event{};
    if(jack_midi_event_get(&event,midiBuffer,midiIndex))break;
    if(event.time>n)break;
    ++midiIndex;
    if(event.size<3 || (event.buffer[0]&0xf0)!=0xb0)continue;
    int ch=int(event.buffer[0]&15)+1,cc=int(event.buffer[1]&127),val=int(event.buffer[2]&127);
    e.midiChannel.store(ch,std::memory_order_relaxed);e.midiController.store(cc,std::memory_order_relaxed);
    e.midiValue.store(val,std::memory_order_relaxed);e.midiSequence.fetch_add(1,std::memory_order_release);
    const float norm=float(val)/127.f;
    for(auto& bind:e.bindings) {
      if(bind.channel.load(std::memory_order_acquire)!=ch || bind.cc.load()!=cc)continue;
      int kind=bind.kind.load(),id=bind.id.load(),idx=bind.index.load();
      if(kind==0){ // effect parameter
       for(unsigned k=0;k<graph->count;++k)
        if(graph->slots[k]->id==id && idx>=0 && unsigned(idx)<graph->slots[k]->plugin->parameter_count)
         graph->slots[k]->params[unsigned(idx)].store(norm,std::memory_order_relaxed);
      }else if(kind==1)e.master.store(norm);
      else if(kind==2)e.bpm.store(20.f+280.f*norm);
      else if(kind==3)e.cabOn.store(val>=64);
      else if(kind==4)e.cabMix.store(norm);
      else if(kind==5)e.cabBlend.store(norm);
      else if(kind==6)e.cabLevel.store(norm);
      else if(kind==7)e.cabLowCut.store(norm);
      else if(kind==8)e.cabHighCut.store(norm);
      else if(kind==9)e.bassOn.store(val>=64);
      else if(kind==10)e.padOn.store(val>=64);
      else if(kind==11)e.root.store(24+int(std::lround(norm*60.f)));
      else if(kind==12)e.bassStyle.store(int(norm*6.f));
      else if(kind==13)e.bassScale.store(int(norm*4.f));
      else if(kind==14)e.density.store(norm);
      else if(kind==15)e.bassVolume.store(norm);
      else if(kind==16)e.padVolume.store(norm);
      else if(kind==17)e.synthVolume.store(norm);
      else if(kind==18)e.synthPan.store(norm*2.f-1.f);
      else if(kind==19)e.bassModel.store(std::min(2,int(norm*3.f)));
      else if(kind==20)e.padChord.store(std::min(4,int(norm*5.f)));
      else if(kind==21)e.padEngine1.store(std::min(4,int(norm*5.f)));
      else if(kind==22)e.padEngine2.store(std::min(4,int(norm*5.f)));
      else if(kind==23)e.evolution.store(norm);
      else if(kind==24)e.bassFx.store(norm);
      else if(kind==25)e.padFx.store(norm);
      else if(kind==26)e.synthSync.store(val>=64);
      else if(kind==27)e.freeBpm.store(20.f+280.f*norm);
     }
   }
   const float mono=input&&finite(input[n])?input[n]:0.f;
   float y[2]{mono,mono};
   // Original BassAmbient generator is a separate source. FX route selects
   // guitar OR synth before the cabinet and before the shared stereo mix.
   float synth[2]{};
   const float synthTarget=(e.bass.bassOn||e.bass.padOn)?1.f:0.f;
   e.synthFade+=std::clamp(synthTarget-e.synthFade,-step,step);
   if(synthTarget>0.f||e.synthFade>0.f)
    e.bass.process(synth,e.synthSync.load()?e.bpm.load(std::memory_order_relaxed):e.freeBpm.load(std::memory_order_relaxed));
   for(unsigned k=0;k<graph->count;++k){
    auto& item=block[k];Slot* slot=item.slot;
    float* bus=item.route==1?synth:y;
    float dry[2]{bus[0],bus[1]},wet[2]{bus[0],bus[1]};
    // Process while bypassed so reactivation preserves effect history.
    slot->plugin->process(slot->instance,dry,wet,item.p.data(),tempo);
    const float target=item.enabled?1.f:0.f;
    slot->fade+=std::clamp(target-slot->fade,-step,step);
    const float mix=slot->fade;
    for(unsigned c=0;c<2;++c){
     const float value=dry[c]*(1.f-mix)+wet[c]*mix;
     bus[c]=finite(value)?std::clamp(value,-8.f,8.f):0.f;
    }
   }
   // True cabinet IR, two independent slots; guitar only.
   float guitar[2]{y[0],y[1]};
   float cabinet[2][2]{{0,0},{0,0}};
   for(int i=0;i<2;++i){
    if(auto* cab=e.cabs[size_t(i)].load(std::memory_order_acquire)) {
     cab->tick(guitar,cabinet[i]);
     for(int ch=0;ch<2;++ch)
      cabinet[i][ch]=e.cabFilters[size_t(i)][size_t(ch)].tick(cabinet[i][ch],sr,lowCut,highCut);
    }
   }
   const bool hasCab=e.cabs[0].load()||e.cabs[1].load();
   const float targetCab=irEnabled&&hasCab?1.f:0.f;
   e.cabFade+=std::clamp(targetCab-e.cabFade,-step,step);
   for(int ch=0;ch<2;++ch){
    const float a=e.cabs[0]?cabinet[0][ch]:0.f;
    const float b=e.cabs[1]?cabinet[1][ch]:0.f;
    const float processed=(a*(1.f-blend)+b*blend)*cabinetGain;
    guitar[ch]=guitar[ch]*(1.f-wet*e.cabFade)+processed*(wet*e.cabFade);
   }
   // Shared mixer is after guitar-only CAB; synth FX stay unfiltered.
   y[0]=std::clamp(guitar[0]+synth[0]*e.synthFade,-8.f,8.f);
   y[1]=std::clamp(guitar[1]+synth[1]*e.synthFade,-8.f,8.f);
   e.level+=std::clamp(e.master.load(std::memory_order_relaxed)-e.level,-stepMaster,stepMaster);
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
 std::string loadCab(int which,const std::string& name){
  if(which<0||which>1||name.empty()||name.size()>100)return "ERR Bad IR name";
  // Only files in a dedicated local directory, no path traversal.
  for(unsigned char ch:name)
   if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||
       ch=='_'||ch=='-'||ch=='.'))return "ERR Bad IR name";
  if(name=="."||name==".."||name.size()<5||name.substr(name.size()-4)!=".wav")return "ERR WAV only";
  std::string error;
  const auto path=std::filesystem::path(irRoot)/name;
  auto next=mfx4::CabinetIR::load(path.string(),jack_get_sample_rate(jack),error);
  if(!next)return "ERR "+error;
  auto* old=cabs[size_t(which)].exchange(next.release(),std::memory_order_acq_rel);
  while(processing.load(std::memory_order_acquire))usleep(1000);
  delete old;
  return "OK";
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
  if(type=="IR_LOAD"){
    int which;std::string name;
    if(ss>>which>>name && !(ss>>extra))return loadCab(which,name);
  }

  if(type=="IR_CLEAR"){
    int which;
    if(ss>>which && !(ss>>extra) && which>=0 && which<2) {
      auto* old=cabs[size_t(which)].exchange(nullptr);
      while(processing.load())usleep(1000);
      delete old;
      return "OK";
    }
  }
  if(type=="GET"){
    int slotId,index;
    if(ss>>slotId>>index && !(ss>>extra)) {
      Slot* fx=find(slotId);
      if(!fx||index<0||unsigned(index)>=fx->plugin->parameter_count)return "ERR Bad FX target";
      return "OK "+std::to_string(fx->params[unsigned(index)].load());
    }
  }
  if(type=="GET_MASTER" && !(ss>>extra))return "OK "+std::to_string(master.load());
  if(type=="GET_BPM" && !(ss>>extra))return "OK "+std::to_string(bpm.load());
  if(type=="GET_CAB"){
    std::string key;
    if(ss>>key && !(ss>>extra)){
      float v;
      if(key=="on")v=cabOn.load()?1.f:0.f;
      else if(key=="mix")v=cabMix.load();
      else if(key=="blend")v=cabBlend.load();
      else if(key=="level")v=cabLevel.load();
      else if(key=="lowcut")v=cabLowCut.load();
      else if(key=="highcut")v=cabHighCut.load();
      else return "ERR Bad CAB key";
      return "OK "+std::to_string(v);
    }
  }
  if(type=="GET_SYNTH"){
    std::string key;
    if(ss>>key && !(ss>>extra)){
      float v;
      if(key=="bass")v=bassOn.load()?1.f:0.f;
      else if(key=="pad")v=padOn.load()?1.f:0.f;
      else if(key=="root")v=float(root.load()-24)/60.f;
      else if(key=="style")v=float(bassStyle.load())/6.f;
      else if(key=="scale")v=float(bassScale.load())/4.f;
      else if(key=="mode")v=float(padMode.load())/2.f;
      else if(key=="density")v=density.load();
      else if(key=="basslevel")v=bassVolume.load();
      else if(key=="padlevel")v=padVolume.load();
      else if(key=="level")v=synthVolume.load();
      else if(key=="pan")v=(synthPan.load()+1.f)/2.f;
      else if(key=="model")v=bassModel.load()/2.f;
      else if(key=="chord")v=padChord.load()/4.f;
      else if(key=="engine1")v=padEngine1.load()/4.f;
      else if(key=="engine2")v=padEngine2.load()/4.f;
      else if(key=="evolution")v=evolution.load();
      else if(key=="bassfx")v=bassFx.load();
      else if(key=="padfx")v=padFx.load();
      else if(key=="sync")v=synthSync.load()?1.f:0.f;
      else if(key=="freebpm")v=(freeBpm.load()-20.f)/280.f;
      else return "ERR Bad synth key";
      return "OK "+std::to_string(v);
    }
  }
  if(type=="MIDI_LAST"&&!(ss>>extra)){
    return "OK "+std::to_string(midiSequence.load())+" "+std::to_string(midiChannel.load())+
     " "+std::to_string(midiController.load())+" "+std::to_string(midiValue.load());
  }
  if(type=="MIDI_BIND"){
    int ch,cc,kind,slotId,paramId;
    if(ss>>ch>>cc>>kind>>slotId>>paramId && !(ss>>extra) &&
       ch>=1&&ch<=16&&cc>=0&&cc<=127&&kind>=0&&kind<=27){
     if(kind==0){
       Slot* fx=find(slotId);
       if(!fx||paramId<0||unsigned(paramId)>=fx->plugin->parameter_count)return "ERR Bad FX param";
     }
     for(auto& m:bindings){
       if(m.channel.load()==ch&&m.cc.load()==cc){
        m.channel.store(-1);m.cc.store(cc);m.kind.store(kind);m.id.store(slotId);
        m.index.store(paramId);m.channel.store(ch,std::memory_order_release);return "OK";
       }
     }
     for(auto& m:bindings){
       if(m.channel.load()==-1){
        m.cc.store(cc);m.kind.store(kind);m.id.store(slotId);
        m.index.store(paramId);m.channel.store(ch,std::memory_order_release);return "OK";
       }
     }
     return "ERR MIDI mapping full";
    }
  }
  if(type=="MIDI_CLEAR"&&!(ss>>extra)){
    for(auto& m:bindings)m.channel.store(-1);return "OK";
  }
  if(type=="CAB"){
    std::string field;float v;
    if(ss>>field>>v && !(ss>>extra) && finite(v)&&v>=0&&v<=1){
      if(field=="on")cabOn.store(v>=.5f);
      else if(field=="mix")cabMix.store(v);
      else if(field=="blend")cabBlend.store(v);
      else if(field=="level")cabLevel.store(v);
      else if(field=="lowcut")cabLowCut.store(v);
      else if(field=="highcut")cabHighCut.store(v);
      else return "ERR Bad CAB key";
      return "OK";
    }
  }
  if(type=="SYNTH"){
    std::string field;float v;
    if(ss>>field>>v && !(ss>>extra) && finite(v)&&v>=0&&v<=1){
      if(field=="bass")bassOn.store(v>=.5f);
      else if(field=="pad")padOn.store(v>=.5f);
      else if(field=="root")root.store(24+int(std::lround(v*60.f)));
      else if(field=="style")bassStyle.store(std::min(6,int(v*7.f)));
      else if(field=="scale")bassScale.store(std::min(4,int(v*5.f)));
      else if(field=="mode")padMode.store(std::min(2,int(v*3.f)));
      else if(field=="density")density.store(v);
      else if(field=="basslevel")bassVolume.store(v);
      else if(field=="padlevel")padVolume.store(v);
      else if(field=="level")synthVolume.store(v);
      else if(field=="pan")synthPan.store(v*2.f-1.f);
      else if(field=="model")bassModel.store(std::min(2,int(v*3.f)));
      else if(field=="chord")padChord.store(std::min(4,int(v*5.f)));
      else if(field=="engine1")padEngine1.store(std::min(4,int(v*5.f)));
      else if(field=="engine2")padEngine2.store(std::min(4,int(v*5.f)));
      else if(field=="evolution")evolution.store(v);
      else if(field=="bassfx")bassFx.store(v);
      else if(field=="padfx")padFx.store(v);
      else if(field=="sync")synthSync.store(v>=.5f);
      else if(field=="freebpm")freeBpm.store(20.f+280.f*v);
      else return "ERR Bad SYNTH key";
      return "OK";
    }
  }
  if(type=="CLEAR"&&!(ss>>extra))return reset();
  if(type=="ADD"&&(ss>>key)&&!(ss>>extra))return add(key);
  if(type=="REMOVE"&&(ss>>id)&&!(ss>>extra))return remove(id);
  if(type=="MOVE"&&(ss>>id>>pos)&&!(ss>>extra))return move(id,pos);
  if(type=="ENABLE"&&(ss>>id>>index)&&!(ss>>extra)){
   Slot* slot=find(id);
   if(!slot||index<0||index>1)return "ERR Bad toggle";
   slot->enabled.store(index!=0);return "OK";
  }
  if(type=="ROUTE"&&(ss>>id>>index)&&!(ss>>extra)){
   Slot* slot=find(id);
   if(!slot||index<0||index>1)return "ERR Bad FX route";
   slot->route.store(index,std::memory_order_relaxed);
   return "OK";
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
 std::cout<<"Jerzy MultiFX v0.4 / dual engine CAB IR MIDI 127.0.0.1:9811\n"<<std::flush;
 while(!quit&&!engine.jackLost.load()){
  sockaddr_in peer{};socklen_t peerSize=sizeof(peer);
  int client=accept(fd,reinterpret_cast<sockaddr*>(&peer),&peerSize);
  if(client<0){if(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)continue;break;}
  timeval receiveTimeout{};receiveTimeout.tv_sec=2;
  setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&receiveTimeout,sizeof(receiveTimeout));
  char buf[512]{};
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
 engine.midi=jack_port_register(engine.jack,"midi_in",JACK_DEFAULT_MIDI_TYPE,JackPortIsInput,0);
 if(!engine.in||!engine.left||!engine.right||!engine.midi){jack_client_close(engine.jack);return 1;}
 engine.bass.prepare(jack_get_sample_rate(engine.jack));
 jack_set_process_callback(engine.jack,Engine::audio,&engine);
 jack_on_shutdown(engine.jack,Engine::shutdown,&engine);
 if(jack_activate(engine.jack)){jack_client_close(engine.jack);return 1;}
 const int result=control(engine);
 jack_deactivate(engine.jack);
 jack_client_close(engine.jack);
 engine.jack=nullptr;
 return result;
}
