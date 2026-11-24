#include "jfx.h"
#include <array>
#include <cmath>
#include <new>
namespace {
struct State {
  double rate, phase = 0;
  uint32_t max;
  std::array<float, 2048> wave{};
};
void *create(double sr, uint32_t max, const char *) {
  auto *s = new (std::nothrow) State;
  if (!s)
    return nullptr;
  s->rate = sr;
  s->max = max;
  for (unsigned i = 0; i < s->wave.size(); i++)
    s->wave[i] = float(std::sin(6.283185307179586 * i / s->wave.size()));
  return s;
}
void destroy(void *p) { delete static_cast<State *>(p); }
void process(void *p, const JfxBlock *b) {
  auto &s = *static_cast<State *>(p);
  if (b->frames > s.max)
    return;
  double step = b->parameters[0] * s.wave.size() / s.rate;
  for (uint32_t i = 0; i < b->frames; i++) {
    auto index = uint32_t(s.phase);
    float frac = float(s.phase - index);
    float sample = (s.wave[index] * (1 - frac) +
                    s.wave[(index + 1) % s.wave.size()] * frac) *
                   b->parameters[1];
    b->outputs[0][i] = b->outputs[1][i] = sample;
    s.phase += step;
    if (s.phase >= s.wave.size())
      s.phase -= s.wave.size();
  }
}
uint64_t bytes(void *) { return sizeof(State); }
JfxParameter params[]{{"frequency", "Frequency", 20, 2000, 110},
                      {"gain", "Gain", 0, .25f, .05f}};
JfxDescriptor d{JFX_MAGIC,
                sizeof(JfxDescriptor),
                3,
                0,
                "generator",
                "1.0.0",
                "Wavetable tone generator",
                0,
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
