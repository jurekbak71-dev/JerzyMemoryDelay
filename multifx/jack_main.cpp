#include "dsp.h"
#include <jack/jack.h>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t sigstop=0;
void onSignal(int){sigstop=1;}
struct App {
    jack_client_t* client=nullptr;
    jack_port_t* input=nullptr;
    jack_port_t* left=nullptr;
    jack_port_t* right=nullptr;
    JerzyMultiFX::Engine engine;
    std::array<std::atomic<double>,JerzyMultiFX::Params> p{};
    std::array<std::atomic<int>,JerzyMultiFX::Effects> on{};
    std::atomic<unsigned> order{0x3210u};
    std::atomic<double> bpm{120.};
    std::atomic<bool> disconnected{false};
    App(){
        for(int i=0;i<JerzyMultiFX::Params;++i)
            p[i].store(JerzyMultiFX::defaults[i]);
        for(auto& item:on)item.store(1);
    }
    static int process(jack_nframes_t n,void* opaque){
        auto& a=*static_cast<App*>(opaque);
        auto* in=static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(a.input,n));
        auto* l=static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(a.left,n));
        auto* r=static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(a.right,n));
        if(!l||!r)return 0;
        JerzyMultiFX::State state;
        for(int i=0;i<JerzyMultiFX::Params;++i)
            state.values[i]=a.p[i].load(std::memory_order_relaxed);
        for(int i=0;i<JerzyMultiFX::Effects;++i)
            state.enabled[i]=a.on[i].load(std::memory_order_relaxed)!=0;
        const unsigned packed=a.order.load(std::memory_order_relaxed);
        for(int i=0;i<JerzyMultiFX::Effects;++i)
            state.order[i]=int((packed>>(i*4))&15u);
        state.bpm=a.bpm.load(std::memory_order_relaxed);
        for(jack_nframes_t i=0;i<n;++i){
            double output[2]{0.,0.};
            a.engine.tick(in?double(in[i]):0.,output,state);
            l[i]=float(output[0]);r[i]=float(output[1]);
        }
        return 0;
    }
    static void shutdown(void* ptr){
        static_cast<App*>(ptr)->disconnected.store(true,std::memory_order_relaxed);
    }
};
bool applyMessage(App& app,const char* input){
    int a,b,c,d;double value;
    if(std::sscanf(input,"P %d %lf",&a,&value)==2){
        if(a<0||a>=JerzyMultiFX::Params||!std::isfinite(value)||value<0.||value>1.)return false;
        app.p[a].store(value,std::memory_order_relaxed);return true;
    }
    if(std::sscanf(input,"E %d %d",&a,&b)==2){
        if(a<0||a>=JerzyMultiFX::Effects||(b!=0&&b!=1))return false;
        app.on[a].store(b,std::memory_order_relaxed);return true;
    }
    if(std::sscanf(input,"O %d %d %d %d",&a,&b,&c,&d)==4){
        const std::array<int,JerzyMultiFX::Effects> order{{a,b,c,d}};
        if(!JerzyMultiFX::Engine::validOrder(order))return false;
        const unsigned packed=unsigned(a)|(unsigned(b)<<4)|(unsigned(c)<<8)|(unsigned(d)<<12);
        app.order.store(packed,std::memory_order_relaxed);return true;
    }
    if(std::sscanf(input,"B %lf",&value)==1){
        if(!std::isfinite(value)||value<20.||value>300.)return false;
        app.bpm.store(value,std::memory_order_relaxed);return true;
    }
    return false;
}
}
int main(){
    std::signal(SIGINT,onSignal);
    std::signal(SIGTERM,onSignal);
    int fd=::socket(AF_INET,SOCK_DGRAM,0);
    if(fd<0){std::perror("UDP socket");return 1;}
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=htons(9810);
    if(::bind(fd,reinterpret_cast<const sockaddr*>(&address),sizeof(address))<0){
        std::perror("UDP control bind 127.0.0.1:9810");::close(fd);return 1;
    }
    timeval timeout{};timeout.tv_sec=0;timeout.tv_usec=200000;
    ::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    App app;
    jack_status_t status{};
    app.client=jack_client_open("JerzyMultiFX",JackNoStartServer|JackUseExactName,&status);
    if(!app.client){
        std::cerr<<"Cannot connect to existing JACK server\n";::close(fd);return 1;
    }
    app.input=jack_port_register(app.client,"in_1",JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput,0);
    app.left=jack_port_register(app.client,"out_1",JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
    app.right=jack_port_register(app.client,"out_2",JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
    if(!app.input||!app.left||!app.right){
        std::cerr<<"Cannot register JACK ports\n";jack_client_close(app.client);::close(fd);return 1;
    }
    app.engine.prepare(double(jack_get_sample_rate(app.client)));
    jack_set_process_callback(app.client,App::process,&app);
    jack_on_shutdown(app.client,App::shutdown,&app);
    if(jack_activate(app.client)){
        std::cerr<<"Cannot activate JACK engine\n";jack_client_close(app.client);::close(fd);return 1;
    }
    std::cout<<"Jerzy MultiFX JACK running (ports: in_1/out_1/out_2, UDP 127.0.0.1:9810)\n"<<std::flush;
    while(!sigstop&&!app.disconnected.load(std::memory_order_relaxed)){
        char buffer[128]{};
        const ssize_t count=::recv(fd,buffer,sizeof(buffer)-1,0);
        if(count>0){
            buffer[count]=0;
            if(!applyMessage(app,buffer))std::cerr<<"Ignored invalid control message\n";
        }
        else if(count<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR){
            std::perror("UDP recv");break;
        }
    }
    jack_deactivate(app.client);
    jack_client_close(app.client);
    ::close(fd);
    return 0;
}
