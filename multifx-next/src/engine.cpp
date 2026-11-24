#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>
#include <time.h>
namespace nextfx {
namespace {
static_assert(std::atomic<float>::is_always_lock_free &&
              std::atomic<double>::is_always_lock_free &&
              std::atomic<void *>::is_always_lock_free &&
              std::atomic<uint64_t>::is_always_lock_free &&
              std::atomic<uint32_t>::is_always_lock_free &&
              std::atomic<bool>::is_always_lock_free);
uint64_t now() noexcept {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return uint64_t(t.tv_sec) * 1000000000 + uint64_t(t.tv_nsec);
}
void maximum(std::atomic<uint64_t> &a, uint64_t n) noexcept {
  if (n > a.load(std::memory_order_relaxed))
    a.store(n, std::memory_order_relaxed);
}
using Stereo = std::array<std::vector<float>, 2>;
void prepare(Stereo &b, uint32_t n) {
  for (auto &c : b)
    c.resize(n);
}
struct Delay {
  uint32_t frames = 0, pos = 0;
  Stereo ring;
  void prepare(uint32_t n) {
    frames = n;
    if (n)
      nextfx::prepare(ring, n);
  }
  float tick(unsigned c, float x) noexcept {
    if (!frames)
      return x;
    float y = ring[c][pos];
    ring[c][pos] = x;
    return y;
  }
  void advance() noexcept {
    if (frames && ++pos == frames)
      pos = 0;
  }
};
} // namespace
struct Engine::Plan {
  struct Node {
    SlotSpec spec;
    std::vector<float> runtime;
    std::vector<uint32_t> inherit_parameters;
    std::unique_ptr<std::atomic<float>[]> observed;
    Stereo input, output, previous_output, tail_input;
    Node *predecessor = nullptr; // borrowed from controller-owned current plan
    Instance *old_instance = nullptr;
    uint32_t remaining = 0, total = 0, warmup = 0;
    bool spill = false;
    Delay bypass;
    uint32_t end_latency = 0;
    uint32_t retire_channels = 0;
  };
  struct Edge {
    AudioEdge spec;
    int source = -1, target = -1;
    Delay delay;
  };
  double bpm = 120, runtime_bpm = 120;
  float master = 1, runtime_master = 1;
  bool master_update = true, bpm_update = true;
  std::vector<MidiBinding> bindings;
  std::vector<float> master_curve, bpm_curve;
  std::atomic<float> observed_master{1};
  std::atomic<double> observed_bpm{120};
  std::atomic<uint64_t> observation_epoch{0};
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  std::vector<MidiRoute> routes;
  std::vector<size_t> order;
  Stereo output;
  uint32_t latency = 0;
  uint64_t bytes = 0;
};
Engine::Engine(double sr, uint32_t frames)
    : sample_rate_(sr), max_frames_(frames) {
  if (!std::isfinite(sr) || sr < 8000 || sr > 192000 || frames == 0 ||
      frames > 8192)
    throw std::runtime_error("invalid engine configuration");
  current_ = compile(GraphSpec{});
  active_ = current_.get();
}
Engine::~Engine() = default;
std::unique_ptr<Engine::Plan> Engine::compile(const GraphSpec &g) {
  if (!std::isfinite(g.bpm) || g.bpm < 20 || g.bpm > 300)
    throw std::runtime_error("invalid tempo");
  if (g.slots.size() > 16 || g.edges.size() > 64 || g.midi_routes.size() > 64)
    throw std::runtime_error("graph capacity exceeded");
  auto p = std::make_unique<Plan>();
  if (!std::isfinite(g.master) || g.master < 0 || g.master > 4 ||
      g.bindings.size() > 128)
    throw std::runtime_error("invalid master/bindings");
  p->bpm = p->runtime_bpm = g.bpm;
  p->master = p->runtime_master = g.master;
  p->master_update = g.master_update;
  p->bpm_update = g.bpm_update;
  p->bindings = g.bindings;
  p->master_curve.resize(max_frames_);
  p->bpm_curve.resize(max_frames_);
  p->observed_master.store(g.master);
  p->observed_bpm.store(g.bpm);
  prepare(p->output, max_frames_);
  p->routes = g.midi_routes;
  auto index = [&](int id) -> int {
    for (size_t i = 0; i < g.slots.size(); ++i)
      if (g.slots[i].id == id)
        return int(i);
    throw std::runtime_error("unknown graph node");
  };
  for (size_t i = 0; i < g.slots.size(); ++i) {
    const auto &s = g.slots[i];
    if (s.id <= 0 || s.latency > 131072 || (s.inputs != 0 && s.inputs != 2))
      throw std::runtime_error("invalid slot");
    for (size_t j = 0; j < i; ++j)
      if (g.slots[j].id == s.id)
        throw std::runtime_error("duplicate slot ID");
    if (s.mode != Mode::Unloaded) {
      if (!s.instance)
        throw std::runtime_error("missing live instance");
      const auto &d = s.instance->descriptor();
      if (d.parameter_count != s.parameters.size() ||
          d.audio_inputs != s.inputs || d.latency_frames != s.latency ||
          bool(d.flags & JFX_MIDI_INPUT) != s.midi)
        throw std::runtime_error("slot metadata differs from plugin");
      for (size_t k = 0; k < s.parameters.size(); ++k)
        if (!std::isfinite(s.parameters[k]) ||
            s.parameters[k] < d.parameters[k].minimum ||
            s.parameters[k] > d.parameters[k].maximum)
          throw std::runtime_error("parameter out of range");
    } else if (s.instance)
      throw std::runtime_error("UNLOADED retains instance");
    Plan::Node n;
    if (!s.parameter_keys.empty()) {
      if (s.parameter_keys.size() != s.parameters.size())
        throw std::runtime_error("parameter key count mismatch");
      for (size_t k = 0; k < s.parameter_keys.size(); ++k) {
        const auto &key = s.parameter_keys[k];
        if (key.empty() || key.size() > 128)
          throw std::runtime_error("invalid parameter key");
        for (char c : key)
          if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                c == '.' || c == '-'))
            throw std::runtime_error("invalid parameter key");
        for (size_t j = 0; j < k; ++j)
          if (key == s.parameter_keys[j])
            throw std::runtime_error("duplicate parameter key");
      }
    }
    n.spec = s;
    if (s.parameters.size() > 48)
      throw std::runtime_error("too many parameters");
    n.runtime = s.parameters;
    n.inherit_parameters.resize(s.parameters.size(),
                                std::numeric_limits<uint32_t>::max());
    n.observed = std::make_unique<std::atomic<float>[]>(n.runtime.size());
    for (size_t k = 0; k < n.runtime.size(); ++k)
      n.observed[k].store(n.runtime[k]);
    prepare(n.input, max_frames_);
    prepare(n.output, max_frames_);
    n.bypass.prepare(s.latency);
    prepare(n.previous_output, max_frames_);
    prepare(n.tail_input, max_frames_);
    if (s.tail_frames > uint32_t(sample_rate_ * 10))
      throw std::runtime_error("tail exceeds 10 seconds");
    if (current_)
      for (auto &old : current_->nodes)
        if (old.spec.id == s.id) {
          n.predecessor = &old;
          for (size_t k = 0; k < s.parameters.size(); ++k) {
            size_t source = k;
            if (!s.parameter_keys.empty() && !old.spec.parameter_keys.empty()) {
              auto found =
                  std::find(old.spec.parameter_keys.begin(),
                            old.spec.parameter_keys.end(), s.parameter_keys[k]);
              if (found == old.spec.parameter_keys.end())
                continue;
              source = size_t(found - old.spec.parameter_keys.begin());
            }
            if (source < old.spec.parameters.size())
              n.inherit_parameters[k] = uint32_t(source);
          }
          bool before = old.spec.mode == Mode::Active,
               after = s.mode == Mode::Active;
          bool replaced = before && after && old.spec.instance != s.instance;
          if (before != after || replaced) {
            n.total = n.remaining = 64;
            n.old_instance = before ? old.spec.instance.get() : nullptr;
            n.warmup = replaced ? s.latency : 0;
            n.spill = before && !after && s.inputs && s.tail_frames > 64;
            if (n.spill)
              n.total = n.remaining = s.tail_frames;
          }
          break;
        }
    p->nodes.push_back(std::move(n));
  }
  for (size_t i = 0; i < p->bindings.size(); ++i) {
    const auto &binding = p->bindings[i];
    if (binding.channel < 1 || binding.channel > 16 || binding.cc > 127 ||
        !std::isfinite(binding.minimum) || !std::isfinite(binding.maximum) ||
        binding.minimum > binding.maximum)
      throw std::runtime_error("invalid MIDI binding");
    float low = 0, high = 1.4f;
    if (binding.target == -1) {
      low = 20;
      high = 300;
    } else if (binding.target > 0) {
      const auto &slot = p->nodes[size_t(index(binding.target))].spec;
      if (binding.parameter >= slot.parameters.size())
        throw std::runtime_error("binding parameter outside slot");
      if (slot.instance) {
        const auto &param =
            slot.instance->descriptor().parameters[binding.parameter];
        low = param.minimum;
        high = param.maximum;
      } else {
        if (slot.parameter_ranges.size() != slot.parameters.size())
          throw std::runtime_error("unloaded binding lacks ranges");
        low = slot.parameter_ranges[binding.parameter][0];
        high = slot.parameter_ranges[binding.parameter][1];
      }
    } else if (binding.target != 0)
      throw std::runtime_error("invalid binding target");
    if (binding.minimum < low || binding.maximum > high ||
        (binding.target <= 0 && binding.parameter))
      throw std::runtime_error("binding range outside target");
    for (size_t k = 0; k < i; k++) {
      const auto &old = p->bindings[k];
      if (old.channel == binding.channel && old.cc == binding.cc &&
          old.target == binding.target && old.parameter == binding.parameter)
        throw std::runtime_error("duplicate binding");
    }
  }
  std::vector<unsigned> indegree(g.slots.size(), 0);
  for (const auto &edge : g.edges) {
    if (!std::isfinite(edge.gain) || std::abs(edge.gain) > 8 ||
        edge.channels == 0 || edge.channels > 2 || edge.source_channel > 1 ||
        edge.target_channel > 1 || edge.channels > 2 - edge.source_channel ||
        edge.channels > 2 - edge.target_channel || edge.to == 0 ||
        edge.from == -1)
      throw std::runtime_error("invalid audio edge");
    Plan::Edge e;
    e.spec = edge;
    e.source = edge.from == 0 ? -1 : index(edge.from);
    e.target = edge.to == -1 ? -1 : index(edge.to);
    if (e.target >= 0 && !g.slots[size_t(e.target)].inputs)
      throw std::runtime_error("audio edge to instrument input");
    if (e.source >= 0 && e.target >= 0)
      ++indegree[size_t(e.target)];
    p->edges.push_back(std::move(e));
  }
  for (size_t step = 0; step < g.slots.size(); ++step) {
    size_t i = 0;
    for (; i < indegree.size(); ++i)
      if (indegree[i] == 0)
        break;
    if (i == indegree.size())
      throw std::runtime_error("audio graph cycle");
    indegree[i] = std::numeric_limits<unsigned>::max();
    p->order.push_back(i);
    for (const auto &e : p->edges)
      if (e.source == int(i) && e.target >= 0)
        --indegree[size_t(e.target)];
  }
  uint64_t delay_samples = 0;
  for (size_t i : p->order) {
    uint32_t incoming = 0;
    for (const auto &e : p->edges)
      if (e.target == int(i) && e.source >= 0)
        incoming = std::max(incoming, p->nodes[size_t(e.source)].end_latency);
    if (incoming > 131072 - p->nodes[i].spec.latency)
      throw std::runtime_error("graph latency limit");
    p->nodes[i].end_latency = incoming + p->nodes[i].spec.latency;
    for (auto &e : p->edges)
      if (e.target == int(i)) {
        auto src = e.source < 0 ? 0 : p->nodes[size_t(e.source)].end_latency;
        e.delay.prepare(incoming - src);
        delay_samples += 2ull * (incoming - src);
      }
  }
  for (const auto &e : p->edges)
    if (e.target < 0 && e.source >= 0)
      p->latency = std::max(p->latency, p->nodes[size_t(e.source)].end_latency);
  for (auto &e : p->edges)
    if (e.target < 0) {
      auto src = e.source < 0 ? 0 : p->nodes[size_t(e.source)].end_latency;
      e.delay.prepare(p->latency - src);
      delay_samples += 2ull * (p->latency - src);
    }
  if (delay_samples > 4 * 1024 * 1024)
    throw std::runtime_error("graph delay memory limit");
  for (const auto &route : g.midi_routes) {
    if (route.from != 0 || route.channels > 65535 || !route.channels ||
        !g.slots[size_t(index(route.to))].midi)
      throw std::runtime_error("invalid MIDI route");
  }
  // Route retirement only compares immutable configuration and instance
  // identity.
  if (current_)
    for (auto &node : p->nodes)
      if (node.spec.midi && node.spec.mode == Mode::Active) {
        for (const auto &old : current_->nodes)
          if (old.spec.instance && old.spec.instance == node.spec.instance) {
            uint32_t previous = 0, next = 0;
            for (const auto &r : current_->routes)
              if (r.to == old.spec.id)
                previous |= r.channels;
            for (const auto &r : p->routes)
              if (r.to == node.spec.id)
                next |= r.channels;
            node.retire_channels = previous & ~next;
          }
      }
  p->bytes = sizeof(Plan) + uint64_t(max_frames_) * 2 * sizeof(float) +
             delay_samples * sizeof(float);
  for (const auto &n : p->nodes)
    p->bytes += sizeof(Plan::Node) + uint64_t(max_frames_) * 8 * sizeof(float) +
                uint64_t(n.spec.latency) * 2 * sizeof(float) +
                (n.spec.instance ? n.spec.instance->resource_bytes() : 0) +
                n.spec.parameters.capacity() * sizeof(float);
  p->bytes += p->edges.capacity() * sizeof(Plan::Edge) +
              p->routes.capacity() * sizeof(MidiRoute) +
              p->order.capacity() * sizeof(size_t) +
              p->bindings.capacity() * sizeof(MidiBinding) +
              uint64_t(max_frames_) * 2 * sizeof(float);
  for (const auto &node : p->nodes)
    p->bytes +=
        node.runtime.capacity() * sizeof(float) +
        node.runtime.size() * sizeof(std::atomic<float>) +
        node.inherit_parameters.capacity() * sizeof(uint32_t) +
        node.spec.parameter_ranges.capacity() * sizeof(std::array<float, 2>) +
        node.spec.parameter_keys.capacity() * sizeof(std::string);
  return p;
}
bool Engine::publish(const GraphSpec &g) {
  if (queued_)
    return false;
  auto next = compile(g);
  queued_ = std::move(next);
  pending_.store(queued_.get(), std::memory_order_release);
  return true;
}
bool Engine::collect() {
  if (!queued_ || !acknowledged_.exchange(false, std::memory_order_acq_rel))
    return false;
  current_ = std::move(queued_);
  return true;
}
void Engine::process(const BlockView &b) noexcept {
  const uint64_t start = now();
  if (auto *next = pending_.exchange(nullptr, std::memory_order_acquire)) {
    if (!next->master_update)
      next->runtime_master = active_->runtime_master;
    if (!next->bpm_update)
      next->runtime_bpm = active_->runtime_bpm;
    for (auto &node : next->nodes)
      if (node.predecessor) {
        if (node.predecessor->spec.latency == node.spec.latency)
          std::swap(node.bypass, node.predecessor->bypass);
        for (size_t k = 0; k < node.runtime.size(); ++k)
          if (!(node.spec.parameter_updates & (uint64_t(1) << k)) &&
              node.inherit_parameters[k] < node.predecessor->runtime.size())
            node.runtime[k] =
                node.predecessor->runtime[node.inherit_parameters[k]];
      }
    active_ = next;
    awaiting_ack_ = true;
    latency_.store(next->latency, std::memory_order_relaxed);
  }
  auto &p = *active_;
  const uint32_t n = b.frames;
  if (n > max_frames_) {
    for (auto *out : b.outputs)
      if (out)
        std::fill_n(out, n, 0.f);
    counters_.oversize.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (n == 0)
    return;
  std::array<JfxMidi, 256> events{};
  uint32_t count = 0;
  bool panic =
      b.midi_overflow || b.event_count > 256 || (b.event_count && !b.events);
  if (!panic)
    for (uint32_t i = 0; i < b.event_count; ++i) {
      const auto &e = b.events[i];
      uint8_t type = e.data[0] & 0xf0;
      uint32_t size = type == 0xc0 ? 2 : 3;
      if (e.frame >= n || e.size != size ||
          (type != 0x80 && type != 0x90 && type != 0xb0 && type != 0xc0) ||
          e.data[1] > 127 || (size == 3 && e.data[2] > 127))
        continue;
      uint32_t j = count;
      while (j && events[j - 1].frame > e.frame) {
        events[j] = events[j - 1];
        --j;
      }
      events[j] = e;
      ++count;
    }
  if (panic) {
    counters_.overflow.fetch_add(1, std::memory_order_relaxed);
    count = 16;
    for (unsigned i = 0; i < 16; ++i)
      events[i] = {0, 3, {uint8_t(0xb0 | i), 120, 0}};
  }
  auto matches = [&](const MidiBinding &binding,
                     const JfxMidi &event) noexcept {
    return !panic && (event.data[0] & 0xf0) == 0xb0 &&
           uint32_t((event.data[0] & 15) + 1) == binding.channel &&
           event.data[1] == binding.cc;
  };
  auto mapped = [](const MidiBinding &binding, const JfxMidi &event) noexcept {
    return binding.minimum +
           (binding.maximum - binding.minimum) * (float(event.data[2]) / 127.f);
  };
  std::array<uint32_t, 258> boundaries{};
  uint32_t boundary_count = 1;
  for (uint32_t i = 0; i < count; i++) {
    if (!events[i].frame || boundaries[boundary_count - 1] == events[i].frame)
      continue;
    for (const auto &binding : p.bindings)
      if (matches(binding, events[i])) {
        boundaries[boundary_count++] = events[i].frame;
        break;
      }
  }
  boundaries[boundary_count++] = n;
  uint32_t control_cursor = 0;
  for (uint32_t f = 0; f < n; f++) {
    while (control_cursor < count && events[control_cursor].frame == f) {
      for (const auto &binding : p.bindings)
        if (binding.target <= 0 && matches(binding, events[control_cursor])) {
          if (binding.target == 0)
            p.runtime_master = mapped(binding, events[control_cursor]);
          else
            p.runtime_bpm = mapped(binding, events[control_cursor]);
        }
      ++control_cursor;
    }
    p.master_curve[f] = p.runtime_master;
    p.bpm_curve[f] =
        b.transport.valid ? float(b.transport.bpm) : float(p.runtime_bpm);
  }
  for (auto &node : p.nodes)
    for (auto &c : node.input)
      std::fill_n(c.data(), n, 0.f);
  for (auto &c : p.output)
    std::fill_n(c.data(), n, 0.f);
  auto mix = [&](Plan::Edge &e, Stereo &target) {
    for (uint32_t f = 0; f < n; ++f) {
      for (uint32_t c = 0; c < e.spec.channels; ++c) {
        unsigned sc = e.spec.source_channel + c, tc = e.spec.target_channel + c;
        float x = e.source < 0 ? (b.inputs[sc] ? b.inputs[sc][f] : 0.f)
                               : p.nodes[size_t(e.source)].output[sc][f];
        if (!std::isfinite(x)) {
          x = 0;
          counters_.bad.fetch_add(1, std::memory_order_relaxed);
        }
        target[tc][f] += e.delay.tick(c, x) * e.spec.gain;
      }
      e.delay.advance();
    }
  };
  for (size_t idx : p.order) {
    auto &node = p.nodes[idx];
    for (auto &edge : p.edges)
      if (edge.target == int(idx))
        mix(edge, node.input);
    const auto &s = node.spec;
    uint32_t binding_cursor = 0;
    auto apply_cc = [&](uint32_t frame) {
      while (binding_cursor < count && events[binding_cursor].frame <= frame) {
        const auto &event = events[binding_cursor++];
        if (event.frame == frame)
          for (const auto &binding : p.bindings)
            if (binding.target == s.id && matches(binding, event))
              node.runtime[binding.parameter] = mapped(binding, event);
      }
    };
    if (s.mode == Mode::Active || (node.remaining && node.old_instance)) {
      std::array<JfxMidi, 256> routed{};
      uint32_t eventCount = 0, mask = 0;
      for (const auto &r : p.routes)
        if (r.to == s.id)
          mask |= r.channels;
      if (s.midi) {
        for (unsigned channel = 0; channel < 16; ++channel)
          if (node.retire_channels & (1u << channel))
            routed[eventCount++] = {0, 3, {uint8_t(0xb0 | channel), 120, 0}};
        node.retire_channels = 0;
        for (uint32_t j = 0; j < count; ++j)
          if (panic || (mask & (1u << (events[j].data[0] & 15)))) {
            if (eventCount == routed.size()) {
              counters_.overflow.fetch_add(1, std::memory_order_relaxed);
              eventCount = 16;
              for (unsigned channel = 0; channel < 16; ++channel)
                routed[channel] = {0, 3, {uint8_t(0xb0 | channel), 120, 0}};
              break;
            }
            routed[eventCount++] = events[j];
          }
      }
      const float *in[]{node.input[0].data(), node.input[1].data()};
      float *out[]{node.output[0].data(), node.output[1].data()};
      for (auto &c : node.output)
        std::fill_n(c.data(), n, 0.f);
      auto transport = b.transport;
      if (!transport.valid) {
        transport.bpm = p.bpm;
        transport.valid = 1;
      }
      uint32_t midi_cursor = 0;
      for (uint32_t segment = 0; segment + 1 < boundary_count; segment++) {
        uint32_t offset = boundaries[segment],
                 frames = boundaries[segment + 1] - offset;
        apply_cc(offset);
        if (s.mode != Mode::Active)
          continue;
        std::array<JfxMidi, 256> local_events{};
        uint32_t local_count = 0;
        while (midi_cursor < eventCount &&
               routed[midi_cursor].frame < offset + frames) {
          auto event = routed[midi_cursor++];
          event.frame -= offset;
          local_events[local_count++] = event;
        }
        const float *segment_in[]{in[0] + offset, in[1] + offset};
        float *segment_out[]{out[0] + offset, out[1] + offset};
        auto position = transport;
        position.frame += offset;
        position.bpm = p.bpm_curve[offset];
        JfxBlock block{frames,
                       s.inputs ? segment_in : nullptr,
                       segment_out,
                       node.runtime.data(),
                       local_events.data(),
                       local_count,
                       position};
        s.instance->process(block);
      }
      if (node.remaining && node.old_instance) {
        uint32_t frames = std::min(n, node.remaining + node.warmup);
        const float *previous_in[]{node.input[0].data(), node.input[1].data()};
        if (node.spill) {
          for (uint32_t f = 0; f < frames; ++f) {
            float weight = std::max(
                0.f, 1.f - float(node.total - node.remaining + f) / 63.f);
            for (unsigned c = 0; c < 2; ++c)
              node.tail_input[c][f] = node.input[c][f] * weight;
          }
          previous_in[0] = node.tail_input[0].data();
          previous_in[1] = node.tail_input[1].data();
        }
        float *previous_out[]{node.previous_output[0].data(),
                              node.previous_output[1].data()};
        for (auto &channel : node.previous_output)
          std::fill_n(channel.data(), frames, 0.f);
        std::array<JfxMidi, 16> panic_events{};
        for (unsigned c = 0; c < 16; ++c)
          panic_events[c] = {frames - 1, 3, {uint8_t(0xb0 | c), 120, 0}};
        JfxBlock previous{frames,
                          node.predecessor->spec.inputs ? previous_in : nullptr,
                          previous_out,
                          node.predecessor->runtime.data(),
                          panic_events.data(),
                          node.predecessor->spec.midi &&
                                  node.remaining + node.warmup <= n
                              ? 16u
                              : 0u,
                          transport};
        node.old_instance->process(previous);
      }
    }
    if (s.mode != Mode::Active && !(node.remaining && node.old_instance))
      for (uint32_t i = 0; i + 1 < boundary_count; i++)
        apply_cc(boundaries[i]);
    for (uint32_t f = 0; f < n; ++f) {
      float alpha =
          node.remaining
              ? std::min(1.f, float(node.total - node.remaining) / 63.f)
              : 1.f;
      if (node.warmup)
        alpha = 0;
      float previous_weight =
          node.spill ? std::min(1.f, float(node.remaining) / 64.f) : 1 - alpha;
      for (unsigned c = 0; c < 2; ++c) {
        float dry = s.inputs ? node.bypass.tick(c, node.input[c][f]) : 0.f;
        float target = s.mode == Mode::Active ? node.output[c][f] : dry;
        if (node.remaining || node.warmup) {
          float before = node.old_instance ? node.previous_output[c][f] : dry;
          node.output[c][f] = node.spill
                                  ? dry * alpha + before * previous_weight
                                  : before * (1 - alpha) + target * alpha;
        } else
          node.output[c][f] = target;
      }
      node.bypass.advance();
      if (node.warmup)
        --node.warmup;
      else if (node.remaining)
        --node.remaining;
    }
  }
  for (auto &edge : p.edges)
    if (edge.target < 0)
      mix(edge, p.output);
  for (unsigned c = 0; c < 2; ++c)
    if (b.outputs[c])
      for (uint32_t f = 0; f < n; ++f) {
        float x = p.output[c][f] * p.master_curve[f];
        if (!std::isfinite(x)) {
          x = 0;
          counters_.bad.fetch_add(1, std::memory_order_relaxed);
        }
        b.outputs[c][f] = x;
      }
  p.observation_epoch.fetch_add(1, std::memory_order_seq_cst);
  p.observed_master.store(p.runtime_master, std::memory_order_seq_cst);
  p.observed_bpm.store(p.runtime_bpm, std::memory_order_seq_cst);
  for (auto &node : p.nodes)
    for (size_t k = 0; k < node.runtime.size(); k++)
      if (node.observed[k].load(std::memory_order_relaxed) != node.runtime[k])
        node.observed[k].store(node.runtime[k], std::memory_order_seq_cst);
  p.observation_epoch.fetch_add(1, std::memory_order_seq_cst);
  if (awaiting_ack_) {
    bool finished = true;
    for (auto &node : p.nodes)
      if (node.remaining || node.warmup)
        finished = false;
    if (finished) {
      awaiting_ack_ = false;
      acknowledged_.store(true, std::memory_order_release);
    }
  }
  uint64_t elapsed = now() - start;
  counters_.histogram[std::min(uint64_t(4095), elapsed / 1000)].fetch_add(
      1, std::memory_order_relaxed);
  counters_.blocks.fetch_add(1, std::memory_order_relaxed);
  counters_.total_ns.fetch_add(elapsed, std::memory_order_relaxed);
  maximum(counters_.max_ns, elapsed);
  if (elapsed > uint64_t(double(n) * 1e9 / sample_rate_))
    counters_.deadline_misses.fetch_add(1, std::memory_order_relaxed);
}
Observation Engine::observation() const {
  const auto &p = *current_;
  Observation result;
  for (;;) {
    auto epoch = p.observation_epoch.load(std::memory_order_seq_cst);
    if (epoch & 1) {
      std::this_thread::yield();
      continue;
    }
    result.master = p.observed_master.load(std::memory_order_seq_cst);
    result.bpm = p.observed_bpm.load(std::memory_order_seq_cst);
    for (size_t i = 0; i < p.nodes.size(); i++) {
      const auto &node = p.nodes[i];
      result.ids[i] = node.spec.id;
      result.counts[i] = uint32_t(node.runtime.size());
      for (size_t k = 0; k < node.runtime.size(); k++)
        result.parameters[i][k] =
            node.observed[k].load(std::memory_order_seq_cst);
    }
    if (epoch == p.observation_epoch.load(std::memory_order_seq_cst))
      return result;
  }
}
Diagnostics Engine::diagnostics() const {
  Diagnostics d;
  for (size_t i = 0; i < d.callback_histogram.size(); ++i)
    d.callback_histogram[i] =
        counters_.histogram[i].load(std::memory_order_relaxed);
  d.blocks = counters_.blocks.load();
  d.total_ns = counters_.total_ns.load();
  d.max_ns = counters_.max_ns.load();
  d.deadline_misses = counters_.deadline_misses.load();
  d.oversize_blocks = counters_.oversize.load();
  d.midi_overflows = counters_.overflow.load();
  d.bad_samples = counters_.bad.load();
  for (size_t i = 0; i < current_->nodes.size(); ++i) {
    const auto &node = current_->nodes[i];
    d.slot_ids[i] = node.spec.id;
    if (node.spec.instance) {
      auto stats = node.spec.instance->statistics();
      d.slot_calls[i] = stats.calls;
      d.slot_total_ns[i] = stats.total_ns;
      d.slot_max_ns[i] = stats.max_ns;
    }
  }
  d.latency = latency_.load();
  d.owned_bytes = current_->bytes + (queued_ ? queued_->bytes : 0);
  return d;
}
} // namespace nextfx
