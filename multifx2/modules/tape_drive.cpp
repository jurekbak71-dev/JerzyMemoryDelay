#include "../api.h"
#include "../vendor/tapedrive/tapedrive_dsp.h"
#include <new>
namespace {
using Params=JerzyAudio::TapeDriveParams;
struct State{JerzyAudio::TapeDriveDSP<float> dsp;explicit State(double sr){dsp.prepare(sr,2);}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* x){delete static_cast<State*>(x);}
static const double Params::* members[]{
 &Params::sat,&Params::level,&Params::dry,&Params::gainMode,
 &Params::shift,&Params::bypass,&Params::hpfCutoff,&Params::hpfRes,
 &Params::lpfCutoff,&Params::lpfRes,&Params::wowFlutter,
 &Params::preampMode,&Params::preampDrive,&Params::flutter,
 &Params::tapeAge,&Params::optoAmount,&Params::optoMakeup,&Params::optoBypass
};
void process(void* ctx,const float in[2],float out[2],const float* p,double){
 Params v;for(unsigned i=0;i<18;++i)v.*members[i]=p[i];
 float l=in[0],r=in[1],yL=0,yR=0;
 const float* ins[]{&l,&r};
 float* outs[]{&yL,&yR};
 double ip=0,sp=0,op=0,rp=0;
 static_cast<State*>(ctx)->dsp.process(ins,outs,2,1,v,ip,sp,op,rp);
 out[0]=yL;out[1]=yR;
}
static constexpr JfxParameter params[]{
 {"saturation","Saturation","%",.35f},{"level","Output Level","dB",.5f},
 {"dry","Dry Level","%",0.f},{"gain_mode","Gain Mode","",0.f},
 {"shift","Tape Shift","",.5f},{"drive_bypass","Drive Bypass","",0.f},
 {"hpf","High Pass Frequency","Hz",0.f},{"hpf_q","High Pass Resonance","",.018f},
 {"lpf","Low Pass Frequency","Hz",1.f},{"lpf_q","Low Pass Resonance","",.018f},
 {"wow","Wow","%",0.f},{"preamp_mode","Preamp Mode","",0.f},
 {"preamp_drive","Preamp Drive","%",.2f},{"flutter","Flutter","%",0.f},
 {"tape_age","Tape Age","%",0.f},{"opto_amount","Optical Comp","%",.35f},
 {"opto_makeup","Optical Makeup","dB",.5f},{"opto_bypass","Opto Bypass","",0.f}
};
static const JfxDescriptor desc{JFX_ABI_VERSION,"tape_drive","Tape Drive",18,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &desc;}
