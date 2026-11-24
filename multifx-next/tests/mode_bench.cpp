// Offline actual DSP CPU comparison. No audio device, no realtime deadline
// claim.
#include "session.hpp"
#include <chrono>
#include <ctime>
#include <iostream>
using namespace nextfx;
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("plugin.so asset");
    Engine e(44100, 64);
    Session s(e);
    float in[64]{}, l[64]{}, r[64]{};
    in[0] = .1f;
    BlockView b{64, {in, in}, {l, r}, nullptr, 0, {120, 0, 0, 0}, false};
    auto apply = [&](std::string command) {
      auto begin = std::chrono::steady_clock::now();
      auto reply = s.command(command);
      if (reply.rfind("OK", 0))
        throw std::runtime_error(reply);
      for (int i = 0; i < 8; ++i) {
        e.process(b);
        s.collect();
      }
      return std::chrono::duration_cast<std::chrono::nanoseconds>(
                 std::chrono::steady_clock::now() - begin)
          .count();
    };
    Json result;
    result["scope"] =
        "offline x86 actual DSP; process CPU time; no audio driver";
    result["load_prepare_and_8_blocks_ns"] =
        apply("ADD " + std::string(argv[1]) + " " + argv[2]);
    for (auto mode : {"ACTIVE", "BYPASS", "UNLOADED"}) {
      if (std::string(mode) != "ACTIVE")
        apply("MODE 1 " + std::string(mode));
      for (int i = 0; i < 128; ++i)
        e.process(b);
      auto before = e.diagnostics();
      auto begin = std::clock();
      for (int i = 0; i < 10000; ++i)
        e.process(b);
      auto elapsed = std::clock() - begin;
      auto after = e.diagnostics();
      auto calls = after.slot_calls[0] - before.slot_calls[0];
      if (std::string(mode) != "ACTIVE" && calls)
        throw std::runtime_error("sleeping DSP called");
      result["modes"][mode] = {{"blocks", 10000},
                               {"process_cpu_ns_per_block",
                                double(elapsed) * 1e9 / CLOCKS_PER_SEC / 10000},
                               {"slot_calls", calls},
                               {"owned_bytes_estimate", after.owned_bytes}};
    }
    result["reload_prepare_and_8_blocks_ns"] = apply("MODE 1 ACTIVE");
    std::cout << result.dump(2) << '\n';
  } catch (const std::exception &ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
