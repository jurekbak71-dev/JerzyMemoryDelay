#include "jack_backend.hpp"
#include <algorithm>
#include <jack/midiport.h>
#include <jack/transport.h>
#include <stdexcept>
namespace nextfx {
JackBackend::JackBackend(uint32_t max_frames) {
  jack_status_t status{};
  client_ = jack_client_open(
      "JerzyMultiFXNext",
      static_cast<jack_options_t>(JackNoStartServer | JackUseExactName),
      &status);
  if (!client_)
    throw std::runtime_error(
        "JACK server unavailable or client name already in use");
  try {
    rate_ = jack_get_sample_rate(client_);
    max_frames_ = max_frames;
    if (jack_get_buffer_size(client_) > max_frames_)
      throw std::runtime_error("JACK buffer exceeds configured maximum");
    engine_ = std::make_unique<Engine>(rate_, max_frames_);
    for (unsigned c = 0; c < 2; ++c) {
      input_[c] =
          jack_port_register(client_, c ? "in_2" : "in_1",
                             JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
      output_[c] =
          jack_port_register(client_, c ? "out_2" : "out_1",
                             JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
      if (!input_[c] || !output_[c])
        throw std::runtime_error("JACK audio port registration failed");
    }
    midi_ = jack_port_register(client_, "midi_in", JACK_DEFAULT_MIDI_TYPE,
                               JackPortIsInput, 0);
    if (!midi_)
      throw std::runtime_error("JACK MIDI port registration failed");
    if (jack_set_process_callback(client_, audio, this) ||
        jack_set_xrun_callback(client_, xrun, this) ||
        jack_set_sample_rate_callback(client_, sample_rate, this) ||
        jack_set_buffer_size_callback(client_, buffer_size, this) ||
        jack_set_latency_callback(client_, latency, this))
      throw std::runtime_error("JACK callback registration failed");
    jack_on_shutdown(client_, shutdown, this);
  } catch (...) {
    jack_client_close(client_);
    client_ = nullptr;
    throw;
  }
}
JackBackend::~JackBackend() { stop(); }
void JackBackend::start() {
  if (jack_activate(client_))
    throw std::runtime_error("JACK activate failed");
  activated_ = true;
}
void JackBackend::stop() noexcept {
  if (client_) {
    if (activated_)
      jack_deactivate(client_);
    jack_client_close(client_);
    client_ = nullptr;
    activated_ = false;
  }
}
void JackBackend::update_latency() {
  if (client_ && !lost())
    jack_recompute_total_latencies(client_);
}
int JackBackend::xrun(void *p) noexcept {
  static_cast<JackBackend *>(p)->xruns_.fetch_add(1, std::memory_order_relaxed);
  return 0;
}
void JackBackend::shutdown(void *p) noexcept {
  static_cast<JackBackend *>(p)->lost_.store(true, std::memory_order_release);
}
int JackBackend::sample_rate(jack_nframes_t n, void *p) noexcept {
  auto &b = *static_cast<JackBackend *>(p);
  if (n != b.rate_)
    b.reconfigure_.store(true, std::memory_order_release);
  return 0;
}
int JackBackend::buffer_size(jack_nframes_t n, void *p) noexcept {
  auto &b = *static_cast<JackBackend *>(p);
  if (n > b.max_frames_)
    b.reconfigure_.store(true, std::memory_order_release);
  return 0;
}
void JackBackend::latency(jack_latency_callback_mode_t mode, void *p) noexcept {
  auto &b = *static_cast<JackBackend *>(p);
  jack_latency_range_t total{};
  for (unsigned c = 0; c < 2; ++c) {
    jack_latency_range_t r{};
    jack_port_get_latency_range(
        mode == JackCaptureLatency ? b.input_[c] : b.output_[c], mode, &r);
    total.min = std::max(total.min, r.min);
    total.max = std::max(total.max, r.max);
  }
  auto delay = b.engine_->latency_frames();
  total.min += delay;
  total.max += delay;
  for (unsigned c = 0; c < 2; ++c)
    jack_port_set_latency_range(
        mode == JackCaptureLatency ? b.output_[c] : b.input_[c], mode, &total);
}
int JackBackend::audio(jack_nframes_t n, void *opaque) noexcept {
  auto &b = *static_cast<JackBackend *>(opaque);
  TimeHistogram::Scope timing(b.callback_time_);
  BlockView v{};
  v.frames = n;
  for (unsigned c = 0; c < 2; ++c) {
    v.inputs[c] =
        static_cast<const float *>(jack_port_get_buffer(b.input_[c], n));
    v.outputs[c] = static_cast<float *>(jack_port_get_buffer(b.output_[c], n));
  }
  if (b.reconfigure_.load(std::memory_order_acquire)) {
    for (auto *out : v.outputs)
      if (out)
        std::fill_n(out, n, 0.f);
    return 0;
  }
  std::array<JfxMidi, 256> events{};
  auto *buffer = jack_port_get_buffer(b.midi_, n);
  uint32_t count = buffer ? jack_midi_get_event_count(buffer) : 0;
  v.midi_overflow = count > events.size() ||
                    (buffer && jack_midi_get_lost_event_count(buffer) > 0);
  if (!v.midi_overflow)
    for (uint32_t i = 0; i < count; ++i) {
      jack_midi_event_t event{};
      if (jack_midi_event_get(&event, buffer, i) || event.size < 1 ||
          event.size > 3 || event.time >= n)
        continue;
      JfxMidi e{};
      e.frame = event.time;
      e.size = uint32_t(event.size);
      for (size_t j = 0; j < event.size; ++j)
        e.data[j] = event.buffer[j];
      events[v.event_count++] = e;
    }
  v.events = events.data();
  jack_position_t pos{};
  auto transport = jack_transport_query(b.client_, &pos);
  v.transport.frame = pos.frame;
  v.transport.playing = transport == JackTransportRolling;
  v.transport.valid = (pos.valid & JackPositionBBT) &&
                      pos.beats_per_minute >= 20 && pos.beats_per_minute <= 300;
  v.transport.bpm = v.transport.valid ? pos.beats_per_minute : 120;
  b.engine_->process(v);
  return 0;
}
} // namespace nextfx
