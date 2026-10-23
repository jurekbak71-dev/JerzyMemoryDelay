#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>
#include "tapedrive_params.h"

namespace JerzyAudio {

template <typename Sample>
class TapeDriveDSP {
public:
    void prepare(double sr, int channels=2) {
        sampleRate=std::max(8000.0,sr);
        numChannels=std::clamp(channels,1,2);
        const size_t delaySize=static_cast<size_t>(sampleRate*0.060)+12;
        for(int ch=0;ch<2;++ch){
            wowBuffer[ch].assign(delaySize,0.0);
            wowWrite[ch]=0;
        }
        reset();
    }

    void reset(){
        for(int i=0;i<2;++i){
            low[i]=0.0; env[i]=0.0; dc[i]=0.0;
            hp_z1[i]=hp_z2[i]=0.0;
            lp_z1[i]=lp_z2[i]=0.0;
            wearLP1[i]=wearLP2[i]=headBumpLP[i]=0.0;
            tubeSag[i]=tubeTone[i]=transistorTone[i]=0.0;
            optoColorLP[i]=optoColorHF[i]=optoColorSag[i]=0.0;
            hissLP[i]=crackle[i]=dropout[i]=dropoutTarget[i]=microDrop[i]=thump[i]=0.0;
            dropoutLeft[i]=microDropLeft[i]=0;
            std::fill(wowBuffer[i].begin(),wowBuffer[i].end(),0.0);
            wowWrite[i]=0;
        }
        randomState=0x4a65727au;
        wowMotion={}; flutterMotion={}; scrapeMotion={}; wearMotion={};
        smoothWow=smoothFlutter=smoothAge=0.0;
        smoothOpto=0.0; smoothMakeup=1.0; smoothOptoMix=0.0;
        optoDetectorFast=optoDetectorSlow=0.0;
        optoFastReduction=optoSlowReduction=optoReduction=optoHistory=0.0;
        optoSidechainLow=0.0;
        wowPhase1=wowPhase2=flutterPhase1=flutterPhase2=scrapePhase=0.0;
        slipLeft=slipLength=0; slipDepthMs=0.0;
    }

