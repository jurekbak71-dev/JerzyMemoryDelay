#include "../api.h"
#include "../vendor/atomic/Dsp.h"
#include <new>
namespace {
struct State{atomiccluster::Engine dsp;explicit State(double sr){dsp.reset(sr);}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* x){delete static_cast<State*>(x);}
void process(void* ctx,const float in[2],float out[2],const float* p,double bpm){
 atomiccluster::Values v{};for(int i=0;i<atomiccluster::NumParams;++i)v[i]=p[i];
 auto y=static_cast<State*>(ctx)->dsp.process(in[0],in[1],v,bpm);
 out[0]=float(y[0]);out[1]=float(y[1]);
}
static constexpr JfxParameter params[]{
 {"volume","Output Volume","%",.5f},{"blend","Blend","%",1.f},
 {"speed","Speed","",.4f},{"atoms","Atom Count","",.666f},
 {"smooth","Smooth / Gate","",0.f},{"flat","Pitch Mode","",0.f},
 {"bypass","Circuit Bypass","",0.f},{"sync","Tempo Sync","",0.f},
 {"division","Division","",.5f},{"tap","Tap Enabled","",1.f},
 {"motion","Motion","%",0.f},{"stereo","Stereo Spread","%",1.f}
};
static_assert(sizeof(params)/sizeof(params[0])==atomiccluster::NumParams);
static const JfxDescriptor desc{JFX_ABI_VERSION,"atomic_cluster","Atomic Cluster",atomiccluster::NumParams,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &desc;}
