#include "dsp.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace Jerzy;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    int cases=0;
    for(double sr:{44100.,48000.,96000.})for(int model=0;model<6;++model){
        Engine e;e.prepare(sr);Parameters p;p.set(Model,model/5.);p.set(Blend,1);p.set(Feedback,0);p.set(Depth,0);p.set(Delay,80./530.);
        int peakAt=0;double peak=0,pre=0;
        for(int i=0;i<int(sr*.7);++i){double in[2]{i==0?.5:0,0},out[2]{};e.tick(in,out,2,p);
            check(std::isfinite(out[0])&&std::abs(out[0])<2,"impulse output invalid");
            check(out[1]==0,"stereo crosstalk");
            if(std::abs(out[0])>peak){peak=std::abs(out[0]);peakAt=i;}
            if(i<int(sr*.099))pre+=std::abs(out[0]);
        }
        // Gated profiles deliberately suppress echoes when the input envelope closes.
        check(peak>1e-9,"missing echo");check(pre<1e-12,"echo arrived early");
        check(peakAt>=int(sr*.1)-2&&peakAt<int(sr*.108),"delay time incorrect");++cases;
    }
    {
        Engine e;e.prepare(48000);Parameters p;p.set(Bypass,1);
        for(int i=0;i<1000;++i){double in[2]{std::sin(i*.1)*.3,-.2},out[2]{};e.tick(in,out,2,p);check(out[0]==in[0]&&out[1]==in[1],"bypass changed signal");}++cases;
    }
    {
        Engine e;e.prepare(48000);Parameters p;p.set(Blend,0);p.set(Level,.5);
        for(int i=0;i<1000;++i){double in[2]{.1,-.3},out[2]{};e.tick(in,out,2,p);check(std::abs(out[0]-.1)<1e-14&&std::abs(out[1]+.3)<1e-14,"dry gain incorrect");}++cases;
    }
    for(int m=0;m<6;++m){
        Engine e;e.prepare(48000);Parameters p;p.set(Model,m/5.);p.set(Feedback,1);p.set(Depth,1);p.set(Level,1);p.set(Blend,1);
        double energy=0;
        for(int i=0;i<144000;++i){if(i%4800==0)p.set(Delay,double((i/4800)%10)/9.);
            double in[2]{i<480?.9*std::sin(i*.2):0,0},out[2]{};e.tick(in,out,2,p);
            check(std::isfinite(out[0])&&std::abs(out[0])<4,"feedback runaway");energy+=std::abs(out[0]);}
        check(energy>1e-3,"feedback produced no output");++cases;
    }
    {
        Engine e;e.prepare(48000);Parameters p;double input[2]{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()},output[2]{};
        e.tick(input,output,2,p);check(std::isfinite(output[0])&&std::isfinite(output[1]),"nonfinite input poisoned engine");
        p.set(Delay,std::numeric_limits<double>::quiet_NaN());check(p.value[Delay]==defaults[Delay],"invalid parameter accepted");++cases;
    }
    {
        Engine e;e.prepare(48000);Parameters p;double input[2]{.5,0},output[2]{};e.tick(input,output,2,p);e.reset();
        input[0]=0;double energy=0;for(int i=0;i<48000;++i){e.tick(input,output,2,p);energy+=std::abs(output[0]);}
        check(energy==0,"reset leaked old audio");++cases;
    }
    std::cout<<cases<<" DSP cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
