#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

namespace jerzy {
constexpr float pi = std::numbers::pi_v<float>;
inline float db(float x) { return std::pow(10.0f, x / 20.0f); }
inline float curve(float t, int shape) {
    t = std::clamp(t, 0.0f, 1.0f);
    return shape == 0 ? t : shape == 1 ? t * t : t * t * (3.0f - 2.0f * t);
}
inline float seconds(float position, bool poly = false) {
    return 0.004f * std::pow(poly ? 1000.0f : 2000.0f, std::clamp(position, 0.0f, 1.0f));
}
struct Parameters {
    float attack = .4f, decay = 1, blend = 1, volume = .5f, sens = .5f;
    float harmonix = .3f, tone = .5f, hvol = .25f, hblend = 1, rblend = 1;
    bool poly = false, fuzz = false, compMono = true, compPoly = true;
    bool continuousMono = false, continuousPoly = false, bypass = false;
    bool useReturn = false, returnBypass = false, fuzzOnReturn = false;
    int attackShape = 1, decayShape = 1, triggerMode = 0; // auto, manual, MIDI
};
class Envelope {
public:
    void reset() { phase = idle; value = 0; progress = 0; resetStart = 0; }
    void trigger() { resetStart = value; progress = 0; phase = value > .0001f ? retrigger : attack; }
    float tick(float rate, float a, float d, int as, int ds, bool continuous, float samples = 1, bool poly = false) {
        const bool ao = a <= .00001f, doff = d >= .99999f;
        if (ao && doff) { value = 1; phase = sustain; return value; }
        if (phase == sustain && !doff) { phase = decay; progress = 0; }
        double remaining = std::max(0.0, static_cast<double>(samples));
        const double attackSamples = std::max(1.0, static_cast<double>(rate) * (ao ? .0005 : seconds(a, poly)));
        const double decaySamples = std::max(1.0, static_cast<double>(rate) * seconds(d, poly));
        while (remaining > 0) {
            if (phase == idle) {
                value = 0;
                if (!continuous) break;
                phase = attack; progress = 0;
            }
            if (phase == sustain) { value = 1; break; }
            // Skip whole free-running cycles without an unbounded loop for large time steps.
            if (phase == attack && progress == 0 && continuous && !doff) {
                const double cycle = attackSamples + decaySamples;
                if (remaining >= cycle) {
                    remaining = std::fmod(remaining, cycle);
                    if (remaining == 0) { value = 0; break; }
                }
            }
            const double duration = phase == retrigger ? std::max(1.0, static_cast<double>(rate) * .0015)
                                  : phase == attack ? attackSamples : decaySamples;
            const double untilBoundary = std::max(0.0, (1 - progress) * duration);
            const bool crosses = remaining >= untilBoundary;
            const double used = std::min(remaining, untilBoundary);
            progress = crosses ? 1 : progress + used / duration;
            remaining -= used;
            const float position = static_cast<float>(progress);
            value = phase == retrigger ? resetStart * (1 - curve(position, 2))
                  : phase == attack ? curve(position, as) : curve(1 - position, ds);
            if (!crosses) break;
            progress = 0;
            phase = phase == retrigger ? attack : phase == attack ? (doff ? sustain : decay) : idle;
        }
        return value;
    }
    float current() const { return value; }
private:
    enum Phase { idle, retrigger, attack, decay, sustain } phase = idle;
    float value = 0, resetStart = 0;
    double progress = 0;
};
class PluckDetector {
public:
    void prepare(float sr) {
        rate=sr; attackC=std::exp(-1/(sr*.0003f)); releaseC=std::exp(-1/(sr*.012f));
        slowC=std::exp(-1/(sr*.035f)); highPassC=std::exp(-2*pi*700/sr); highReleaseC=std::exp(-1/(sr*.025f)); reset();
    }
    void reset() { fast=slow=low=highFast=highSlow=0; cooldown=rearmSamples=0; armed=true; }
    bool process(float x,float sens) {
        low=highPassC*low+(1-highPassC)*x;
        float high=std::abs(x-low);
        highFast=(high>highFast?attackC:highReleaseC)*highFast+(1-(high>highFast?attackC:highReleaseC))*high;
        highSlow=slowC*highSlow+(1-slowC)*highFast;
        x=std::abs(x);
        // A peak envelope with a release bridges individual string waveform cycles.
        // Following abs(x) with a sub-ms symmetric filter mistook bass cycles for plucks.
        float c=x>fast?attackC:releaseC;
        fast=c*fast+(1-c)*x;
        slow=slowC*slow+(1-slowC)*fast;
        const float floor=db(-32-38*std::clamp(sens,0.0f,1.0f));
        const float ratio=1.9f-.45f*sens;
        // A new pick can restore upper partials without exceeding the old note's peak.
        // Relative HF content rejects bass zero crossings while detecting that onset.
        bool spectralOnset=highFast>highSlow*(1.4f-.1f*sens)&&highFast>fast*.12f;
        bool event=armed&&cooldown==0&&fast>floor&&(fast>slow*ratio||spectralOnset);
        if(event){cooldown=static_cast<int>(rate*.020f);armed=false;rearmSamples=0;}
        if(cooldown>0)--cooldown;
        // Require a settled envelope before another rising edge; never re-arm on a
        // single zero crossing or bypass the latch with an individual sample slope.
        if(fast<slow*1.08f||fast<floor*.5f){
            if(++rearmSamples>=static_cast<int>(rate*.006f))armed=true;
        }else rearmSamples=0;
        return event;
    }
private:
    float rate=48000,attackC=0,releaseC=0,slowC=0,highPassC=0,highReleaseC=0,fast=0,slow=0,low=0,highFast=0,highSlow=0;
    int cooldown=0,rearmSamples=0;bool armed=true;
};
class Compressor {
public:
    void prepare(float sr) { atk=std::exp(-1/(sr*.008f)); rel=std::exp(-1/(sr*.18f)); gainC=std::exp(-1/(sr*.003f)); level=0; gain=1; }
    float process(float peak) {
        float c = peak > level ? atk : rel;
        level = c*level+(1-c)*peak;
        float inputDb = 20*std::log10(std::max(level, 1.0e-9f));
        float over = inputDb+24, reduction = 0;
        if (over > 3) reduction = .75f*over;
        else if (over > -3) reduction = .75f*(over+3)*(over+3)/12;
        float makeup = 12*std::clamp((inputDb+70)/20,0.0f,1.0f);
        float target = db(makeup-reduction);
        gain=gainC*gain+(1-gainC)*target;
        return gain;
    }
private:
    float atk=0, rel=0, gainC=0, level=0, gain=1;
};
template <int N> class Delay {
public:
    void reset() { data.fill(0); position=0; }
    float tick(float x) { float y=data[position]; data[position]=x; position=(position+1)%N; return y; }
private: std::array<float,N> data{}; int position=0;
};
class Fuzz {
    static constexpr int taps=257;
    static constexpr int factor=16;
    struct Fir {
        std::array<float,taps> data{}; int index=0;
        float tick(float x,const std::array<float,taps>& c,bool calculate=true) {
            data[index]=x; float y=0; int p=index;
            if(calculate) for (int i=0;i<taps;++i) { y+=c[i]*data[p]; if (--p<0) p=taps-1; }
            index=(index+1)%taps; return y;
        }
    } down;
    std::array<float,17> interpolationHistory{};
    int interpolationIndex=0;
public:
    void prepare(float sr) {
        rate=sr; down={}; interpolationHistory.fill(0); interpolationIndex=0; lastIn=lastOut=memory=low=0;
        float sum=0;
        for(int i=0;i<taps;++i) {
            float n=static_cast<float>(i-128);
            float sinc=n==0 ? .05625f : std::sin(2*pi*.028125f*n)/(pi*n);
            coeff[i]=sinc*(.42f-.5f*std::cos(2*pi*i/256)+.08f*std::cos(4*pi*i/256)); sum+=coeff[i];
        }
        for(auto& x:coeff) x/=sum;
    }
    float process(float x,float drive,float tone,float volume) {
        const float gain=3+drive*drive*110;
        const float dc=std::exp(-2*pi*25/(rate*factor));
        const float tc=std::exp(-2*pi*(500+tone*tone*11000)/(rate*factor));
        float output=0;
        interpolationHistory[interpolationIndex]=x;
        for(int i=0;i<factor;++i) {
            // Polyphase interpolation: all omitted high-rate inputs are exactly zero.
            float v=0;
            for(int j=i;j<taps;j+=factor) {
                int offset=(j-i)/factor;
                int index=(interpolationIndex-offset+17)%17;
                v+=coeff[j]*interpolationHistory[index]*factor;
            }
            v*=gain;
            v+=.12f*memory;
            v=v/(1+(v>=0? .75f:1.2f)*std::abs(v));
            memory=v;
            v=v*2.2f; v=v/(1+(v>=0?1.1f:.8f)*std::abs(v));
            float high=v-lastIn+dc*lastOut; lastIn=v; lastOut=high;
            low=tc*low+(1-tc)*high;
            float filtered=down.tick(low,coeff,i==0);
            if(i==0) output=filtered;
        }
        interpolationIndex=(interpolationIndex+1)%17;
        return output*db(-24+36*volume);
    }
private:
    std::array<float,taps> coeff{};
    float rate=48000,lastIn=0,lastOut=0,memory=0,low=0;
};
// Fixed storage radix-2 complex FFT: no backend locks or heap work in process.
class Spectrum {
public:
    static constexpr int size=2048, hop=128;
    void prepare(float sr) {
        rate=sr; write=read=count=0; manualPending=false;
        for(auto& a:input) a.fill(0);
        for(auto& a:output) a.fill(0);
        previous.fill(0); average.fill(0); cooldown.fill(0); gains.fill(0); active.fill(false);
        for(auto& e:envelopes) e.reset();
        for(int i=0;i<size;++i) window[i]=std::sqrt(.5f-.5f*std::cos(2*pi*i/size));
        for(int i=0;i<size/2;++i) roots[i]=std::polar(1.0f,-2*pi*i/size);
        for(int i=0;i<size;++i) { unsigned x=static_cast<unsigned>(i),r=0; for(int b=0;b<11;++b){r=(r<<1)|(x&1);x>>=1;} reversed[i]=static_cast<int>(r); }
    }
    bool process(float left,float right,float& yl,float& yr,const Parameters& p,bool manual) {
        input[0][write]=left; input[1][write]=right; write=(write+1)%size;
        yl=output[0][read]; yr=output[1][read]; output[0][read]=output[1][read]=0;
        bool triggered=false;
        manualPending=manualPending||manual;
        if(++count==hop) { count=0; triggered=frame(p); }
        read=(read+1)%(size*2);
        return triggered;
    }
private:
    void fft(std::array<std::complex<float>,size>& a,bool inverse) {
        for(int i=0;i<size;++i) if(i<reversed[i]) std::swap(a[i],a[reversed[i]]);
        for(int len=2;len<=size;len*=2) {
            const int half=len/2,step=size/len;
            for(int base=0;base<size;base+=len) for(int j=0;j<half;++j) {
                auto w=inverse ? std::conj(roots[j*step]) : roots[j*step];
                auto u=a[base+j],v=a[base+j+half]*w;
                a[base+j]=u+v; a[base+j+half]=u-v;
            }
        }
        if(inverse) for(auto& v:a) v/=static_cast<float>(size);
    }
    bool frame(const Parameters& p) {
        for(int ch=0;ch<2;++ch) {
            for(int i=0;i<size;++i) frames[ch][i]={input[ch][(write+i)%size]*window[i],0};
            fft(frames[ch],false);
        }
        bool event=false;
        const bool bothPhases=p.attack>.00001f && p.decay<.99999f;
        const float floor=bothPhases ? db(-72+22*(1-p.sens)) : db(-64);
        const float ratio=bothPhases ? 2.0f-.75f*p.sens : 2.4f;
        const float follower=std::exp(-static_cast<float>(hop)/(rate*.037f));
        const int refractory=std::max(1,static_cast<int>(std::ceil(rate*.032f/hop)));
        for(int k=0;k<=size/2;++k) {
            float magnitude=std::max(std::abs(frames[0][k]),std::abs(frames[1][k]))/size;
            bool onset=magnitude>floor && (magnitude>average[k]*ratio)
                && magnitude>previous[k]*1.08f && cooldown[k]==0;
            if(p.triggerMode!=0) onset=false;
            if(manualPending || onset) {
                envelopes[k].trigger(); active[k]=true; cooldown[k]=refractory; event=true;
            }
            if(cooldown[k]>0) --cooldown[k];
            if(magnitude<floor*.3f) active[k]=false;
            average[k]=average[k]*follower+magnitude*(1-follower); previous[k]=magnitude;
            gains[k]=envelopes[k].tick(rate,p.attack,p.decay,1,1,p.continuousPoly&&active[k],static_cast<float>(hop),true);
        }
        manualPending=false;
        for(int ch=0;ch<2;++ch) {
            for(int k=0;k<size;++k) frames[ch][k]*=gains[static_cast<std::size_t>(k<=size/2 ? k : size-k)];
            fft(frames[ch],true);
            for(int i=0;i<size;++i) output[ch][(read+1+i)%(size*2)]+=frames[ch][i].real()*window[i]*(2.0f*hop/size);
        }
        return event;
    }
    float rate=48000;
    int write=0,read=0,count=0; bool manualPending=false;
    std::array<std::array<float,size>,2> input{};
    std::array<std::array<float,size*2>,2> output{};
    std::array<std::array<std::complex<float>,size>,2> frames{};
    std::array<std::complex<float>,size/2> roots{};
    std::array<int,size> reversed{};
    std::array<float,size> window{};
    std::array<float,size/2+1> previous{},average{},gains{};
    std::array<int,size/2+1> cooldown{};
    std::array<bool,size/2+1> active{};
    std::array<Envelope,size/2+1> envelopes{};
};
class Engine {
public:
    static constexpr int latency=Spectrum::size+16;
    void prepare(float sr) {
        rate=sr; detector.prepare(sr); compressor.prepare(sr); envelope.reset(); spectral.prepare(sr);
        for(auto& f:fuzz) f.prepare(sr);
        for(auto& d:cleanDelay) d.reset();
        for(auto& d:returnFuzzDelay) d.reset();
        for(auto& d:dryDelay) d.reset();
        for(auto& d:returnDelay) d.reset();
        for(auto& d:monoDelay) d.reset();
        envDelay.reset(); polyMix=fuzzMix=bypassMix=0; smooth={}; initialized=false; compMix=1;
    }
    bool process(float& l,float& r,float retL,float retR,const Parameters& target,bool trigger=false) {
        if(!initialized) { smooth=target; polyMix=target.poly?1.0f:0.0f; fuzzMix=target.fuzz?1.0f:0.0f; bypassMix=target.bypass?1.0f:0.0f; compMix=(target.poly?target.compPoly:target.compMono)?1.0f:0.0f; initialized=true; }
        float c=1-std::exp(-1/(rate*.008f));
        auto approach=[c](float& x,float v){ x+=c*(v-x); };
        approach(smooth.attack,target.attack); approach(smooth.decay,target.decay);
        // Preserve OFF endpoints exactly after a short, continuous parameter ramp.
        if(std::abs(smooth.attack-target.attack)<1e-6f) smooth.attack=target.attack;
        if(std::abs(smooth.decay-target.decay)<1e-6f) smooth.decay=target.decay;
        approach(smooth.blend,target.blend); approach(smooth.volume,target.volume); approach(smooth.harmonix,target.harmonix);
        approach(smooth.tone,target.tone); approach(smooth.hvol,target.hvol); approach(smooth.hblend,target.hblend); approach(smooth.rblend,target.rblend);
        smooth.sens=target.sens; smooth.attackShape=target.attackShape; smooth.decayShape=target.decayShape;
        smooth.continuousMono=target.continuousMono; smooth.continuousPoly=target.continuousPoly; smooth.triggerMode=target.triggerMode;
        approach(polyMix,target.poly?1.0f:0.0f); approach(fuzzMix,target.fuzz?1.0f:0.0f); approach(bypassMix,target.bypass?1.0f:0.0f);
        float inputL=l,inputR=r;
        bool onset=detector.process(std::abs(l)>=std::abs(r)?l:r,target.sens);
        bool monoEvent=trigger || (target.triggerMode==0&&onset);
        if(monoEvent) envelope.trigger();
        float env=envelope.tick(rate,smooth.attack,smooth.decay,target.attackShape,target.decayShape,target.continuousMono);
        env=envDelay.tick(env);
        float cg=compressor.process(std::max(std::abs(l),std::abs(r)));
        approach(compMix,(target.poly?target.compPoly:target.compMono)?1.0f:0.0f);
        float compGain=1+compMix*(cg-1);
        float material[2]={l*compGain,r*compGain};
        if(target.useReturn && target.fuzzOnReturn) { material[0]=retL; material[1]=retR; }
        float processed[2]{};
        for(int ch=0;ch<2;++ch) {
            float clean=cleanDelay[ch].tick(material[ch]);
            float dirty=fuzz[ch].process(material[ch],smooth.harmonix,smooth.tone,smooth.hvol);
            processed[ch]=clean+fuzzMix*smooth.hblend*(dirty-clean);
        }
        if(target.useReturn&&!target.fuzzOnReturn) { processed[0]=returnFuzzDelay[0].tick(retL); processed[1]=returnFuzzDelay[1].tick(retR); }
        else { returnFuzzDelay[0].tick(retL); returnFuzzDelay[1].tick(retR); }
        float monoL=monoDelay[0].tick(processed[0]*env), monoR=monoDelay[1].tick(processed[1]*env);
        float polyL=0,polyR=0;
        bool polyEvent=spectral.process(processed[0],processed[1],polyL,polyR,smooth,trigger);
        const float dryL=dryDelay[0].tick(inputL),dryR=dryDelay[1].tick(inputR);
        const float returnL=returnDelay[0].tick(retL),returnR=returnDelay[1].tick(retR);
        float wet[2]={monoL+polyMix*(polyL-monoL),monoR+polyMix*(polyR-monoR)};
        const float dry[2]={dryL,dryR},ret[2]={returnL,returnR};
        float out[2]{};
        for(int ch=0;ch<2;++ch) {
            if(target.useReturn) wet[ch]=ret[ch]+smooth.rblend*(wet[ch]-ret[ch]);
            float effect=(dry[ch]+smooth.blend*(wet[ch]-dry[ch]))*db(-26+39*smooth.volume);
            float bypass=target.returnBypass&&target.useReturn ? ret[ch]:dry[ch];
            out[ch]=effect+bypassMix*(bypass-effect);
            if(!std::isfinite(out[ch])) out[ch]=0;
        }
        l=out[0]; r=out[1];
        return target.poly ? polyEvent:monoEvent;
    }
private:
    float rate=48000,polyMix=0,fuzzMix=0,bypassMix=0,compMix=1;
    bool initialized=false; Parameters smooth;
    PluckDetector detector; Compressor compressor; Envelope envelope; Spectrum spectral;
    std::array<Fuzz,2> fuzz;
    std::array<Delay<16>,2> cleanDelay,returnFuzzDelay;
    std::array<Delay<latency>,2> dryDelay,returnDelay;
    std::array<Delay<Spectrum::size>,2> monoDelay;
    Delay<16> envDelay;
};
}
