#pragma once
#include "jfx.h"
#include <array>
#include <atomic>
namespace nextfx {
// Single sequencer producer, single audio consumer. All storage allocated
// upfront.
class MidiQueue {
  std::array<JfxMidi, 1024> events_{};
  alignas(64) std::atomic<uint64_t> write_{0};
  alignas(64) std::atomic<uint64_t> read_{0};

public:
  bool push(const JfxMidi &m) noexcept {
    auto w = write_.load(std::memory_order_relaxed);
    if (w - read_.load(std::memory_order_acquire) == events_.size())
      return false;
    events_[w % events_.size()] = m;
    write_.store(w + 1, std::memory_order_release);
    return true;
  }
  bool pop(JfxMidi &m) noexcept {
    auto r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire))
      return false;
    m = events_[r % events_.size()];
    read_.store(r + 1, std::memory_order_release);
    return true;
  }
};
} // namespace nextfx
