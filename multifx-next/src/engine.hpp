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
struct GraphSpec {
    double bpm = 120;
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
    uint64_t blocks = 0, total_ns = 0, max_ns = 0, deadline_misses = 0, oversize_blocks = 0,
             midi_overflows = 0, bad_samples = 0;
    std::array<int, 16> slot_ids{};
    std::array<uint64_t, 16> slot_calls{}, slot_total_ns{}, slot_max_ns{};
    uint32_t latency = 0;
    uint64_t owned_bytes = 0;
};
class Engine {
    struct Plan;
    double sample_rate_;
    uint32_t max_frames_;
    std::unique_ptr<Plan> current_, queued_;
    Plan *active_{};
    std::atomic<Plan *> pending_{nullptr};
    std::atomic<bool> acknowledged_{false};
    struct Counters {
        std::atomic<uint64_t> blocks{0}, total_ns{0}, max_ns{0}, deadline_misses{0}, oversize{0},
            overflow{0}, bad{0};
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
    // One audio reader. Destructor requires backend stopped and all callbacks joined.
    void process(const BlockView &) noexcept;
    Diagnostics diagnostics() const; // controller thread only
    uint32_t latency_frames() const noexcept { return latency_.load(std::memory_order_relaxed); }
    double sample_rate() const noexcept { return sample_rate_; }
    uint32_t max_frames() const noexcept { return max_frames_; }
};
} // namespace nextfx
