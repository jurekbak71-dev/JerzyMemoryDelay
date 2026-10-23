#pragma once
// Cabinet IR: uniform partitioned FFT convolver, realtime allocation free.
// A 64-frame block adds exactly 64 frames of convolution processing latency.
// WAV PCM16/PCM24/PCM32 or IEEE float32, mono or stereo; on load resample to JACK SR.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace mfx4 {
constexpr int IR_BLOCK=64,IR_FFT=128,IR_MAX_FRAMES=32768;
using C=std::complex<float>;
inline void fft(std::array<C,IR_FFT>& v,bool inv){
 for(int i=1,j=0;i<IR_FFT;++i) {
  int bit=IR_FFT>>1;
  for(;j&bit;bit>>=1)j^=bit;
  j^=bit;
  if(i<j)std::swap(v[i],v[j]);
 }
 for(int len=2;len<=IR_FFT;len<<=1){
  const float step=(inv?2.f:-2.f)*3.14159265358979323846f/float(len);
  const C root(std::cos(step),std::sin(step));
  for(int offset=0;offset<IR_FFT;offset+=len){
   C w(1,0);
   for(int k=0;k<len/2;++k){
    C a=v[offset+k],b=w*v[offset+k+len/2];
    v[offset+k]=a+b;v[offset+k+len/2]=a-b;w*=root;
   }
  }
 }
 if(inv)for(auto& v0:v)v0/=IR_FFT;
}
inline uint16_t u16(const unsigned char* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline uint32_t u32(const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
struct Wav {
 int sampleRate=0,channels=0;
 std::array<std::vector<float>,2> samples;
};
inline bool readWav(const std::string& path,Wav& result,std::string& err){
 std::ifstream input(path,std::ios::binary);
 if(!input){err="Cannot open IR WAV";return false;}
 input.seekg(0,std::ios::end); auto len=input.tellg();
 if(len<44||len>8*1024*1024){err="IR WAV must be 44 B - 8 MB";return false;}
 input.seekg(0);
 std::vector<unsigned char> raw(size_t(len),0);
 input.read(reinterpret_cast<char*>(raw.data()),std::streamsize(raw.size()));
 if(!input){err="WAV read error";return false;}
 if(std::memcmp(raw.data(),"RIFF",4)||std::memcmp(raw.data()+8,"WAVE",4)){err="Expected WAV RIFF";return false;}
 int channels=0,rate=0,bits=0,format=0,alignment=0;
 size_t audio=0,bytes=0;
 for(size_t p=12;p+8<=raw.size();) {
  uint32_t sz=u32(raw.data()+p+4);
  size_t start=p+8;
  if(start>raw.size()||size_t(sz)>raw.size()-start){err="Corrupt WAV chunk";return false;}
  if(!std::memcmp(raw.data()+p,"fmt ",4) && sz>=16){
   format=u16(raw.data()+start);
   channels=u16(raw.data()+start+2);
   rate=int(u32(raw.data()+start+4));
   alignment=u16(raw.data()+start+12);
   bits=u16(raw.data()+start+14);
  }
  if(!std::memcmp(raw.data()+p,"data",4)){audio=start;bytes=sz;}
  p=start+size_t(sz)+(sz&1);
 }
 if(channels<1||channels>2||rate<8000||rate>192000||
    !((format==1&&(bits==16||bits==24||bits==32))||(format==3&&bits==32))||
    alignment!=channels*(bits/8)||bytes<size_t(alignment)){err="Unsupported WAV PCM/float format";return false;}
 size_t frames=bytes/size_t(alignment);
 if(frames>size_t(rate)*8){err="IR too long";return false;}
 result.sampleRate=rate;result.channels=channels;
 for(int c=0;c<channels;++c)result.samples[c].reserve(frames);
 for(size_t i=0;i<frames;++i){
  for(int c=0;c<channels;++c){
   const auto* s=raw.data()+audio+i*size_t(alignment)+size_t(c*bits/8);
   float value=0;
   if(format==3){std::memcpy(&value,s,4);}
   else if(bits==16){value=float(int16_t(u16(s)))/32768.f;}
   else if(bits==24){int32_t v=int32_t(s[0])|(int32_t(s[1])<<8)|(int32_t(s[2])<<16);
    if(v&0x800000)v|=~0xffffff;value=float(v)/8388608.f;}
   else {int32_t v=int32_t(u32(s));value=float(double(v)/2147483648.);}
   if(!std::isfinite(value)){err="Non-finite IR";return false;}
   result.samples[c].push_back(std::clamp(value,-4.f,4.f));
  }
 }
 if(channels==1)result.samples[1]=result.samples[0];
 return true;
}
struct Conv {
 std::vector<std::array<C,IR_FFT>> H,history;
 std::array<float,IR_BLOCK> input{},output{};
 std::array<C,IR_FFT> previous{};
 size_t head=0;
 int position=0;
 void prepare(const std::vector<float>& taps){
  size_t n=std::max(size_t(1),(taps.size()+IR_BLOCK-1)/IR_BLOCK);
  H.resize(n);history.resize(n);
  for(size_t block=0;block<n;++block){
   auto& h=H[block];h.fill(C{});
   for(int i=0;i<IR_BLOCK;++i) {
    const size_t idx=block*IR_BLOCK+size_t(i);
    if(idx<taps.size())h[size_t(i)]=C(taps[idx],0.f);
   }
   fft(h,false);
  }
  for(auto& x:history)x.fill(C{});
  previous.fill(C{});
  input.fill(0);output.fill(0);head=0;position=0;
 }
 float tick(float in) {
  // return previous completed block: fixed 64-frame latency.
  const float y=output[size_t(position)];
  input[size_t(position)]=std::isfinite(in)?in:0.f;
  if(++position==IR_BLOCK){
   position=0;
   std::array<C,IR_FFT> frame{};
   for(int i=0;i<IR_BLOCK;++i){frame[size_t(i)]=previous[size_t(i)];frame[size_t(i+IR_BLOCK)]=C(input[size_t(i)],0);}
   fft(frame,false);
   history[head]=frame;
   std::array<C,IR_FFT> mixed{};mixed.fill(C{});
   for(size_t part=0;part<H.size();++part){
    const auto& spectrum=history[(head+H.size()-part)%H.size()];
    for(int k=0;k<IR_FFT;++k)mixed[size_t(k)]+=spectrum[size_t(k)]*H[part][size_t(k)];
   }
   fft(mixed,true);
   for(int i=0;i<IR_BLOCK;++i){
    output[size_t(i)]=mixed[size_t(i+IR_BLOCK)].real();
    previous[size_t(i)]=C(input[size_t(i)],0.f);
   }
   head=(head+1)%H.size();
  }
  return std::isfinite(y)?std::clamp(y,-8.f,8.f):0.f;
 }
};
struct CabinetIR {
 std::array<Conv,2> cv;
 std::string filename;
 static std::unique_ptr<CabinetIR> load(const std::string& path,double targetRate,std::string& err) {
  Wav w;
  if(!readWav(path,w,err))return {};
  auto out=std::make_unique<CabinetIR>();
  out->filename=std::filesystem::path(path).filename().string();
  for(int c=0;c<2;++c){
   const auto& src=w.samples[c];
   const size_t count=std::min<size_t>(IR_MAX_FRAMES,std::max<size_t>(1,size_t(std::ceil(src.size()*targetRate/w.sampleRate))));
   std::vector<float> taps; taps.reserve(count);
   for(size_t n=0;n<count;++n){
    double p=double(n)*double(w.sampleRate)/targetRate;
    size_t i=std::min(src.size()-1,size_t(p));
    size_t j=std::min(src.size()-1,i+1);
    double frac=std::clamp(p-double(i),0.,1.);
    taps.push_back(float(src[i]*(1.-frac)+src[j]*frac));
   }
   out->cv[c].prepare(taps);
  }
  return out;
 }
 void tick(const float x[2],float y[2]){
  for(int c=0;c<2;++c)y[c]=cv[c].tick(x[c]);
 }
};
struct CabinetFilter {
 float low=0,high=0;
 float tick(float x,double sr,float hpHz,float lpHz){
  const float a=float(1.-std::exp(-6.28318530718*std::clamp(double(lpHz),100.,sr*.45)/sr));
  low+=(x-low)*a;
  const float b=float(1.-std::exp(-6.28318530718*std::clamp(double(hpHz),20.,sr*.45)/sr));
  high+=(low-high)*b;
  return low-high;
 }
};
}
