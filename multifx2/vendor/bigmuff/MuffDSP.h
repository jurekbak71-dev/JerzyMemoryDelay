#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace muff {
// Voicing approximations, not measured component-level circuit emulations.
struct Voice { double drive, clip, bass, treble, scoop, asymmetry; };
inline constexpr std::array<Voice,6> voices {{{18,.80,190,2200,.64,.02},{22,.75,240,1900,.72,.04},{30,.65,180,2600,.48,0},{17,.92,150,1800,.52,.06},{20,.88,130,1700,.58,.08},{25,.72,210,2400,.70,.02}}};
class Engine {
 struct Channel { double input=0, bass=0, high=0, dc=0; };
 std::array<Channel,2> states{};
 double rate=192000;
 double inputDC=0,outputDC=0;
 std::array<double,6> bassCoefficients{},trebleCoefficients{};
 static double lp(double x,double& state,double coefficient) { state += coefficient*(x-state); return state; }
 double coeff(double hz) const { return 1-std::exp(-6.283185307179586*hz/rate); }
public:
 void prepare(double sampleRate) { rate=sampleRate; inputDC=coeff(25);outputDC=coeff(12);for(size_t i=0;i<6;++i){bassCoefficients[i]=coeff(voices[i].bass);trebleCoefficients[i]=coeff(voices[i].treble);}reset(); }
 void reset() { states={}; }
 void mirrorChannel() { states[1]=states[0]; }
 float process(float input,int channel,int model,float sustain,float tone) {
  model=std::clamp(model,0,5);
  auto& s=states.at(static_cast<size_t>(channel)); auto v=voices.at(static_cast<size_t>(std::clamp(model,0,5)));
  double x=std::isfinite(input)?input:0;
  x-=lp(x,s.input,inputDC);
  double gain=2+v.drive*std::clamp(double(sustain),0.,1.);
  for(int stage=0;stage<2;++stage) x=v.clip*(std::tanh(x*gain+v.asymmetry)-std::tanh(v.asymmetry));
  double lo=lp(x,s.bass,bassCoefficients[static_cast<size_t>(model)]);
  double hi=x-lp(x,s.high,trebleCoefficients[static_cast<size_t>(model)]);
  double t=std::clamp(double(tone),0.,1.);
  double y=2*((1-t)*lo+t*hi)+(1-v.scoop)*x*.35;
  y-=lp(y,s.dc,outputDC);
  return static_cast<float>(y);
 }
};
}
