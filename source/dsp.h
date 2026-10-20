#pragma once
#include "parameters.h"
#include <vector>
namespace Jerzy {
class Engine {
    struct Channel {
        std::vector<double> line;
        double pre1=0,pre2=0,post1=0,post2=0,low=0,feedback=0,env=0,boostLow=0;
        double previousCharge=0,heldCharge=0,oldHeldCharge=0,compressEnv=0,expandEnv=0;
    };
    std::array<Channel,2> channels;
    std::array<double,Count> smooth=defaults;
    double sr=48000,phase=0,delaySamples=0,overload=0,clockPhase=0;
    std::size_t pos=0;
    bool first=true;
    int activeModel=3;
    double modelFade=1.;
    static constexpr double pi=3.14159265358979323846;
    static double sat(double x,double d){return std::tanh(x*d)/d;}
public:
    void prepare(double sampleRate) {
        sr=std::isfinite(sampleRate)?std::clamp(sampleRate,8000.,384000.):48000.;
        for(auto& c:channels)c.line.assign(8192,0.);
        reset();
    }
    void reset(){
        for(auto& c:channels){std::fill(c.line.begin(),c.line.end(),0.);c.pre1=c.pre2=c.post1=c.post2=c.low=c.feedback=c.env=c.boostLow=c.previousCharge=c.heldCharge=c.oldHeldCharge=c.compressEnv=c.expandEnv=0;}
        pos=0;phase=0;clockPhase=0;first=true;overload=0;modelFade=1.;
    }
    double peak()const{return overload;}
    // Single-sample entry supports sample-offset host automation and in-place audio.
    void tick(const double* input,double* output,int count,const Parameters& p,double tempo=120.){
        if(channels[0].line.empty()){for(int c=0;c<count;++c)output[c]=input[c];return;}
        const double smoothing=1.-std::exp(-1./(.012*sr));
        if(first){smooth=p.value;activeModel=p.model();delaySamples=requestedDelayMs(p,smooth[Delay],activeModel,tempo)*sr*.001;first=false;}
        // Switching physical circuits fades through dry audio and clears the old circuit.
        if(activeModel!=p.model()){
            modelFade=std::max(0.,modelFade-1./(.02*sr));
            if(modelFade==0.){
                for(auto& c:channels){std::fill(c.line.begin(),c.line.end(),0.);c.pre1=c.pre2=c.post1=c.post2=c.low=c.feedback=c.env=c.boostLow=c.previousCharge=c.heldCharge=c.oldHeldCharge=c.compressEnv=c.expandEnv=0.;}
                pos=0;clockPhase=0.;
                activeModel=p.model();
                delaySamples=requestedDelayMs(p,smooth[Delay],activeModel,tempo)*sr*.001;
            }
        }else modelFade=std::min(1.,modelFade+1./(.02*sr));
        const auto& profile=profiles[activeModel];
        for(int i=0;i<Count;++i)smooth[i]+=smoothing*(p.value[i]-smooth[i]);
        const double target=requestedDelayMs(p,smooth[Delay],activeModel,tempo)*sr*.001;
        delaySamples+=(1.-std::exp(-1./(.035*sr)))*(target-delaySamples);
        const double rate=.15+7.85*smooth[ModRate];
        phase+=rate/sr;if(phase>=1.)phase-=1.;
        const double sine=std::sin(2*pi*phase),triangle=1.-4.*std::abs(phase-.5);
        const double lfo=p.value[Wave]>=.5?triangle:sine;
        // Clock excursion is proportional to delay time; both channels share one clock.
        const double depth=profile.modulation*smooth[Depth]*smooth[ModEnable]*.018;
        const double tickRate=profile.stages/std::max(2.,delaySamples*(1.+depth*lfo));
        const double initialPhase=clockPhase;
        // Tracking low-pass loses bandwidth as the BBD clock slows down.
        double cutoff=std::min(sr*.22,profile.bandwidth*std::sqrt(180./std::max(20.,delaySamples*1000./sr)));
        if(p.value[TempoSync]>=.5)cutoff=std::min(cutoff,.22*tickRate*sr);
        const double a=1.-std::exp(-2*pi*cutoff/sr),hp=1.-std::exp(-2*pi*65./sr);
        const double gain=std::pow(10.,(-12.+24.*smooth[Level])/20.);
        const double mix=smooth[Blend],fb=profile.feedbackMax*smooth[Feedback];
        const double bypass=smooth[Bypass];
        overload*=std::exp(-1./(.2*sr));
        const int channelCount=std::clamp(count,0,2);
        std::array<double,2> charges{};
        for(int n=0;n<channelCount;++n){
            auto& c=channels[n];const double dry=std::isfinite(input[n])?std::clamp(input[n],-32.,32.):0.;
            // Relative EH7500 gain: 3.5/2 at LF, 4.5/2 at HF.
            c.boostLow+=(1.-std::exp(-2*pi*1200./sr))*(dry-c.boostLow);
            const double boost=(activeModel==0)?smooth[Boost]:0.;
            const double driven=gain*(dry*(1.+1.25*boost)-.5*boost*c.boostLow);
            overload=std::max(overload,std::abs(driven));
            double wet=c.oldHeldCharge+clockPhase*(c.heldCharge-c.oldHeldCharge);
            if(profile.compander){
                // Rectifier-controlled complementary soft companding. Time constants
                // approximate the envelope network; this is not a transistor NE570 model.
                const double rect=std::abs(wet);
                c.expandEnv+=(1.-std::exp(-1./((rect>c.expandEnv?.002:.035)*sr)))*(rect-c.expandEnv);
                const double q=c.expandEnv/.2;
                wet*=.5*(q+std::sqrt(q*q+4.));
            }
            c.post1+=a*(wet-c.post1);c.post2+=a*(c.post1-c.post2);
            wet=c.post2;
            c.env+=(1.-std::exp(-1./((std::abs(driven)>c.env?.004:.12)*sr)))*(std::abs(driven)-c.env);
            if(profile.gate>0){const double threshold=profile.gate*(.15+8.*smooth[Squelch]);wet*=std::clamp(c.env/threshold,0.,1.);}
            c.feedback=wet;
            double feed=driven*(1.-bypass)+fb*c.feedback;
            c.low+=hp*(feed-c.low);feed-=c.low;
            c.pre1+=a*(feed-c.pre1);c.pre2+=a*(c.pre1-c.pre2);
            feed=c.pre2;
            if(profile.compander){
                const double rect=std::abs(feed);
                c.compressEnv+=(1.-std::exp(-1./((rect>c.compressEnv?.002:.035)*sr)))*(rect-c.compressEnv);
                feed/=std::sqrt(1.+c.compressEnv/.2);
            }
            feed=sat(feed,profile.drive);
            charges[n]=feed;
            const double effected=(1.-mix)*driven+mix*wet;
            const double effectAmount=(1.-bypass)*modelFade;
            output[n]=effectAmount*effected+(1.-effectAmount)*dry;
        }
        // Finite BBD charge cells run on their own variable clock, not the host clock.
        // Interpolate acquisition instants when several transfers occur in one host sample.
        double eventPhase=1.-initialPhase;
        while(eventPhase<=tickRate){
            const double fraction=eventPhase/tickRate;
            for(int n=0;n<channelCount;++n){auto& c=channels[n];
                c.oldHeldCharge=c.heldCharge;c.heldCharge=c.line[pos];
                c.line[pos]=c.previousCharge+fraction*(charges[n]-c.previousCharge);
            }
            pos=(pos+1)%std::size_t(profile.stages);eventPhase+=1.;
        }
        clockPhase=initialPhase+tickRate-std::floor(initialPhase+tickRate);
        for(int n=0;n<channelCount;++n)channels[n].previousCharge=charges[n];
    }
};
}
