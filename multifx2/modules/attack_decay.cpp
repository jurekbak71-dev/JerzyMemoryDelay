#include "../api.h"
#include "../vendor/attackdecay/Engine.h"
#include <new>
#include <cmath>
namespace {
struct State{jerzy::Engine dsp;explicit State(double sr){dsp.prepare(float(sr));}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* x){delete static_cast<State*>(x);}
void process(void* ctx,const float in[2],float out[2],const float* p,double){
 jerzy::Parameters v;
 v.attack=p[0];v.decay=p[1];v.blend=p[2];v.volume=p[3];v.sens=p[4];
 v.harmonix=p[5];v.tone=p[6];v.hvol=p[7];v.hblend=p[8];v.rblend=p[9];
 v.poly=p[10]>=.5f;v.fuzz=p[11]>=.5f;v.compMono=p[12]>=.5f;v.compPoly=p[13]>=.5f;
 v.continuousMono=p[14]>=.5f;v.continuousPoly=p[15]>=.5f;
 v.bypass=p[16]>=.5f;v.attackShape=int(std::lround(p[17]*2));
 v.decayShape=int(std::lround(p[18]*2));
 v.triggerMode=int(std::lround(p[19]*2)); // auto, manual, MIDI
 float a=in[0],b=in[1];
 static_cast<State*>(ctx)->dsp.process(a,b,0.f,0.f,v,false);
 out[0]=a;out[1]=b;
}
static constexpr JfxParameter params[]{
 {"attack","Attack Time","s",.4f},{"decay","Decay Time","s",1.f},
 {"blend","Blend","%",1.f},{"volume","Volume","dB",.5f},
 {"sensitivity","Trigger Sensitivity","%",.5f},
 {"harmonix","Harmonix Gain","%",.3f},{"tone","Fuzz Tone","%",.5f},
 {"harmonix_vol","Harmonix Volume","%",.25f},{"harmonix_blend","Harmonix Blend","%",1.f},
 {"return_blend","Return Blend","%",1.f},
 {"poly","Poly Mode","",0.f},{"fuzz","Harmonix Fuzz","",0.f},
 {"mono_comp","Mono Compressor","",1.f},{"poly_comp","Poly Compressor","",1.f},
 {"continuous_mono","Continuous Mono","",0.f},{"continuous_poly","Continuous Poly","",0.f},
 {"circuit_bypass","Circuit Bypass","",0.f},
 {"attack_shape","Attack Shape","",.5f},{"decay_shape","Decay Shape","",.5f},
 {"trigger_mode","Trigger Mode","",0.f}
};
static const JfxDescriptor desc{JFX_ABI_VERSION,"attack_decay","EHX Attack Decay",20,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &desc;}
