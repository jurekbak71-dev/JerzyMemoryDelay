#pragma once
#include "pffft_cab.hpp"
namespace nextfx {
class DualCab {
 struct Filter {float low=0,high=0; float tick(float x,float a,float b) noexcept {low+=(x-low)*a;high+=(low-high)*b;return low-high;}};
 std::array<std::unique_ptr<CabinetIR>,2> ir_;
 std::array<std::array<Filter,2>,2> filters_{};
 std::array<std::array<float,64>,2> dry_{};
 unsigned pos_=0; uint32_t max_; double sr_; bool aligned_=true;
 std::array<float,2> weights_{};
 float a_=0,b_=0,lastLow_=-1,lastHigh_=-1;
 std::array<unsigned,2> prime_{}; std::array<bool,2> active_{};
 public:
 DualCab(double sr,uint32_t max,const std::string& asset);
 void process(uint32_t frames,const float* const* in,float* const* out,const float* parameters) noexcept;
 uint64_t resource_bytes() const noexcept;
};
}
