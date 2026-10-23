#include "dsp.h"
#include <jack/jack.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace {
std::atomic<bool> running{true};
void stop(int) { running.store(false, std::memory_order_relaxed); }
bool parseNumber(const char* a, double& v) {
    try { std::string s(a); size_t n=0; v=std::stod(s,&n); return n==s.size() && std::isfinite(v); }
    catch (...) { return false; }
}
constexpr int port=9808;
struct App {
    jack_client_t* client{};
    jack_port_t* in{};
    jack_port_t* left{};
    jack_port_t* right{};
    Jerzy::Engine engine;
    std::array<std::atomic<double>,Jerzy::Count> control{};
    double bpm=120.;
    std::atomic<double> requestedBpm{120.};
    App() { for(int i=0;i<Jerzy::Count;++i) control[i].store(Jerzy::defaults[i]); }
    static int process(jack_nframes_t frames,void* user) {
        auto& app=*static_cast<App*>(user);
        auto* input=static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(app.in,frames));
        auto* a=static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(app.left,frames));
        auto* b=static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(app.right,frames));
        if(!input || !a || !b) return 0;
        Jerzy::Parameters p;
        for(int i=0;i<Jerzy::Count;++i) p.value[i]=app.control[i].load(std::memory_order_relaxed);
        const double tempo=app.requestedBpm.load(std::memory_order_relaxed);
        for(jack_nframes_t n=0;n<frames;++n) {
            double s=std::isfinite(input[n])?double(input[n]):0.;
            const double dry[2]{s,s}; double wet[2]{};
            app.engine.tick(dry,wet,2,p,tempo);
            a[n]=float(std::clamp(wet[0],-1.,1.));
            b[n]=float(std::clamp(wet[1],-1.,1.));
        }
        return 0;
    }
    static void shutdown(void*) { running.store(false); }
};
void controlLoop(App& app) {
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if(fd<0) { std::cerr<<"UDP control socket failed\n"; return; }
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=htons(port);
    if(bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0) {
        std::cerr<<"UDP control port "<<port<<" is already used\n"; close(fd); return;
    }
    timeval timeout{}; timeout.tv_sec=0; timeout.tv_usec=200000;
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    while(running.load()) {
        char buf[128]{};
        ssize_t n=recv(fd,buf,sizeof(buf)-1,0);
        if(n<=0) continue;
        buf[n]=0;
        int id=-1; double v=0;
        if(std::sscanf(buf,"%d %lf",&id,&v)!=2 || !std::isfinite(v)) continue;
        if(id==-1) {
            if(v>=10. && v<=1000.) app.requestedBpm.store(v,std::memory_order_relaxed);
        } else if(id>=0 && id<Jerzy::Count && v>=0. && v<=1.) {
            app.control[id].store(v,std::memory_order_relaxed);
        }
    }
    close(fd);
}
}
int main(int argc,char** argv) {
    App app;
    for(int i=1;i<argc;i+=2) {
        if(std::string(argv[i])=="--help") {
            std::cout<<"JerzyMemoryDelayJACK [--model 0..5] [--blend 0..1] [--feedback 0..1] [--delay-ms 20..550] [--level 0..1]\n";
            return 0;
        }
        if(i+1>=argc) return 2;
        double v=0; if(!parseNumber(argv[i+1],v)) return 2;
        const std::string k(argv[i]);
        int id=-1;
        if(k=="--model" && std::floor(v)==v && v>=0 && v<=5) {id=Jerzy::Model;v/=5.;}
        else if(k=="--blend") id=Jerzy::Blend;
        else if(k=="--feedback") id=Jerzy::Feedback;
        else if(k=="--level") id=Jerzy::Level;
        else if(k=="--delay-ms" && v>=20 && v<=550) {id=Jerzy::Delay;v=(v-20)/530.;}
        if(id<0 || v<0 || v>1) {std::cerr<<"Invalid parameter "<<k<<"\n";return 2;}
        app.control[id].store(v);
    }
    jack_status_t status{};
    app.client=jack_client_open("JerzyMemoryDelay",JackNoStartServer,&status);
    if(!app.client) {std::cerr<<"Start jackd first.\n";return 1;}
    app.in=jack_port_register(app.client,"in_1",JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput,0);
    app.left=jack_port_register(app.client,"out_1",JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
    app.right=jack_port_register(app.client,"out_2",JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
    if(!app.in||!app.left||!app.right) {jack_client_close(app.client);return 1;}
    app.engine.prepare(jack_get_sample_rate(app.client));
    jack_set_process_callback(app.client,App::process,&app);
    jack_on_shutdown(app.client,App::shutdown,nullptr);
    std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
    if(jack_activate(app.client)) {jack_client_close(app.client);return 1;}
    std::thread server(controlLoop,std::ref(app));
    std::cout<<"Jerzy Memory Delay JACK active at "<<jack_get_sample_rate(app.client)<<" Hz; control UDP 127.0.0.1:"<<port<<"\n";
    std::cout<<"Audio ports: JerzyMemoryDelay:in_1 / out_1 / out_2. Ctrl+C to stop.\n";
    while(running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    server.join();
    jack_deactivate(app.client);
    jack_client_close(app.client);
}
