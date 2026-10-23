#include "../api.h"
#include <cmath>
#include <new>
namespace {
struct State {double sr,tone[2]{0.,0.};explicit State(double hz):sr(hz){}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* p){delete static_cast<State*>(p);}
void process(void* ptr,const float in[2],float out[2],const float* p,double){
 auto& s=*static_cast<State*>(ptr);
 const double gain=1.+p[0]*35.;
 const double cutoff=350.+p[1]*9000.;
 const double a=1.-std::exp(-6.283185307179586*cutoff/s.sr);
 const double level=.08+p[2]*1.55;
 for(int ch=0;ch<2;++ch){
  const double driven=std::tanh(in[ch]*gain)*level;
  s.tone[ch]+=a*(driven-s.tone[ch]);out[ch]=float(s.tone[ch]);
 }
}
constexpr JfxParameter params[]{{"gain","Gain","x",.35f},{"tone","Tone","Hz",.65f},{"output","Output","%",.42f}};
const JfxDescriptor d{JFX_ABI_VERSION,"overdrive","Overdrive",3,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &d;}
