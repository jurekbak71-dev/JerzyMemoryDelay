// Test-only real, deliberately expensive, bounded DSP; not a product plugin.
#include "jfx.h"
#include <cmath>
#include <new>
namespace {
void *create(double, uint32_t, const char *) { return new (std::nothrow) double(.2); }
void destroy(void *p) { delete static_cast<double *>(p); }
void process(void *p, const JfxBlock *b) {
    double value = *static_cast<double *>(p);
    for (unsigned i = 0; i < 200000; ++i)
        value = std::sin(value + .01);
    *static_cast<double *>(p) = value;
    for (unsigned c = 0; c < 2; ++c)
        for (uint32_t i = 0; i < b->frames; ++i)
            b->outputs[c][i] = float(value) + b->inputs[c][i] * .5f;
}
const JfxDescriptor d{JFX_MAGIC,
                      sizeof(JfxDescriptor),
                      3,
                      0,
                      "overload",
                      "1.0.0",
                      "Overload test fixture",
                      2,
                      2,
                      0,
                      0,
                      nullptr,
                      0,
                      0,
                      create,
                      destroy,
                      process,
                      nullptr,
                      nullptr,
                      nullptr,
                      nullptr};
} // namespace
extern "C" JFX_EXPORT const JfxDescriptor *jfx_entry_v3() { return &d; }
