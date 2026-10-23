#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace jerzy {
constexpr double pi=3.14159265358979323846;
enum Param { Rate, Depth, Mix, Color, Level, Model, Stereo, Filter, Bypass, Sync, Division, NumParams };
using Values=std::array<double,NumParams>;
inline double clean(double x){return std::isfinite(x)?std::clamp(x,0.,1.):0.;}
inline Values defaults(){return {.30,.45,.5,.25,.5,0,0,0,0,0,7./17.};}
struct Circuit {const char* name; double stages, clock, sweep, cutoff, emphasis, feedback; bool chorus;};
// Clocks and excursion are initial behavioural tuning, not measured calibration.
inline constexpr std::array<Circuit,14> circuits{{
 {"Small Clone",1024,85000,.48,6500, .35,0,true},
 {"Clone Theory SAD",1024,45000,.55,7800,.55,0,true},
 {"Clone Theory MN",1024,45000,.58,8200,.60,0,true},
 {"Electric Mistress V2",512,170000,.88,11000,.25,.92,false},
 {"Electric Mistress V3",512,175000,.88,11000,.25,.92,false},
 {"Electric Mistress V4",512,180000,.87,11000,.25,.92,false},
 {"Electric Mistress V5",512,180000,.87,11000,.25,.92,false},
 {"Electric Mistress V6",512,165000,.83,10000,.32,.93,false},
 {"Deluxe Mistress V1",512,155000,.88,11500,.48,.94,false},
 {"Deluxe Mistress V2",512,155000,.88,11500,.48,.94,false},
 {"Deluxe Mistress V3",512,160000,.87,10500,.52,.94,false},
 {"Deluxe Mistress V4",1024,310000,.88,9500,.3,.93,false},
 {"Deluxe Mistress V5",1024,310000,.88,9500,.3,.93,false},
 {"Stereo Mistress Inspired",1024,140000,.75,10000,.3,.75,false}
}};
inline int modelIndex(double v){return int(std::round(clean(v)*(circuits.size()-1)));}
inline double rateHz(double v){return .05*std::pow(160.,clean(v));}
struct BeatDivision {const char* label; double beats;};
inline constexpr std::array<BeatDivision,18> divisions{{
 {"4 bars",16},{"2 bars",8},{"1 bar",4},{"1/2 D",3},{"1/2",2},{"1/2 T",4./3.},
 {"1/4 D",1.5},{"1/4",1},{"1/4 T",2./3.},{"1/8 D",.75},{"1/8",.5},{"1/8 T",1./3.},
 {"1/16 D",.375},{"1/16",.25},{"1/16 T",1./6.},{"1/32",.125},{"1/32 T",1./12.},{"1/64",.0625}
}};
inline int divisionIndex(double v){return int(std::round(clean(v)*(divisions.size()-1)));}
inline double syncHz(double bpm,double division){return (std::isfinite(bpm)&&bpm>0?bpm:120.)/(60*divisions[divisionIndex(division)].beats);}
struct OnePole {double z=0; double run(double x,double a){z+=a*(x-z); if(std::abs(z)<1e-20)z=0;return z;}};
class Engine {
 struct Channel { std::vector<double> ring; size_t pos=0; OnePole input, pre, low1,low2, dc; double feedback=0;
  void reset(int n){ring.assign(n,0);pos=0;input={};pre={};low1={};low2={};dc={};feedback=0;}
 };
 std::array<Channel,2> ch;
 Values smooth=defaults(); double fs=48000, phase=0, fade=1, bypass=0;
 int current=0, pending=0; bool switching=false;
 double coeff(double hz)const{return 1-std::exp(-2*pi*std::min(hz,fs*.42)/fs);}
 public:
 void reset(double sampleRate,const Values& p=defaults()) {fs=sampleRate; smooth=p; current=pending=modelIndex(p[Model]); phase=0;fade=1;switching=false;bypass=p[Bypass]; for(auto& c:ch)c.reset(int(fs*.12)+8);}
 double getPhase() const{return phase;}
 std::array<double,2> process(double left,double right,const Values& raw,double bpm=120.,double beat=NAN){
  Values p{};for(int i=0;i<NumParams;++i)p[i]=clean(raw[i]);
  for(int i=0;i<NumParams;++i) if(i!=Model&&i!=Filter&&i!=Bypass&&i!=Sync&&i!=Division)smooth[i]+=(p[i]-smooth[i])*coeff(35);
  pending=modelIndex(p[Model]);
  if(pending!=current)switching=true;
  const double step=1./(fs*.012);
  if(switching){fade=std::max(0.,fade-step); if(fade==0){current=pending; for(auto& c:ch){std::fill(c.ring.begin(),c.ring.end(),0);c.feedback=0;c.low1={};c.low2={};}switching=false;}}
  else fade=std::min(1.,fade+step);
  bypass+=(p[Bypass]-bypass)*coeff(90);
  const auto& m=circuits[current];
  if(p[Filter]<.5){
   if(p[Sync]>=.5&&std::isfinite(beat)){
    phase=beat/divisions[divisionIndex(p[Division])].beats;
   }else phase+=(p[Sync]>=.5?syncHz(bpm,p[Division]):rateHz(smooth[Rate]))/fs;
   phase-=std::floor(phase);
  }
  const double triangle=1-4*std::abs(phase-.5);
  const double lfo=m.chorus? std::sin(2*pi*phase):triangle;
  std::array<double,2> result{}, in{left,right};
  const double highpass=coeff(25), pre=coeff(900), lp=coeff(m.cutoff), output=std::pow(10.,(smooth[Level]*24-12)/20.);
  for(int c=0;c<2;++c){
   auto& s=ch[c]; double x=std::isfinite(in[c])?std::clamp(in[c],-16.,16.):0.;
   // Optional stereo is an explicit plugin extension: opposite modulation phase.
   double wave=c==1? lfo*(1-2*smooth[Stereo]):lfo;
   double excursion=m.sweep*smooth[Depth];
   double clock=m.clock*std::max(.08,1+excursion*wave);
   double delay=std::clamp(m.stages/(2*clock)*fs,1.,double(s.ring.size()-4));
   double read=double(s.pos)-delay; if(read<0)read+=s.ring.size();
   auto k=size_t(read);double f=read-k;
   double delayed=s.ring[k]*(1-f)+s.ring[(k+1)%s.ring.size()]*f;
   double dry=x;
   double ac=x-s.input.run(x,highpass);
   double emphasized=ac+m.emphasis*(ac-s.pre.run(ac,pre));
   double fb=m.feedback*smooth[Color];
   s.ring[s.pos]=std::tanh((emphasized+s.feedback*fb)*1.15)/1.15;
   s.pos=(s.pos+1)%s.ring.size();
   double wet=s.low2.run(s.low1.run(delayed,lp),lp);
   wet-=s.dc.run(wet,highpass);
   s.feedback=wet;
   // Unity dry/wet endpoints, equal-amplitude chorus at centre.
   double mixed=(dry*(1-smooth[Mix])+wet*smooth[Mix])*output;
   result[c]=dry+(mixed-dry)*fade*(1-bypass);
  }
  return result;
 }
};
}
