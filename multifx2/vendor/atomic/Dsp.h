#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>

namespace atomiccluster {
constexpr double pi = 3.14159265358979323846;
constexpr int fftSize = 2048, hopSize = 256, bins = fftSize / 2 + 1;
enum Parameter { Volume, Blend, Speed, Atoms, Smooth, Flat, Bypass, Sync, Division, TapEnabled, Motion, Stereo, NumParams };
using Values = std::array<double, NumParams>;
inline Values defaults() { return {0.5, 1.0, 0.4, std::log(16.) / std::log(64.), 0, 0, 0, 0, 0.5, 1, 0, 1.0}; }
inline double clean(double v) { return std::isfinite(v) ? std::clamp(v, 0., 1.) : 0.; }
inline double wrap(double x) { return std::remainder(x, 2 * pi); }
inline int atomCount(double v) { return std::clamp(int(std::round(std::pow(64., clean(v)))), 1, 64); }
inline double divisionBeats(double v) { constexpr double beats[]{.25,.5,1,2,4}; return beats[int(std::round(clean(v)*4))]; }
inline const char* divisionLabel(double v) { const char* names[]{"1/16","1/8","1/4","1/2","1 bar"}; return names[int(std::round(clean(v)*4))]; }
inline double interval(double speed) { return 2. * std::pow(1. / 750., clean(speed)); }
inline void fft(std::array<std::complex<double>, fftSize>& a, bool inverse = false) {
    for (int i = 1, j = 0; i < fftSize; ++i) {
        int b = fftSize >> 1;
        for (; j & b; b >>= 1) j ^= b;
        j ^= b;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= fftSize; len <<= 1) {
        const auto step = std::polar(1., (inverse ? 2 : -2) * pi / len);
        for (int start = 0; start < fftSize; start += len) {
            std::complex<double> w(1., 0.);
            for (int j = 0; j < len / 2; ++j) {
                const auto u = a[start + j], v = a[start + j + len / 2] * w;
                a[start + j] = u + v;
                a[start + j + len / 2] = u - v;
                w *= step;
            }
        }
    }
    if (inverse) for (auto& v : a) v /= fftSize;
}

// Harmonic resynthesis: the wet bus consists exclusively of oscillator voices.
// FFT is analysis only; increasing ATOMS never restores a dry/full-spectrum bus.
// All buffers are fixed and process() does not allocate or lock.
class Engine {
    static constexpr int voices = 64;
    struct Voice {
        double phase=0, frequency=220, targetFrequency=220;
        double amplitude=0, targetAmplitude=0, left=1, right=1, targetLeft=1, targetRight=1;
    };
    std::array<Voice, voices> voice{};
    std::array<std::array<double,fftSize>,2> input{};
    std::array<double,fftSize> window{};
    std::array<std::complex<double>,fftSize> spectrum{};
    std::array<double,bins> previousPhase{}, previousMagnitude{}, frequencies{};
    std::array<int,bins> peaks{};
    uint64_t cursor=0, step=0;
    uint32_t randomState=71;
    double sr=48000, mix=.8, gain=1, engaged=1, smoothing=.004;
    double remaining=0, period=4800, progress=0, voiceSmoothing=.004;
    int lastCount=-1, lastMotion=-1;
    double lastSpeed=-1, lastSync=-1, lastDivision=-1, lastFlat=-1, lastSmooth=-1, lastBpm=-1;
    bool gridActive=false, forceCapture=false;
    int64_t lastGrid=0;
    bool silentCapture=true;
    bool first=true;
    double random() { randomState^=randomState<<13; randomState^=randomState>>17; randomState^=randomState<<5; return randomState/4294967296.; }
    void frame(const Values& p, double bpm, double beat) {
        double inputEnergy=0;
        for(int i=0;i<fftSize;++i) {
            // Energy-preserving stereo analysis: avoid losing anti-phase input.
            const auto n=(cursor+i)%fftSize;
            const double l=input[0][n], r=input[1][n];
            const double mono=(l+r)*.5;
            inputEnergy+=(l*l+r*r)*.5;
            spectrum[i]=mono*window[i];
        }
        // If an anti-phase source cancels in the mid channel, analyze left.
        double midEnergy=0;
        for(const auto& v:spectrum) midEnergy+=std::norm(v);
        if(midEnergy<inputEnergy*.01)
            for(int i=0;i<fftSize;++i) spectrum[i]=input[0][(cursor+i)%fftSize]*window[i];
        fft(spectrum);
        for(int k=1;k<bins-1;++k) {
            const double angle=std::arg(spectrum[k]), expected=2*pi*k*hopSize/fftSize;
            const double advance=expected+wrap(angle-previousPhase[k]-expected);
            double fraction=0;
            const double l=std::log(std::max(1e-20,std::abs(spectrum[k-1])));
            const double m=std::log(std::max(1e-20,std::abs(spectrum[k])));
            const double r=std::log(std::max(1e-20,std::abs(spectrum[k+1])));
            if(std::abs(l-2*m+r)>1e-12) fraction=std::clamp(.5*(l-r)/(l-2*m+r),-.5,.5);
            const double magnitude=std::abs(spectrum[k]);
            const bool reliableHistory=!first && previousMagnitude[k]>std::max(1e-8,magnitude*.2);
            frequencies[k]=reliableHistory ? advance*sr/(2*pi*hopSize) : (k+fraction)*sr/fftSize;
            previousMagnitude[k]=magnitude;
            previousPhase[k]=angle;
        }
        const int count=atomCount(p[Atoms]), motion=int(std::round(clean(p[Motion])*2));
        const bool newOnset=silentCapture && inputEnergy/fftSize>1e-8;
        const bool grid = p[Sync]>=.5 && std::isfinite(beat);
        if(first || (!grid && remaining<=0) || forceCapture || count!=lastCount || motion!=lastMotion || newOnset ||
           p[Speed]!=lastSpeed || p[Sync]!=lastSync || p[Division]!=lastDivision || p[Flat]!=lastFlat || p[Smooth]!=lastSmooth || (p[Sync]>=.5 && bpm!=lastBpm)) {
            double seconds=interval(p[Speed]);
            if(p[Sync]>=.5) {
                seconds=60./std::clamp(bpm,20.,400.)*divisionBeats(p[Division]);
            }
            period=std::max(double(hopSize),seconds*sr);
            // Retain timer overshoot so free-running BPM does not accumulate
            // up to one FFT hop of drift at every capture.
            if(!first && remaining<=0 && !forceCapture) remaining+=period;
            else remaining=period;
            progress=0;
            if(grid) step=static_cast<uint64_t>(lastGrid);
            forceCapture=false;
            voiceSmoothing=1-std::exp(-1./(sr*(p[Smooth]>=.5 ? std::clamp(seconds*.24,.008,.4) : .002)));
            double strongest=0;
            for(int k=1;k<bins-1;++k) if(frequencies[k]>=55 && frequencies[k]<sr*.45)
                strongest=std::max(strongest,std::norm(spectrum[k]));
            int found=0;
            for(int k=1;k<bins-1;++k) {
                const double m=std::norm(spectrum[k]);
                if(frequencies[k]>=55 && frequencies[k]<sr*.45 && m>1e-20 && m>strongest*.0001 &&
                   m>std::norm(spectrum[k-1]) && m>=std::norm(spectrum[k+1])) peaks[found++]=k;
            }
            std::sort(peaks.begin(),peaks.begin()+found,[this](int a,int b){ return std::norm(spectrum[a])>std::norm(spectrum[b]); });
            double root=found ? frequencies[peaks[0]] : 220;
            // Use the lowest substantial measured partial as harmonic anchor.
            for(int i=0;i<found;++i) if(std::norm(spectrum[peaks[i]])>strongest*.12)
                root=std::min(root,frequencies[peaks[i]]);
            root=std::clamp(root,55.,sr*.2);
            std::array<double,voices> pitch{}, weight{};
            int active=0;
            if(found && inputEnergy/fftSize>1e-12) {
                if(motion==0) {
                    // Keep independent measured partials, then add harmonics.
                    for(int i=0;i<std::min(count,found);++i) {
                        pitch[active]=frequencies[peaks[i]];
                        weight[active++]=p[Flat]>=.5 ? 1 : std::max(.35,std::pow(std::norm(spectrum[peaks[i]])/strongest,.20));
                    }
                    for(int harmonic=2;active<count && harmonic<1024;++harmonic) {
                        double f=root*harmonic;
                        // Fill sub-octave harmonics once the upper band is full.
                        if(f>=sr*.43) f=root*(harmonic*.5+.25);
                        while(f>=sr*.43) f*=.5;
                        bool duplicate=false;
                        for(int i=0;i<active;++i) if(std::abs(pitch[i]-f)<root*.08) duplicate=true;
                        if(!duplicate) { pitch[active]=f; weight[active++]=p[Flat]>=.5 ? 1 : .6/std::pow(harmonic,.12); }
                    }
                } else {
                    // Octaves/fifths/thirds are harmonic ratios of the source,
                    // rather than arbitrary chromatic notes or random detuning.
                    constexpr double harmony[]{.5,1,1.25,1.5,2,2.5,3,4};
                    const int offset=motion==1 ? int(step%8) : int(random()*8);
                    for(int i=0;i<count;++i) {
                        const int degree=motion==1 ? (offset+i*3)%8 : int(random()*8);
                        const int octave=i/8;
                        double f=motion==1 ? root*harmony[offset]*(i+1) : root*harmony[degree]*(1+octave);
                        while(f>=sr*.43) f*=.5;
                        pitch[active]=f;
                        weight[active++]=p[Flat]>=.5 ? 1 : 1./std::pow(1.+i,.18);
                    }
                }
            }
            double weightEnergy=0;
            for(int i=0;i<active;++i) weightEnergy+=weight[i]*weight[i];
            // Keep quiet partials audible and the total wet RMS near the input
            // as density changes. No fixed gain division by the atom count.
            const double rms=std::min(.6,std::sqrt(inputEnergy/fftSize));
            const double level=weightEnergy>0 ? rms*1.5/std::sqrt(weightEnergy) : 0;
            for(int i=0;i<voices;++i) {
                voice[i].targetAmplitude=i<active ? level*weight[i] : 0;
                if(i<active) voice[i].targetFrequency=pitch[i];
            }
            lastCount=count; lastMotion=motion;
            lastBpm=bpm; lastSpeed=p[Speed]; lastSync=p[Sync]; lastDivision=p[Division]; lastFlat=p[Flat]; lastSmooth=p[Smooth];
            silentCapture=active==0; ++step;
        }
        const double time=double(cursor)/sr;
        for(int i=0;i<voices;++i) {
            // Each atom has its own orbit, not a pan on the complete output.
            // Synchronized voice orbits keep a clear stereo excursion even
            // when many independently panned atoms balance one another.
            const double orbit=std::sin(time*2*pi*.32+.35*std::sin(i*2.399963229728653));
            const double pan=clean(p[Stereo])*std::tanh(2.0*orbit);
            voice[i].targetLeft=std::sqrt(1-pan);
            voice[i].targetRight=std::sqrt(1+pan);
        }
        first=false;
    }
public:
    void reset(double rate,const Values& p=defaults()) {
        sr=rate; cursor=step=0; remaining=0; period=4800; progress=0;
        lastCount=lastMotion=-1; lastSpeed=lastSync=lastDivision=lastFlat=lastSmooth=lastBpm=-1;
        gridActive=false; forceCapture=false; lastGrid=0;
        silentCapture=true; first=true; randomState=71;
        for(auto& c:input) c.fill(0);
        previousPhase.fill(0); previousMagnitude.fill(0); frequencies.fill(0);
        for(int i=0;i<fftSize;++i) window[i]=.5-.5*std::cos(2*pi*i/fftSize);
        for(int i=0;i<voices;++i) { voice[i]=Voice{}; voice[i].phase=std::fmod(i*2.399963229728653,2*pi); }
        mix=clean(p[Blend]); gain=clean(p[Volume])*2; engaged=p[Bypass]>=.5 ? 0 : 1;
        smoothing=1-std::exp(-1./(sr*.005)); voiceSmoothing=smoothing;
    }
    std::array<double,2> process(double l,double r,const Values& p,double bpm=120,double beat=NAN) {
        const auto pos=cursor%fftSize;
        const std::array<double,2> dry{input[0][pos],input[1][pos]};
        input[0][pos]=std::isfinite(l) ? std::clamp(l,-16.,16.) : 0;
        input[1][pos]=std::isfinite(r) ? std::clamp(r,-16.,16.) : 0;
        ++cursor;
        bpm=std::isfinite(bpm) ? std::clamp(bpm,20.,400.) : 120;
        const bool grid = p[Sync]>=.5 && std::isfinite(beat) && std::abs(beat)<1e12;
        if(grid) {
            const int64_t index=static_cast<int64_t>(std::floor(beat/divisionBeats(p[Division])+1e-9));
            if(!gridActive || index!=lastGrid) { forceCapture=true; lastGrid=index; }
        } else if(gridActive) forceCapture=true;
        gridActive=grid;
        if(cursor>=fftSize && cursor%hopSize==0) frame(p,bpm,grid ? beat : NAN);
        if(grid) {
            period=std::max(1.,60./bpm*divisionBeats(p[Division])*sr);
            double fraction=beat/divisionBeats(p[Division])-std::floor(beat/divisionBeats(p[Division])+1e-9);
            progress=std::clamp(fraction,0.,1.)*period;
        }
        mix+=smoothing*(clean(p[Blend])-mix);
        // Exact end stops: no asymptotic dry leak at 100% wet, including
        // automation from a previously mixed setting.
        if(p[Blend]>=1) mix=1;
        if(p[Blend]<=0) mix=0;
        gain+=smoothing*(clean(p[Volume])*2-gain);
        engaged+=smoothing*((p[Bypass]>=.5 ? 0:1)-engaged);
        if(std::abs(engaged-(p[Bypass]>=.5 ? 0:1))<1e-9) engaged=p[Bypass]>=.5 ? 0:1;
        std::array<double,2> wet{};
        const double position=std::clamp(progress/period,0.,1.);
        const double gate=p[Smooth]>=.5 ? 1 : std::min({1.,position/.035,(1-position)/.16});
        for(auto& v:voice) {
            v.amplitude+=voiceSmoothing*(v.targetAmplitude-v.amplitude);
            if(v.amplitude<1e-14 && v.targetAmplitude==0) { v.amplitude=0; continue; }
            v.frequency+=voiceSmoothing*(v.targetFrequency-v.frequency);
            v.left+=smoothing*(v.targetLeft-v.left); v.right+=smoothing*(v.targetRight-v.right);
            v.phase+=2*pi*v.frequency/sr;
            if(v.phase>=2*pi) v.phase-=2*pi;
            const double sample=std::sin(v.phase)*v.amplitude*gate;
            wet[0]+=sample*v.left; wet[1]+=sample*v.right;
        }
        --remaining; ++progress;
        std::array<double,2> result{};
        for(int c=0;c<2;++c) {
            // Gentle bounded saturation protects dense/flat clusters, wet only.
            wet[c]=.95*std::tanh(wet[c]/.95);
            const double effected=gain*((1-mix)*dry[c]+mix*wet[c]);
            result[c]=engaged==1 ? effected : dry[c]+engaged*(effected-dry[c]);
        }
        return result;
    }
};
}
