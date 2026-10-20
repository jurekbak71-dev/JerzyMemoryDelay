#include "dsp.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#if JERZY_GUI
#include "vstgui/plugin-bindings/vst3editor.h"
namespace Jerzy { Steinberg::IPlugView* createEditor(Steinberg::Vst::EditController*); }
#endif
#include <cstring>
#include <cstdio>
#include "pluginterfaces/base/ustring.h"
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace Jerzy {
static const FUID processorID(0xAA719D42,0x3E7D4B81,0xAAE7C883,0x45E016B2);
static const FUID controllerID(0xBE7CA5D9,0xCA984617,0xB99A5712,0x2F38D819);
bool readState(IBStream* stream,Parameters& p){
    if(!stream)return false;
    IBStreamer s(stream,kLittleEndian);int32 version=0;
    if(!s.readInt32(version)||(version<1||version>3))return false;
    Parameters next;
    for(int i=0;i<(version==1?9:version==2?12:Count);++i){double v;if(!s.readDouble(v)||!std::isfinite(v))return false;next.set(i,v);}
    p=next;return true;
}
class Processor final:public AudioEffect {
    Parameters p;Engine engine;double tempo=120.;
public:
    Processor(){setControllerClass(controllerID);processContextRequirements.needTempo();}
    static FUnknown* create(void*){return static_cast<IAudioProcessor*>(new Processor);}
    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE {
        auto r=AudioEffect::initialize(context);if(r!=kResultOk)return r;
        addAudioInput(STR16("Input"),SpeakerArr::kStereo);addAudioOutput(STR16("Output"),SpeakerArr::kStereo);return kResultOk;
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in,int32 ni,SpeakerArrangement* out,int32 no) SMTG_OVERRIDE {
        if(ni!=1||no!=1||in[0]!=out[0]||(in[0]!=SpeakerArr::kMono&&in[0]!=SpeakerArr::kStereo))return kResultFalse;
        return AudioEffect::setBusArrangements(in,ni,out,no);
    }
    tresult PLUGIN_API canProcessSampleSize(int32 s) SMTG_OVERRIDE {return s==kSample32||s==kSample64?kResultTrue:kResultFalse;}
    tresult PLUGIN_API setupProcessing(ProcessSetup& s) SMTG_OVERRIDE {engine.prepare(s.sampleRate);return AudioEffect::setupProcessing(s);}
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE {if(state)engine.reset();return AudioEffect::setActive(state);}
    uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE {return kInfiniteTail;}
    tresult PLUGIN_API setState(IBStream* s) SMTG_OVERRIDE {return readState(s,p)?kResultOk:kResultFalse;}
    tresult PLUGIN_API getState(IBStream* stream) SMTG_OVERRIDE {
        if(!stream)return kResultFalse;IBStreamer s(stream,kLittleEndian);
        if(!s.writeInt32(3))return kResultFalse;
        for(auto v:p.value)if(!s.writeDouble(v))return kResultFalse;return kResultOk;
    }
    tresult PLUGIN_API process(ProcessData& d) SMTG_OVERRIDE {
        if(d.processContext&&(d.processContext->state&ProcessContext::kTempoValid)&&validTempo(d.processContext->tempo))tempo=d.processContext->tempo;
        if(d.symbolicSampleSize!=kSample32&&d.symbolicSampleSize!=kSample64)return kResultFalse;
        // Fixed-size queue cursors: no allocation, locks or sorting on the audio thread.
        std::array<IParamValueQueue*,Count> queues{};std::array<int32,Count> cursors{};
        if(d.inputParameterChanges)for(int32 q=0;q<d.inputParameterChanges->getParameterCount();++q){
            auto* queue=d.inputParameterChanges->getParameterData(q);
            if(queue&&queue->getParameterId()<Count)queues[queue->getParameterId()]=queue;
        }
        auto changes=[&](int32 sample){for(int id=0;id<Count;++id)if(auto* q=queues[id]){
            while(cursors[id]<q->getPointCount()){
                int32 offset;ParamValue v;
                if(q->getPoint(cursors[id],offset,v)!=kResultTrue){++cursors[id];continue;}
                if(offset>sample)break;p.set(id,v);++cursors[id];
            }
        }};
        if(d.numSamples<=0){changes(0);return kResultOk;}
        if(d.numInputs<1||d.numOutputs<1){changes(d.numSamples-1);return kResultOk;}
        const auto ch=std::min<int32>(2,std::min(d.inputs[0].numChannels,d.outputs[0].numChannels));
        if(ch<=0)return kResultOk;
        for(int32 sample=0;sample<d.numSamples;++sample){
            changes(sample);double input[2]{},output[2]{};
            for(int c=0;c<ch;++c){
                const bool silent=(d.inputs[0].silenceFlags&(uint64(1)<<c))!=0;
                if(d.symbolicSampleSize==kSample32){auto** b=d.inputs[0].channelBuffers32;if(!silent&&b&&b[c])input[c]=b[c][sample];}
                else{auto** b=d.inputs[0].channelBuffers64;if(!silent&&b&&b[c])input[c]=b[c][sample];}
            }
            engine.tick(input,output,ch,p,tempo);
            for(int c=0;c<ch;++c){
                if(d.symbolicSampleSize==kSample32){auto** b=d.outputs[0].channelBuffers32;if(b&&b[c])b[c][sample]=static_cast<float>(output[c]);}
                else{auto** b=d.outputs[0].channelBuffers64;if(b&&b[c])b[c][sample]=output[c];}
            }
        }
        if(d.outputParameterChanges){
            int32 index=0;
            if(auto* q=d.outputParameterChanges->addParameterData(OverloadMeter,index)){
                int32 point=0;q->addPoint(d.numSamples-1,std::clamp(engine.peak(),0.,1.),point);
            }
        }
        d.outputs[0].silenceFlags=0;return kResultOk;
    }
};
class Controller final:public EditControllerEx1 {
public:
    static FUnknown* create(void*){return static_cast<IEditController*>(new Controller);}
    tresult PLUGIN_API getParamStringByValue(ParamID id,ParamValue value,String128 text) SMTG_OVERRIDE {
        char buffer[64]{};
        switch(id){
        case Blend:case Depth:case Squelch:std::snprintf(buffer,sizeof(buffer),"%.1f %%",value*100.);break;
        case Feedback:std::snprintf(buffer,sizeof(buffer),"%.1f %%",value*116.);break;
        case Level:std::snprintf(buffer,sizeof(buffer),"%+.1f dB",value*24.-12.);break;
        case ModRate:std::snprintf(buffer,sizeof(buffer),"%.2f Hz",.15+7.85*value);break;
        case Delay:std::snprintf(buffer,sizeof(buffer),"%.0f ms",delayMs(value));break;
        default:return EditControllerEx1::getParamStringByValue(id,value,text);
        }
        UString(text,128).fromAscii(buffer);return kResultOk;
    }
    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE {
        auto r=EditControllerEx1::initialize(context);if(r!=kResultOk)return r;
        parameters.addParameter(new RangeParameter(STR16("Blend"),Blend,STR16("%"),0,100,defaults[Blend]*100));
        parameters.addParameter(new RangeParameter(STR16("Level"),Level,STR16("dB"),-12,12,0));
        parameters.addParameter(new RangeParameter(STR16("Feedback"),Feedback,STR16("%"),0,116,defaults[Feedback]*116));
        parameters.addParameter(new RangeParameter(STR16("Mod Rate"),ModRate,STR16("Hz"),.15,8.,.15+7.85*defaults[ModRate]));
        parameters.addParameter(new RangeParameter(STR16("Depth"),Depth,STR16("%"),0,100,defaults[Depth]*100));
        parameters.addParameter(new RangeParameter(STR16("Delay"),Delay,STR16("ms"),20,550,delayMs(defaults[Delay])));
        auto* models=new StringListParameter(STR16("Circuit"),Model);
        models->appendString(STR16("EH7500 / SAD"));models->appendString(STR16("Issue A / SAD"));models->appendString(STR16("Squelch / SAD"));
        models->appendString(STR16("Deluxe / MN"));models->appendString(STR16("Boy / BL"));models->appendString(STR16("Toy / BL"));
        models->setNormalized(defaults[Model]);parameters.addParameter(models);
        auto* waves=new StringListParameter(STR16("Modulation Wave"),Wave);waves->appendString(STR16("Sine"));waves->appendString(STR16("Triangle"));parameters.addParameter(waves);
        parameters.addParameter(STR16("Bypass"),nullptr,1,0,ParameterInfo::kCanAutomate|ParameterInfo::kIsBypass,Bypass);
        parameters.addParameter(STR16("Boost"),nullptr,1,defaults[Boost],ParameterInfo::kCanAutomate,Boost);
        parameters.addParameter(new RangeParameter(STR16("Squelch"),Squelch,STR16("%"),0,100,defaults[Squelch]*100));
        parameters.addParameter(STR16("Modulation"),nullptr,1,defaults[ModEnable],ParameterInfo::kCanAutomate,ModEnable);
        parameters.addParameter(STR16("Tempo Sync"),nullptr,1,defaults[TempoSync],ParameterInfo::kCanAutomate,TempoSync);
        auto* notes=new StringListParameter(STR16("Note Division"),Division);
        for(const auto& note:divisions){String128 name{};UString(name,128).fromAscii(note.name);notes->appendString(name);}
        notes->setNormalized(defaults[Division]);parameters.addParameter(notes);
        parameters.addParameter(STR16("Overload"),nullptr,0,0,ParameterInfo::kIsReadOnly,OverloadMeter);
        return kResultOk;
    }
    tresult PLUGIN_API setComponentState(IBStream* s) SMTG_OVERRIDE {
        Parameters p;if(!readState(s,p))return kResultFalse;
        for(int i=0;i<Count;++i)setParamNormalized(i,p.value[i]);return kResultOk;
    }
    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE {
#if JERZY_GUI
        if(name&&std::strcmp(name,ViewType::kEditor)==0){
            return createEditor(this);
        }
#endif
        return nullptr;
    }
};
}
BEGIN_FACTORY_DEF("Jerzy Audio","","")
DEF_CLASS2(INLINE_UID_FROM_FUID(Jerzy::processorID),PClassInfo::kManyInstances,kVstAudioEffectClass,
    "Jerzy Memory Delay",Vst::kDistributable,"Fx|Delay","0.1.1",kVstVersionString,Jerzy::Processor::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(Jerzy::controllerID),PClassInfo::kManyInstances,kVstComponentControllerClass,
    "Jerzy Memory Delay Controller",0,"","0.1.1",kVstVersionString,Jerzy::Controller::create)
END_FACTORY
