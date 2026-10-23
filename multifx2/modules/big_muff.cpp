#include "../api.h"
#include "../vendor/bigmuff/MuffDSP.h"
#include <new>
#include <cmath>
namespace {
struct State{muff::Engine dsp;explicit State(double sr){dsp.prepare(sr);}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* x){delete static_cast<State*>(x);}
void process(void* ctx,const float in[2],float out[2],const float* p,double){
 auto& dsp=static_cast<State*>(ctx)->dsp;
 const int model=int(std::lround(p[0]*5));
 const float gain=std::pow(10.f,(p[3]*24.f-12.f)/20.f),mix=p[4];
 for(int ch=0;ch<2;++ch){
  const float y=dsp.process(in[ch],ch,model,p[1],p[2])*gain;
  out[ch]=in[ch]*(1-mix)+y*mix;
 }
}
static constexpr JfxParameter params[]{
 {"model","Muff Model","",0.f},
 {"sustain","Sustain / Gain","%",.45f},
 {"tone","Tone","%",.5f},
 {"output","Output","dB",.5f},
 {"mix","Blend","%",1.f}
};
static const JfxDescriptor desc{JFX_ABI_VERSION,"big_muff","Big Muff Collection",5,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &desc;}
