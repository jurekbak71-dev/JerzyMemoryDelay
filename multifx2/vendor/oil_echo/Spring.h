#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
namespace jerzy {
// Four coupled waveguides with all-pass dispersion model a spring tank's
// frequency-dependent propagation. All storage is prepared off the audio thread.
class SpringTank {
 struct Line {std::vector<float> buffer;size_t pos=0;float lp=0,dc=0,gain=.8f;};
 struct Coil {std::vector<float> buffer;size_t pos=0;float tick(float x){float delayed=buffer[pos];float y=delayed-.62f*x;buffer[pos]=x+.62f*y;if(++pos==buffer.size())pos=0;return y;}};
 std::array<Line,4> lines;
 std::array<std::array<Coil,4>,4> coils;
 float damping=.2f,highpass=.99f;double rate=48000;
public:
 void prepare(double sr){rate=sr;const double lengths[]={.0437,.0563,.0691,.0797};for(size_t j=0;j<4;++j){auto& l=lines[j];l.buffer.assign(size_t(sr*lengths[j])+1,0);l.pos=0;l.lp=l.dc=0;for(size_t k=0;k<4;++k){auto& c=coils[j][k];c.buffer.assign(size_t(sr*(.00041+.00017*k+.00011*j))+1,0);c.pos=0;}}configure(2.2f,.55f);}
 void configure(float seconds,float tone){seconds=std::clamp(seconds,.4f,6.f);for(auto& l:lines)l.gain=std::pow(10.f,-3.f*float(double(l.buffer.size())/rate)/seconds);damping=float(1-std::exp(-6.28318530718*(900+tone*8500)/rate));highpass=float(1-std::exp(-6.28318530718*100/rate));}
 void process(float inL,float inR,float& outL,float& outR){
 float v[4];for(size_t j=0;j<4;++j){auto& l=lines[j];float x=l.buffer[l.pos];for(auto& c:coils[j])x=c.tick(x);l.lp+=damping*(x-l.lp);l.dc+=highpass*(l.lp-l.dc);v[j]=(l.lp-l.dc)*l.gain;}
 float feedback[4]={(v[0]+v[1]+v[2]+v[3])*.5f,(v[0]-v[1]+v[2]-v[3])*.5f,(v[0]+v[1]-v[2]-v[3])*.5f,(v[0]-v[1]-v[2]+v[3])*.5f};
 for(size_t j=0;j<4;++j){auto& l=lines[j];float drive=j%2?inR:inL;l.buffer[l.pos]=std::tanh(drive*.4f+feedback[j]);if(++l.pos==l.buffer.size())l.pos=0;}
 outL=(v[0]+v[2]-v[3])*.65f;outR=(v[1]+v[3]-v[2])*.65f;
 }
 size_t memoryBytes()const{size_t n=0;for(auto& l:lines)n+=l.buffer.capacity()*sizeof(float);for(auto& a:coils)for(auto& c:a)n+=c.buffer.capacity()*sizeof(float);return n;}
};
}
