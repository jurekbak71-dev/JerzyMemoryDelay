#pragma once
#include "engine.hpp"
#include "session.hpp"
#include <array>
#include <atomic>
#include <jack/jack.h>
#include <memory>
namespace nextfx {
class JackBackend {
  jack_client_t *client_{};
  std::array<jack_port_t *, 2> input_{}, output_{};
  jack_port_t *midi_{};
  std::unique_ptr<Engine> engine_;
  uint32_t rate_ = 0, max_frames_ = 0;
  bool activated_ = false;
  std::atomic<uint64_t> xruns_{0};
  std::atomic<bool> lost_{false}, reconfigure_{false};
  static int audio(jack_nframes_t, void *) noexcept;
  static int xrun(void *) noexcept;
  static void shutdown(void *) noexcept;
  static int sample_rate(jack_nframes_t, void *) noexcept;
  static int buffer_size(jack_nframes_t, void *) noexcept;
  static void latency(jack_latency_callback_mode_t, void *) noexcept;

public:
  explicit JackBackend(uint32_t max_frames = 4096);
  ~JackBackend();
  JackBackend(const JackBackend &) = delete;
  JackBackend &operator=(const JackBackend &) = delete;
  void describe(Json &j) const { j["backend"] = "JACK2"; }
  void start();
  void stop() noexcept;
  void update_latency();
  Engine &engine() noexcept { return *engine_; }
  uint64_t xruns() const noexcept { return xruns_.load(); }
  bool lost() const noexcept { return lost_.load(); }
  bool reconfigure() const noexcept { return reconfigure_.load(); }
};
} // namespace nextfx
