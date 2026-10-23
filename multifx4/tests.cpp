#include "bass_ambient.h"
#include "ir_cab.h"
#include <cassert>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
int main() {
 mfx4::BassAmbient synth;synth.prepare(44100);
 synth.bassOn=true;synth.padOn=true;
 double total=0;
 for(int i=0;i<44100;++i){
  float x[2]{};
  synth.process(x,115);
  assert(std::isfinite(x[0])&&std::isfinite(x[1]));
  total+=std::abs(x[0])+std::abs(x[1]);
 }
 assert(total>0.1); // native original DSP actually produces sound without MIDI
 synth.bassOn=false;synth.padOn=false;
 // Create a 16-bit PCM mono impulse for convolution.
 const char* file="/tmp/jfx_cab_test.wav";
 std::ofstream out(file,std::ios::binary);
 auto w16=[&](uint16_t v){char b[2]{char(v),char(v>>8)};out.write(b,2);};
 auto w32=[&](uint32_t v){w16(uint16_t(v));w16(uint16_t(v>>16));};
 out.write("RIFF",4);w32(36+64*2);out.write("WAVEfmt ",8);w32(16);
 w16(1);w16(1);w32(44100);w32(88200);w16(2);w16(16);
 out.write("data",4);w32(64*2);
 w16(16384);for(int i=1;i<64;++i)w16(0);
 out.close();
 std::string err;
 auto ir=mfx4::CabinetIR::load(file,44100,err);
 if(!ir){std::cerr<<err<<"\n";return 1;}
 float sum=0;float peak=0;int peakAt=-1;
 for(int i=0;i<256;++i){
  float input[2]{i==0?1.f:0.f,i==0?1.f:0.f}, output[2]{};
  ir->tick(input,output);
  sum+=std::abs(output[0]);if(std::abs(output[0])>peak){peak=std::abs(output[0]);peakAt=i;}
  assert(std::isfinite(output[0])&&std::isfinite(output[1]));
 }
 assert(std::abs(peak-.5)<.01&&peakAt>=64&&peakAt<128);
 assert(sum>.49f&&sum<.51f);
 std::cout<<"PASS Original BassAmbient standalone audio, 64-frame IR FFT, WAV loading\n";
}
