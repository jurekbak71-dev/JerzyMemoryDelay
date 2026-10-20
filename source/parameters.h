#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace Jerzy {
enum Param { Blend, Level, Feedback, ModRate, Depth, Delay, Model, Wave, Bypass, Boost, Squelch, ModEnable, Count };
inline constexpr unsigned OverloadMeter=100;
inline constexpr std::array<double,Count> defaults{{.4,.5,.35,.15,.18,.55,.6,0.,0.,0.,.35,1.}};
inline constexpr std::array<const char*,6> modelNames{{"EH7500 / SAD", "Issue A / SAD", "Squelch / SAD", "Deluxe / MN", "Boy / BL", "Toy / BL"}};
struct Profile { double maxMs, bandwidth, drive, feedbackMax, gate, modulation; bool compander; int stages; };
// Sound-design profiles, not measured component models. See docs/MODELLING.md.
inline constexpr std::array<Profile,6> profiles{{
    {300,3000,1.65,1.16,0,0,false,3072},
    {300,2600,1.8,1.16,.0006,0,false,3072},
    {400,3100,1.55,1.16,.0015,0,true,3072},
    {550,4800,1.05,1.16,0,1,true,8192},
    {550,3200,1.7,1.16,0,1.35,true,4096},
    {550,2800,1.9,1.16,0,.65,true,4096}
}};
inline double normalized(double x) { return std::isfinite(x)?std::clamp(x,0.,1.):0.; }
struct Parameters {
    std::array<double,Count> value=defaults;
    void set(int id,double x) { if(id>=0&&id<Count&&std::isfinite(x))value[id]=normalized(x); }
    int model()const { return std::clamp(int(std::lround(value[Model]*5)),0,5); }
};
inline double delayMs(double n) { return 20.+530.*normalized(n); }
}
