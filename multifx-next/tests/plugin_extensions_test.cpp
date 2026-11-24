#include "audio_audit.hpp"
#include "session.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace nextfx;
void check(bool v, const char *s) {
  if (!v)
    throw std::runtime_error(s);
}
int main(int argc, char **argv) {
  try {
    AudioAudit audit;
    if (argc != 4)
      return 2;
    std::string dir = argv[1];
    auto lib = Library::open(dir + "/generator/plugin.so");
    auto generator = Instance::create(lib, 44100, 64, "");
    float y[64]{}, z[64]{};
    float parameters[]{110, .05};
    JfxBlock block{64,      nullptr, nullptr,       parameters,
                   nullptr, 0,       {120, 0, 1, 1}};
    float *out[]{y, z};
    block.outputs = out;
    audited_audio([&] { generator->process(block); });
    double energy = 0;
    for (float v : y) {
      check(std::isfinite(v), "finite generator");
      energy += std::abs(v);
    }
    check(energy > .1, "actual generator without audio input/MIDI");
    bool rejected = false;
    try {
      auto bad = Instance::create(Library::open(argv[2]), 44100, 64, "");
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "factory error rejected");
    Engine e(44100, 64);
    Session s(e);
    BlockView view{64, {nullptr, nullptr}, {y, z}, nullptr,
                   0,  {120, 0, 1, 0},     false};
    check(s.command(std::string("ADD ") + argv[3]) == "OK 1",
          "transport fixture load");
    audited_audio([&] { e.process(view); });
    s.collect();
    auto preset = s.snapshot();
    preset["midi_bindings"] = {{{"channel", 1},
                                {"cc", 10},
                                {"target", -1},
                                {"param", 0},
                                {"min", 20.},
                                {"max", 300.}}};
    s.restore(preset);
    audited_audio([&] { e.process(view); });
    s.collect();
    JfxMidi cc{31, 3, {0xb0, 10, 127}};
    view.events = &cc;
    view.event_count = 1;
    audited_audio([&] { e.process(view); });
    s.collect();
    check(std::abs(y[30] - .4f) < 1e-6 && y[31] == 1,
          "BPM applied at exact MIDI frame");
    check(s.snapshot()["bpm"] == 300, "MIDI tempo saved");
    audit.clean();
    std::cout << "PASS generator, factory failure, sample accurate tempo and "
                 "preset\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
