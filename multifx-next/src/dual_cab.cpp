#include "dual_cab.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace nextfx {
DualCab::DualCab(double sr,uint32_t max,const std::string& asset):max_(max),sr_(sr){
 if(!std::isfinite(sr)||sr<8000||sr>192000||!max)throw std::runtime_error("invalid CAB configuration");
 std::ifstream f(asset); if(!f)throw std::runtime_error("cannot read CAB asset");
 nlohmann::json j;f>>j;
 auto mode=j.value("dry_mode",std::string("aligned"));
 if(mode!="aligned"&&mode!="legacy")throw std::runtime_error("invalid dry_mode"); aligned_=mode=="aligned";
 for(unsigned s=0;s<2;++s){auto path=j.value(s?"ir_b":"ir_a",std::string{});if(!path.empty()){
 if(!std::filesystem::path(path).is_absolute())throw std::runtime_error("IR path must be absolute");ir_[s]=CabinetIR::load(path,sr);}}
}
void DualCab::process(uint32_t frames,const float* const* in,float* const* out,const float* p) noexcept {
 if(frames>max_)return;
 auto norm=[](float v){return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;};
 float mix=norm(p[0]),blend=norm(p[1]),gain=std::pow(10.f,(norm(p[2])*36.f-18.f)/20.f);
 float lo=norm(p[3]),hi=norm(p[4]);
 if(lo!=lastLow_||hi!=lastHigh_){
 auto coeff=[&](double hz,double minimum){return float(1.-std::exp(-6.28318530718*std::clamp(hz,minimum,sr_*.45)/sr_));};
 a_=coeff(1000.f*std::pow(10.f,hi*1.45f),100.);b_=coeff(30.f*std::pow(10.f,lo*1.6f),20.);lastLow_=lo;lastHigh_=hi;}
 bool enabled=norm(p[5])>=.5f&&(ir_[0]||ir_[1]);
 std::array<float,2> target{enabled?mix*(1-blend):0.f,enabled?mix*blend:0.f};
 for(uint32_t i=0;i<frames;++i){
 float x[2]{std::isfinite(in[0][i])?in[0][i]:0.f,std::isfinite(in[1][i])?in[1][i]:0.f};float y[2][2]{};
 bool ready=true;
 for(unsigned s=0;s<2;++s){
 bool needed=bool(ir_[s])&&(target[s]>0||weights_[s]>0);
 if(needed&&!active_[s])prime_[s]=64;
 active_[s]=needed;
 if(needed){
 ir_[s]->tick(x,y[s]);for(unsigned c=0;c<2;++c)y[s][c]=filters_[s][c].tick(y[s][c],a_,b_);
 if(prime_[s]){--prime_[s];ready=false;}
 }
 }
 // Hold existing A/B balance until every newly requested IR has a valid block.
 // Always permit a complete bypass to fade out immediately.
 if(ready||!enabled||mix==0)for(unsigned s=0;s<2;++s)
 weights_[s]+=std::clamp(target[s]-weights_[s],-1.f/64,1.f/64);
 float amount=weights_[0]+weights_[1];
 for(unsigned c=0;c<2;++c){float d=aligned_?dry_[c][pos_]:x[c];dry_[c][pos_]=x[c];
 float wet=(y[0][c]*weights_[0]+y[1][c]*weights_[1])*gain;
 float result=d*(1-amount)+wet;out[c][i]=std::isfinite(result)?result:0.f;}

 pos_=(pos_+1)%64;
 }
}
uint64_t DualCab::resource_bytes() const noexcept {uint64_t n=sizeof(*this);for(auto& ir:ir_)if(ir)n+=ir->resource_bytes();return n;}
}
