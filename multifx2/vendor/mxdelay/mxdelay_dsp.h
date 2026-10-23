#pragma once
#include "mxdelay_params.h"
#include "mxdelay_mechanics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace JerzyAudio {

template<class T> class MXDelayDSP {
    static constexpr double pi=3.14159265358979323846;
    struct Stereo { T l{},r{}; };

    struct SlotDSP {
        double sr=44100.0;
        std::vector<T> l,r;
        size_t w=0;
        T lpL{},lpR{},hpMemL{},hpMemR{},wetLpL{},wetLpR{},duckEnv{},admPredL{},admPredR{};
        T tapeMemL{},tapeMemR{},tubeEnvL{},tubeEnvR{};
        double phase1=0.0,phase2=0.0,clockPhase=0.0;
        double admStepL=1.0/128.0,admStepR=1.0/128.0;
        double delayMsSm=-1.0,feedbackSm=-1.0,levelSm=-1.0;
        double timeSmoothA=1.0,controlSmoothA=1.0;
        bool enabledLast=true;
        uint32_t seed=0x12345678u;
        AnalogRng noiseRng;
        MechanicalTransport transport;

        void prepare(double sampleRate,uint32_t slotSeed){
            sr=std::max(8000.0,sampleRate);
            seed=slotSeed?slotSeed:0x12345678u;
            const size_t n=(size_t)std::ceil(sr*6.0)+16;
            l.assign(n,T{});r.assign(n,T{});
            timeSmoothA=1.0-std::exp(-1.0/(0.035*sr));
            controlSmoothA=1.0-std::exp(-1.0/(0.008*sr));
            transport.prepare(sr,seed^0x8f6c2a1du);
            reset();
        }
        void clearAudio(){
            std::fill(l.begin(),l.end(),T{});
            std::fill(r.begin(),r.end(),T{});
            w=0;
            lpL=lpR=hpMemL=hpMemR=wetLpL=wetLpR=duckEnv=admPredL=admPredR=T{};
            tapeMemL=tapeMemR=tubeEnvL=tubeEnvR=T{};
            phase1=phase2=clockPhase=0.0;
            admStepL=admStepR=1.0/128.0;
            delayMsSm=feedbackSm=levelSm=-1.0;
            noiseRng.seed(seed^0xc2b2ae35u);
            transport.reset();
        }
        void reset(){clearAudio();enabledLast=true;}

        static T sat(T x,double drive=1.0,double asym=0.0){
            const double v=(double)x*drive+asym*0.08;
            const double y=std::tanh(v)-std::tanh(asym*0.08);
            return(T)(y/std::max(0.25,std::tanh(std::max(0.25,drive))));
        }
        T tapeSat(T x,double drive,double bias,double age,T& mem){
            const double xv=double(x);
            const double a=1.0-std::exp(-1.0/(std::max(0.001,0.010+0.035*age)*sr));
            mem=(T)(double(mem)+a*(xv-double(mem)));
            const double hysteresis=xv+(0.05+0.20*age)*double(mem);
            const double asym=(std::clamp(bias,0.0,1.0)*2.0-1.0)*(0.10+0.24*age);
            return sat((T)hysteresis,1.0+2.8*drive+0.8*age,asym);
        }
        T tubeStage(T x,double pre,double record,double bias,T& env){
            const double av=std::abs(double(x));
            const double a=1.0-std::exp(-1.0/(0.025*sr));
            env=(T)(double(env)+a*(av-double(env)));
            const double drive=1.0+3.4*pre+2.2*record;
            const double sag=1.0/(1.0+(0.25+0.75*pre)*double(env)*2.8);
            const double shaped=double(x)*(0.72+0.28*sag);
            return sat((T)shaped,drive,(bias*2.0-1.0)*0.32);
        }
        static T compandCompress(T x,double amount){
            const double a=std::clamp(amount,0.0,1.0);
            const double v=double(x);
            const double mag=std::abs(v);
            const double c=mag/(1.0+(1.8+5.0*a)*mag);
            return(T)std::copysign((1.0-a)*mag+a*c*(1.8+3.2*a),v);
        }
        static T compandExpand(T x,double amount){
            const double a=std::clamp(amount,0.0,1.0);
            const double v=double(x);
            const double mag=std::abs(v);
            const double e=mag*(1.0+(0.35+1.6*a)*mag);
            return(T)std::copysign((1.0-a)*mag+a*e,v);
        }

        T read(const std::vector<T>& b,double ds)const{
            if(b.empty())return T{};
            ds=std::clamp(ds,2.0,double(b.size()-4));
            double p=double(w)-ds;
            while(p<0)p+=b.size();
            while(p>=double(b.size()))p-=b.size();
            const long i1=(long)std::floor(p);
            const double f=p-double(i1);
            auto at=[&](long idx)->double{
                const long n=(long)b.size();
                idx%=n;if(idx<0)idx+=n;
                return double(b[(size_t)idx]);
            };
            const double y0=at(i1-1),y1=at(i1),y2=at(i1+1),y3=at(i1+2);
            const double c0=y1;
            const double c1=0.5*(y2-y0);
            const double c2=y0-2.5*y1+2.0*y2-0.5*y3;
            const double c3=0.5*(y3-y0)+1.5*(y1-y2);
            return(T)(((c3*f+c2)*f+c1)*f+c0);
        }
        static double lpCoeff(double hz,double sampleRate){
            return 1.0-std::exp(-2.0*pi*std::clamp(hz,20.0,0.48*sampleRate)/sampleRate);
        }
        T lowpass(T x,T& z,double hz){double a=lpCoeff(hz,sr);z=(T)(double(z)+a*(double(x)-double(z)));return z;}
        T highpass(T x,T& z,double hz){double a=lpCoeff(hz,sr);z=(T)(double(z)+a*(double(x)-double(z)));return(T)(double(x)-double(z));}
        double smooth(double target,double& state,double a){if(state<0.0||!std::isfinite(state))state=target;else state+=(target-state)*a;return state;}

        double periodicMod(double depth,double rate){
            phase1+=2*pi*rate/sr;if(phase1>2*pi)phase1-=2*pi;
            phase2+=2*pi*(rate*1.731+0.37)/sr;if(phase2>2*pi)phase2-=2*pi;
            return depth*(0.68*std::sin(phase1)+0.32*std::sin(phase2));
        }
        Stereo digitalColor(Stereo x,int type){
            if(type==0)return x;
            if(type==2){
                const double q=2048.0;
                x.l=(T)(std::round(double(x.l)*q)/q);
                x.r=(T)(std::round(double(x.r)*q)/q);
                return x;
            }
            auto adm=[&](T in,T& pred,double& step){
                double d=double(in)-double(pred),sg=d>=0?1.0:-1.0;
                pred=(T)(double(pred)+sg*step);
                if(std::abs(d)>step*1.7)step*=1.08;else step*=0.995;
                step=std::clamp(step,1.0/2048.0,0.10);
                return pred;
            };
            x.l=adm(x.l,admPredL,admStepL);x.r=adm(x.r,admPredR,admStepR);return x;
        }
        static std::array<double,4> volanteRatios(double spacing){
            static constexpr double m[4][4]={{.25,.50,.75,1.0},{1.0/6.0,1.0/3.0,2.0/3.0,1.0},{.236,.382,.618,1.0},{.172,.414,.707,1.0}};
            double x=std::clamp(spacing,0.0,1.0)*3.0;int a=std::min(2,(int)x);double t=x-a;
            std::array<double,4> z{};for(int i=0;i<4;++i)z[i]=m[a][i]+(m[a+1][i]-m[a][i])*t;return z;
        }
        void applyPhysicalFrame(Stereo& wet,Stereo& fbs,const MechanicalFrame& m,double cutoff){
            const double c=std::clamp(cutoff*m.tone,280.0,0.46*sr);
            wet.l=lowpass((T)(double(wet.l)*m.gain+m.impulse+0.30*m.noise),wetLpL,c);
            wet.r=lowpass((T)(double(wet.r)*m.gain-0.47*m.impulse+0.21*m.noise),wetLpR,c*0.985);
            fbs.l=(T)(double(fbs.l)*m.gain+m.impulse+0.62*m.noise);
            fbs.r=(T)(double(fbs.r)*m.gain-0.41*m.impulse+0.49*m.noise);
        }

        Stereo process(T inL,T inR,const SlotParams& p,double bpm,double machineCondition){
            const double condition=std::clamp(machineCondition,0.0,1.0);
            const double damage=condition*condition*(0.82+0.18*condition);
            auto aged=[&](double local,double amount){return std::clamp(local+damage*amount*(1.0-0.28*local),0.0,1.0);};
            if(l.empty())return{};
            if(p.enable<.5){
                if(enabledLast)clearAudio();
                enabledLast=false;
                return{};
            }
            enabledLast=true;

            const int algo=normIndex(p.algorithm,kAlgorithmCount);
            auto& c=p.c[algo];
            const double targetMs=slotDelayMs(p,bpm);
            double ms=smooth(targetMs,delayMsSm,timeSmoothA);
            const double feedbackNorm=std::clamp(p.feedback,0.0,1.0);
            // Sub-unity feedback even at 100%, with more useful low-end knob resolution.
            const double feedbackGain=0.965*std::pow(feedbackNorm,2.55);
            double fb=smooth(feedbackGain,feedbackSm,controlSmoothA);
            double level=smooth(std::clamp(p.level*1.25,0.0,1.25),levelSm,controlSmoothA);
            const double duckAmt=std::clamp(p.duck,0.0,1.0);
            const double ia=std::max(std::abs((double)inL),std::abs((double)inR));
            const double atk=1.0-std::exp(-1.0/(0.008*sr)),rel=1.0-std::exp(-1.0/(0.180*sr));
            duckEnv=(T)(double(duckEnv)+(ia-double(duckEnv))*(ia>double(duckEnv)?atk:rel));
            const double duckGain=1.0-duckAmt*std::clamp(double(duckEnv)*2.0,0.0,0.85);
            Stereo wet{},fbs{};

            if(algo==(int)DelayAlgorithm::Volante){
                ms=std::clamp(ms,100.0,4000.0);
                const double mechanics=c[0],wear=c[1],spread=c[4],drive=c[5];
                const double globalWear=aged(wear,0.92),globalMechanics=aged(mechanics,0.78);
                auto mech=transport.tick(globalMechanics,aged(0.32*mechanics+0.22*wear,0.72),globalWear,aged(std::max(mechanics,wear),0.94),aged(0.30*wear,0.48));
                auto ratios=volanteRatios(c[2]);double sp=0,sf=0;Stereo feedback{};
                for(int h=0;h<4;++h){
                    if(c[6+h]<0.5&&c[10+h]<0.5)continue;
                    const double local=std::clamp(1.0+mech.pitch*(0.72+0.045*h),0.94,1.06);
                    const double ds=std::clamp(sr*ms*ratios[h]/1000.0*local,2.0,double(l.size()-4));
                    const double tapL=double(read(l,ds)),tapR=double(read(r,ds));
                    const double mono=.5*(tapL+tapR);
                    const double side=.5*(tapL-tapR);
                    const double pos=std::clamp((p.headPan[h]*2.0-1.0)*spread,-1.0,1.0);
                    const double gl=std::sqrt(.5*(1-pos)),gr=std::sqrt(.5*(1+pos));
                    const double headL=mono*gl+0.20*side;
                    const double headR=mono*gr-0.20*side;
                    if(c[6+h]>=.5){wet.l+=(T)headL;wet.r+=(T)headR;sp++;}
                    if(c[10+h]>=.5){feedback.l+=(T)headL;feedback.r+=(T)headR;sf++;}
                }
                if(sp>0){wet.l=(T)(double(wet.l)/std::sqrt(sp));wet.r=(T)(double(wet.r)/std::sqrt(sp));}
                if(sf>0){feedback.l=(T)(double(feedback.l)/sf);feedback.r=(T)(double(feedback.r)/sf);}
                const double cutoff=11000-7600*globalWear;
                fbs.l=lowpass(tapeSat(feedback.l,drive,.58,globalWear,tapeMemL),lpL,cutoff);
                fbs.r=lowpass(tapeSat(feedback.r,drive,.43,globalWear,tapeMemR),lpR,cutoff*.97);
                const double lc=35+450*c[3];
                fbs.l=highpass(fbs.l,hpMemL,lc);fbs.r=highpass(fbs.r,hpMemR,lc);
                MechanicalFrame reelFrame=mech;
                reelFrame.impulse*=0.20;
                reelFrame.noise*=0.45;
                reelFrame.gain=0.72+0.28*reelFrame.gain;
                applyPhysicalFrame(wet,fbs,reelFrame,15000-9000*globalWear);
            }else if(algo==(int)DelayAlgorithm::ElCapistan){
                ms=std::clamp(ms,35.0,2500.0);
                const double age=c[0],wow=c[1],flutter=c[2],crinkle=c[3],bias=c[4],lowContour=c[5],spring=c[6];
                const double effectiveAge=aged(age,0.78),effectiveCrinkle=aged(crinkle,0.96);
                const double wear=std::clamp(0.52*effectiveAge+0.78*effectiveCrinkle,0.0,1.0);
                auto mech=transport.tick(aged(wow,0.66),aged(flutter,0.74),wear,aged(std::clamp(0.28*wow+0.30*flutter+0.95*crinkle,0.0,1.0),0.92),aged(0.22*age+0.38*crinkle,0.62));
                const double base=sr*ms/1000.0*(1.0+mech.pitch);
                const int mode=normIndex(c[7],3);
                auto tap=[&](double rr){return Stereo{read(l,base*rr),read(r,base*rr)};};
                if(mode==0)wet=tap(1);
                else if(mode==1){auto a=tap(.5),b=tap(1);wet={(T)(.64*a.l+.64*b.l),(T)(.64*a.r+.64*b.r)};}
                else{auto a=tap(.72),b=tap(1.0);wet={(T)(.38*a.l+.78*b.l),(T)(.38*a.r+.78*b.r)};}
                if(spring>.001){
                    auto s1=tap(.071),s2=tap(.103),s3=tap(.137);
                    wet.l+=(T)(spring*(.19*double(s1.r)-.13*double(s2.l)+.08*double(s3.r)));
                    wet.r+=(T)(spring*(.19*double(s1.l)-.13*double(s2.r)+.08*double(s3.l)));
                }
                const double cutoff=12500-8600*effectiveAge;
                fbs.l=lowpass(tapeSat(wet.l,.30+.78*bias,bias,effectiveAge,tapeMemL),lpL,cutoff);
                fbs.r=lowpass(tapeSat(wet.r,.30+.78*bias,bias*.94,effectiveAge,tapeMemR),lpR,cutoff*.98);
                fbs.l=highpass(fbs.l,hpMemL,45+350*lowContour);fbs.r=highpass(fbs.r,hpMemR,45+350*lowContour);
                applyPhysicalFrame(wet,fbs,mech,14500-9300*effectiveAge);
            }else if(algo==(int)DelayAlgorithm::Olivera){
                ms=std::clamp(ms,155.0,620.0);
                const double wear=c[0],visc=c[1],statik=c[2],hm=c[3],tone=c[4],dr=c[5];
                const double effectiveWear=aged(wear,0.88),effectiveStatic=aged(statik,0.80),effectiveDrift=aged(dr,0.74);
                auto mech=transport.tick(effectiveDrift*(1.0-0.48*visc),0.16*effectiveDrift,effectiveWear,aged(std::clamp(dr+0.42*wear,0.0,1.0),0.90),effectiveStatic);
                const double base=sr*ms/1000.0*(1.0+mech.pitch*(0.55+0.45*(1.0-visc)));
                auto sh=Stereo{read(l,base*.47),read(r,base*.47)};
                auto lh=Stereo{read(l,base),read(r,base)};
                auto disc=Stereo{read(l,std::min(base*1.29,double(l.size()-4))),read(r,std::min(base*1.29,double(r.size()-4)))};
                auto smearA=Stereo{read(l,base*(.985-.010*visc)),read(r,base*(.989-.006*visc))};
                auto smearB=Stereo{read(l,base*(1.014+.012*visc)),read(r,base*(1.010+.009*visc))};
                wet.l=(T)((1-hm)*double(sh.l)+hm*double(lh.l)+.18*double(disc.l)+.16*visc*(double(smearA.l)+double(smearB.l)));
                wet.r=(T)((1-hm)*double(sh.r)+hm*double(lh.r)+.18*double(disc.r)+.16*visc*(double(smearA.r)+double(smearB.r)));
                const double cutoff=1800+5200*tone-900*effectiveWear;
                fbs.l=lowpass(tapeSat(wet.l,.35+.65*effectiveWear,.54,effectiveWear,tapeMemL),lpL,cutoff);
                fbs.r=lowpass(tapeSat(wet.r,.35+.65*effectiveWear,.46,effectiveWear,tapeMemR),lpR,cutoff*.96);
                applyPhysicalFrame(wet,fbs,mech,4200+2900*tone-1200*effectiveWear);
            }else if(algo==(int)DelayAlgorithm::EC1){
                ms=std::clamp(ms,40.0,2500.0);
                const double mechAmt=c[0],age=c[1],bias=c[2],pre=c[3],rec=c[4],st=c[5];
                const double effectiveAge=aged(age,0.76),effectiveMech=aged(mechAmt,0.78);
                auto mech=transport.tick(effectiveMech,0.34*effectiveMech,effectiveAge,aged(std::clamp(mechAmt+0.34*age,0.0,1.0),0.90),aged(0.12*age,0.42));
                const double base=sr*ms/1000.0*(1.0+mech.pitch);
                wet={read(l,base*(1-.0025*st)),read(r,base*(1+.0025*st))};
                const double cutoff=13000-8200*effectiveAge;
                fbs.l=lowpass(tubeStage(wet.l,pre,rec,bias,tubeEnvL),lpL,cutoff);
                fbs.r=lowpass(tubeStage(wet.r,pre,rec,1.0-bias,tubeEnvR),lpR,cutoff*.985);
                applyPhysicalFrame(wet,fbs,mech,15500-9000*effectiveAge);
            }else if(algo==(int)DelayAlgorithm::Brig){
                const int voice=normIndex(c[0],3);
                const bool sync=p.sync>=.5;
                const double maxMs=sync?2000.0:(voice==0?300.0:1000.0),minMs=voice==0?30.0:100.0;
                ms=std::clamp(ms,minMs,maxMs);
                const double filter=c[1],md=c[2],mr=c[3],comp=c[4],noise=c[5];
                const double effectiveNoise=aged(noise,0.72),effectiveMod=aged(md,0.38);
                const double jitter=periodicMod((.00025+.0065*effectiveMod)*(voice==0?1.20:1.0),.22+3.4*mr)+noiseRng.bipolar()*(.00022*effectiveMod+.00035*damage);
                const double base=sr*ms/1000.0*(1.0+jitter);
                if(voice==2){auto a=Stereo{read(l,base),read(r,base*.618)};wet=a;fbs={(T)((1-c[1]*.25)*double(a.r)),(T)((1-c[1]*.25)*double(a.l))};}
                else{wet={read(l,base),read(r,base)};fbs=wet;}
                const double stages=voice==2?8192.0:4096.0;
                const double physicalBw=std::clamp((voice==0?.28:.40)*stages*1000.0/ms,850.0,voice==0?5200.0:9800.0);
                const double cutoff=physicalBw*(.58+.68*filter);
                wet.l=compandExpand(wet.l,comp*.52);wet.r=compandExpand(wet.r,comp*.52);
                fbs.l=lowpass(compandCompress(sat(fbs.l,voice==0?2.2:1.55,.03),comp),lpL,cutoff);
                fbs.r=lowpass(compandCompress(sat(fbs.r,voice==0?2.2:1.55,-.03),comp),lpR,cutoff*.97);
                const double clockHz=std::clamp(stages*500.0/ms,1800.0,0.44*sr);
                clockPhase+=2*pi*clockHz/sr;if(clockPhase>2*pi)clockPhase-=2*pi;
                const double clockBleed=std::sin(clockPhase)*(0.000015+0.00022*effectiveNoise+0.00012*damage);
                const double n=noiseRng.bipolar()*effectiveNoise*(voice==0?.0010:.00048);
                wet.l=(T)(double(wet.l)+.22*n+.22*clockBleed);wet.r=(T)(double(wet.r)-.18*n+.17*clockBleed);
                fbs.l=(T)(double(fbs.l)+n+clockBleed);fbs.r=(T)(double(fbs.r)-.79*n+.71*clockBleed);
            }else if(algo==(int)DelayAlgorithm::Deco){
                ms=std::clamp(ms,.3,500.0);
                const double saturation=c[0],wobble=c[1],blend=c[2],width=c[4],flange=c[5];
                const double effectiveWobble=aged(wobble,0.72),effectiveWear=aged(.16*saturation,0.56);
                auto mech=transport.tick(effectiveWobble,.22*effectiveWobble,effectiveWear,aged(.42*wobble,0.78),aged(.06*saturation,0.30));
                double d=sr*ms/1000.0*(1.0+mech.pitch);
                const int type=normIndex(c[3],3);
                const double flangeSweep=periodicMod(1.0,.055+.20*wobble);
                if(flange>0&&ms<20)d=std::max(2.0,d+sr*.0035*flange*flangeSweep);
                auto lag=Stereo{read(l,d*(1-.002*width)),read(r,d*(1+.002*width))};
                lag.l=tapeSat(lag.l,saturation,.56,aged(.18+.28*wobble,.48),tapeMemL);
                lag.r=tapeSat(lag.r,saturation,.44,aged(.18+.28*wobble,.48),tapeMemR);
                if(type==1){lag.l=(T)-lag.l;lag.r=(T)-lag.r;}
                if(type==2){T x=lag.r;lag.r=(T)(.25*double(lag.l));lag.l=x;}
                wet.l=(T)((1-blend)*double(inL)+blend*double(lag.l));
                wet.r=(T)((1-blend)*double(inR)+blend*double(lag.r));
                fbs={};fb=0;
                applyPhysicalFrame(wet,fbs,mech,16500-4200*saturation-2800*damage);
            }else if(algo==(int)DelayAlgorithm::SpaceEcho202){
                ms=std::clamp(ms,40.0,4000.0);
                const double satAmt=c[0],wowFlutter=c[1],tapeAge=c[2],bass=c[3],treble=c[4],reverb=c[5],headSpread=c[6],twist=c[7];
                const double age=aged(tapeAge,0.92);
                auto mech=transport.tick(aged(wowFlutter,0.72),aged(wowFlutter*0.84,0.78),age,aged(wowFlutter+0.42*age,0.92),aged(0.18*age,0.56));
                static constexpr double hr[4]={0.25,0.50,0.75,1.0};
                double active=0.0; Stereo sum{};
                for(int h=0;h<4;++h){
                    if(c[8+h]<0.5)continue;
                    const double drift=1.0+mech.pitch*(1.0+0.04*h);
                    const double ds=sr*ms*hr[h]/1000.0*drift;
                    auto t=Stereo{read(l,ds*(1.0-0.0025*headSpread*h)),read(r,ds*(1.0+0.0025*headSpread*h))};
                    const double pan=(h-1.5)/1.5*headSpread;
                    const double gl=std::sqrt(.5*(1-pan)),gr=std::sqrt(.5*(1+pan));
                    const double mono=.5*(double(t.l)+double(t.r));
                    sum.l+=(T)(mono*gl);sum.r+=(T)(mono*gr);active+=1.0;
                }
                if(active<0.5){sum={read(l,sr*ms/1000.0),read(r,sr*ms/1000.0)};active=1.0;}
                wet.l=(T)(double(sum.l)/std::sqrt(active));wet.r=(T)(double(sum.r)/std::sqrt(active));
                if(twist>0.001){
                    const double tw=periodicMod(0.035*twist,0.08+1.20*twist);
                    wet.l=(T)(double(wet.l)*(1.0+tw));
                    wet.r=(T)(double(wet.r)*(1.0-tw));
                }
                const double hp=28.0+220.0*(1.0-bass);
                const double lp=3500.0*std::pow(4.7,treble)*(1.0-0.48*age);
                fbs.l=highpass(lowpass(tapeSat(wet.l,0.30+0.92*satAmt,.58,age,tapeMemL),lpL,lp),hpMemL,hp);
                fbs.r=highpass(lowpass(tapeSat(wet.r,0.30+0.92*satAmt,.42,age,tapeMemR),lpR,lp*.985),hpMemR,hp);
                if(reverb>0.001){
                    auto a=Stereo{read(l,sr*.029),read(r,sr*.031)},b=Stereo{read(l,sr*.043),read(r,sr*.047)};
                    wet.l+=(T)(reverb*(.24*double(a.r)-.17*double(b.l)));
                    wet.r+=(T)(reverb*(.24*double(a.l)-.17*double(b.r)));
                }
                applyPhysicalFrame(wet,fbs,mech,15000-9800*age);
            }else if(algo==(int)DelayAlgorithm::DM101){
                ms=std::clamp(ms,20.0,2000.0);
                const int mode=normIndex(c[0],12);
                const double variation=c[1],md=c[2],mr=c[3],tone=c[4],stereo=c[5],comp=c[6],grit=c[7];
                const double mod=periodicMod((.00015+.0060*md),.08+4.2*mr);
                auto tap=[&](double ratio,double offset=0.0){double ds=sr*ms*ratio/1000.0*(1.0+mod+offset);return Stereo{read(l,ds),read(r,ds)};};
                Stereo x{};
                if(mode==0||mode==1||mode==2){x=tap(1.0);}
                else if(mode==3){auto a=tap(.5),b=tap(.75),d=tap(1.0);x={(T)(.46*a.l+.43*b.l+.58*d.l),(T)(.46*a.r+.43*b.r+.58*d.r)};}
                else if(mode==4){auto a=tap(.18),b=tap(.34),d=tap(.52+variation*.28);x={(T)(.22*a.l+.38*b.l+.72*d.l),(T)(.22*a.r+.38*b.r+.72*d.r)};}
                else if(mode==5||mode==6){auto a=tap(.08+.10*variation),b=tap(.13+.13*variation);x={(T)(.62*a.l+.44*b.r),(T)(.62*a.r+.44*b.l)};}
                else if(mode==7){auto a=tap(.035),b=tap(1.0);x={(T)(.35*a.r+.78*b.l),(T)(.35*a.l+.78*b.r)};}
                else if(mode==8){x={tap(1.0,-.004*stereo).l,tap(1.0,.004*stereo).r};}
                else if(mode==9){const double m=periodicMod(.007*md,.05+3.5*mr);x={tap(1.0,m).l,tap(1.0,-m).r};}
                else if(mode==10){auto a=tap(.50-.18*variation),b=tap(.75),d=tap(1.0+.12*variation);x={(T)(.55*a.l+.58*d.r),(T)(.55*b.r+.58*d.l)};}
                else {static constexpr double pat[5]={.25,.375,.5,.75,1.0};for(int k=0;k<5;++k){auto q=tap(pat[k]*(.84+.32*variation));x.l+=(T)(double(q.l)*.30);x.r+=(T)(double(q.r)*.30);}}
                wet=x;
                const double modeDark=(mode==1?.72:(mode==0?.82:1.0));
                const double cutoff=(1800.0+10800.0*std::pow(tone,1.55))*modeDark;
                wet.l=compandExpand(wet.l,.46*comp);wet.r=compandExpand(wet.r,.46*comp);
                fbs.l=lowpass(compandCompress(sat(wet.l,1.18+1.8*grit,.025),comp),lpL,cutoff);
                fbs.r=lowpass(compandCompress(sat(wet.r,1.18+1.8*grit,-.025),comp),lpR,cutoff*.97);
                const double n=(0.00002+0.00032*grit)*noiseRng.bipolar();
                fbs.l=(T)(double(fbs.l)+n);fbs.r=(T)(double(fbs.r)-.83*n);
            }else{
                ms=std::clamp(ms,20.0,3200.0);
                const int type=normIndex(c[0],3);
                const double ratio=.5+c[1],md=c[2],cross=c[3],dyn=c[4],tone=c[5];
                const double mod=periodicMod(.0002+.004*md+.0012*damage,.25+2.2*md)+noiseRng.bipolar()*.00012*damage;
                const double d1=sr*ms/1000.0*(1+mod),d2=std::clamp(d1*ratio,2.0,double(l.size()-4));
                Stereo a{read(l,d1),read(r,d1)},b{read(l,d2),read(r,d2)};
                a=digitalColor(a,type);b=digitalColor(b,type);
                wet={(T)(.72*double(a.l)+.62*double(b.l)),(T)(.72*double(a.r)+.62*double(b.r))};
                const double dg=1-dyn*std::clamp(double(duckEnv)*1.4,0.0,.65);
                fbs.l=(T)(dg*((1-cross)*double(wet.l)+cross*double(wet.r)));
                fbs.r=(T)(dg*((1-cross)*double(wet.r)+cross*double(wet.l)));
                const double cutoff=5500+12500*tone;
                fbs.l=lowpass((T)(double(fbs.l)+noiseRng.bipolar()*.00012*damage),lpL,cutoff*(1.0-.24*damage));fbs.r=lowpass((T)(double(fbs.r)+noiseRng.bipolar()*.00010*damage),lpR,cutoff*(1.0-.22*damage));
            }

            // Keep bad upstream samples or automation from poisoning the feedback ring.
            const double nextL=double(inL)+fb*double(fbs.l);
            const double nextR=double(inR)+fb*double(fbs.r);
            T recL=std::isfinite(nextL)?sat((T)std::clamp(nextL,-16.0,16.0),1.05,.01):T{};
            T recR=std::isfinite(nextR)?sat((T)std::clamp(nextR,-16.0,16.0),1.05,-.01):T{};
            l[w]=recL;r[w]=recR;w=(w+1)%l.size();

            const double pan=std::clamp(p.pan*2-1.0,-1.0,1.0);
            const double gl=std::sqrt(.5*(1-pan))*1.41421356237,gr=std::sqrt(.5*(1+pan))*1.41421356237;
            const double wL=double(wet.l)*level*duckGain*gl;
            const double wR=double(wet.r)*level*duckGain*gr;
            wet.l=std::isfinite(wL)?(T)std::clamp(wL,-8.0,8.0):T{};
            wet.r=std::isfinite(wR)?(T)std::clamp(wR,-8.0,8.0):T{};
            return wet;
        }
    };

    struct WetFxDSP {
        double sr=44100.0,phase=0.0;
        std::vector<T> l,r; size_t w=0; uint32_t rng=0x9e3779b9u; int hold=0,holdPeriod=1; T heldL{},heldR{};
        void prepare(double sampleRate){sr=std::max(8000.0,sampleRate);const size_t n=(size_t)std::ceil(sr*.25)+8;l.assign(n,T{});r.assign(n,T{});reset();}
        void reset(){std::fill(l.begin(),l.end(),T{});std::fill(r.begin(),r.end(),T{});w=0;phase=0;hold=0;holdPeriod=1;heldL=heldR=T{};rng=0x9e3779b9u;}
        T read(const std::vector<T>& b,double ds)const{if(b.empty())return T{};ds=std::clamp(ds,1.0,double(b.size()-2));double p=double(w)-ds;while(p<0)p+=b.size();const size_t i0=(size_t)p,i1=(i0+1)%b.size();const double f=p-std::floor(p);return (T)(double(b[i0])+(double(b[i1])-double(b[i0]))*f);}
        Stereo process(Stereo in,const WetFxParams& p){
            const int type=normIndex(p.type,(int)WetFxType::Count);const double a=std::clamp(p.amount,0.0,1.0);if(type==0||a<1e-5)return in;
            const double rate=.03+5.0*p.rate*p.rate,depth=std::clamp(p.depth,0.0,1.0),drive=std::clamp(p.drive,0.0,1.0);
            if(type==(int)WetFxType::Overdrive){Stereo q{(T)std::tanh(double(in.l)*(1.0+8.0*drive)),(T)std::tanh(double(in.r)*(1.0+8.0*drive))};return {(T)((1-a)*double(in.l)+a*double(q.l)),(T)((1-a)*double(in.r)+a*double(q.r))};}
            if(type==(int)WetFxType::LoFi){
                holdPeriod=1+(int)std::lround((2.0+46.0*depth)*(0.25+0.75*a));if(++hold>=holdPeriod){hold=0;heldL=in.l;heldR=in.r;}
                const double bits=std::clamp(16.0-11.0*depth-3.0*a,3.0,16.0),q=std::pow(2.0,bits-1.0);
                Stereo z{(T)(std::round(double(heldL)*q)/q),(T)(std::round(double(heldR)*q)/q)};
                return {(T)((1-a)*double(in.l)+a*double(z.l)),(T)((1-a)*double(in.r)+a*double(z.r))};
            }
            if(l.empty())return in;
            l[w]=in.l;r[w]=in.r;
            phase+=2*pi*rate/sr;if(phase>2*pi)phase-=2*pi;
            Stereo fx{};
            if(type==(int)WetFxType::Chorus){
                const double mod=.5+.5*std::sin(phase),dL=sr*(.010+.018*depth*mod),dR=sr*(.011+.018*depth*(1.0-mod));
                fx={read(l,dL),read(r,dR)};
            }else if(type==(int)WetFxType::Flanger){
                const double mod=.5+.5*std::sin(phase),dL=sr*(.0006+.0055*depth*mod),dR=sr*(.0008+.0050*depth*(1.0-mod));
                fx={(T)(double(in.l)-.72*double(read(l,dL))),(T)(double(in.r)-.72*double(read(r,dR)))};
            }else{
                rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
                const double jitter=double(rng&0xffffu)/65535.0;
                const double d=sr*(.018+.095*depth*(.35+.65*jitter));
                fx={read(l,d),read(r,d*(.93+.14*jitter))};
            }
            w=(w+1)%l.size();
            return {(T)((1-a)*double(in.l)+a*double(fx.l)),(T)((1-a)*double(in.r)+a*double(fx.r))};
        }

    };

    SlotDSP s[2];
    std::array<WetFxDSP,4> wetFx;
    double sr=44100.0;
public:
    void prepare(double sampleRate,int=2){
        sr=sampleRate;
        s[0].prepare(sampleRate,0x13579bdfu);
        s[1].prepare(sampleRate,0x2468ace1u);
        for(auto& fx:wetFx)fx.prepare(sampleRate);
    }
    void reset(){for(auto& x:s)x.reset();for(auto& fx:wetFx)fx.reset();}

    template<class Sample> void process(Sample** in,Sample** out,int channels,int n,const MXDelayParams& p,double bpm,double& peak){
        const double inGain=gainFromNorm(p.inputTrim,-18,18);
        const double outGain=gainFromNorm(p.outputTrim,-18,12);
        const double mix=std::clamp(p.mix,0.0,1.0);
        const double dryG=1.0-mix,wetG=mix;
        const int routing=normIndex(p.routing,3);
        const bool bypass=p.bypass>=.5,trails=p.spill>=.5;
        peak=0;

        if(bypass&&!trails){s[0].reset();s[1].reset();}

        for(int i=0;i<n;++i){
            const T rawL=(T)(in&&in[0]?in[0][i]:0);
            const T rawR=(channels>1&&in&&in[1])?(T)in[1][i]:rawL;

            if(bypass&&!trails){
                if(out&&out[0])out[0][i]=(Sample)rawL;
                if(channels>1&&out&&out[1])out[1][i]=(Sample)rawR;
                peak=std::max({peak,std::abs((double)rawL),std::abs((double)rawR)});
                continue;
            }

            T inL=(T)(bypass?0.0:double(rawL)*inGain);
            T inR=(T)(bypass?0.0:double(rawR)*inGain);
            if(!bypass){
                Stereo pre{inL,inR};
                for(int fx=0;fx<4;++fx)if(p.fx[fx].route<.5)pre=wetFx[fx].process(pre,p.fx[fx]);
                inL=pre.l;inR=pre.r;
            }
            Stereo a{},b{},wet{};

            if(routing==(int)DelayRouting::Parallel){
                a=s[0].process(inL,inR,p.slot[0],bpm,p.machineCondition);
                b=s[1].process(inL,inR,p.slot[1],bpm,p.machineCondition);
                wet={(T)((double(a.l)+double(b.l))*0.70710678118),(T)((double(a.r)+double(b.r))*0.70710678118)};
            }else if(routing==(int)DelayRouting::Series){
                a=s[0].process(inL,inR,p.slot[0],bpm,p.machineCondition);
                b=s[1].process((T)(double(inL)+.72*double(a.l)),(T)(double(inR)+.72*double(a.r)),p.slot[1],bpm,p.machineCondition);
                wet={(T)(.72*(.48*double(a.l)+double(b.l))),(T)(.72*(.48*double(a.r)+double(b.r)))};
            }else{
                a=s[0].process(inL,inL,p.slot[0],bpm,p.machineCondition);
                b=s[1].process(inR,inR,p.slot[1],bpm,p.machineCondition);
                wet={a.l,b.r};
            }

            for(int fx=0;fx<4;++fx)if(p.fx[fx].route>=.5)wet=wetFx[fx].process(wet,p.fx[fx]);
            T yL{},yR{};
            if(bypass&&trails){
                yL=(T)(double(rawL)+wetG*double(wet.l)*outGain);
                yR=(T)(double(rawR)+wetG*double(wet.r)*outGain);
            }else{
                yL=(T)((dryG*double(inL)+wetG*double(wet.l))*outGain);
                yR=(T)((dryG*double(inR)+wetG*double(wet.r))*outGain);
            }
            if(out&&out[0])out[0][i]=(Sample)yL;
            if(channels>1&&out&&out[1])out[1][i]=(Sample)yR;
            peak=std::max({peak,std::abs((double)yL),std::abs((double)yR)});
        }
    }
};
} // namespace JerzyAudio
