#include "audio_audit.hpp"
#include "engine.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace nextfx;
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      return 2;
    AudioAudit audit;
    Engine e(44100, 64);
    GraphSpec g;
    SlotSpec slot;
    slot.id = 1;
    slot.instance = Instance::create(
        Library::open(std::string(argv[1]) + "/gain/plugin.so"), 44100, 64, "");
    slot.parameters = {.5f, .5f};
    g.slots = {slot};
    g.edges = {{0, 1}, {1, -1}};
    g.bindings = {{1, 7, 1, 0, 0, 1}, {1, 7, 1, 1, 0, 1}};
    std::atomic<bool> stop{false};
    std::thread thread([&] {
      float x[64]{}, y[64]{}, z[64]{};
      JfxMidi events[]{{16, 3, {0xb0, 7, 127}}, {32, 3, {0xb0, 8, 0}}};
      BlockView b{64, {x, x}, {y, z}, events, 2, {120, 0, 1, 0}, false};
      while (!stop.load(std::memory_order_acquire))
        audited_audio([&] { e.process(b); });
    });
    try {
      for (unsigned i = 0; i < 1000; i++) {
        while (e.busy()) {
          e.collect();
          std::this_thread::yield();
        }
        auto cc = (i & 1) ? 7u : 8u;
        g.bindings = {{1, cc, 1, 0, 0, 1}, {1, cc, 1, 1, 0, 1}};
        if (!e.publish(g))
          throw std::runtime_error("single controller publication failed");
        for (unsigned j = 0; j < 20; j++) {
          auto seen = e.observation();
          if (seen.ids[0] == 1 &&
              seen.parameters[0][0] != seen.parameters[0][1])
            throw std::runtime_error(
                "mixed-generation binding/parameter observation");
        }
      }
    } catch (...) {
      stop.store(true, std::memory_order_release);
      thread.join();
      throw;
    }
    stop.store(true, std::memory_order_release);
    thread.join();
    e.collect();
    audit.clean();
    std::cout << "PASS immutable binding swaps and coherent preset "
                 "observations during audio\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