    void process(const Sample* const* in, Sample* const* out, int channels, int n,
                 const TapeDriveParams& p,
                 double& inputPeak, double& saturationPeak, double& outputPeak,
                 double& reductionPeak) {
        double sat=std::clamp(p.sat,0.0,1.0);
        double levelNorm=std::clamp(p.level,0.0,1.0);
        double dryNorm=std::clamp(p.dry,0.0,1.0);
        const int gainMode=p.gainMode>=0.5?1:0;
        const int shiftMode=std::clamp(static_cast<int>(std::lround(p.shift*2.0)),0,2);
        const int preampMode=std::clamp(static_cast<int>(std::lround(p.preampMode*2.0)),0,2);
        const double preampDriveNorm=std::clamp(p.preampDrive,0.0,1.0);
        const double wowTarget=std::clamp(p.wowFlutter,0.0,1.0);
        const double flutterTarget=std::clamp(p.flutter,0.0,1.0);
        const double ageTarget=std::clamp(p.tapeAge,0.0,1.0);
        const double optoTarget=std::clamp(p.optoAmount,0.0,1.0);
        const bool optoBypassed=p.optoBypass>=0.5;
        const double makeupTarget=optoBypassed?1.0:std::pow(10.0,(-12.0+24.0*std::clamp(p.optoMakeup,0.0,1.0))/20.0);
        channels=std::clamp(channels,1,numChannels);

        inputPeak=0.0; saturationPeak=0.0; outputPeak=0.0; reductionPeak=0.0;

        const double driveDb=(gainMode?12.0+30.0*sat:22.0*sat);
        const double drive=std::pow(10.0,driveDb/20.0);
        const double outDb=-24.0+36.0*levelNorm;
        const double outGain=std::pow(10.0,outDb/20.0);
        const double dryGain=dryNorm<=0.001?0.0:std::pow(10.0,(6.0*dryNorm)/20.0)*dryNorm;
        const double lowA=std::exp(-2.0*3.141592653589793*2200.0/sampleRate);
        const double atk=std::exp(-1.0/(0.006*sampleRate));
        const double rel=std::exp(-1.0/(0.090*sampleRate));
        const double dcA=std::exp(-2.0*3.141592653589793*18.0/sampleRate);

        Biquad hp=makeHighPass(mapLog(p.hpfCutoff,20.0,2000.0),mapQ(p.hpfRes));
        Biquad lp=makeLowPass(mapLog(p.lpfCutoff,1000.0,20000.0),mapQ(p.lpfRes));

        const double smoothA=std::exp(-1.0/(0.030*sampleRate));
        const double detectorAttack=std::exp(-1.0/(0.010*sampleRate));
        const double detectorRelease=std::exp(-1.0/(0.080*sampleRate));
        const double detectorMemory=std::exp(-1.0/(0.650*sampleRate));
        const double historyAttack=std::exp(-1.0/(0.350*sampleRate));
        const double historyRelease=std::exp(-1.0/(8.0*sampleRate));
        const double fastRelease=std::exp(-1.0/(0.060*sampleRate));
        const double slowAttack=std::exp(-1.0/(0.030*sampleRate));
        const double sidechainLowA=std::exp(-2.0*3.141592653589793*180.0/sampleRate);
        const double optoColorLowA=std::exp(-2.0*3.141592653589793*180.0/sampleRate);
        const double optoColorSagA=std::exp(-1.0/(0.055*sampleRate));

        const double hissA=std::exp(-2.0*3.141592653589793*2200.0/sampleRate);
        const double crackA=std::exp(-1.0/(0.0018*sampleRate));
        const double dropAttack=std::exp(-1.0/(0.003*sampleRate));
        const double dropRelease=std::exp(-1.0/(0.045*sampleRate));
        const double microRelease=std::exp(-1.0/(0.0035*sampleRate));
        const double thumpRelease=std::exp(-1.0/(0.028*sampleRate));

        const double tubeDrive=std::pow(10.0,(4.0+30.0*preampDriveNorm)/20.0);
        const double transistorDrive=std::pow(10.0,(2.0+34.0*preampDriveNorm)/20.0);
        const double tubeSagAtk=std::exp(-1.0/(0.018*sampleRate));
        const double tubeToneA=std::exp(-2.0*3.141592653589793*(11500.0-3500.0*preampDriveNorm)/sampleRate);
        const double transistorToneA=std::exp(-2.0*3.141592653589793*4200.0/sampleRate);
        const double pi=3.14159265358979323846;

        for(int i=0;i<n;++i){
            smoothWow=smoothA*smoothWow+(1.0-smoothA)*wowTarget;
            smoothFlutter=smoothA*smoothFlutter+(1.0-smoothA)*flutterTarget;
            smoothAge=smoothA*smoothAge+(1.0-smoothA)*ageTarget;
            smoothOpto=smoothA*smoothOpto+(1.0-smoothA)*optoTarget;
            smoothMakeup=smoothA*smoothMakeup+(1.0-smoothA)*makeupTarget;
            smoothOptoMix=smoothA*smoothOptoMix+(1.0-smoothA)*(optoBypassed?0.0:1.0);

            // Transport: two quasi-periodic mechanical components plus correlated
            // random drift. The aged-tape path also adds fast scrape flutter and
            // occasional capstan slips, so maximum settings become intentionally
            // unstable rather than sounding like a tidy chorus LFO.
            const double wowRnd=nextMotion(wowMotion,0.18,1.40);
            const double flutterRnd=nextMotion(flutterMotion,0.008,0.055);
            const double scrapeRnd=nextMotion(scrapeMotion,0.0025,0.014);
            const double wearRnd=nextMotion(wearMotion,0.030,0.240);
            advancePhase(wowPhase1,0.31+0.09*smoothWow);
            advancePhase(wowPhase2,0.73+0.13*smoothWow);
            advancePhase(flutterPhase1,5.4+1.8*smoothFlutter);
            advancePhase(flutterPhase2,10.7+3.5*smoothFlutter);
            advancePhase(scrapePhase,31.0+29.0*smoothAge);

            if(slipLeft<=0 && smoothAge>0.35 && uniform()<0.34*std::pow(smoothAge,3.2)/sampleRate){
                slipLength=std::max(1,static_cast<int>((0.030+0.170*uniform())*sampleRate));
                slipLeft=slipLength;
                slipDepthMs=(2.0*uniform()-1.0)*(0.35+2.10*std::pow(smoothAge,1.7));
            }
            double slipMs=0.0;
            if(slipLeft>0){
                const double t=1.0-static_cast<double>(slipLeft)/std::max(1,slipLength);
                const double e=std::sin(pi*std::clamp(t,0.0,1.0));
                slipMs=slipDepthMs*e*e;
                --slipLeft;
            }

            const double wowMs=smoothWow*(0.48*std::sin(wowPhase1)+0.22*std::sin(wowPhase2)+1.65*wowRnd);
            const double flutterMs=smoothFlutter*(0.055*std::sin(flutterPhase1)+0.028*std::sin(flutterPhase2)+0.085*flutterRnd);
            const double scrapeMs=smoothAge*smoothAge*(0.012*std::sin(scrapePhase)+0.024*scrapeRnd);
            const double movement=std::max({smoothWow,smoothFlutter,smoothAge*0.55});
            const double centreDelayMs=movement>0.0001?5.8:0.0;
            const double delaySamples=std::clamp((centreDelayMs+wowMs+flutterMs+scrapeMs+slipMs)*0.001*sampleRate,0.0,0.050*sampleRate);

            // Program/frequency-dependent optical detector. Low frequencies are
            // allowed to drive the cell slightly harder, while the memory state
            // makes sustained compression recover far more slowly.
            double detector=0.0;
            for(int ch=0;ch<channels;++ch)
                if(in && in[ch])detector=std::max(detector,std::abs(static_cast<double>(in[ch][i])));
            optoSidechainLow=(1.0-sidechainLowA)*detector+sidechainLowA*optoSidechainLow;
            const double weightedDetector=std::max(detector,0.82*detector+0.28*optoSidechainLow);
            const double detectorCoeff=weightedDetector>optoDetectorFast?detectorAttack:detectorRelease;
            optoDetectorFast=detectorCoeff*optoDetectorFast+(1.0-detectorCoeff)*weightedDetector;
            optoDetectorSlow=detectorMemory*optoDetectorSlow+(1.0-detectorMemory)*optoDetectorFast;

            const double detectorDb=20.0*std::log10(std::max(1e-10,0.78*optoDetectorFast+0.22*optoDetectorSlow));
            const double threshold=-7.0-29.0*smoothOpto;
            const double over=detectorDb-threshold;
            const double knee=8.0;
            const double kneeOver=over<=-0.5*knee?0.0:
                (over>=0.5*knee?over:(over+0.5*knee)*(over+0.5*knee)/(2.0*knee));
            const double ratio=3.0+17.0*std::pow(smoothOpto,1.25);
            const double desiredReduction=kneeOver*(1.0-1.0/ratio)*smoothOpto;

            const double historyTarget=std::clamp((desiredReduction-1.0)/15.0,0.0,1.0);
            const double historyCoeff=historyTarget>optoHistory?historyAttack:historyRelease;
            optoHistory=historyCoeff*optoHistory+(1.0-historyCoeff)*historyTarget;

            const double fastCoeff=desiredReduction>optoFastReduction?detectorAttack:fastRelease;
            optoFastReduction=fastCoeff*optoFastReduction+(1.0-fastCoeff)*desiredReduction;
            const double slowReleaseSeconds=1.0+14.0*std::pow(std::clamp(optoHistory,0.0,1.0),0.72);
            const double slowRelease=std::exp(-1.0/(slowReleaseSeconds*sampleRate));
            const double slowCoeff=desiredReduction>optoSlowReduction?slowAttack:slowRelease;
            optoSlowReduction=slowCoeff*optoSlowReduction+(1.0-slowCoeff)*desiredReduction;
            optoReduction=0.55*optoFastReduction+0.45*optoSlowReduction;
            const double compressedGain=std::pow(10.0,-optoReduction/20.0)*smoothMakeup;
            const double optoGain=1.0+smoothOptoMix*(compressedGain-1.0);
            reductionPeak=std::max(reductionPeak,std::clamp(optoReduction*smoothOptoMix/24.0,0.0,1.0));

            const double wearBaseCutoff=18000.0*std::pow(3400.0/18000.0,std::pow(smoothAge,1.25));
            const double wrinkleLoss=1.0-0.38*smoothAge*smoothAge*std::max(0.0,wearRnd);
            const double wearCutoff=std::clamp(wearBaseCutoff*wrinkleLoss,2200.0,19000.0);
            const double wearA=std::exp(-2.0*pi*wearCutoff/sampleRate);
            const double headBumpA=std::exp(-2.0*pi*115.0/sampleRate);
            const double optoHFCutoff=18500.0-4500.0*smoothOpto;
            const double optoHFA=std::exp(-2.0*pi*std::max(5000.0,optoHFCutoff)/sampleRate);

            for(int ch=0;ch<channels;++ch){
                const double x=in && in[ch]?static_cast<double>(in[ch][i]):0.0;
                inputPeak=std::max(inputPeak,std::abs(x));

                // Optical amplifier colour: when the compressor is active the
                // gain cell is followed by a gentle tube/transformer-like stage.
                // Bypass is still a true DSP bypass of this section.
                double pre=x*optoGain;
                double optoSat=0.0;
                if(smoothOptoMix>1e-6){
                    optoColorSag[ch]=optoColorSagA*optoColorSag[ch]+(1.0-optoColorSagA)*std::abs(pre);
                    const double colour=0.10+0.24*smoothOpto;
                    const double colourDrive=(1.18+0.92*smoothOpto+0.028*optoReduction)/(1.0+0.10*optoColorSag[ch]*smoothOpto);
                    const double z=std::clamp(pre*colourDrive,-5.0,5.0);
                    const double bias=0.045+0.035*smoothOpto;
                    const double tube=0.78*(std::tanh(z+bias)-std::tanh(bias));
                    optoColorLP[ch]=(1.0-optoColorLowA)*pre+optoColorLowA*optoColorLP[ch];
                    double coloured=(1.0-colour)*pre+colour*tube;
                    coloured+=(0.018+0.052*smoothOpto)*optoColorLP[ch];
                    optoColorHF[ch]=(1.0-optoHFA)*coloured+optoHFA*optoColorHF[ch];
                    pre=0.82*coloured+0.18*optoColorHF[ch];
                    optoSat=std::clamp(std::abs(z-tube)/(std::abs(z)+0.30)*colour,0.0,1.0);
                }

                // PREAMP STAGE: OFF / TUBE / TRANSISTOR
                double preSat=optoSat;
                if(preampMode==1){
                    const double absx=std::abs(pre);
                    tubeSag[ch]=tubeSagAtk*tubeSag[ch]+(1.0-tubeSagAtk)*absx;
                    const double sag=1.0/(1.0+tubeSag[ch]*(0.55+1.8*preampDriveNorm));
                    const double z=pre*tubeDrive*sag;
                    const double bias=0.10+0.12*preampDriveNorm;
                    double y=std::tanh(z+bias)-std::tanh(bias);
                    y+=0.10*preampDriveNorm*(y*y)*(y>=0.0?1.0:-0.45);
                    y*=0.78/std::max(0.45,std::sqrt(tubeDrive)*0.22);
                    tubeTone[ch]=(1.0-tubeToneA)*y+tubeToneA*tubeTone[ch];
                    pre=0.72*y+0.28*tubeTone[ch];
                    preSat=std::max(preSat,std::clamp(std::abs(z-y)/(std::abs(z)+0.25),0.0,1.0));
                }else if(preampMode==2){
                    transistorTone[ch]=(1.0-transistorToneA)*pre+transistorToneA*transistorTone[ch];
                    const double presence=pre-transistorTone[ch];
                    const double z=(pre+0.18*presence)*transistorDrive;
                    const double soft=std::tanh(z*1.15);
                    const double hard=std::clamp(z,-1.15,0.92);
                    pre=(0.48*soft+0.52*hard)/(0.75+0.18*preampDriveNorm);
                    pre+=0.045*preampDriveNorm*pre*pre*pre;
                    preSat=std::max(preSat,std::clamp(std::abs(z-pre)/(std::abs(z)+0.22),0.0,1.0));
                }

                low[ch]=(1.0-lowA)*pre+lowA*low[ch];
                const double high=pre-low[ch];
                double shapedIn=pre;
                if(shiftMode==1)shapedIn=pre+0.34*high;
                else if(shiftMode==2)shapedIn=pre+0.18*high+0.10*low[ch];

                const double a=std::abs(shapedIn);
                const double coeff=a>env[ch]?atk:rel;
                env[ch]=coeff*env[ch]+(1.0-coeff)*a;
                const double comp=1.0/(1.0+env[ch]*(0.8+2.7*sat));

                const double bias=0.035+0.075*sat;
                const double z=shapedIn*drive*comp;
                const double soft=std::tanh(z+bias)-std::tanh(bias);
                double wet=soft+0.08*sat*std::tanh(z*z*(z>=0?1.0:-1.0));

                const double tapeSat=std::clamp(std::abs(z-soft)/(std::abs(z)+0.35),0.0,1.0)
                                     *std::min(1.0,0.25+1.15*sat);
                saturationPeak=std::max(saturationPeak,std::max(preSat,tapeSat));

                dc[ch]=dcA*dc[ch]+(1.0-dcA)*wet;
                wet-=dc[ch];
                wet=readDelay(ch,wet,delaySamples);

                // Worn tape: two-stage high-frequency loss, mild head bump,
                // slow level wander, macro dropouts, micro-dropouts, hiss,
                // crackle and low-frequency thumps around damaged sections.
                wearLP1[ch]=(1.0-wearA)*wet+wearA*wearLP1[ch];
                wearLP2[ch]=(1.0-wearA)*wearLP1[ch]+wearA*wearLP2[ch];
                const double wearMix=0.82*smoothAge;
                wet=wet*(1.0-wearMix)+wearLP2[ch]*wearMix;
                headBumpLP[ch]=(1.0-headBumpA)*wet+headBumpA*headBumpLP[ch];
                wet+=headBumpLP[ch]*(0.02+0.085*smoothAge)*smoothAge;
                wet*=1.0-0.035*smoothAge+0.030*smoothAge*wearRnd;

                if(dropoutLeft[ch]>0){
                    --dropoutLeft[ch];
                }else{
                    dropoutTarget[ch]=0.0;
                    if(smoothAge>0.10 && uniform()<(0.03+0.52*smoothAge*smoothAge)*smoothAge/sampleRate){
                        dropoutTarget[ch]=(0.22+0.76*uniform())*std::pow(smoothAge,0.72);
                        dropoutLeft[ch]=std::max(1,static_cast<int>((0.012+0.220*uniform())*sampleRate));
                        thump[ch]+=(2.0*uniform()-1.0)*(0.018+0.075*smoothAge);
                    }
                }
                const double dropA=dropoutTarget[ch]>dropout[ch]?dropAttack:dropRelease;
                dropout[ch]=dropA*dropout[ch]+(1.0-dropA)*dropoutTarget[ch];

                if(microDropLeft[ch]>0){
                    --microDropLeft[ch];
                }else if(smoothAge>0.28 && uniform()<(0.4+9.0*smoothAge*smoothAge)*smoothAge/sampleRate){
                    microDrop[ch]=(0.08+0.50*uniform())*smoothAge;
                    microDropLeft[ch]=std::max(1,static_cast<int>((0.001+0.006*uniform())*sampleRate));
                }
                if(microDropLeft[ch]<=0)microDrop[ch]*=microRelease;
                wet*=std::max(0.015,1.0-dropout[ch]-microDrop[ch]);

                const double noise=2.0*uniform()-1.0;
                hissLP[ch]=hissA*hissLP[ch]+(1.0-hissA)*noise;
                const double hissLevel=(0.0012*smoothAge+0.0135*smoothAge*smoothAge);
                const double hiss=hissLevel*(noise-0.58*hissLP[ch]);
                crackle[ch]*=crackA;
                if(smoothAge>0.0001 && uniform()<(0.35+15.0*smoothAge*smoothAge)*smoothAge/sampleRate)
                    crackle[ch]+=(2.0*uniform()-1.0)*(0.018+0.095*smoothAge)*std::sqrt(smoothAge);
                thump[ch]*=thumpRelease;
                wet+=hiss+crackle[ch]*smoothAge+thump[ch];

                wet*=outGain;
                double y=wet+x*dryGain;
                y=runBiquad(y,hp,hp_z1[ch],hp_z2[ch]);
                y=runBiquad(y,lp,lp_z1[ch],lp_z2[ch]);

                outputPeak=std::max(outputPeak,std::abs(y));
                if(out && out[ch])out[ch][i]=static_cast<Sample>(y);
            }
        }
    }

private:
    struct Biquad { double b0{},b1{},b2{},a1{},a2{}; };
    struct Motion { double from=0.0,to=0.0; int elapsed=0,length=0; };

