#pragma once
#include <algorithm>
#include <alsa/asoundlib.h>
#include <bit>
#include <cmath>
#include <cstring>
namespace nextfx {
inline float pcm_decode(const unsigned char *p, snd_pcm_format_t f) noexcept {
  if (f == SND_PCM_FORMAT_FLOAT_LE) {
    float v;
    std::memcpy(&v, p, 4);
    return std::isfinite(v) ? v : 0;
  }
  if (f == SND_PCM_FORMAT_S16_LE) {
    uint16_t u = uint16_t(p[0]) | (uint16_t(p[1]) << 8);
    return std::bit_cast<int16_t>(u) / 32768.f;
  }
  uint32_t u = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
  if (f == SND_PCM_FORMAT_S32_LE) {
    u |= uint32_t(p[3]) << 24;
    return float(std::bit_cast<int32_t>(u) / 2147483648.0);
  }
  if (u & 0x800000)
    u |= 0xff000000;
  return float(std::bit_cast<int32_t>(u) / 8388608.0);
}
inline void pcm_encode(unsigned char *p, snd_pcm_format_t f, float v) noexcept {
  v = std::isfinite(v) ? std::clamp(v, -1.f, 1.f) : 0;
  if (f == SND_PCM_FORMAT_FLOAT_LE) {
    std::memcpy(p, &v, 4);
    return;
  }
  int bits = f == SND_PCM_FORMAT_S16_LE   ? 16
             : f == SND_PCM_FORMAT_S32_LE ? 32
                                          : 24;
  double scale = std::ldexp(1.0, bits - 1);
  int32_t n =
      int32_t(std::clamp(std::round(double(v) * scale), -scale, scale - 1));
  uint32_t u = std::bit_cast<uint32_t>(n);
  for (int b = 0; b < snd_pcm_format_physical_width(f) / 8; b++)
    p[b] = static_cast<unsigned char>(u >> (8 * b));
}
} // namespace nextfx
