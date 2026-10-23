#include "../api.h"
#include <algorithm>
#include <cmath>
#include <new>
namespace {
struct Filter {
 double smooth[2]{0,0};
 double held[2]{0,0};
 double low[2]{0,0};
 double phase=0;
 double sr;
 explicit Filter(double sample_rate):sr(sample_rate){}
};
void* create(double sample_rate){return new(std::nothrow) Filter(sample_rate);}
void destroy(void* ptr){delete static_cast<Filter*>(ptr);}
void process(void* ptr,const float input[2],float output[2],const float* p,double){
 auto& s=*static_cast<Filter*>(ptr);
 const double bit_depth=4.+p[0]*12.;                 // 4..16 bits
 const double quant=std::pow(2.,bit_depth-1.);
 const double rate=500.+p[1]*(s.sr-500.);         // sample-and-hold 500..SR
 const double blend=p[2];
 const double cutoff=400.+p[3]*15000.;           // warmth filter
 const double filt=1.-std::exp(-6.28318530718*cutoff/s.sr);
 const double drive=1.+p[4]*12.;
 s.phase+=rate/s.sr;
 const bool sample=s.phase>=1.;
 if(sample)s.phase-=std::floor(s.phase);
 for(unsigned i=0;i<2;++i){
   if(sample){
     const double clipped=std::tanh(input[i]*drive);
     s.held[i]=std::round(clipped*quant)/quant;
   }
   s.low[i]+=filt*(s.held[i]-s.low[i]);
   output[i]=float(input[i]*(1.-blend)+s.low[i]*blend);
 }
}
constexpr JfxParameter params[]{
 {"bits","Bit Depth","bit",.66f},
 {"rate","Sample Rate","Hz",.50f},
 {"mix","Blend","%",.65f},
 {"tone","Tone","Hz",.55f},
 {"drive","Drive","x",.25f}
};
const JfxDescriptor desc{JFX_ABI_VERSION,"lofi_crush","Lo-Fi Crush",5,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &desc;}
