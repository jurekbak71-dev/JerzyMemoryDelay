#include "audio_audit.hpp"
#include "engine.hpp"
#include <iostream>
#include <stdexcept>
using namespace nextfx;
void check(bool v, const char *w) {
  if (!v)
    throw std::runtime_error(w);
}
int main(int argc, char **argv) {
  try {
    AudioAudit audit;
    if (argc != 2)
      return 2;
    Engine e(44100, 64);
    GraphSpec g;
    SlotSpec s;
    s.id = 1;
    s.instance = Instance::create(Library::open(argv[1]), 44100, 64, "");
    g.slots = {s};
    s.instance.reset();
    g.edges = {{0, 1}, {1, -1}};
    float x[64]{1}, y[64]{}, z[64]{};
    BlockView b{64, {x, x}, {y, z}, nullptr, 0, {120, 0, 1, 1}, false};
    check(e.publish(g), "publish");
    audited_audio([&] { e.process(b); });
    e.collect();
    x[0] = 0;
    g.slots[0].mode = Mode::Unloaded;
    g.slots[0].tail_frames = 256;
    std::weak_ptr<Instance> weak = g.slots[0].instance;
    g.slots[0].instance.reset();
    check(e.publish(g), "spill publish");
    audited_audio([&] { e.process(b); });
    check(!e.collect() && !weak.expired() && y[20] > .1f,
          "tail real audio and lifetime");
    audited_audio([&] { e.process(b); });
    audited_audio([&] { e.process(b); });
    check(!e.collect(), "wait finite tail");
    audited_audio([&] { e.process(b); });
    check(e.collect() && weak.expired(), "tail retired off audio");
    audited_audio([&] { e.process(b); });
    for (float v : y)
      check(v == 0, "tail DSP/output stopped");
    audit.clean();
    std::cout << "PASS audible zero-input spill, bounded lifetime, unload "
                 "after tail\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
