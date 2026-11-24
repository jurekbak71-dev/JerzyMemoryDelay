#include "audio_audit.hpp"
#include "session.hpp"
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using namespace nextfx;
void write_ir(const std::string &p, int value) {
  std::ofstream out(p, std::ios::binary);
  auto w16 = [&](unsigned x) {
    out.put(char(x));
    out.put(char(x >> 8));
  };
  auto w32 = [&](unsigned x) {
    w16(x);
    w16(x >> 16);
  };
  out.write("RIFF", 4);
  w32(36 + 256);
  out.write("WAVEfmt ", 8);
  w32(16);
  w16(1);
  w16(1);
  w32(44100);
  w32(88200);
  w16(2);
  w16(16);
  out.write("data", 4);
  w32(256);
  w16(unsigned(value));
  for (int i = 1; i < 128; i++)
    w16(0);
}
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      return 2;
    AudioAudit audit;
    std::string dir = argv[1];
    auto folder = std::filesystem::temp_directory_path() /
                  ("jfx-lifecycle-" + std::to_string(getpid()));
    std::filesystem::create_directory(folder);
    struct Cleanup {
      std::filesystem::path p;
      ~Cleanup() { std::filesystem::remove_all(p); }
    } cleanup{folder};
    auto a = (folder / "a.wav").string(), b = (folder / "b.wav").string();
    write_ir(a, 16384);
    write_ir(b, 8192);
    auto asset1 = (folder / "first.json").string(),
         asset2 = (folder / "second.json").string();
    {
      std::ofstream f(asset1);
      f << Json{{"ir_a", a}, {"ir_b", b}, {"dry_mode", "aligned"}};
    }
    {
      std::ofstream f(asset2);
      f << Json{{"ir_a", b}, {"ir_b", a}, {"dry_mode", "aligned"}};
    }
    for (int cycle = 0; cycle < 5; cycle++) {
      Engine engine(44100, 64);
      Session session(engine);
      std::atomic<bool> stop{false}, bad{false};
      std::thread audio([&] {
        float x[64], y[64]{}, z[64]{};
        for (unsigned i = 0; i < 64; i++)
          x[i] = .05f * std::sin(float(i) * .1f);
        JfxMidi cc{17, 3, {0xb0, 7, 80}};
        BlockView block{64, {x, x}, {y, z}, &cc, 1, {120, 0, 1, 0}, false};
        while (!stop.load()) {
          audited_audio([&] { engine.process(block); });
          for (float v : y)
            if (!std::isfinite(v))
              bad = true;
        }
      });
      auto settle = [&] {
        while (engine.busy()) {
          session.collect();
          std::this_thread::yield();
        }
      };
      auto command = [&](std::string text) {
        settle();
        auto r = session.command(text);
        if (r.rfind("OK", 0) != 0)
          throw std::runtime_error(r);
        settle();
      };
      try {
        command("ADD " + dir + "/gain/plugin.so");
        command("ADD " + dir + "/cab_dual/plugin.so " + asset1);
        for (int i = 0; i < 40; i++) {
          command("SET 1 gain " + std::to_string(.25f + .25f * (i % 3)));
          command("MODE 2 BYPASS");
          command("MODE 2 ACTIVE");
          auto saved = session.snapshot();
          for (auto &slot : saved["slots"])
            if (slot["id"] == 2)
              slot["asset"] = (i % 2 ? asset1 : asset2);
          session.restore(saved);
          settle();
          command("MOVE 2 0");
          command("MOVE 2 1");
          command("ADD " + dir + "/gain/plugin.so");
          command("REMOVE " + std::to_string(i + 3));
          command("MODE 2 UNLOADED");
          command("MODE 2 ACTIVE");
        }
        settle();
        auto pending = session.snapshot();
        pending["master"] = .5;
        session.restore(pending);
      } catch (...) {
        stop = true;
        audio.join();
        throw;
      }
      stop = true;
      audio.join();
      if (bad)
        throw std::runtime_error("nonfinite concurrent audio");
    }
    audit.clean();
    std::cout << "PASS five start/stop cycles, 200 IR replacements plus "
                 "parameter/mode/move/add/remove edits during audio and "
                 "pending shutdown\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
