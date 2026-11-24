// The ABI2 bridge must generate exactly the same samples as the old module.
#include "plugin.hpp"
#define JfxParameter LegacyParameter
#define JfxDescriptor LegacyDescriptor
#define JfxEntryPoint LegacyEntryPoint
#include "../../multifx2/api.h"
#undef JfxParameter
#undef JfxDescriptor
#undef JfxEntryPoint
#include <cmath>
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            return 2;
        std::string dir = argv[1], path = dir + "/legacy_tremolo.so";
        void *lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib)
            throw std::runtime_error("legacy fixture unavailable");
        auto get = reinterpret_cast<LegacyEntryPoint>(dlsym(lib, "jfx_plugin"));
        const auto *old = get();
        void *instance = old->create(44100);
        auto bridge = nextfx::Instance::create(nextfx::Library::open(dir + "/legacy/plugin.so"),
                                               44100, 64, path);
        float params[48]{};
        for (unsigned i = 0; i < old->parameter_count; ++i)
            params[i] = old->parameters[i].default_value;
        float x[64], y[64], ol[64]{}, orr[64]{};
        for (unsigned b = 0; b < 40; ++b) {
            for (unsigned n = 0; n < 64; ++n) {
                x[n] = float(std::sin((b * 64 + n) * .017));
                y[n] = x[n] * -.3f;
            }
            const float *in[]{x, y};
            float *out[]{ol, orr};
            JfxBlock block{64, in, out, params, nullptr, 0, {120, 0, 1, 1}};
            bridge->process(block);
            for (unsigned n = 0; n < 64; ++n) {
                float sample[]{x[n], y[n]}, expected[2]{};
                old->process(instance, sample, expected, params, 120);
                if (std::abs(ol[n] - expected[0]) > 1e-7 || std::abs(orr[n] - expected[1]) > 1e-7)
                    throw std::runtime_error("ABI2 bridge audio differs");
            }
        }
        old->destroy(instance);
        dlclose(lib);
        std::cout << "PASS ABI2 migration bridge matches old stereo DSP sample for sample\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
