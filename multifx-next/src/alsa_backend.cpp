#include "alsa_backend.hpp"
#include "pcm_codec.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <pthread.h>
#include <stdexcept>
namespace nextfx {
namespace {
void check(int rc, const char *what) {
  if (rc < 0)
    throw std::runtime_error(std::string(what) + ": " + snd_strerror(rc));
}

} // namespace
void AlsaBackend::configure(snd_pcm_t *pcm, unsigned channels,
                            snd_pcm_format_t &format,
                            snd_pcm_uframes_t &buffer) {
  snd_pcm_hw_params_t *raw = nullptr;
  check(snd_pcm_hw_params_malloc(&raw), "hw params");
  std::unique_ptr<snd_pcm_hw_params_t, decltype(&snd_pcm_hw_params_free)> hw(
      raw, snd_pcm_hw_params_free);
  check(snd_pcm_hw_params_any(pcm, raw), "hw any");
  check(snd_pcm_hw_params_set_access(pcm, raw, SND_PCM_ACCESS_RW_INTERLEAVED),
        "interleaved");
  bool found = false;
  for (auto f :
       {SND_PCM_FORMAT_FLOAT_LE, SND_PCM_FORMAT_S32_LE, SND_PCM_FORMAT_S24_LE,
        SND_PCM_FORMAT_S24_3LE, SND_PCM_FORMAT_S16_LE})
    if (snd_pcm_hw_params_test_format(pcm, raw, f) == 0) {
      format = f;
      found = true;
      break;
    }
  if (!found)
    throw std::runtime_error("no supported PCM sample format");
  check(snd_pcm_hw_params_set_format(pcm, raw, format), "format");
  check(snd_pcm_hw_params_set_channels(pcm, raw, channels), "channels");
  check(snd_pcm_hw_params_set_rate(pcm, raw, settings_.rate, 0),
        "exact sample rate");
  snd_pcm_uframes_t period = settings_.period;
  check(snd_pcm_hw_params_set_period_size(pcm, raw, period, 0),
        "exact period size");
  check(snd_pcm_hw_params_set_periods(pcm, raw, settings_.periods, 0),
        "period count");
  check(snd_pcm_hw_params(pcm, raw), "apply hardware params");
  check(snd_pcm_hw_params_get_buffer_size(raw, &buffer), "buffer size");
  snd_pcm_sw_params_t *swraw = nullptr;
  check(snd_pcm_sw_params_malloc(&swraw), "sw params");
  std::unique_ptr<snd_pcm_sw_params_t, decltype(&snd_pcm_sw_params_free)> sw(
      swraw, snd_pcm_sw_params_free);
  check(snd_pcm_sw_params_current(pcm, swraw), "sw current");
  check(snd_pcm_sw_params_set_avail_min(pcm, swraw, period), "avail min");
  // Playback starts explicitly after priming. Capture starts explicitly too.
  snd_pcm_uframes_t boundary;
  check(snd_pcm_sw_params_get_boundary(swraw, &boundary), "boundary");
  check(snd_pcm_sw_params_set_start_threshold(pcm, swraw, boundary),
        "start threshold");
  check(snd_pcm_sw_params(pcm, swraw), "apply software params");
}
AlsaBackend::AlsaBackend(const AlsaSettings &s) : settings_(s) {
  if (s.rate < 8000 || s.rate > 192000 || s.period < 16 || s.period > 4096 ||
      s.periods < 2 || s.periods > 8 || !s.capture_channels ||
      s.capture_channels > 32 || !s.playback_channels ||
      s.playback_channels > 32 || !s.in_left ||
      s.in_left > s.capture_channels || !s.in_right ||
      s.in_right > s.capture_channels || !s.out_left ||
      s.out_left > s.playback_channels || !s.out_right ||
      s.out_right > s.playback_channels || s.out_left == s.out_right)
    throw std::runtime_error("invalid ALSA rate/period/channels/map");
  if (s.midi != "off" && s.midi != "auto" && s.midi != "required")
    throw std::runtime_error("midi must be auto, off or required");
  try {
    check(snd_pcm_open(
              &capture_,
              (s.capture_device.empty() ? s.device : s.capture_device).c_str(),
              SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK),
          "capture open");
    check(
        snd_pcm_open(
            &playback_,
            (s.playback_device.empty() ? s.device : s.playback_device).c_str(),
            SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK),
        "playback open");
    configure(capture_, s.capture_channels, in_format_, in_buffer_);
    configure(playback_, s.playback_channels, out_format_, out_buffer_);
    null_ = snd_pcm_type(capture_) == SND_PCM_TYPE_NULL;
    linked_ = snd_pcm_link(capture_, playback_) == 0;
    input_.resize(size_t(s.period) * s.capture_channels *
                  snd_pcm_format_physical_width(in_format_) / 8);
    output_.resize(size_t(s.period) * s.playback_channels *
                   snd_pcm_format_physical_width(out_format_) / 8);
    for (auto &v : in_)
      v.resize(s.period);
    for (auto &v : out_)
      v.resize(s.period);
    if (s.midi != "off") {
      int rc =
          snd_seq_open(&seq_, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK);
      if (rc < 0) {
        seq_ = nullptr;
        if (s.midi == "required")
          check(rc, "MIDI sequencer open");
      }
      if (seq_) {
        check(snd_seq_set_client_name(seq_, "JerzyMultiFXNextALSA"),
              "MIDI name");
        check(snd_seq_create_simple_port(seq_, "MIDI in",
                                         SND_SEQ_PORT_CAP_WRITE |
                                             SND_SEQ_PORT_CAP_SUBS_WRITE,
                                         SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                                             SND_SEQ_PORT_TYPE_APPLICATION),
              "MIDI port");
        check(snd_seq_set_input_buffer_size(seq_, 65536), "MIDI buffer");
      }
    }
    engine_ = std::make_unique<Engine>(s.rate, s.period);
  } catch (...) {
    if (seq_)
      snd_seq_close(seq_);
    if (playback_)
      snd_pcm_close(playback_);
    if (capture_)
      snd_pcm_close(capture_);
    throw;
  }
}
AlsaBackend::~AlsaBackend() {
  stop();
  if (seq_)
    snd_seq_close(seq_);
  if (playback_)
    snd_pcm_close(playback_);
  if (capture_)
    snd_pcm_close(capture_);
}
void AlsaBackend::describe(Json &j) const {
  j["backend"] = "ALSA";
  j["callback_histogram"] = work_time_.snapshot();
  j["callback_histogram_width_ns"] = 1000;
  j["callback_histogram_overflow_bin"] = 4095;
  j["timing_scope"] = "alsa_decode_dsp_encode";
  j["linked"] = linked_;
  j["device"] = settings_.device;
  j["capture_device"] = settings_.capture_device.empty()
                            ? settings_.device
                            : settings_.capture_device;
  j["playback_device"] = settings_.playback_device.empty()
                             ? settings_.device
                             : settings_.playback_device;
  j["input_map"] = {settings_.in_left, settings_.in_right};
  j["output_map"] = {settings_.out_left, settings_.out_right};
  j["midi_client"] = seq_ ? snd_seq_client_id(seq_) : -1;
  j["capture_channels"] = settings_.capture_channels;
  j["playback_channels"] = settings_.playback_channels;
  j["period"] = settings_.period;
  j["rate"] = settings_.rate;
  j["capture_buffer"] = in_buffer_;
  j["playback_buffer"] = out_buffer_;
  j["midi_available"] = bool(seq_);
  j["capture_format"] = snd_pcm_format_name(in_format_);
  j["playback_format"] = snd_pcm_format_name(out_format_);
  j["realtime"] = realtime_.load();
  j["error"] = error_.load();
  j["null_test"] = null_;
}
bool AlsaBackend::transfer(snd_pcm_t *p, unsigned char *data,
                           bool capture) noexcept {
  size_t done = 0,
         stride =
             (capture ? settings_.capture_channels
                      : settings_.playback_channels) *
             snd_pcm_format_physical_width(capture ? in_format_ : out_format_) /
             8;
  while (done < settings_.period && !stopping_.load()) {
    auto rc =
        capture
            ? snd_pcm_readi(p, data + done * stride, settings_.period - done)
            : snd_pcm_writei(p, data + done * stride, settings_.period - done);
    if (rc > 0) {
      done += size_t(rc);
      continue;
    }
    if (rc == 0 || rc == -EAGAIN || rc == -EINTR) {
      int w = snd_pcm_wait(p, 20);
      if (w < 0 && w != -EINTR) {
        error_ = w;
        return false;
      }
      continue;
    }
    error_ = int(rc);
    return false;
  }
  return done == settings_.period;
}
bool AlsaBackend::prepare() noexcept {
  snd_pcm_drop(capture_);
  snd_pcm_drop(playback_);
  int rc = snd_pcm_prepare(capture_);
  if (rc < 0) {
    error_ = rc;
    return false;
  }
  rc = snd_pcm_prepare(playback_);
  if (rc < 0) {
    error_ = rc;
    return false;
  }
  std::fill(output_.begin(), output_.end(), 0);
  // Prime all periods; normal hardware remains stopped until explicit start.
  for (unsigned n = 0; n < settings_.periods; n++)
    if (!transfer(playback_, output_.data(), false))
      return false;
  rc = snd_pcm_start(playback_);
  if (rc < 0) {
    error_ = rc;
    return false;
  }
  rc = linked_ ? 0 : snd_pcm_start(capture_);
  if (rc < 0) {
    error_ = rc;
    return false;
  }
  error_ = 0;
  return true;
}
void AlsaBackend::start() {
  if (thread_.joinable())
    throw std::runtime_error("already started");
  stopping_ = false;
  if (!prepare())
    throw std::runtime_error(std::string("ALSA prepare: ") +
                             snd_strerror(error_));
  try {
    thread_ = std::thread(&AlsaBackend::run, this);
    if (seq_)
      midi_thread_ = std::thread(&AlsaBackend::midi_run, this);
  } catch (...) {
    stop();
    throw;
  }
}
void AlsaBackend::stop() noexcept {
  stopping_ = true;
  if (midi_thread_.joinable())
    midi_thread_.join();
  if (thread_.joinable())
    thread_.join();
  snd_pcm_drop(capture_);
  snd_pcm_drop(playback_);
}
void AlsaBackend::midi_run() noexcept {
  while (!stopping_) {
    for (unsigned n = 0; n < 256 && !stopping_; n++) {
      snd_seq_event_t *e = nullptr;
      int rc = snd_seq_event_input(seq_, &e);
      if (rc == -EAGAIN)
        break;
      if (rc < 0) {
        midi_overflow_ = true;
        break;
      }
      JfxMidi m{};
      m.frame = 0;
      m.size = 3;
      switch (e->type) {
      case SND_SEQ_EVENT_NOTEON:
        m.data[0] = 0x90 | e->data.note.channel;
        m.data[1] = e->data.note.note;
        m.data[2] = e->data.note.velocity;
        break;
      case SND_SEQ_EVENT_NOTEOFF:
        m.data[0] = 0x80 | e->data.note.channel;
        m.data[1] = e->data.note.note;
        m.data[2] = e->data.note.velocity;
        break;
      case SND_SEQ_EVENT_CONTROLLER:
        m.data[0] = 0xb0 | e->data.control.channel;
        m.data[1] = e->data.control.param & 127;
        m.data[2] = e->data.control.value & 127;
        break;
      case SND_SEQ_EVENT_PGMCHANGE:
        m.data[0] = 0xc0 | e->data.control.channel;
        m.data[1] = e->data.control.value & 127;
        m.size = 2;
        break;
      default:
        m.size = 0;
        break;
      }
      if (m.size && !midi_queue_.push(m))
        midi_overflow_ = true;
      snd_seq_free_event(e);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
void AlsaBackend::run() noexcept {
  sched_param priority{};
  priority.sched_priority = 20;
  realtime_ = pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority) == 0;

  uint64_t frame = 0;
  bool panic = false;
  while (!stopping_.load()) {
    if (!transfer(capture_, input_.data(), true)) {
      if (stopping_)
        break;
      if (error_ != -EPIPE && error_ != -ESTRPIPE) {
        lost_ = true;
        break;
      }
      ++xruns_;
      panic = true;
      if (!prepare()) {
        lost_ = true;
        break;
      }
      continue;
    }
    uint64_t work_start = monotonic_ns();
    unsigned inbytes = snd_pcm_format_physical_width(in_format_) / 8,
             outbytes = snd_pcm_format_physical_width(out_format_) / 8;
    for (unsigned f = 0; f < settings_.period; f++)
      for (unsigned c = 0; c < 2; c++)
        in_[c][f] = pcm_decode(
            input_.data() + (size_t(f) * settings_.capture_channels +
                             (c ? settings_.in_right : settings_.in_left) - 1) *
                                inbytes,
            in_format_);
    unsigned count = 0;
    bool overflow = panic;
    panic = false;
    overflow = overflow || midi_overflow_.exchange(false);
    if (overflow) {
      JfxMidi discarded{};
      for (unsigned n = 0; n < 1024 && midi_queue_.pop(discarded); ++n) {
      }
    } else {
      while (count < events_.size() && midi_queue_.pop(events_[count]))
        ++count;
    }
    BlockView block{settings_.period,
                    {in_[0].data(), in_[1].data()},
                    {out_[0].data(), out_[1].data()},
                    events_.data(),
                    count,
                    {120, frame, 1, 0},
                    overflow};
    engine_->process(block);
    frame += settings_.period;
    std::fill(output_.begin(), output_.end(), 0);
    for (unsigned f = 0; f < settings_.period; f++)
      for (unsigned c = 0; c < 2; c++)
        pcm_encode(output_.data() +
                       (size_t(f) * settings_.playback_channels +
                        (c ? settings_.out_right : settings_.out_left) - 1) *
                           outbytes,
                   out_format_, out_[c][f]);
    work_time_.record(monotonic_ns() - work_start);
    if (!transfer(playback_, output_.data(), false)) {
      if (stopping_)
        break;
      if (error_ != -EPIPE && error_ != -ESTRPIPE) {
        lost_ = true;
        break;
      }
      ++xruns_;
      panic = true;
      if (!prepare()) {
        lost_ = true;
        break;
      }
    }
    // null has no hardware clock; pacing is exclusively for automated tests.
    if (null_)
      std::this_thread::sleep_for(std::chrono::microseconds(
          uint64_t(settings_.period) * 1000000 / settings_.rate));
  }
}
} // namespace nextfx
