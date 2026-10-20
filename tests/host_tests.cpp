#include "parameters.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <filesystem>
using namespace Steinberg;using namespace Steinberg::Vst;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void point(ParameterChanges& changes,ParamID id,double value,int offset=0){int32 i=0,j=0;changes.addParameterData(id,i)->addPoint(offset,value,j);}
int main(int argc,char** argv){try{
 check(argc>1,"plugin path required");std::string error;
 auto module=VST3::Hosting::Module::create(std::filesystem::canonical(argv[1]).string(),error);check(bool(module),error.c_str());
 auto host=owned(new HostApplication);module->getFactory().setHostContext(host);
 IPtr<IComponent> component;IPtr<IEditController> controller;
 for(const auto& info:module->getFactory().classInfos()){
  if(info.category()==kVstAudioEffectClass)component=module->getFactory().createInstance<IComponent>(info.ID());
  if(info.category()==kVstComponentControllerClass)controller=module->getFactory().createInstance<IEditController>(info.ID());
 }
 check(component&&controller,"missing processor or controller");
 check(component->initialize(host)==kResultOk,"processor initialize");check(controller->initialize(host)==kResultOk,"controller initialize");
 FUnknownPtr<IAudioProcessor> processor(component);check(bool(processor),"no audio processor");
 ParameterChanges changes(Jerzy::Count),meters(1);ProcessData data{};data.inputParameterChanges=&changes;data.outputParameterChanges=&meters;
 // Zero-sample parameter flush and persistent state roundtrip.
 data.symbolicSampleSize=kSample32;
 for(int id=0;id<Jerzy::Count;++id)point(changes,id,double(id%5)/4.);
 check(processor->process(data)==kResultOk,"parameter flush");
 MemoryStream saved;check(component->getState(&saved)==kResultOk,"save state");
 saved.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&saved)==kResultOk,"controller restore");
 for(int id=0;id<Jerzy::Count;++id)check(std::abs(controller->getParamNormalized(id)-double(id%5)/4.)<1e-12,"controller state mismatch");
 changes.clearQueue();for(int id=0;id<Jerzy::Count;++id)point(changes,id,0);
 processor->process(data);saved.seek(0,IBStream::kIBSeekSet,nullptr);check(component->setState(&saved)==kResultOk,"processor restore");
 MemoryStream restored;check(component->getState(&restored)==kResultOk,"save restored state");
 check(saved.getSize()==restored.getSize()&&!std::memcmp(saved.getData(),restored.getData(),saved.getSize()),"state roundtrip mismatch");
 MemoryStream truncated(saved.getData(),8);check(component->setState(&truncated)!=kResultOk,"truncated state accepted");
 MemoryStream afterFailure;component->getState(&afterFailure);
 check(afterFailure.getSize()==saved.getSize()&&!std::memcmp(saved.getData(),afterFailure.getData(),saved.getSize()),"bad state modified processor");
 // All normalised defaults must agree between controller and DSP, including stepped parameters.
 MemoryStream initial;
 {ParameterChanges defaults(Jerzy::Count);for(int id=0;id<Jerzy::Count;++id)point(defaults,id,Jerzy::defaults[id]);data.inputParameterChanges=&defaults;processor->process(data);}
 component->getState(&initial);initial.seek(0,IBStream::kIBSeekSet,nullptr);controller->setComponentState(&initial);
 for(int id=0;id<Jerzy::Count;++id)check(std::abs(controller->getParamNormalized(id)-Jerzy::defaults[id])<1e-9,"default mismatch");
 // Existing 0.1.0 sessions have version 2 and exactly 12 parameters.
 // New sync controls must default to OFF / quarter note without shifting old IDs.
 for(int version:{1,2}){
  MemoryStream legacy;IBStreamer stream(&legacy,kLittleEndian);stream.writeInt32(version);
  for(int id=0;id<(version==1?9:12);++id)stream.writeDouble(Jerzy::defaults[id]);
  legacy.seek(0,IBStream::kIBSeekSet,nullptr);check(component->setState(&legacy)==kResultOk,"legacy processor state rejected");
  legacy.seek(0,IBStream::kIBSeekSet,nullptr);check(controller->setComponentState(&legacy)==kResultOk,"legacy controller state rejected");
  for(int id=0;id<Jerzy::Count;++id)check(std::abs(controller->getParamNormalized(id)-Jerzy::defaults[id])<1e-9,"legacy state changed defaults");
 }
 // Real host buffers: both precisions, mono/stereo, true in-place operation.
 for(auto size:{kSample32,kSample64})for(int channels:{1,2}){
  SpeakerArrangement arrangement=channels==1?SpeakerArr::kMono:SpeakerArr::kStereo;
  check(processor->setBusArrangements(&arrangement,1,&arrangement,1)==kResultOk,"bus layout");
  ProcessSetup setup{};setup.processMode=kRealtime;setup.symbolicSampleSize=size;setup.maxSamplesPerBlock=256;setup.sampleRate=48000;
  check(processor->setupProcessing(setup)==kResultOk,"setup");component->activateBus(kAudio,kInput,0,true);component->activateBus(kAudio,kOutput,0,true);
  check(component->setActive(true)==kResultOk,"activate");processor->setProcessing(true);
  float f[2][256]{};double d[2][256]{};float* fp[2]{f[0],f[1]};double* dp[2]{d[0],d[1]};
  for(int c=0;c<channels;++c)for(int i=0;i<256;++i){f[c][i]=float(.1*std::sin(i*.1));d[c][i]=.1*std::sin(i*.1);}
  AudioBusBuffers bus{};bus.numChannels=channels;if(size==kSample32)bus.channelBuffers32=fp;else bus.channelBuffers64=dp;
  changes.clearQueue();for(int id=0;id<Jerzy::Count;++id)point(changes,id,Jerzy::defaults[id]);point(changes,Jerzy::Blend,0);point(changes,Jerzy::Bypass,1);
  data={};data.symbolicSampleSize=size;data.numSamples=256;data.numInputs=data.numOutputs=1;data.inputs=data.outputs=&bus;data.inputParameterChanges=&changes;data.outputParameterChanges=&meters;
  check(processor->process(data)==kResultOk,"audio processing");
  for(int c=0;c<channels;++c)for(int i=0;i<256;++i)check(std::abs((size==kSample32?double(f[c][i]):d[c][i])-.1*std::sin(i*.1))<1e-7,"in-place bypass mismatch");
  processor->setProcessing(false);component->setActive(false);
  // A wet-mix event at sample 128 must not alter samples 0..127.
  component->setActive(true);processor->setProcessing(true);
  for(int c=0;c<channels;++c)for(int i=0;i<256;++i){f[c][i]=.2f;d[c][i]=.2;}
  changes.clearQueue();
  for(int id=0;id<Jerzy::Count;++id)point(changes,id,Jerzy::defaults[id]);
  point(changes,Jerzy::Bypass,0);point(changes,Jerzy::Blend,0);point(changes,Jerzy::Feedback,0);point(changes,Jerzy::Blend,1,128);
  check(processor->process(data)==kResultOk,"sample-offset automation processing");
  for(int i=0;i<128;++i)check(std::abs((size==kSample32?double(f[0][i]):d[0][i])-.2)<1e-7,"automation applied before sample offset");
  check((size==kSample32?double(f[0][255]):d[0][255])<.19,"automation event not applied");
  processor->setProcessing(false);component->setActive(false);
 }
 // Drive actual VST3 processContext tempo into the plugin, then change it.
 SpeakerArrangement stereo=SpeakerArr::kStereo;processor->setBusArrangements(&stereo,1,&stereo,1);
 ProcessSetup syncSetup{};syncSetup.processMode=kRealtime;syncSetup.symbolicSampleSize=kSample32;syncSetup.maxSamplesPerBlock=256;syncSetup.sampleRate=48000;
 processor->setupProcessing(syncSetup);ProcessContext context{};context.state=ProcessContext::kTempoValid;
 for(double bpm:{100.,150.}){
  context.tempo=bpm;component->setActive(true);processor->setProcessing(true);
  float samples[2][256]{};float* buffers[2]{samples[0],samples[1]};AudioBusBuffers bus{};bus.numChannels=2;bus.channelBuffers32=buffers;
  data={};data.symbolicSampleSize=kSample32;data.numSamples=256;data.numInputs=data.numOutputs=1;data.inputs=data.outputs=&bus;data.processContext=&context;data.inputParameterChanges=&changes;
  changes.clearQueue();for(int id=0;id<Jerzy::Count;++id)point(changes,id,Jerzy::defaults[id]);
  point(changes,Jerzy::Blend,1);point(changes,Jerzy::Feedback,0);point(changes,Jerzy::Depth,0);point(changes,Jerzy::TempoSync,1);point(changes,Jerzy::Division,.3);
  double peak=0.;int peakAt=0;
  for(int block=0;block<140;++block){
   for(auto& channel:samples)std::fill(std::begin(channel),std::end(channel),0.f);
   if(block==0)samples[0][0]=.5f;
   bus.silenceFlags=0;check(processor->process(data)==kResultOk,"host tempo processing");changes.clearQueue();
   for(int i=0;i<256;++i)if(std::abs(samples[0][i])>peak){peak=std::abs(samples[0][i]);peakAt=block*256+i;}
  }
  const int expected=int(48000.*60./bpm);
  check(peak>1e-8&&peakAt>=expected-2&&peakAt<expected+384,"host BPM not applied to sync delay");
  processor->setProcessing(false);component->setActive(false);
 }
 controller->terminate();component->terminate();controller=nullptr;processor=nullptr;component=nullptr;
 std::cout<<"Host tests passed: state, truncated state, defaults, mono/stereo, float/double, in-place bypass, sample-offset automation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
