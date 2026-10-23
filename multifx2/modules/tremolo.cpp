#include "../api.h"
#include <cmath>
#include <new>
namespace {
struct State {double sr,phase=0.;explicit State(double hz):sr(hz){}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* p){delete static_cast<State*>(p);}
void process(void* ptr,const float in[2],float out[2],const float* p,double){
 auto& s=*static_cast<State*>(ptr);
 s.phase+=(.3+11.7*p[0])/s.sr;if(s.phase>=1.)s.phase-=1.;
 const double shape=.5+.5*std::sin(6.283185307179586*s.phase);
 const double gain=1.-p[1]*(1.-shape);
 out[0]=float(in[0]*gain);out[1]=float(in[1]*gain);
}
constexpr JfxParameter params[]{{"rate","Rate","Hz",.33f},{"depth","Depth","%",.55f}};
const JfxDescriptor d{JFX_ABI_VERSION,"tremolo","Tremolo",2,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &d;}
