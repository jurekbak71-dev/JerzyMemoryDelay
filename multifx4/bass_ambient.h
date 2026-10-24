#pragma once
// Native BassAmbient DSP from user's JerzyBassAmbientVST3 Source/Core.h.
// Separate from guitar chain. No allocation and no mutex in audio callback.
#include "vendor/BassAmbientCore.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mfx4 {
struct BassAmbient {
 jerzy::Instrument instrument;
 jerzy::Parameters settings;
 double sampleRate=44100, ppq=0, tempo=120;
 int root=40;
 bool started=false;
 bool bassOn=false, padOn=false;
 float gain=.35f,pan=0.f;
 // Settings are copied from controller to audio thread through atomics in host.
 void prepare(double sr){
  sampleRate=sr;instrument.prepare(sr);ppq=0;
  settings.direct=false;settings.latch=true;settings.restart=true;
  settings.bass=true;settings.pad=true;settings.padMode=2; // Krell
  settings.style=2; settings.scale=1;settings.model=0;
  settings.padAttack=2.;settings.padRelease=5.;settings.evolution=7.;
  settings.bassFX=.15;settings.padFX=.40;settings.masterFX=0;
  settings.master=.72;settings.limiter=true;
 }
 void setRoot(int n) {
  n=std::clamp(n,24,84);
  if(n!=root){instrument.allOff();root=n;started=false;}
 }
 void process(float out[2],double bpm) {
  out[0]=out[1]=0.f;
  if(!started) {instrument.noteOn(root,1,.76,settings,ppq);started=true;}
  settings.bass=bassOn;settings.pad=padOn;
  instrument.beginBlock(ppq,bpm,true);
  const auto y=instrument.next(settings,ppq);
  double theta=(std::clamp(double(pan),-1.,1.)+1.)*.7853981633974483;
  const double a=std::cos(theta)*1.4142135623730951, b=std::sin(theta)*1.4142135623730951;
  out[0]=float(std::clamp(y.l*double(gain)*a,-2.,2.));
  out[1]=float(std::clamp(y.r*double(gain)*b,-2.,2.));
  ppq+=std::clamp(bpm,20.,300.)/(60.*sampleRate);
 }
};
}
