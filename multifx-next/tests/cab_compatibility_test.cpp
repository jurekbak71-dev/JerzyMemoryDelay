#include "../../multifx4/ir_cab.h"
#include "plugin.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace nextfx;
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            return 2;
        std::string dir = argv[1];
        std::mt19937 rng(153);
        std::uniform_real_distribution<float> noise(-.2f, .2f);
        double peak = 0;
        uint64_t samples = 0;
        for (unsigned sourceRate : {22050u, 44100u, 48000u, 96000u}) {
            std::string wav = dir + "/compatibility.wav";
            std::ofstream f(wav, std::ios::binary);
            auto w16 = [&](unsigned x) {
                f.put(char(x));
                f.put(char(x >> 8));
            };
            auto w32 = [&](uint32_t x) {
                w16(x);
                w16(x >> 16);
            };
            f.write("RIFF", 4);
            w32(36 + 1025 * 8);
            f.write("WAVEfmt ", 8);
            w32(16);
            w16(3);
            w16(2);
            w32(sourceRate);
            w32(sourceRate * 8);
            w16(8);
            w16(32);
            f.write("data", 4);
            w32(1025 * 8);
            for (unsigned i = 0; i < 1025; ++i)
                for (unsigned c = 0; c < 2; ++c) {
                    float v = i ? noise(rng) * .1f : (c ? .3f : .5f);
                    uint32_t bits;
                    std::memcpy(&bits, &v, 4);
                    w32(bits);
                }
            f.close();
            for (double targetRate : {8000., 44100., 48000., 192000.}) {
                std::string error;
                auto reference = mfx4::CabinetIR::load(wav, targetRate, error);
                if (!reference)
                    throw std::runtime_error(error);
                auto plugin =
                    Instance::create(Library::open(dir + "/cab/plugin.so"), targetRate, 256, wav);
                unsigned done = 0, chunk = 0;
                float mix[]{1};
                while (done < 10000) {
                    unsigned size = std::min(
                        std::array<unsigned, 8>{1, 31, 63, 64, 65, 127, 128, 256}[chunk++ % 8],
                        10000 - done);
                    float l[256]{}, r[256]{}, ol[256]{}, orr[256]{};
                    for (unsigned i = 0; i < size; ++i) {
                        l[i] = done + i < 512 ? noise(rng) : 0;
                        r[i] = done + i < 512 ? noise(rng) : 0;
                    }
                    const float *in[]{l, r};
                    float *out[]{ol, orr};
                    JfxBlock block{size, in, out, mix, nullptr, 0, {120, 0, 0, 1}};
                    plugin->process(block);
                    for (unsigned i = 0; i < size; ++i) {
                        float x[]{l[i], r[i]}, y[2]{};
                        reference->tick(x, y);
                        for (unsigned c = 0; c < 2; ++c) {
                            double e = std::abs(double(out[c][i]) - y[c]);
                            peak = std::max(peak, e);
                            ++samples;
                            if (!std::isfinite(out[c][i]) || e > 2e-5)
                                throw std::runtime_error(
                                    "CAB stereo/resampling/chunk compatibility failed");
                        }
                    }
                    done += size;
                }
            }
        }
        std::cout << "PASS CAB stereo float WAV, 16 rate pairs and irregular frame counts; max_abs="
                  << peak << " samples=" << samples << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
