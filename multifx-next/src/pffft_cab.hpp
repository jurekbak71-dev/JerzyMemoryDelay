#pragma once
#include "pffft.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace nextfx {
// 64-frame overlap-save partitions, 128-point real FFT. All setup/allocation is in prepare.
class PartitionedConvolver {
    struct alignas(64) Spectrum {
        std::array<float, 128> data{};
    };
    std::unique_ptr<PFFFT_Setup, void (*)(PFFFT_Setup *)> setup_{nullptr, pffft_destroy_setup};
    std::vector<Spectrum> impulse_, history_;
    Spectrum frame_, mixed_, work_;
    std::array<float, 64> input_{}, output_{}, previous_{};
    size_t head_ = 0;
    unsigned position_ = 0;

  public:
    void prepare(const std::vector<float> &taps);
    float tick(float input) noexcept;
    uint64_t resource_bytes() const noexcept;
};
class CabinetIR {
    std::array<PartitionedConvolver, 2> channels_;

  public:
    static std::unique_ptr<CabinetIR> load(const std::string &path, double sample_rate);
    void tick(const float input[2], float output[2]) noexcept;
    uint64_t resource_bytes() const noexcept;
};
} // namespace nextfx
