#pragma once
#include "Parameters.h"
#include "Spring.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace jerzy {
inline float db(float x) {return std::pow(10.f,x*.05f);}
inline float clamp(float x,float a,float b) {return std::max(a,std::min(b,x));}
constexpr double pi=3.14159265358979323846;
struct Meter { float input=0,output=0,dry=0,wet=0,feedback=0,inGR=0,outGR=0; std::array<float,4> head{}; };
struct Compressor {
 float env=0,gr=0,at=.1f,rel=.001f,threshold=-18,ratio=3,knee=6,makeup=1; int mode=0;
 void configure(float sr,int m,float t,float r,float a,float re,float k,float gain) {mode=m;threshold=t;ratio=r;knee=k;makeup=db(gain);at=1-std::exp(-1.f/(sr*a*.001f));rel=1-std::exp(-1.f/(sr*re*.001f));}
 float process(float& l,float& r) {
 if(!mode){gr=0;return 0;}
 float level=std::max(std::abs(l),std::abs(r)); if(mode==1)level=std::sqrt((l*l+r*r)*.5f);
 env+=(level-env)*(level>env?at:rel);
 float over=20*std::log10(std::max(env,1.e-8f))-threshold;
 float reduce=0;
 if(knee>0 && over>-knee*.5f && over<knee*.5f)reduce=(1-1/ratio)*(over+knee*.5f)*(over+knee*.5f)/(2*knee);
 else if(over>=knee*.5f)reduce=(1-1/ratio)*over;
 gr=std::max(0.f,reduce);float gain=db(-gr)*makeup;l*=gain;r*=gain;return gr;
 }
};
class Engine {
 double sr=48000,phase=0,modphase=0; size_t pos=0,mp=0;
 std::array<std::vector<float>,2> surface,modbuf,mechanicsBuffer,spaceBuffer;
 SpringTank spring;size_t mechanicsPos=0,spacePos=0;
 std::array<float,2> mechanicsLP{},headTone{},headLow{},spaceLP{};
 float mechanicsFade=1,springFade=0;
 std::array<float,96> smooth{};
 std::array<float,2> inputLP{},outputLP{},bodyLP{},feedbackLP{},dcX{},dcY{},modLP{},focusLP{};
 Compressor inputComp,outputComp;
 float duckEnv=0,sagEnv=0; uint32_t rng=0x71826ad;
 bool initial=true; double rotationMs=480;
 float random(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(double(rng)/2147483648.0-1.0);}
 static float read(const std::vector<float>& b,size_t write,double delay) {
 double t=double(write)-delay;while(t<0)t+=double(b.size());
 size_t i=size_t(t)%b.size(),j=(i+1)%b.size();float f=float(t-std::floor(t));return b[i]+(b[j]-b[i])*f;
 }
 void mechanicalDry(float& l,float& r,const std::array<float,96>& s,float amount) {
 using namespace key;
 mechanicsBuffer[0][mechanicsPos]=l;mechanicsBuffer[1][mechanicsPos]=r;
 double movement=.012*s[wow]*std::sin(phase)+.0012*s[flutter]*std::sin(phase*47.13)+.008*s[drift]*std::sin(phase*.117);
 for(int ch=0;ch<2;++ch){float original=ch==0?l:r;
 float x=read(mechanicsBuffer[ch],mechanicsPos,sr*(.024+movement));
 float cutoff=std::max(1000.f,17000-11500*s[age]-3500*s[oil]);
 mechanicsLP[ch]+=float(1-std::exp(-2*pi*cutoff/sr))*(x-mechanicsLP[ch]);
 float colored=mechanicsLP[ch];float hardness=1+s[oil]*2+s[age];colored=std::tanh(colored*hardness)/std::sqrt(hardness);
 float pressure=1-s[contact]*(.22f+.65f*float(std::pow(.5+.5*std::sin(phase*7.03),4)));
 colored*=pressure*(1-.25f*s[decay])*(1+.035f*s[motor]*float(std::sin(phase*3)));
 colored+=random()*s[noise]*.003f;
 float y=original*(1-amount)+colored*amount;if(ch==0)l=y;else r=y;
 }
 mechanicsPos=(mechanicsPos+1)%mechanicsBuffer[0].size();
 }
 float pre(float x,int ch,float drive,float sat,float tone) {
 const float gain=db(drive);float y=std::tanh(x*gain)/std::sqrt(gain);
 y=(1-sat)*y+sat*(std::tanh(1.8f*y+.12f)-std::tanh(.12f))/1.8f;
 float a=float(1-std::exp(-2*pi*(1800+tone*16000)/sr));inputLP[ch]+=a*(y-inputLP[ch]);return inputLP[ch];
 }
 float powerAmp(float x,int ch,float drive,float distort,float sat,float tone,float body,float sag) {
 float gain=db(drive),y=x*gain/(1+3*sag*sagEnv);
 // Drive: soft clip; distortion: asymmetric hard clip; saturation: polynomial soft knee.
 y=std::tanh(y)/std::sqrt(gain);
 y=y*(1-distort)+distort*(clamp(y*3+.08f,-.7f,1.f)-.08f)/2.f;
 float z=clamp(y,-1.5f,1.5f);y=y*(1-sat)+sat*(z-z*z*z/6);
 float a=float(1-std::exp(-2*pi*(1500+tone*17000)/sr));outputLP[ch]+=a*(y-outputLP[ch]);
 bodyLP[ch]+=float(1-std::exp(-2*pi*220/sr))*(outputLP[ch]-bodyLP[ch]);
 y=outputLP[ch]+(body-.5f)*bodyLP[ch];
 float out=y-dcX[ch]+.9995f*dcY[ch];dcX[ch]=y;dcY[ch]=out;return out;
 }
 void modulation(float& l,float& r,const std::array<float,96>& s) {
 using namespace key;int kind=int(std::lround(s[mod]));if(kind==0)return;
 modbuf[0][mp]=l;modbuf[1][mp]=r;float result[2]{};
 int n=kind==2?int(std::lround(s[voices])):kind==1?2:1;
 for(int ch=0;ch<2;++ch){
 for(int v=0;v<n;++v){double ph=modphase+double(v)/n+ch*s[width]*.23;
 double movement=std::sin(2*pi*ph);if(kind==2) movement=.75*movement+.25*std::sin(2*pi*(ph*1.031+v*.13));
 double base=kind==3?.006:kind==2?.019:.012;
 double d=sr*(base+movement*s[depth]*(kind==3?.004:.007));
 result[ch]+=read(modbuf[ch],mp,d)/float(n);}
 modLP[ch]+=float(1-std::exp(-2*pi*(1500+s[modtone]*16000)/sr))*(result[ch]-modLP[ch]);
 }
 float mix=s[modmix];l=l*(1-mix)+modLP[0]*mix;r=r*(1-mix)+modLP[1]*mix;
 // Both 1.0 and 1.031 LFO frequencies complete integer cycles at this wrap.
 mp=(mp+1)%modbuf[0].size();modphase+=s[speed]/sr;if(modphase>1000)modphase-=1000;
 }
public:
 void prepare(double sampleRate) {
 sr=sampleRate;for(auto& b:surface)b.assign(size_t(std::ceil(sr*8.1))+8,0);
 for(auto& b:modbuf)b.assign(size_t(std::ceil(sr*.08))+8,0);
 for(auto& b:mechanicsBuffer)b.assign(size_t(sr*.09)+8,0);
 for(auto& b:spaceBuffer)b.assign(size_t(sr*.09)+8,0);
 spring.prepare(sr);mechanicsPos=spacePos=0;mechanicsLP={};headTone={};headLow={};spaceLP={};mechanicsFade=1;springFade=0;
 pos=mp=0;phase=modphase=0;inputLP={};outputLP={};bodyLP={};feedbackLP={};dcX={};dcY={};modLP={};focusLP={};
 duckEnv=sagEnv=0;inputComp={};outputComp={};rng=0x71826ad;initial=true;
 }
 size_t memoryBytes() const {return (surface[0].capacity()+surface[1].capacity()+modbuf[0].capacity()+modbuf[1].capacity()+mechanicsBuffer[0].capacity()+mechanicsBuffer[1].capacity()+spaceBuffer[0].capacity()+spaceBuffer[1].capacity())*sizeof(float)+spring.memoryBytes();}
 Meter process(float* left,float* right,int samples,const Settings& settings,double bpm=120) {
 using namespace key;
 const auto& target=settings.v;if(initial){smooth=target;rotationMs=target[key::time];mechanicsFade=target[key::mechanicson];springFade=target[key::springon];initial=false;}
 inputComp.configure(float(sr),int(target[icmode]),target[icthreshold],target[icratio],target[icattack],target[icrelease],target[icknee],target[icmakeup]);
 outputComp.configure(float(sr),int(target[ocmode]),target[octhreshold],target[ocratio],target[ocattack],target[ocrelease],target[ocknee],target[ocmakeup]);
 spring.configure(target[springdecay],target[springtone]);
 float smoothing=float(1-std::exp(-1/(.04*sr)));Meter meters;
 const float divisions[]={.25f,1.f/3,.5f,1,1.5f,2,4,8};
 for(int sample=0;sample<samples;++sample){
 for(int k=0;k<int(key::count);++k){bool discrete=parameters()[size_t(k)].max<=3 && parameters()[size_t(k)].min==0 && (parameters()[size_t(k)].choices[0] || parameters()[size_t(k)].max==1); if(discrete && parameters()[size_t(k)].choices[0])smooth[size_t(k)]=target[size_t(k)];else smooth[size_t(k)]+=(target[size_t(k)]-smooth[size_t(k)])*smoothing;}
 const auto& s=smooth;float l=left[sample],r=right[sample];if(!std::isfinite(l))l=0;if(!std::isfinite(r))r=0;
 meters.input=std::max(meters.input,std::max(std::abs(l),std::abs(r)));
 if(target[inbypass]<.5f){l=pre(l,0,s[drive],s[insat],s[intone]);r=pre(r,1,s[drive],s[insat],s[intone]);}
 meters.inGR=std::max(meters.inGR,inputComp.process(l,r));
 float dl=l,dr=r,wl=0,wr=0,fb[2]={};
 mechanicsFade+=(target[mechanicson]-mechanicsFade)*smoothing;
 float mechanism=s[mechmix]*mechanicsFade;
 // Keep the rotor moving in MASTER ONLY; its dry processor has no echo feedback.
 phase+=2*pi*(.12+s[motor]*1.1)/sr;if(phase>2*pi*1000)phase-=2*pi*1000;
 bool dryMechanics=target[master]>.5f||target[mechroute]>.5f;
 mechanicalDry(dl,dr,s,dryMechanics?mechanism:0.f);
 float env=std::max(std::abs(l),std::abs(r));duckEnv+=(env-duckEnv)*float(1-std::exp(-1/(sr*(env>duckEnv?.003:.18))));
 if(target[master]<.5f){
 double milliseconds=s[time];if(target[sync]>.5f)milliseconds=60000/std::max(20.0,bpm)*divisions[std::clamp(int(target[division]),0,7)];
 milliseconds=std::min(milliseconds,target[extended]>.5f?8000.:1000.);
 rotationMs+=(milliseconds-rotationMs)*(1-std::exp(-1/(sr*.25))); milliseconds=rotationMs;
 // A continuous rotating charge track. Four pickups sample angular positions.
 float echoMechanics=(target[mechroute]<.5f||target[mechroute]>1.5f)?mechanism:0.f;
 float echoAge=s[age]*echoMechanics,echoOil=s[oil]*echoMechanics,echoDecay=s[decay]*echoMechanics;
 double movement=sr*echoMechanics*(.012*s[wow]*std::sin(phase)+.0012*s[flutter]*std::sin(phase*47.13)+.008*s[drift]*std::sin(phase*.117));
 double mechanical=1+echoMechanics*s[drift]*.004*std::sin(phase*.117);
 float norm=0;int countHeads=(target[heads]>.5f||target[h3on]>.5f||target[h4on]>.5f)?4:2;
 for(int h=0;h<4;++h){int on=h1on+h*4;float enabled=(h<countHeads?s[size_t(on)]:0);float level=s[size_t(on+1)]*enabled;
 double fraction=countHeads==4?(h+1)*.25:(h+1)*.5;
 double delay=clamp(float(milliseconds*.001*sr*fraction*mechanical+movement),2,float(surface[0].size()-3));
 float charge=std::exp(-echoDecay*float(delay/sr)*.9f);
 float hl=read(surface[0],pos,delay)*charge,hr=read(surface[1],pos,delay)*charge;
 float monoSignal=(hl+hr)*.5f;float pan=s[size_t(on+2)];if(int(target[stereo])==0)pan*=.25f;
 float widthSignal=(hl-hr)*.5f*(1-s[mono]);
 float a=std::sqrt((1-pan)*.5f),b=std::sqrt((1+pan)*.5f);
 float headL=(monoSignal*a+widthSignal)*level,headR=(monoSignal*b-widthSignal)*level;
 wl+=headL;wr+=headR;meters.head[size_t(h)]=std::max(meters.head[size_t(h)],std::max(std::abs(headL),std::abs(headR)));
 float send=s[size_t(on+3)]*enabled;
 if(int(target[stereo])==2){fb[0]+=(monoSignal*a*1.41421356f+widthSignal)*send;fb[1]+=(monoSignal*b*1.41421356f-widthSignal)*send;}
 else {fb[0]+=hl*send;fb[1]+=hr*send;}norm+=send;
 }
 if(norm>0){fb[0]/=norm;fb[1]/=norm;}if(int(target[stereo])==2)std::swap(fb[0],fb[1]);
 float cutoff=11000-8500*echoAge-1200*echoOil;float alpha=float(1-std::exp(-2*pi*cutoff/sr));
 for(int ch=0;ch<2;++ch){feedbackLP[ch]+=alpha*(fb[ch]-feedbackLP[ch]);
 float signal=ch==0?l:r;float contactGain=1-echoMechanics*s[contact]*.65f*float(std::pow(.5+.5*std::sin(phase*7.03),4));
 float write=(signal+feedbackLP[ch]*s[feedback])*contactGain;
 float noiseValue=random()*s[noise]*.003f*echoMechanics;
 float softness=1+echoOil*1.5f;float charge=std::tanh(write*softness)/softness+noiseValue;
 surface[ch][pos]=std::isfinite(charge)?charge:0;
 meters.feedback=std::max(meters.feedback,std::abs(feedbackLP[ch]*s[feedback]));}
 pos=(pos+1)%surface[0].size();
 // Band-limit pickups separately from the dry signal, giving repeats their own
 // register; short unequal stereo reflections create depth without smearing attacks.
 float wetValues[2]={wl,wr};
 for(int ch=0;ch<2;++ch){float x=wetValues[ch];headLow[ch]+=float(1-std::exp(-2*pi*(100+300*s[focus])/sr))*(x-headLow[ch]);
 float highpassed=x-headLow[ch]*(.6f+.4f*s[focus]);headTone[ch]+=float(1-std::exp(-2*pi*(9500-6000*echoAge)/sr))*(highpassed-headTone[ch]);
 spaceBuffer[ch][spacePos]=headTone[ch];
 float early=read(spaceBuffer[ch],spacePos,sr*(ch==0?.017:.029));
 float cross=read(spaceBuffer[1-ch],spacePos,sr*(ch==0?.043:.037));
 wetValues[ch]=headTone[ch]+s[space]*(early*.38f+cross*.24f);
 }
 spacePos=(spacePos+1)%spaceBuffer[0].size();wl=wetValues[0];wr=wetValues[1];
 float duckGain=1/(1+duckEnv*s[duck]*12);
 float focusAlpha=float(1-std::exp(-2*pi*650/sr));focusLP[0]+=focusAlpha*(wl-focusLP[0]);focusLP[1]+=focusAlpha*(wr-focusLP[1]);
 wl=(wl+s[focus]*(wl-focusLP[0])*.65f-focusLP[0]*s[focus]*.25f)*duckGain;
 wr=(wr+s[focus]*(wr-focusLP[1])*.65f-focusLP[1]*s[focus]*.25f)*duckGain;
 }
 if(target[route]<.5f)modulation(wl,wr,s);else modulation(dl,dr,s);
 float dryGain=target[master]>.5f?1:s[dry];float wetGain=target[master]>.5f?0:s[wet];
 meters.dry=std::max(meters.dry,std::max(std::abs(dl*dryGain),std::abs(dr*dryGain)));
 meters.wet=std::max(meters.wet,std::max(std::abs(wl*wetGain),std::abs(wr*wetGain)));
 l=dl*dryGain+wl*wetGain;r=dr*dryGain+wr*wetGain;
 sagEnv+=(std::max(std::abs(l),std::abs(r))-sagEnv)*float(1-std::exp(-1/(sr*.08)));
 if(target[outbypass]<.5f){l=powerAmp(l,0,s[power],s[distortion],s[outsat],s[outtone],s[body],s[sag]);r=powerAmp(r,1,s[power],s[distortion],s[outsat],s[outtone],s[body],s[sag]);}
 meters.outGR=std::max(meters.outGR,outputComp.process(l,r));
 springFade+=(target[springon]-springFade)*smoothing;
 float springL=0,springR=0;spring.process(l*springFade,r*springFade,springL,springR);
 float springAmount=s[springmix]*springFade;l=l*(1-springAmount)+springL*springAmount;r=r*(1-springAmount)+springR*springAmount;
 float gain=db(s[output]);l*=gain;r*=gain;
 left[sample]=std::isfinite(l)?l:0;right[sample]=std::isfinite(r)?r:0;
 meters.output=std::max(meters.output,std::max(std::abs(left[sample]),std::abs(right[sample])));
 }
 return meters;
 }
};
}
