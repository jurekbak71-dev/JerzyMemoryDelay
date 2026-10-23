#include "dsp.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

int main(){
    using namespace JerzyMultiFX;
    assert(Engine::validOrder({{0,1,2,3}}));
    assert(Engine::validOrder({{3,2,1,0}}));
    assert(!Engine::validOrder({{0,1,1,3}}));
    assert(!Engine::validOrder({{0,1,2,9}}));

    Engine e;
    e.prepare(44100.);
    State state;
    constexpr int rate=44100;
    // Exercise all four DSP blocks and note long delay tail; assert finite audio.
    for(int n=0;n<rate;++n) {
        double out[2]{};
        const double input=(n<rate/10)?std::sin(n*440.*2.*3.14159265358979323846/rate)*.25:0.;
        e.tick(input,out,state);
        assert(std::isfinite(out[0])&&std::isfinite(out[1]));
        assert(std::abs(out[0])<=1.&&std::abs(out[1])<=1.);
    }
    // Reorder / bypass while running; should remain bounded and not crash.
    state.order={{3,0,2,1}};
    state.enabled={{false,true,false,true}};
    for(int n=0;n<rate/5;++n){
        double out[2]{};
        e.tick(.2,out,state);
        assert(std::isfinite(out[0])&&std::isfinite(out[1]));
    }
    for(int i=0;i<Params;++i){
        state.values[i]=0.;
        double out[2]{};
        e.tick(0.,out,state);
        assert(std::isfinite(out[0])&&std::isfinite(out[1]));
        state.values[i]=1.;
        e.tick(.1,out,state);
        assert(std::isfinite(out[0])&&std::isfinite(out[1]));
    }
    std::cout<<"MultiFX DSP finite, permutations and bypass OK\n";
}
