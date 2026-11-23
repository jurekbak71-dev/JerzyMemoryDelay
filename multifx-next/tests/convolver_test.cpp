// Incorrect FFT scaling, overlap-save history, or per-block allocation must fail here.
#include "../../multifx4/ir_cab.h"
#include "pffft_cab.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#ifdef NEXT_ALLOC_GUARD
extern "C" void jfx_test_rt(int);
extern "C" unsigned long long jfx_test_allocations();
extern "C" unsigned long long jfx_test_frees();
extern "C" void jfx_test_reset();
#endif
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> noise(-.2f, .2f);
        double max_error = 0, sum_error = 0;
        uint64_t count = 0;
        for (size_t length : {0u, 1u, 63u, 64u, 65u, 127u, 128u, 129u, 511u, 1024u, 32768u}) {
            std::vector<float> taps(length);
            for (auto &x : taps)
                x = noise(rng) * .1f;
            if (length)
                taps[0] = .5f;
            nextfx::PartitionedConvolver fast;
            fast.prepare(taps);
            mfx4::Conv reference;
            reference.prepare(taps);
            const size_t frames = length + 768;
            std::vector<float> input(frames), actual(frames), expected(frames);
            for (size_t i = 0; i < frames; ++i)
                input[i] = i == 0 ? 1.f : i < 512 ? noise(rng) : 0.f;
#ifdef NEXT_ALLOC_GUARD
            jfx_test_reset();
            jfx_test_rt(1);
#endif
            for (size_t i = 0; i < frames; ++i) {
                actual[i] = fast.tick(input[i]);
                expected[i] = reference.tick(input[i]);
            }
#ifdef NEXT_ALLOC_GUARD
            jfx_test_rt(0);
            check(jfx_test_allocations() == 0 && jfx_test_frees() == 0,
                  "convolver allocates/frees on audio");
#endif
            for (size_t i = 0; i < frames; ++i) {
                double err = std::abs(double(actual[i]) - expected[i]);
                max_error = std::max(max_error, err);
                sum_error += err * err;
                ++count;
                check(std::isfinite(actual[i]) && err < 2e-5, "PFFFT differs from reference");
                if (length <= 129) {
                    double direct = 0;
                    if (i >= 64)
                        for (size_t k = 0; k < length && k <= i - 64; ++k)
                            direct += double(taps[k]) * input[i - 64 - k];
                    check(std::abs(actual[i] - direct) < 2e-5, "direct FIR oracle differs");
                }
            }
            if (length == 32768)
                check(fast.resource_bytes() < 700000, "real spectrum memory not reduced");
        }
        nextfx::PartitionedConvolver impulse;
        impulse.prepare({1.f});
        for (int i = 0; i < 256; ++i)
            check(std::abs(impulse.tick(i == 0 ? 1.f : 0.f) - (i == 64 ? 1.f : 0.f)) < 1e-6,
                  "fixed 64-frame latency");
        for (bool nyquist : {false, true}) {
            nextfx::PartitionedConvolver boundary;
            boundary.prepare({.5f, .25f});
            for (unsigned i = 0; i < 256; ++i) {
                float x = nyquist && (i & 1) ? -1.f : 1.f;
                float y = boundary.tick(x);
                if (i >= 65)
                    check(std::abs(y - (nyquist ? .25f * x : .75f)) < 1e-6,
                          "DC/Nyquist real FFT bins");
            }
        }
        nextfx::PartitionedConvolver sanitized;
        sanitized.prepare({1.f});
        for (unsigned i = 0; i < 256; ++i) {
            float y = sanitized.tick(i == 0 ? NAN : i == 1 ? INFINITY : i == 2 ? 16.f : 0.f);
            check(std::abs(y - (i == 66 ? 8.f : 0.f)) < 1e-6,
                  "sanitization and clipping preserved");
        }
        nextfx::PartitionedConvolver bad;
        bool rejected = false;
        try {
            bad.prepare({NAN});
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "nonfinite taps rejected");
        std::cout << "PASS PFFFT reference/direct FIR and boundary lengths; max_abs=" << max_error
                  << " rms=" << std::sqrt(sum_error / count) << " samples=" << count << '\n';
        std::cout << "SIMD=" << pffft_simd_arch() << " lanes=" << pffft_simd_size() << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
