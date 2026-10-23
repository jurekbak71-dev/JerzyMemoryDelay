#pragma once
// Jerzy MultiFX: allocation-free per-sample DSP, reusable in JACK and host tests.
// Memory Delay is the EXISTING Jerzy::Engine (not a rewritten approximation).
#include "../source/dsp.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace JerzyMultiFX {
enum Effect { Compressor=0, Drive=1, Tremolo=2, MemoryDelay=3, Effects=4 };
enum Param {
    CompThreshold, CompRatio, CompMakeup,
    DriveGain, DriveTone, DriveOutput,
    TremRate, TremDepth,
    DelayMix, DelayFeedback, DelayTime, DelayModel,
    MasterLevel, Params
};
inline constexpr std::array<double, Params> defaults {{
    .50, .40, .50, .35, .65, .42, .33, .55,
    .50, .35, (350.-20.)/530., .60, .70
}};
// All parameters are normalized to [0, 1].
struct State {
    std::array<double, Params> values = defaults;
    std::array<bool, Effects> enabled {{ true,true,true,true }};
    std::array<int, Effects> order {{ Compressor,Drive,Tremolo,MemoryDelay }};
    double bpm = 120.;
};
class Engine {
    Jerzy::Engine delay_;
    double sr_=44100., compEnv_=0., compA_=0., compR_=0., phase_=0.;
    std::array<double,2> tone_{{0.,0.}};
    std::array<double,Effects> active_{{1.,1.,1.,1.}};
    static constexpr double pi=3.14159265358979323846;
    static double finite(double v) {return std::isfinite(v)?std::clamp(v,-8.,8.):0.;}
public:
    void prepare(double sampleRate) {
        sr_=std::clamp(sampleRate,8000.,192000.);
        compA_=std::exp(-1./(.008*sr_));
        compR_=std::exp(-1./(.110*sr_));
        delay_.prepare(sr_);
        compEnv_=phase_=0.; tone_.fill(0.);active_.fill(1.);
    }
    static bool validOrder(const std::array<int,Effects>& order) {
        unsigned mask=0;
        for(int v:order) {if(v<0||v>=Effects||(mask&(1u<<v)))return false;mask|=1u<<v;}
        return mask==(1u<<Effects)-1;
    }
    // No allocation, blocking, locks or network calls; safe inside JACK callback.
    void tick(double in, double out[2], const State& s) {
        std::array<double,2> y{{finite(in),finite(in)}};
        const auto& p=s.values;
        for(int pos=0;pos<Effects;++pos) {
            const int id=s.order[pos];
            std::array<double,2> processed=y;
            switch(id) {
            case Compressor: {
                const double a=std::max(std::abs(y[0]),std::abs(y[1]));
                compEnv_=(a>compEnv_?compA_:compR_)*compEnv_+(1.-(a>compEnv_?compA_:compR_))*a;
                const double threshold=std::pow(10.,(-6.-42.*p[CompThreshold])/20.);
                const double ratio=1.+9.*p[CompRatio];
                const double gain=compEnv_>threshold?
                    std::pow(std::max(compEnv_,1e-9)/threshold,1./ratio-1.):1.;
                const double makeup=std::pow(10.,(p[CompMakeup]*12.-6.)/20.);
                processed[0]*=gain*makeup;processed[1]*=gain*makeup;
                break;
            }
            case Drive: {
                const double gain=1.+p[DriveGain]*35.;
                const double cut=350.+p[DriveTone]*9000.;
                const double a=1.-std::exp(-2.*pi*cut/sr_);
                const double level=.08+p[DriveOutput]*1.55;
                for(int ch=0;ch<2;++ch){
                    const double v=std::tanh(y[ch]*gain)*level;
                    tone_[ch]+=a*(v-tone_[ch]);
                    processed[ch]=tone_[ch];
                }
                break;
            }
            case Tremolo: {
                const double hz=.3+11.7*p[TremRate];
                phase_+=hz/sr_;if(phase_>=1.)phase_-=1.;
                const double shape=.5+.5*std::sin(2.*pi*phase_);
                const double amp=1.-p[TremDepth]*(1.-shape);
                processed[0]*=amp;processed[1]*=amp;
                break;
            }
            case MemoryDelay: {
                Jerzy::Parameters dp;
                dp.set(Jerzy::Model,p[DelayModel]);
                dp.set(Jerzy::Blend,p[DelayMix]);
                dp.set(Jerzy::Feedback,p[DelayFeedback]);
                dp.set(Jerzy::Delay,p[DelayTime]);
                dp.set(Jerzy::Level,.5);
                delay_.tick(y.data(),processed.data(),2,dp,s.bpm);
                break;
            }
            default: break;
            }
            // Slewed enable/disable avoids switching clicks, even on live stage.
            const double target=s.enabled[id]?1.:0.;
            const double step=1./(.010*sr_);
            active_[id]+=(target>active_[id]?std::min(step,target-active_[id]):
                                          -std::min(step,active_[id]-target));
            const double wet=active_[id];
            for(int ch=0;ch<2;++ch)y[ch]=finite(y[ch]*(1.-wet)+processed[ch]*wet);
        }
        const double master=p[MasterLevel]*1.4;
        for(int ch=0;ch<2;++ch)out[ch]=std::clamp(finite(y[ch]*master),-1.,1.);
    }
};
} // namespace JerzyMultiFX
