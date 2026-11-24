#include "alsa_backend.hpp"
#include "control.hpp"
#include <charconv>
#include <string_view>
int main(int argc, char **argv) {
  try {
    nextfx::AlsaSettings s;
    for (int i = 1; i < argc; i++) {
      std::string_view key = argv[i];
      if (key == "--help") {
        std::cout
            << "Direct ALSA host; commands on stdin; QUIT exits.\n--device "
               "hw:CARD=Core,DEV=0 --rate 44100 --period 64 --periods "
               "3\n--capture-channels 6 --playback-channels 4\n--in-left 1 "
               "--in-right 2 --out-left 1 --out-right 2 (1-based)\n--midi "
               "auto|off|required (sequencer; events at next block boundary)\n";
        return 0;
      }
      if (i + 1 >= argc)
        throw std::runtime_error("missing option value");
      std::string_view v = argv[++i];
      if (key == "--device") {
        s.device = v;
        continue;
      }
      if (key == "--capture-device") {
        s.capture_device = v;
        continue;
      }
      if (key == "--playback-device") {
        s.playback_device = v;
        continue;
      }
      if (key == "--midi") {
        s.midi = v;
        continue;
      }
      unsigned n = 0;
      auto r = std::from_chars(v.data(), v.data() + v.size(), n);
      if (r.ec != std::errc{} || r.ptr != v.data() + v.size())
        throw std::runtime_error("invalid unsigned option");
      if (key == "--rate")
        s.rate = n;
      else if (key == "--period")
        s.period = n;
      else if (key == "--periods")
        s.periods = n;
      else if (key == "--capture-channels")
        s.capture_channels = n;
      else if (key == "--playback-channels")
        s.playback_channels = n;
      else if (key == "--in-left")
        s.in_left = n;
      else if (key == "--in-right")
        s.in_right = n;
      else if (key == "--out-left")
        s.out_left = n;
      else if (key == "--out-right")
        s.out_right = n;
      else
        throw std::runtime_error("unknown option");
    }
    nextfx::AlsaBackend b(s);
    return run_control(b, "JerzyMultiFXNextALSA");
  } catch (const std::exception &e) {
    std::cerr << "ALSA: " << e.what() << '\n';
    return 1;
  }
}
