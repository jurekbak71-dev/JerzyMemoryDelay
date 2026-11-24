#include "audio_audit.hpp"
#include "session.hpp"
#include <cmath>
#include <fstream>
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
    if (argc != 2)
      return 2;
    Engine e(44100, 64);
    Session s(e);
    float x[64], y[64]{}, z[64]{};
    for (float &v : x)
      v = 1;
    BlockView b{64, {x, x}, {y, z}, nullptr, 0, {120, 0, 1, 0}, false};
    auto step = [&] {
      audited_audio([&] { e.process(b); });
      s.collect();
    };
    check(s.command(std::string("ADD ") + argv[1] + "/gain/plugin.so") ==
              "OK 1",
          "add gain");
    step();
    check(s.command("SET 1 gain 0.25") == "OK", "set");
    step();
    auto preset = s.snapshot();
    preset["master"] = .5;
    s.restore(preset);
    step();
    check(std::abs(y[0] - .125f) < 1e-6, "master must be restored and applied");
    preset = s.snapshot();
    preset["master"] = 1.;
    preset["midi_bindings"] = {{{"channel", 1},
                                {"cc", 7},
                                {"target", 1},
                                {"param", 0},
                                {"min", 0.},
                                {"max", 2.}}};
    s.restore(preset);
    step();
    JfxMidi cc{16, 3, {0xb0, 7, 127}};
    b.events = &cc;
    b.event_count = 1;
    step();
    check(std::abs(y[15] - .25f) < 1e-6 && std::abs(y[16] - 2.f) < 1e-6,
          "CC parameter sample offset");
    b.events = nullptr;
    b.event_count = 0;
    check(s.snapshot()["slots"][0]["parameters"]["gain"] == 2.f,
          "preset observes MIDI parameter");
    check(s.command("SET 1 offset 0.1") == "OK", "other parameter");
    step();
    check(std::abs(y[0] - 2.1f) < 1e-6,
          "unrelated graph edit preserves MIDI state");
    check(s.command("SET 1 gain 0.25") == "OK", "explicit reset");
    step();
    check(std::abs(y[0] - .35f) < 1e-6,
          "explicit write restores old declared value despite MIDI state");
    preset = s.snapshot();
    preset["midi_bindings"].push_back({{"channel", 1},
                                       {"cc", 8},
                                       {"target", 0},
                                       {"param", 0},
                                       {"min", 0.},
                                       {"max", 1.}});
    s.restore(preset);
    step();
    cc = {32, 3, {0xb0, 8, 0}};
    b.events = &cc;
    b.event_count = 1;
    step();
    check(y[31] > .3f && y[32] == 0, "sample accurate master CC");
    b.events = nullptr;
    b.event_count = 0;
    check(s.snapshot()["master"] == 0, "master saved after MIDI");
    auto bad = s.snapshot();
    bad["midi_bindings"][0]["target"] = 99;
    bool failed = false;
    try {
      s.restore(bad);
    } catch (...) {
      failed = true;
    }
    check(failed, "invalid binding rejected before publication");
    step();
    check(y[0] == 0, "invalid binding rollback preserved");
    auto asset = std::string(argv[1]) + "/cc-empty-cab.json";
    {
      std::ofstream file(asset);
      file << "{}";
    }
    Engine second(44100, 64);
    Session cabinet(second);
    float cy[64]{}, cz[64]{};
    BlockView cb{64, {x, x}, {cy, cz}, nullptr, 0, {120, 0, 1, 0}, false};
    auto settle = [&] {
      for (int i = 0; i < 10; i++) {
        audited_audio([&] { second.process(cb); });
        cabinet.collect();
        if (!second.busy())
          return;
      }
      throw std::runtime_error("CAB transition failed to settle");
    };
    check(cabinet.command(std::string("ADD ") + argv[1] +
                          "/cab_dual/plugin.so " + asset) == "OK 1",
          "dual CAB for unloaded parameter test");
    settle();
    check(cabinet.command("SET 1 mix 0.25") == "OK", "mix initial");
    settle();
    auto bound = cabinet.snapshot();
    bound["midi_bindings"] = {{{"channel", 1},
                               {"cc", 7},
                               {"target", 1},
                               {"param", 0},
                               {"min", 0.},
                               {"max", 1.}}};
    cabinet.restore(bound);
    settle();
    check(cabinet.command("MODE 1 UNLOADED") == "OK", "unload CAB");
    settle();
    auto saved = cabinet.snapshot();
    cabinet.restore(saved);
    settle();
    JfxMidi cab_cc{0, 3, {0xb0, 7, 127}};
    cb.events = &cab_cc;
    cb.event_count = 1;
    settle();
    auto state = cabinet.snapshot();
    check(state["slots"][0]["parameters"]["mix"] == 1.f &&
              state["slots"][0]["parameters"]["blend"] == 0.f,
          "unloaded preset must preserve descriptor indices for MIDI");
    cb.events = nullptr;
    cb.event_count = 0;
    check(cabinet.command("MODE 1 ACTIVE") == "OK",
          "reactivate CAB with binding");
    settle();
    check(cabinet.snapshot()["slots"][0]["parameters"]["mix"] == 1.f,
          "correct CC parameter survives reactivation");
    audit.clean();
    std::cout << "PASS master, immutable CC bindings, sample timing, state "
                 "persistence, transactional errors\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
