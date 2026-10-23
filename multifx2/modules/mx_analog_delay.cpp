#include "../api.h"
#include "../vendor/mxdelay/mxdelay_dsp.h"
#include <new>
namespace {
struct State{JerzyAudio::MXDelayDSP<float> dsp;JerzyAudio::MXDelayParams p;explicit State(double sr){dsp.prepare(sr,2);}};
void* create(double sr){return new(std::nothrow) State(sr);}
void destroy(void* x){delete static_cast<State*>(x);}
void process(void* ctx,const float in[2],float out[2],const float* p,double bpm){
 auto& state=*static_cast<State*>(ctx);
 auto& v=state.p;
 v.mix=p[0];v.inputTrim=p[1];v.outputTrim=p[2];v.routing=p[3];
 v.bypass=p[4];v.spill=p[5];v.machineCondition=p[6];
 for(int n=0;n<2;++n) {
   const int offset=7+n*8;
   auto& slot=v.slot[n];
   slot.algorithm=p[offset];
   slot.enable=p[offset+1];
   slot.sync=p[offset+2];
   slot.division=p[offset+3];
   slot.time=p[offset+4];
   slot.feedback=p[offset+5];
   slot.level=p[offset+6];
   slot.pan=p[offset+7];
 }
 v.fx[0].type=p[23];v.fx[0].amount=p[24];v.fx[0].rate=p[25];v.fx[0].depth=p[26];
 v.fx[1].type=p[27];v.fx[1].amount=p[28];
 float l=in[0],r=in[1],yL=0,yR=0;float* ins[]{&l,&r};float* outs[]{&yL,&yR};
 double peak=0;
 state.dsp.process(ins,outs,2,1,v,bpm,peak);
 out[0]=yL;out[1]=yR;
}
static constexpr JfxParameter params[]{
 {"mix","Blend","%",.35f},{"input","Input Trim","dB",.5f},
 {"output","Output Trim","dB",.5f},{"routing","A/B Routing","",0.f},
 {"bypass","Circuit Bypass","",0.f},{"spill","Delay Trails","",1.f},
 {"condition","Machine Wear","%",0.f},
 {"a_model","A · Delay Model","",0.f},{"a_enable","A · Enabled","",1.f},
 {"a_sync","A · Sync","",1.f},{"a_division","A · Note Division","",.5f},
 {"a_time","A · Free Time","ms",.2f},{"a_feedback","A · Feedback","%",.42f},
 {"a_level","A · Level","%",.78f},{"a_pan","A · Pan","",.5f},
 {"b_model","B · Delay Model","",.125f},{"b_enable","B · Enabled","",1.f},
 {"b_sync","B · Sync","",1.f},{"b_division","B · Note Division","",.5f},
 {"b_time","B · Free Time","ms",.2f},{"b_feedback","B · Feedback","%",.42f},
 {"b_level","B · Level","%",.78f},{"b_pan","B · Pan","",.5f},
 {"wet_fx1","Wet FX 1 Type","",0.f},{"wet_fx1_amount","Wet FX 1 Amount","%",0.f},
 {"wet_fx1_rate","Wet FX 1 Rate","Hz",.3f},{"wet_fx1_depth","Wet FX 1 Depth","%",.45f},
 {"wet_fx2","Wet FX 2 Type","",0.f},{"wet_fx2_amount","Wet FX 2 Amount","%",0.f}
};
static const JfxDescriptor desc{JFX_ABI_VERSION,"mx_analog_delay","MX Analog Delay",29,params,create,destroy,process};
}
extern "C" __attribute__((visibility("default"))) const JfxDescriptor* jfx_plugin(){return &desc;}