    double uniform(){
        randomState^=randomState<<13; randomState^=randomState>>17; randomState^=randomState<<5;
        return static_cast<double>(randomState)/4294967296.0;
    }

    double nextMotion(Motion& motion,double minSeconds,double maxSeconds){
        if(motion.elapsed>=motion.length){
            motion.from=motion.to;
            motion.to=2.0*uniform()-1.0;
            motion.length=std::max(1,static_cast<int>((minSeconds+(maxSeconds-minSeconds)*uniform())*sampleRate));
            motion.elapsed=0;
        }
        const double t=static_cast<double>(motion.elapsed++)/motion.length;
        return motion.from+(motion.to-motion.from)*t*t*(3.0-2.0*t);
    }

    void advancePhase(double& phase,double hz){
        phase+=2.0*3.14159265358979323846*hz/sampleRate;
        if(phase>2.0*3.14159265358979323846)phase-=2.0*3.14159265358979323846;
    }

    static double mapLog(double n,double lo,double hi){
        n=std::clamp(n,0.0,1.0);
        return lo*std::pow(hi/lo,n);
    }
    static double mapQ(double n){
        n=std::clamp(n,0.0,1.0);
        return 0.5+11.5*n;
    }

    Biquad makeLowPass(double freq,double q) const { return makeFilter(freq,q,false); }
    Biquad makeHighPass(double freq,double q) const { return makeFilter(freq,q,true); }

