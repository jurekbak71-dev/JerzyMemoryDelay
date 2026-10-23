#include "../api.h"
#include <algorithm>
#include <cmath>
#include <new>
namespace {
struct State {double env=0,attack=0,release=0;explicit State(double sr){
 attack=std::exp(-1./(.008*sr));release=std::exp(-1./(.110*sr));
}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* p){delete static_cast<State*>(p);}
void process(void* ptr,const float in[2],float out[2],const float* p,double){
 auto& s=*static_cast<State*>(ptr);
 const double peak=std::max(std::abs(in[0]),std::abs(in[1]));
 const double c=peak>s.env?s.attack:s.release;
 s.env=c*s.env+(1.-c)*peak;
 const double threshold=std::pow(10.,(-6.-42.*p[0])/20.);
 const double ratio=1.+9.*p[1];
 const double gain=s.env>threshold?std::pow(std::max(s.env,1e-9)/threshold,1./ratio-1.):1.;
 const double makeup=std::pow(10.,(p[2]*12.-6.)/20.);
 out[0]=float(in[0]*gain*makeup);out[1]=float(in[1]*gain*makeup);
}
constexpr JfxParameter params[]{{"threshold","Threshold","dB",.5f},{"ratio","Ratio",":1",.4f},{"makeup","Makeup","dB",.5f}};
const JfxDescriptor d{JFX_ABI_VERSION,"compressor","Compressor",3,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &d;}
