#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <time.h>
namespace nextfx {
inline uint64_t monotonic_ns() noexcept {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return uint64_t(t.tv_sec) * 1000000000 + uint64_t(t.tv_nsec);
}
class TimeHistogram {
  std::array<std::atomic<uint64_t>, 4096> bins_{};

public:
  void record(uint64_t nanoseconds) noexcept {
    auto bin = nanoseconds / 1000;
    if (bin > 4095)
      bin = 4095;
    bins_[size_t(bin)].fetch_add(1, std::memory_order_relaxed);
  }
  std::array<uint64_t, 4096> snapshot() const noexcept {
    std::array<uint64_t, 4096> result{};
    for (size_t i = 0; i < result.size(); i++)
      result[i] = bins_[i].load(std::memory_order_relaxed);
    return result;
  }
  struct Scope {
    TimeHistogram &histogram;
    uint64_t start;
    explicit Scope(TimeHistogram &h) noexcept
        : histogram(h), start(monotonic_ns()) {}
    ~Scope() { histogram.record(monotonic_ns() - start); }
  };
};
} // namespace nextfx
