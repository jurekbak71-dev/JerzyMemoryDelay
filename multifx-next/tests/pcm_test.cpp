#include "pcm_codec.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
int main() {
  for (auto format :
       {SND_PCM_FORMAT_FLOAT_LE, SND_PCM_FORMAT_S32_LE, SND_PCM_FORMAT_S24_LE,
        SND_PCM_FORMAT_S24_3LE, SND_PCM_FORMAT_S16_LE}) {
    for (float x : {-1.f, -.75f, 0.f, .125f, .999f, 1.f}) {
      std::array<unsigned char, 4> b{};
      nextfx::pcm_encode(b.data(), format, x);
      float y = nextfx::pcm_decode(b.data(), format);
      if (std::abs(x - y) > 1.f / 32768)
        throw std::runtime_error("PCM amplitude/sign mismatch");
    }
    std::array<unsigned char, 4> b{};
    nextfx::pcm_encode(b.data(), format,
                       std::numeric_limits<float>::quiet_NaN());
    if (nextfx::pcm_decode(b.data(), format) != 0)
      throw std::runtime_error("nonfinite output");
  }
  std::array<unsigned char, 3> neg{0, 0, 128};
  if (nextfx::pcm_decode(neg.data(), SND_PCM_FORMAT_S24_3LE) != -1)
    throw std::runtime_error("packed24 sign");
}
