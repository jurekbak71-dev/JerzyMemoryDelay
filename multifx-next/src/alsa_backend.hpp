#pragma once
#include "engine.hpp"
#include "midi_queue.hpp"
#include "rt_metrics.hpp"
#include "session.hpp"
#include <alsa/asoundlib.h>
#include <atomic>
#include <thread>
#include <vector>
namespace nextfx {
struct AlsaSettings {
  std::string device = "hw:CARD=Core,DEV=0";
  std::string capture_device, playback_device;
  unsigned rate = 44100, period = 64, periods = 3, capture_channels = 6,
           playback_channels = 4;
  unsigned in_left = 1, in_right = 2, out_left = 1, out_right = 2;
  std::string midi = "auto";
};
class AlsaBackend {
  AlsaSettings settings_;
  TimeHistogram work_time_;
  snd_pcm_t *capture_{}, *playback_{};
  snd_seq_t *seq_{};
  snd_pcm_format_t in_format_{}, out_format_{};
  snd_pcm_uframes_t in_buffer_{}, out_buffer_{};
  std::unique_ptr<Engine> engine_;
  std::thread thread_, midi_thread_;
  MidiQueue midi_queue_;
  std::atomic<bool> midi_overflow_{false}, realtime_{false};
  void midi_run() noexcept;
  std::atomic<bool> stopping_{false}, lost_{false};
  std::atomic<uint64_t> xruns_{0};
  std::atomic<int> error_{0};
  std::vector<unsigned char> input_, output_;
  std::array<std::vector<float>, 2> in_, out_;
  std::array<JfxMidi, 256> events_{};
  bool null_ = false, linked_ = false;
  void configure(snd_pcm_t *, unsigned, snd_pcm_format_t &,
                 snd_pcm_uframes_t &);
  void run() noexcept;
  bool transfer(snd_pcm_t *, unsigned char *, bool) noexcept;
  bool prepare() noexcept;

public:
  explicit AlsaBackend(const AlsaSettings &);
  ~AlsaBackend();
  void start();
  void stop() noexcept;
  void update_latency() {}
  Engine &engine() noexcept { return *engine_; }
  uint64_t xruns() const noexcept { return xruns_.load(); }
  bool lost() const noexcept { return lost_.load(); }
  bool reconfigure() const noexcept { return false; }
  void describe(Json &) const;
};
} // namespace nextfx
