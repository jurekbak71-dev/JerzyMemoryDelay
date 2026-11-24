#include "jfx.h"
#include <array>
#include <cmath>
#include <new>
namespace {
constexpr unsigned tableSize = 2048;
struct Voice {
    double phase = 0, increment = 0;
    float velocity = 0, volume = 1;
    int note = -1, program = 0;
};
struct State {
    uint32_t max;
    std::array<float, tableSize + 1> sine{};
    std::array<double, 128> increment{};
    std::array<Voice, 16> voices{};
};
void *create(double sr, uint32_t frames, const char *) {
    auto *s = new (std::nothrow) State;
    if (!s)
        return nullptr;
    s->max = frames;
    for (unsigned i = 0; i <= tableSize; ++i)
        s->sine[i] = float(std::sin(6.283185307179586 * i / tableSize));
    for (unsigned i = 0; i < 128; ++i)
        s->increment[i] = 440. * std::pow(2., (double(i) - 69) / 12.) / sr;
    return s;
}
void destroy(void *p) { delete static_cast<State *>(p); }
void event(State &s, const JfxMidi &e) {
    if (e.size < 2)
        return;
    auto &v = s.voices[e.data[0] & 15];
    auto type = e.data[0] & 0xf0;
    unsigned a = e.data[1] & 127, b = e.data[2] & 127;
    if (type == 0xc0) {
        v.program = int(a);
        return;
    }
    if (e.size < 3)
        return;
    if (type == 0x90 && b) {
        v.note = int(a);
        v.velocity = float(b) / 127;
        v.increment = s.increment[a];
        v.phase = 0;
    } else if ((type == 0x80 || (type == 0x90 && !b)) && v.note == int(a)) {
        v.note = -1;
        v.velocity = 0;
    } else if (type == 0xb0) {
        if (a == 7)
            v.volume = float(b) / 127;
        else if (a == 120 || a == 123) {
            v.note = -1;
            v.velocity = 0;
        }
    }
}
void process(void *p, const JfxBlock *b) {
    auto &s = *static_cast<State *>(p);
    if (b->frames > s.max)
        return;
    uint32_t e = 0;
    for (uint32_t i = 0; i < b->frames; ++i) {
        while (e < b->event_count && b->events[e].frame <= i)
            event(s, b->events[e++]);
        float y = 0;
        for (auto &v : s.voices)
            if (v.note >= 0) {
                float x;
                if (v.program & 1)
                    x = float(1. - 4. * std::abs(v.phase - .5));
                else {
                    double t = v.phase * tableSize;
                    unsigned k = unsigned(t);
                    x = s.sine[k] + float(t - k) * (s.sine[k + 1] - s.sine[k]);
                }
                y += x * v.velocity * v.volume * b->parameters[0];
                v.phase += v.increment;
                v.phase -= std::floor(v.phase);
            }
        b->outputs[0][i] = y;
        b->outputs[1][i] = y;
    }
}
uint64_t bytes(void *) { return sizeof(State); }
const JfxParameter params[]{{"level", "Level", 0, 1, .15f}};
const JfxDescriptor d{JFX_MAGIC,
                      sizeof(JfxDescriptor),
                      3,
                      0,
                      "synth",
                      "1.0.0",
                      "MIDI Wavetable Instrument",
                      0,
                      2,
                      JFX_MIDI_INPUT,
                      1,
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