    Biquad makeFilter(double freq,double q,bool highPass) const {
        const double pi=3.14159265358979323846;
        freq=std::clamp(freq,10.0,sampleRate*0.45);
        q=std::clamp(q,0.5,12.0);
        const double w0=2.0*pi*freq/sampleRate;
        const double c=std::cos(w0);
        const double s=std::sin(w0);
        const double alpha=s/(2.0*q);
        double b0,b1,b2;
        if(highPass){
            b0=(1.0+c)*0.5; b1=-(1.0+c); b2=(1.0+c)*0.5;
        }else{
            b0=(1.0-c)*0.5; b1=1.0-c; b2=(1.0-c)*0.5;
        }
        const double a0=1.0+alpha;
        Biquad b;
        b.b0=b0/a0; b.b1=b1/a0; b.b2=b2/a0;
        b.a1=(-2.0*c)/a0; b.a2=(1.0-alpha)/a0;
        return b;
    }

    static double runBiquad(double x,const Biquad& b,double& z1,double& z2){
        const double y=b.b0*x+z1;
        z1=b.b1*x-b.a1*y+z2;
        z2=b.b2*x-b.a2*y;
        return y;
    }

    double readDelay(int ch,double input,double delaySamples){
        auto& b=wowBuffer[ch];
        if(b.empty())return input;
        const size_t size=b.size();
        b[wowWrite[ch]]=input;
        double rp=static_cast<double>(wowWrite[ch])-delaySamples;
        while(rp<0.0)rp+=static_cast<double>(size);
        while(rp>=static_cast<double>(size))rp-=static_cast<double>(size);
        const size_t i1=static_cast<size_t>(rp);
        const size_t i0=(i1+size-1)%size;
        const size_t i2=(i1+1)%size;
        const size_t i3=(i1+2)%size;
        const double t=rp-static_cast<double>(i1);
        const double p0=b[i0],p1=b[i1],p2=b[i2],p3=b[i3];
        const double y=0.5*((2.0*p1)+(-p0+p2)*t+
            (2.0*p0-5.0*p1+4.0*p2-p3)*t*t+
            (-p0+3.0*p1-3.0*p2+p3)*t*t*t);
        wowWrite[ch]=(wowWrite[ch]+1)%size;
        return y;
    }

