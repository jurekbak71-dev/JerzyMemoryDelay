#include "../api.h"
#include "../../source/dsp.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace {
struct Delay {
    Jerzy::Engine dsp;
    Jerzy::Parameters params;
    explicit Delay(double sr) { dsp.prepare(sr); }
};
void* create(double sr) {
    auto* d=new(std::nothrow) Delay(sr);
    return d;
}
void destroy(void* p) { delete static_cast<Delay*>(p); }
void process(void* object, const float input[2], float output[2], const float* p, double bpm) {
    auto& d=*static_cast<Delay*>(object);
    for(unsigned i=0;i<Jerzy::Count;++i)
        d.params.set(int(i),p[i]);
    const double src[2]{double(input[0]),double(input[1])};
    double dest[2]{};
    d.dsp.tick(src,dest,2,d.params,bpm);
    output[0]=std::isfinite(dest[0])?float(dest[0]):0.f;
    output[1]=std::isfinite(dest[1])?float(dest[1]):0.f;
}
// Keep indices identical to Jerzy::Param in source/parameters.h.
constexpr JfxParameter parameters[]{
 {"blend","Blend / Dry-Wet","%",.4f},
 {"level","Level / Input Gain","dB",.5f},
 {"feedback","Feedback","%",.35f},
 {"mod_rate","Modulation Rate","Hz",.15f},
 {"depth","Modulation Depth","%",.18f},
 {"delay","Delay Time","ms",.55f},
 {"model","BBD Model","",.6f},
 {"wave","LFO Wave (Sine / Triangle)","",0.f},
 {"bypass","Circuit Bypass","",0.f},
 {"boost","Input Boost","%",0.f},
 {"squelch","Squelch / Noise Gate","%",.35f},
 {"mod_enable","Modulation Enable","",1.f},
 {"tempo_sync","Tempo Sync","",0.f},
 {"division","Note Division","",.3f}
};
static_assert(sizeof(parameters)/sizeof(parameters[0])==Jerzy::Count);
const JfxDescriptor descriptor{
 JFX_ABI_VERSION,"memory_delay","Jerzy Memory Delay",Jerzy::Count,parameters,
 create,destroy,process
};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin() { return &descriptor; }
