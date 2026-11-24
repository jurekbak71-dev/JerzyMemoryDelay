#pragma once
#include "plugin.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
namespace nextfx {
enum class Mode { Active, Bypass, Unloaded };
struct SlotSpec {
  int id = 0;
  Mode mode = Mode::Active;
  std::shared_ptr<Instance> instance;
  std::vector<float> parameters;
  uint32_t inputs = 2, latency = 0;
  bool midi = false;
  uint64_t parameter_updates = ~uint64_t(0);
  std::vector<std::string> parameter_keys;
  std::vector<std::array<float, 2>> parameter_ranges;
  uint32_t tail_frames =
      0; // optional finite zero-input FX spill, includes transition
};
struct AudioEdge {
  int from = 0, to = -1;
  float gain = 1;
  uint32_t source_channel = 0, target_channel = 0, channels = 2;
};
struct MidiRoute {
  int from = 0, to = 0;
  uint32_t channels = 65535;
};
struct MidiBinding {
  uint32_t channel = 1, cc = 0;
  int target = 0;
  uint32_t parameter = 0;
  float minimum = 0, maximum = 1;
};
struct Observation {
  float master = 1;
  double bpm = 120;
  std::array<int, 16> ids{};
  std::array<uint32_t, 16> counts{};
  std::array<std::array<float, 48>, 16> parameters{};
};
struct GraphSpec {
  double bpm = 120;
  float master = 1;
  bool master_update = true, bpm_update = true;
  std::vector<MidiBinding> bindings;
  std::vector<SlotSpec> slots;
  std::vector<AudioEdge> edges{{0, -1}};
  std::vector<MidiRoute> midi_routes;
};
struct BlockView {
  uint32_t frames;
  std::array<const float *, 2> inputs;
  std::array<float *, 2> outputs;
  const JfxMidi *events;
  uint32_t event_count;
  JfxTransport transport;
  bool midi_overflow;
};
struct Diagnostics {
  uint64_t blocks = 0, total_ns = 0, max_ns = 0, deadline_misses = 0,
           oversize_blocks = 0, midi_overflows = 0, bad_samples = 0;
  std::array<int, 16> slot_ids{};
  std::array<uint64_t, 16> slot_calls{}, slot_total_ns{}, slot_max_ns{};
  uint32_t latency = 0;
  uint64_t owned_bytes = 0;
  std::array<uint64_t, 4096> callback_histogram{};
};
class Engine {
  struct Plan;
  double sample_rate_;
  uint32_t max_frames_;
  std::unique_ptr<Plan> current_, queued_;
  Plan *active_{};
  std::atomic<Plan *> pending_{nullptr};
  std::atomic<bool> acknowledged_{false};
  bool awaiting_ack_ = false; // audio-owned; ack only after last retiring use

  struct Counters {
    std::atomic<uint64_t> blocks{0}, total_ns{0}, max_ns{0}, deadline_misses{0},
        oversize{0}, overflow{0}, bad{0};
    std::array<std::atomic<uint64_t>, 4096> histogram{};
  } counters_;
  std::atomic<uint32_t> latency_{0};
  std::unique_ptr<Plan> compile(const GraphSpec &);

public:
  Engine(double sample_rate, uint32_t max_frames);
  ~Engine();
  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;
  bool publish(const GraphSpec &);
  bool collect();
  bool busy() const noexcept { return bool(queued_); }
  // One audio reader. Destructor requires backend stopped and all callbacks
  // joined.
  void process(const BlockView &) noexcept;
  Observation observation() const; // controller-only coherent atomic snapshot
  Diagnostics diagnostics() const; // controller thread only
  uint32_t latency_frames() const noexcept {
    return latency_.load(std::memory_order_relaxed);
  }
  double sample_rate() const noexcept { return sample_rate_; }
  uint32_t max_frames() const noexcept { return max_frames_; }
};
} // namespace nextfx
