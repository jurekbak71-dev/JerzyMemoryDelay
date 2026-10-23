#include "../api.h"
#include "../vendor/smallclone/Dsp.h"
#include <new>
#include <algorithm>
namespace {
struct State {jerzy::Engine dsp; explicit State(double sr){dsp.reset(sr);}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* x){delete static_cast<State*>(x);}
void process(void* ctx,const float in[2],float out[2],const float* p,double bpm){
 auto& engine=static_cast<State*>(ctx)->dsp;
 jerzy::Values values{};for(int i=0;i<jerzy::NumParams;++i)values[i]=p[i];
 const auto y=engine.process(in[0],in[1],values,bpm);
 out[0]=float(y[0]);out[1]=float(y[1]);
}
static constexpr JfxParameter params[]{
 {"rate","LFO Rate","Hz",.30f},{"depth","Depth","%",.45f},{"mix","Mix","%",.50f},
 {"color","Color / Feedback","%",.25f},{"level","Output Level","dB",.50f},
 {"model","Circuit Model","",0.f},{"stereo","Stereo Motion","%",0.f},
 {"filter","Freeze LFO","",0.f},{"bypass","Circuit Bypass","",0.f},
 {"sync","Tempo Sync","",0.f},{"division","Note Division","",7.f/17.f}};
static_assert(sizeof(params)/sizeof(params[0])==jerzy::NumParams);
static const JfxDescriptor desc{JFX_ABI_VERSION,"small_clone","Small Clone / Mistress",jerzy::NumParams,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &desc;}
