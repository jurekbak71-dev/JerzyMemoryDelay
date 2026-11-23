// PFFFT convolution; original Jerzy WAV/resampling behavior retained. See vendor/pffft/LICENSE.txt.
#include "jfx.h"
#include "pffft_cab.hpp"
namespace {
struct State {
    std::unique_ptr<nextfx::CabinetIR> ir;
    std::array<std::array<float, 64>, 2> dry{};
    unsigned pos = 0;
    uint32_t max = 0;
};
void *create(double sr, uint32_t max, const char *asset) {
    try {
        auto ir = nextfx::CabinetIR::load(asset, sr);
        if (!ir)
            return nullptr;
        auto s = std::make_unique<State>();
        s->ir = std::move(ir);
        s->max = max;
        return s.release();
    } catch (...) {
        return nullptr;
    }
}
void destroy(void *p) { delete static_cast<State *>(p); }
void process(void *p, const JfxBlock *b) {
    auto &s = *static_cast<State *>(p);
    if (b->frames > s.max)
        return;
    float wet = b->parameters[0];
    for (uint32_t i = 0; i < b->frames; ++i) {
        float x[]{b->inputs[0][i], b->inputs[1][i]}, y[2]{};
        s.ir->tick(x, y);
        for (unsigned c = 0; c < 2; ++c) {
            float dry = s.dry[c][s.pos];
            s.dry[c][s.pos] = x[c];
            b->outputs[c][i] = dry * (1 - wet) + y[c] * wet;
        }
        s.pos = (s.pos + 1) % 64;
    }
}
uint64_t bytes(void *p) {
    auto &s = *static_cast<State *>(p);
    return sizeof(State) + s.ir->resource_bytes();
}
const JfxParameter params[]{{"mix", "Dry/Wet (aligned)", 0, 1, 1}};
const JfxDescriptor d{JFX_MAGIC,
                      sizeof(JfxDescriptor),
                      3,
                      0,
                      "cab",
                      "1.1.0",
                      "Partitioned CAB IR",
                      2,
                      2,
                      0,
                      1,
                      params,
                      64,
                      0,
                      create,
                      destroy,
                      process,
                      nullptr,
                      nullptr,
                      nullptr,
                      bytes};
} // namespace
extern "C" JFX_EXPORT const JfxDescriptor *jfx_entry_v3() { return &d; }
