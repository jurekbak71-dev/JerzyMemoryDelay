#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <time.h>
namespace nextfx {
namespace {
static_assert(std::atomic<void *>::is_always_lock_free &&
              std::atomic<uint64_t>::is_always_lock_free &&
              std::atomic<uint32_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
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
        Stereo input, output;
        Delay bypass;
        uint32_t end_latency = 0;
        uint32_t retire_channels = 0;
    };
    struct Edge {
        AudioEdge spec;
        int source = -1, target = -1;
        Delay delay;
    };
    double bpm = 120;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<MidiRoute> routes;
    std::vector<size_t> order;
    Stereo output;
    uint32_t latency = 0;
    uint64_t bytes = 0;
};
Engine::Engine(double sr, uint32_t frames) : sample_rate_(sr), max_frames_(frames) {
    if (!std::isfinite(sr) || sr < 8000 || sr > 192000 || frames == 0 || frames > 8192)
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
    p->bpm = g.bpm;
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
            if (d.parameter_count != s.parameters.size() || d.audio_inputs != s.inputs ||
                d.latency_frames != s.latency || bool(d.flags & JFX_MIDI_INPUT) != s.midi)
                throw std::runtime_error("slot metadata differs from plugin");
            for (size_t k = 0; k < s.parameters.size(); ++k)
                if (!std::isfinite(s.parameters[k]) || s.parameters[k] < d.parameters[k].minimum ||
                    s.parameters[k] > d.parameters[k].maximum)
                    throw std::runtime_error("parameter out of range");
        } else if (s.instance)
            throw std::runtime_error("UNLOADED retains instance");
        Plan::Node n;
        n.spec = s;
        prepare(n.input, max_frames_);
        prepare(n.output, max_frames_);
        n.bypass.prepare(s.latency);
        p->nodes.push_back(std::move(n));
    }
    std::vector<unsigned> indegree(g.slots.size(), 0);
    for (const auto &edge : g.edges) {
        if (!std::isfinite(edge.gain) || std::abs(edge.gain) > 8 || edge.channels == 0 ||
            edge.channels > 2 || edge.source_channel > 1 || edge.target_channel > 1 ||
            edge.channels > 2 - edge.source_channel || edge.channels > 2 - edge.target_channel ||
            edge.to == 0 || edge.from == -1)
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
    // Route retirement only compares immutable configuration and instance identity.
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
    p->bytes =
        sizeof(Plan) + uint64_t(max_frames_) * 2 * sizeof(float) + delay_samples * sizeof(float);
    for (const auto &n : p->nodes)
        p->bytes += sizeof(Plan::Node) + uint64_t(max_frames_) * 4 * sizeof(float) +
                    uint64_t(n.spec.latency) * 2 * sizeof(float) +
                    (n.spec.instance ? n.spec.instance->resource_bytes() : 0) +
                    n.spec.parameters.capacity() * sizeof(float);
    p->bytes += p->edges.capacity() * sizeof(Plan::Edge) +
                p->routes.capacity() * sizeof(MidiRoute) + p->order.capacity() * sizeof(size_t);
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
        active_ = next;
        latency_.store(next->latency, std::memory_order_relaxed);
        acknowledged_.store(true, std::memory_order_release);
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
    bool panic = b.midi_overflow || b.event_count > 256 || (b.event_count && !b.events);
    if (!panic)
        for (uint32_t i = 0; i < b.event_count; ++i) {
            const auto &e = b.events[i];
            uint8_t type = e.data[0] & 0xf0;
            uint32_t size = type == 0xc0 ? 2 : 3;
            if (e.frame >= n || e.size != size ||
                (type != 0x80 && type != 0x90 && type != 0xb0 && type != 0xc0) || e.data[1] > 127 ||
                (size == 3 && e.data[2] > 127))
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
        if (s.mode == Mode::Active) {
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
            JfxBlock block{n,
                           s.inputs ? in : nullptr,
                           out,
                           s.parameters.data(),
                           routed.data(),
                           eventCount,
                           transport};
            s.instance->process(block);
        } else
            for (uint32_t f = 0; f < n; ++f) {
                for (unsigned c = 0; c < 2; ++c)
                    node.output[c][f] = s.inputs ? node.bypass.tick(c, node.input[c][f]) : 0.f;
                node.bypass.advance();
            }
    }
    for (auto &edge : p.edges)
        if (edge.target < 0)
            mix(edge, p.output);
    for (unsigned c = 0; c < 2; ++c)
        if (b.outputs[c])
            for (uint32_t f = 0; f < n; ++f) {
                float x = p.output[c][f];
                if (!std::isfinite(x)) {
                    x = 0;
                    counters_.bad.fetch_add(1, std::memory_order_relaxed);
                }
                b.outputs[c][f] = x;
            }
    uint64_t elapsed = now() - start;
    counters_.blocks.fetch_add(1, std::memory_order_relaxed);
    counters_.total_ns.fetch_add(elapsed, std::memory_order_relaxed);
    maximum(counters_.max_ns, elapsed);
    if (elapsed > uint64_t(double(n) * 1e9 / sample_rate_))
        counters_.deadline_misses.fetch_add(1, std::memory_order_relaxed);
}
Diagnostics Engine::diagnostics() const {
    Diagnostics d;
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
