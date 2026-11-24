#include "pffft_cab.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Fixture {
 std::vector<unsigned char> bytes;
 void u16(uint16_t x){bytes.push_back(x&255);bytes.push_back(x>>8);}
 void u32(uint32_t x){u16(uint16_t(x));u16(uint16_t(x>>16));}
 void tag(const char* s){for(unsigned i=0;i<4;++i)bytes.push_back(s[i]);}
 Fixture(unsigned bits,unsigned channels,unsigned rate,const std::vector<std::array<double,2>>& samples,bool floating=false){
 tag("RIFF");u32(36+uint32_t(samples.size()*channels*(bits/8)));tag("WAVE");tag("fmt ");u32(16);u16(floating?3:1);u16(channels);u32(rate);u32(rate*channels*(bits/8));u16(channels*(bits/8));u16(bits);tag("data");u32(uint32_t(samples.size()*channels*(bits/8)));
 for(const auto& sample:samples)for(unsigned c=0;c<channels;++c){
 if(floating){float f=float(sample[c]);uint32_t raw;std::memcpy(&raw,&f,4);u32(raw);}
 else {int64_t value=int64_t(std::llround(sample[c]*std::ldexp(1.,int(bits)-1)));uint32_t raw=uint32_t(value);for(unsigned byte=0;byte<bits/8;++byte)bytes.push_back(uint8_t(raw>>(byte*8)));}
 }
 }
 void save(const std::filesystem::path& path) const {std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!f)throw std::runtime_error("fixture write failed");}
};
void rejection(const Fixture& f,const std::filesystem::path& path){f.save(path);bool threw=false;try{auto ir=nextfx::CabinetIR::load(path.string(),44100);}catch(const std::exception&){threw=true;}check(threw,"malformed WAV accepted");}
}
int main(){try{
 const auto dir=std::filesystem::temp_directory_path()/("next-ir-formats-"+std::to_string(uintptr_t(&std::cout)));std::filesystem::create_directories(dir);auto path=dir/"fixture.wav";
 unsigned cases=0;
 const std::vector<std::array<double,2>> taps{{-.5,.25},{-1.,-.75},{.125,-.25},{0.,.5},{.375,-.125}};
 for(unsigned rate:{44100u,48000u,96000u})for(unsigned channels:{1u,2u})for(unsigned format:{16u,24u,32u,33u}){
 bool floating=format==33;unsigned bits=floating?32:format;Fixture(bits,channels,rate,taps,floating).save(path);auto ir=nextfx::CabinetIR::load(path.string(),rate);
 for(unsigned n=0;n<160;++n){float x[2]{n==0?1.f:0.f,n==0?1.f:0.f},y[2]{};ir->tick(x,y);for(unsigned c=0;c<2;++c){double expected=n>=64&&n<64+taps.size()?taps[n-64][channels==1?0:c]:0;check(std::isfinite(y[c])&&std::abs(y[c]-expected)<2e-5,"format/channel/signed/latency impulse mismatch");}}
 ++cases;
 }
 // Selected-bin complex response: direct FIR DFT versus impulse recovered from FFT convolver.
 const std::vector<std::array<double,2>> response{{.25,.125},{-.125,.25},{.0625,-.125},{.375,.0625},{-.25,.375},{.125,-.25},{.03125,.125}};
 Fixture(32,2,48000,response,true).save(path);auto ir=nextfx::CabinetIR::load(path.string(),48000);std::array<std::vector<double>,2> actual;
 for(unsigned n=0;n<64+128;++n){float x[2]{n==0?1.f:0.f,n==0?1.f:0.f},y[2]{};ir->tick(x,y);if(n>=64)for(unsigned c=0;c<2;++c)actual[c].push_back(y[c]);}
 double maxDftError=0;
 for(unsigned bin:{1u,3u,7u,13u,29u,47u,63u})for(unsigned c=0;c<2;++c){std::complex<double> expected{},observed{};for(unsigned n=0;n<128;++n){auto phase=std::polar(1.,-2.*3.14159265358979323846*bin*n/128.);observed+=actual[c][n]*phase;if(n<response.size())expected+=response[n][c]*phase;}maxDftError=std::max(maxDftError,std::abs(observed-expected));check(std::abs(observed-expected)<2e-5,"selected-bin frequency response mismatch");}
 // Max-length IR must retain its final tap, beyond every preceding partition.
 std::vector<std::array<double,2>> longTaps(32768,{0,0});longTaps[0]={.5,.25};longTaps.back()={-.25,.125};Fixture(24,2,44100,longTaps).save(path);auto longest=nextfx::CabinetIR::load(path.string(),44100);
 for(unsigned n=0;n<32768+128;++n){float x[2]{n==0?1.f:0.f,n==0?1.f:0.f},y[2]{};longest->tick(x,y);for(unsigned c=0;c<2;++c){double expected=n==64?longTaps[0][c]:n==64+32767?longTaps.back()[c]:0;check(std::isfinite(y[c])&&std::abs(y[c]-expected)<2e-5,"max-length final partition mismatch");}}
 Fixture base(16,1,44100,taps);auto bad=base;bad.bytes[20]=2;rejection(bad,path);bad=base;bad.bytes[32]=3;rejection(bad,path);bad=base;bad.bytes.pop_back();rejection(bad,path);bad=base;bad.bytes[34]=8;rejection(bad,path);
 auto nonfinite=taps;nonfinite[1][0]=NAN;rejection(Fixture(32,1,44100,nonfinite,true),path);
 std::filesystem::remove_all(dir);std::cout<<"PASS IR formats "<<cases<<" PCM/float/channel/rate cases; selected-bin DFT max_abs="<<maxDftError<<"; 32768-tap final partition; 5 malformed/nonfinite rejections\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
