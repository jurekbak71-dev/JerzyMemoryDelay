#include "pffft_cab.hpp"
#include "../../multifx4/ir_cab.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nextfx {
void PartitionedConvolver::prepare(const std::vector<float> &taps) {
    if (taps.size() > 32768)
        throw std::runtime_error("IR exceeds 32768 taps");
    for (float tap : taps)
        if (!std::isfinite(tap))
            throw std::runtime_error("nonfinite IR tap");
    std::unique_ptr<PFFFT_Setup, void (*)(PFFFT_Setup *)> setup(pffft_new_setup(128, PFFFT_REAL),
                                                                pffft_destroy_setup);
    if (!setup)
        throw std::bad_alloc();
    size_t parts = std::max(size_t(1), (taps.size() + 63) / 64);
    std::vector<Spectrum> impulse(parts), history(parts);
    Spectrum time{}, work{};
    for (size_t part = 0; part < parts; ++part) {
        time.data.fill(0);
        for (unsigned i = 0; i < 64; ++i) {
            size_t idx = part * 64 + i;
            if (idx < taps.size())
                time.data[i] = taps[idx];
        }
        pffft_transform(setup.get(), time.data.data(), impulse[part].data.data(), work.data.data(),
                        PFFFT_FORWARD);
    }
    setup_ = std::move(setup);
    impulse_ = std::move(impulse);
    history_ = std::move(history);
    input_.fill(0);
    output_.fill(0);
    previous_.fill(0);
    head_ = 0;
    position_ = 0;
}
float PartitionedConvolver::tick(float input) noexcept {
    if (!setup_)
        return 0;
    float result = output_[position_];
    input_[position_] = std::isfinite(input) ? input : 0;
    if (++position_ == 64) {
        position_ = 0;
        for (unsigned i = 0; i < 64; ++i) {
            frame_.data[i] = previous_[i];
            frame_.data[i + 64] = input_[i];
        }
        pffft_transform(setup_.get(), frame_.data.data(), history_[head_].data.data(),
                        work_.data.data(), PFFFT_FORWARD);
        mixed_.data.fill(0);
        size_t history = head_;
        for (size_t part = 0; part < impulse_.size(); ++part) {
            pffft_zconvolve_accumulate(setup_.get(), history_[history].data.data(),
                                       impulse_[part].data.data(), mixed_.data.data(), 1.f / 128.f);
            history = history ? history - 1 : history_.size() - 1;
        }
        pffft_transform(setup_.get(), mixed_.data.data(), frame_.data.data(), work_.data.data(),
                        PFFFT_BACKWARD);
        for (unsigned i = 0; i < 64; ++i) {
            output_[i] = frame_.data[i + 64];
            previous_[i] = input_[i];
        }
        head_ = (head_ + 1) % history_.size();
    }
    return std::isfinite(result) ? std::clamp(result, -8.f, 8.f) : 0.f;
}
uint64_t PartitionedConvolver::resource_bytes() const noexcept {
    return sizeof(*this) + (impulse_.capacity() + history_.capacity()) * sizeof(Spectrum);
}
std::unique_ptr<CabinetIR> CabinetIR::load(const std::string &path, double sr) {
    if (!std::isfinite(sr) || sr < 8000 || sr > 192000)
        throw std::runtime_error("invalid IR sample rate");
    mfx4::Wav wav;
    std::string error;
    if (!mfx4::readWav(path, wav, error))
        throw std::runtime_error(error);
    auto out = std::make_unique<CabinetIR>();
    for (unsigned c = 0; c < 2; ++c) {
        const auto &src = wav.samples[c];
        const size_t count = std::min<size_t>(
            mfx4::IR_MAX_FRAMES,
            std::max<size_t>(1, size_t(std::ceil(src.size() * sr / wav.sampleRate))));
        std::vector<float> taps;
        taps.reserve(count);
        for (size_t n = 0; n < count; ++n) {
            double p = double(n) * double(wav.sampleRate) / sr;
            size_t i = std::min(src.size() - 1, size_t(p)), j = std::min(src.size() - 1, i + 1);
            double frac = std::clamp(p - double(i), 0., 1.);
            taps.push_back(float(src[i] * (1. - frac) + src[j] * frac));
        }
        out->channels_[c].prepare(taps);
    }
    return out;
}
void CabinetIR::tick(const float input[2], float output[2]) noexcept {
    for (unsigned c = 0; c < 2; ++c)
        output[c] = channels_[c].tick(input[c]);
}
uint64_t CabinetIR::resource_bytes() const noexcept {
    uint64_t bytes = sizeof(*this);
    for (const auto &c : channels_)
        bytes += c.resource_bytes() - sizeof(PartitionedConvolver);
    return bytes;
}
} // namespace nextfx
