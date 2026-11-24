// Offline algorithm timing, not JACK latency or a Pi4 performance claim.
#include "../../multifx4/ir_cab.h"
#include "pffft_cab.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
struct Result {
    double median, p95, maximum;
    uint64_t known_bytes;
    double checksum;
};
template <class Conv> Result run(const std::vector<float> &taps, const std::vector<float> &input) {
    std::vector<uint64_t> times;
    times.reserve(512 * 5);
    double checksum = 0;
    uint64_t bytes = 0;
    for (unsigned trial = 0; trial < 5; ++trial) {
        std::array<Conv, 2> cv;
        for (auto &c : cv)
            c.prepare(taps);
        for (unsigned i = 0; i < 4096; ++i)
            for (auto &c : cv)
                checksum += c.tick(input[i % input.size()]);
        for (unsigned block = 0; block < 512; ++block) {
            auto before = std::chrono::steady_clock::now();
            for (unsigned n = 0; n < 64; ++n)
                for (unsigned c = 0; c < 2; ++c)
                    checksum += cv[c].tick(input[block * 64 + n] * (c ? -.7f : 1.f));
            auto after = std::chrono::steady_clock::now();
            times.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
        }
        if constexpr (requires { cv[0].resource_bytes(); })
            bytes = 2 * cv[0].resource_bytes();
        else
            bytes = 2 * (sizeof(Conv) + (cv[0].H.capacity() + cv[0].history.capacity()) *
                                            sizeof(std::array<mfx4::C, mfx4::IR_FFT>));
    }
    std::sort(times.begin(), times.end());
    return {double(times[times.size() / 2]), double(times[times.size() * 95 / 100]),
            double(times.back()), bytes, checksum};
}
int main() {
    std::mt19937 rng(17);
    std::uniform_real_distribution<float> noise(-.2f, .2f);
    std::vector<float> input(32768);
    for (auto &v : input)
        v = noise(rng);
    std::cout << "{\"platform\":\"offline\",\"simd\":\"" << pffft_simd_arch()
              << "\",\"simd_lanes\":" << pffft_simd_size()
              << ",\"frames_per_block\":64,\"channels\":2,\"trials\":5,\"blocks_per_trial\":512,"
                 "\"cases\":[";
    bool first = true;
    for (unsigned length : {128u, 1024u, 8192u, 32768u}) {
        std::vector<float> taps(length);
        for (auto &v : taps)
            v = noise(rng) * .05f;
        taps[0] = .5f;
        auto old = run<mfx4::Conv>(taps, input);
        auto fast = run<nextfx::PartitionedConvolver>(taps, input);
        if (!first)
            std::cout << ',';
        first = false;
        std::cout << "{\"taps\":" << length << ",\"reference_median_ns\":" << old.median
                  << ",\"pffft_median_ns\":" << fast.median << ",\"reference_p95_ns\":" << old.p95
                  << ",\"pffft_p95_ns\":" << fast.p95 << ",\"reference_max_ns\":" << old.maximum
                  << ",\"pffft_max_ns\":" << fast.maximum
                  << ",\"reference_known_bytes\":" << old.known_bytes
                  << ",\"pffft_known_bytes\":" << fast.known_bytes
                  << ",\"median_ratio\":" << old.median / fast.median
                  << ",\"checksum_difference\":" << fast.checksum - old.checksum << '}';
    }
    std::cout << "]}\n";
}
