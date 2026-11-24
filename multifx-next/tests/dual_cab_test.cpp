#include "../../multifx4/ir_cab.h"
#include "audio_audit.hpp"
#include "dual_cab.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void check(bool x, const char *s) {
  if (!x)
    throw std::runtime_error(s);
}
void wav(const std::string &path, float tap, unsigned length = 1) {
  std::ofstream f(path, std::ios::binary);
  auto w16 = [&](unsigned x) {
    f.put(char(x));
    f.put(char(x >> 8));
  };
  auto w32 = [&](unsigned x) {
    w16(x);
    w16(x >> 16);
  };
  f.write("RIFF", 4);
  w32(36 + length * 4);
  f.write("WAVEfmt ", 8);
  w32(16);
  w16(3);
  w16(1);
  w32(44100);
  w32(176400);
  w16(4);
  w16(32);
  f.write("data", 4);
  w32(length * 4);
  for (unsigned i = 0; i < length; ++i) {
    float v = i ? 0 : tap;
    f.write(reinterpret_cast<char *>(&v), 4);
  }
}
std::string asset(const std::string &dir, bool legacy) {
  auto p = dir + (legacy ? "/legacy.json" : "/aligned.json");
  std::ofstream f(p);
  f << "{\"ir_a\":\"" << dir << "/a.wav\",\"ir_b\":\"" << dir
    << "/b.wav\",\"dry_mode\":\"" << (legacy ? "legacy" : "aligned") << "\"}";
  return p;
}
} // namespace
int main() {
  AudioAudit audit;
  try {
    auto dir = std::filesystem::temp_directory_path() / "next-dual-cab-test";
    std::filesystem::create_directories(dir);
    auto d = dir.string();
    wav(d + "/a.wav", .5);
    wav(d + "/b.wav", .25, 32768);
    for (bool legacy : {false, true})
      for (float mix : {0.f, .5f, 1.f})
        for (float blend : {0.f, .5f, 1.f})
          for (float level : {0.f, .5f, 1.f})
            for (float cut : {0.f, .5f, 1.f}) {
              nextfx::DualCab cab(44100, 64, asset(d, legacy));
              mfx4::CabinetFilter filter[2];
              float gain = std::pow(10.f, (level * 36 - 18) / 20);
              float p[]{mix, blend, level, cut, 1 - cut, 1};
              for (unsigned n = 0; n < 512; ++n) {
                float x = n == 128 ? 1 : 0, l = 0, r = 0;
                const float *in[]{&x, &x};
                float *out[]{&l, &r};
                audited_audio([&] { cab.process(1, in, out, p); });
                float wet = 0;
                for (unsigned s = 0; s < 2; ++s) {
                  float z =
                      filter[s].tick(n == 192 ? (s ? .25f : .5f) : 0, 44100,
                                     30 * std::pow(10.f, cut * 1.6f),
                                     1000 * std::pow(10.f, (1 - cut) * 1.45f));
                  wet += z * (s ? blend : 1 - blend);
                }
                float dry = n == (legacy ? 128 : 192) ? 1 : 0;
                float expected = dry * (1 - mix) + wet * gain * mix;
                check(std::isfinite(l) && std::abs(l - expected) < 2e-5 &&
                          l == r,
                      "dual mix/blend/gain/filter/dry oracle");
              }
            }
    nextfx::DualCab toggle(44100, 64, asset(d, false));
    float p[]{1, .5, .5, .12, .85, 1};
    for (unsigned n = 0; n < 4096; ++n) {
      float x = std::sin(float(n) * .1f) * .1f, l = 0, r = 0;
      const float *in[]{&x, &x};
      float *out[]{&l, &r};
      p[5] = (n / 128) % 2;
      audited_audio([&] { toggle.process(1, in, out, p); });
      check(std::isfinite(l) && std::isfinite(r), "toggle finite");
    }
    nextfx::DualCab transition(44100, 64, asset(d, false));
    float controls[]{1, 0, .5, .12, .85, 0};
    float previous = 0;
    double maxStep = 0;
    for (unsigned n = 0; n < 2048; ++n) {
      if (n == 256)
        controls[5] = 1;
      if (n == 512)
        controls[5] = 0;
      if (n == 768)
        controls[5] = 1;
      if (n == 1024)
        controls[0] = 0;
      if (n == 1280)
        controls[0] = 1;
      if (n == 1536)
        controls[1] = 1;
      if (n == 1792)
        controls[1] = 0;
      float x = 1, l = 0, r = 0;
      const float *in[]{&x, &x};
      float *out[]{&l, &r};
      audited_audio([&] { transition.process(1, in, out, controls); });
      if (n >= 128)
        maxStep = std::max(maxStep, double(std::abs(l - previous)));
      previous = l;
    }
    check(maxStep < .1,
          "continuous-input enable/mix/blend transition step exceeds .1");
    std::cout << "transition max step=" << maxStep << '\n';
    bool rejected = false;
    try {
      nextfx::DualCab bad(0, 64, asset(d, false));
    } catch (...) {
      rejected = true;
    }
    check(rejected, "invalid sample rate");
    audit.clean();
    std::cout
        << "PASS dual CAB: 162 oracle cases (A/B endpoints/half, mix, gain, "
           "filter, dry timing), long IR, fresh loads, toggles finite\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
