#include "../api.h"
#include "../vendor/reel/TapeEngine.h"
#include <array>
#include <new>
#include <cmath>
namespace {
struct State{jerzy::TapeEngine dsp;jerzy::Values values=jerzy::defaults();explicit State(double sr){dsp.set(values);dsp.prepare(sr);}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* x){delete static_cast<State*>(x);}
void process(void* ctx,const float in[2],float out[2],const float* p,double bpm){
 auto& s=*static_cast<State*>(ctx);
 for(size_t i=0;i<jerzy::count;++i){
   const auto& desc=jerzy::descriptors[i];
   float v=p[i];
   if(desc.skew!=1.f && desc.skew>0) v=std::pow(v,desc.skew);
   s.values[i]=desc.min+(desc.max-desc.min)*v;
 }
 s.dsp.set(s.values,bpm);
 float left=in[0],right=in[1];
 s.dsp.process(&left,&right,1);
 out[0]=left;out[1]=right;
}
const JfxDescriptor* descriptor(){
 static std::array<JfxParameter,jerzy::count> params{};
 static bool initialized=false;
 static JfxDescriptor desc{JFX_ABI_VERSION,"reel_echo","Reel Echo",unsigned(jerzy::count),params.data(),create,destroy,process};
 if(!initialized){
  for(size_t i=0;i<jerzy::count;++i){
   const auto& d=jerzy::descriptors[i];
   const float norm=(d.max>d.min)?(d.initial-d.min)/(d.max-d.min):0.f;
   const float skewed=(d.skew>0)?std::pow(norm,1.f/d.skew):norm;
   params[i]={d.id,d.name,d.unit,skewed};
  }
  initialized=true;
 }
 return &desc;
}
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return descriptor();}