    double sampleRate=44100.0;
    int numChannels=2;
    double low[2]{},env[2]{},dc[2]{};
    double hp_z1[2]{},hp_z2[2]{},lp_z1[2]{},lp_z2[2]{};
    double wearLP1[2]{},wearLP2[2]{},headBumpLP[2]{};
    double tubeSag[2]{},tubeTone[2]{},transistorTone[2]{};
    double optoColorLP[2]{},optoColorHF[2]{},optoColorSag[2]{};
    std::vector<double> wowBuffer[2];
    size_t wowWrite[2]{};
    uint32_t randomState=0x4a65727au;
    Motion wowMotion{},flutterMotion{},scrapeMotion{},wearMotion{};
    double wowPhase1=0.0,wowPhase2=0.0,flutterPhase1=0.0,flutterPhase2=0.0,scrapePhase=0.0;
    int slipLeft=0,slipLength=0;
    double slipDepthMs=0.0;
    double smoothWow=0.0,smoothFlutter=0.0,smoothAge=0.0;
    double smoothOpto=0.0,smoothMakeup=1.0,smoothOptoMix=0.0;
    double optoDetectorFast=0.0,optoDetectorSlow=0.0,optoSidechainLow=0.0;
    double optoFastReduction=0.0,optoSlowReduction=0.0,optoReduction=0.0,optoHistory=0.0;
    double hissLP[2]{},crackle[2]{},dropout[2]{},dropoutTarget[2]{},microDrop[2]{},thump[2]{};
    int dropoutLeft[2]{},microDropLeft[2]{};
};

}
