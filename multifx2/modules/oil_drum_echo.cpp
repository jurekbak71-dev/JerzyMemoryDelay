#include "../api.h"
#include "../vendor/oil_echo/Engine.h"
#include <array>
#include <algorithm>
#include <new>
namespace {
constexpr std::array<jerzy::key::Id,46> ids{{
 jerzy::key::master,jerzy::key::time,jerzy::key::sync,jerzy::key::division,
 jerzy::key::extended,jerzy::key::heads,jerzy::key::feedback,
 jerzy::key::dry,jerzy::key::wet,jerzy::key::focus,jerzy::key::duck,
 jerzy::key::stereo,jerzy::key::mono,
 jerzy::key::h1on,jerzy::key::h1level,jerzy::key::h1pan,jerzy::key::h1send,
 jerzy::key::h2on,jerzy::key::h2level,jerzy::key::h2pan,jerzy::key::h2send,
 jerzy::key::h3on,jerzy::key::h3level,jerzy::key::h3pan,jerzy::key::h3send,
 jerzy::key::h4on,jerzy::key::h4level,jerzy::key::h4pan,jerzy::key::h4send,
 jerzy::key::age,jerzy::key::oil,jerzy::key::motor,jerzy::key::contact,
 jerzy::key::decay,jerzy::key::noise,jerzy::key::drift,jerzy::key::wow,jerzy::key::flutter,
 jerzy::key::mod,jerzy::key::route,jerzy::key::depth,jerzy::key::modmix,
 jerzy::key::springon,jerzy::key::springmix,jerzy::key::springdecay,jerzy::key::springtone
}};
struct State{
 jerzy::Engine engine;
 jerzy::Settings settings;
 explicit State(double sr){engine.prepare(sr);}
};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* ptr){delete static_cast<State*>(ptr);}
void process(void* ctx,const float in[2],float out[2],const float* p,double bpm){
 auto& state=*static_cast<State*>(ctx);
 static const auto& definitions=jerzy::parameters();
 for(size_t i=0;i<ids.size();++i){
  const auto& source=definitions[size_t(ids[i])];
  state.settings.v[size_t(ids[i])]=source.min+(source.max-source.min)*std::clamp(p[i],0.f,1.f);
 }
 float a=in[0],b=in[1];
 state.engine.process(&a,&b,1,state.settings,bpm);
 out[0]=a;out[1]=b;
}
const JfxDescriptor* descriptor(){
 static const auto data=[]{
  std::array<JfxParameter,ids.size()> items{};
  const auto& original=jerzy::parameters();
  for(size_t i=0;i<ids.size();++i) {
   const auto& p=original[size_t(ids[i])];
   const float normalized=p.max>p.min?(p.def-p.min)/(p.max-p.min):0.f;
   items[i]={p.id,p.name,"",std::clamp(normalized,0.f,1.f)};
  }
  return items;
 }();
 static const JfxDescriptor d{JFX_ABI_VERSION,"oil_drum_echo","Oil Drum Echo",unsigned(ids.size()),data.data(),create,destroy,process};
 return &d;
}
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return descriptor();}
