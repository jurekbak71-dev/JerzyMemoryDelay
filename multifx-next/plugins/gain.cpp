#include "jfx.h"
#include <new>
namespace {
struct State {
    uint32_t max;
};
void *create(double, uint32_t max, const char *) { return new (std::nothrow) State{max}; }
void destroy(void *p) { delete static_cast<State *>(p); }
void process(void *p, const JfxBlock *b) {
    if (b->frames > static_cast<State *>(p)->max)
        return;
    for (unsigned c = 0; c < 2; ++c)
        for (uint32_t i = 0; i < b->frames; ++i)
            b->outputs[c][i] = b->inputs[c][i] * b->parameters[0] + b->parameters[1];
}
uint64_t bytes(void *) { return sizeof(State); }
const JfxParameter params[]{{"gain", "Gain", 0, 2, 1}, {"offset", "DC offset", -1, 1, 0}};
const JfxDescriptor d{JFX_MAGIC,
                      sizeof(JfxDescriptor),
                      3,
                      0,
                      "gain",
                      "1.0.0",
                      "Stereo Gain",
                      2,
                      2,
                      0,
                      2,
                      params,
                      0,
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
