#include "engine.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace nextfx;
void check(bool v, const char *why) {
  if (!v)
    throw std::runtime_error(why);
}
int main(int argc, char **argv) {
  try {
    check(argc == 2, "plugins");
    Engine e(44100, 64);
    GraphSpec g;
    SlotSpec s;
    s.id = 1;
    s.instance = Instance::create(
        Library::open(std::string(argv[1]) + "/gain/plugin.so"), 44100, 64, "");
    s.parameters = {0, 0};
    g.slots = {s};
    s.instance.reset();
    g.edges = {{0, 1}, {1, -1}};
    float in[64], out[64]{}, right[64]{};
    for (auto &x : in)
      x = 1;
    BlockView b{64, {in, in}, {out, right}, nullptr, 0, {120, 0, 1, 1}, false};
    check(e.publish(g), "publish");
    e.process(b);
    check(e.collect(), "initial ack");
    check(out[0] == 0, "gain processes");
    g.slots[0].mode = Mode::Bypass;
    check(e.publish(g), "bypass publish");
    e.process(b);
    check(e.collect(), "bypass ack");
    check(out[0] < .05f && out[63] > .95f,
          "bypass must ramp instead of instantaneous switch");
    for (unsigned i = 1; i < 64; i++)
      check(std::abs(out[i] - out[i - 1]) < .02f, "bounded mode ramp step");
    auto calls = e.diagnostics().slot_calls[0];
    e.process(b);
    check(e.diagnostics().slot_calls[0] == calls,
          "bypass DSP stopped after ramp");
    g.slots[0].mode = Mode::Active;
    check(e.publish(g), "activate");
    e.process(b);
    e.collect();
    check(out[0] > .95f && out[63] < .05f, "activate ramp");
    std::weak_ptr<Instance> old = g.slots[0].instance;
    g.slots[0].mode = Mode::Unloaded;
    g.slots[0].instance.reset();
    check(e.publish(g), "unload");
    b.frames = 16;
    e.process(b);
    check(!e.collect() && !old.expired(),
          "unload retains old DSP through ramp");
    for (int k = 0; k < 3; k++)
      e.process(b);
    check(e.collect() && old.expired(),
          "controller retires DSP after last ramp use");
    e.process(b);
    check(out[0] == 1, "unloaded passthrough");
    GraphSpec instrument;
    SlotSpec voice;
    voice.id = 2;
    voice.inputs = 0;
    voice.midi = true;
    voice.instance = Instance::create(
        Library::open(std::string(argv[1]) + "/synth/plugin.so"), 44100, 64,
        "");
    for (unsigned i = 0; i < voice.instance->descriptor().parameter_count; i++)
      voice.parameters.push_back(
          voice.instance->descriptor().parameters[i].initial);
    instrument.slots = {voice};
    instrument.edges = {{2, -1}};
    instrument.midi_routes = {{0, 2, 65535}};
    check(e.publish(instrument), "instrument publish");
    JfxMidi note{0, 3, {0x90, 69, 100}};
    b.frames = 64;
    b.events = &note;
    b.event_count = 1;
    e.process(b);
    e.collect();
    b.events = nullptr;
    b.event_count = 0;
    instrument.slots[0].mode = Mode::Bypass;
    check(e.publish(instrument), "mute instrument");
    e.process(b);
    e.collect();
    check(std::abs(out[0]) > .01f && std::abs(out[63]) < .001f,
          "instrument fades before panic, rather than cutting notes at block "
          "start");
    std::cout << "PASS mode ramps, zero post-bypass calls, deferred unload "
                 "over small blocks\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
