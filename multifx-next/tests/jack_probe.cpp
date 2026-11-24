// This is a real second JACK client: callbacks generate and capture actual server buffers.
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <thread>
#include <vector>
struct Probe {
    jack_client_t *client{};
    std::array<jack_port_t *, 2> send{}, receive{};
    jack_port_t *midi{}, *midi_extra{};
    bool overflow = false;
    std::atomic<unsigned> blocks{0};
    std::array<std::vector<float>, 2> captured;
    uint32_t frames = 0;
};
int audio(jack_nframes_t n, void *ptr) {
    auto &p = *static_cast<Probe *>(ptr);
    unsigned block = p.blocks.load(std::memory_order_relaxed);
    for (unsigned c = 0; c < 2; ++c) {
        auto *out = static_cast<float *>(jack_port_get_buffer(p.send[c], n));
        auto *in = static_cast<const float *>(jack_port_get_buffer(p.receive[c], n));
        for (unsigned i = 0; i < n; ++i) {
            out[i] = c ? -.3f : .2f;
            if (block < 512 && n == p.frames)
                p.captured[c][size_t(block) * n + i] = in[i];
        }
    }
    void *m = jack_port_get_buffer(p.midi, n);
    jack_midi_clear_buffer(m);
    void *extra = jack_port_get_buffer(p.midi_extra, n);
    jack_midi_clear_buffer(extra);
    if (p.overflow && block == 256) {
        size_t size = jack_midi_max_event_size(m);
        auto *data = jack_midi_event_reserve(m, 0, size);
        if (data) {
            std::fill_n(data, size, uint8_t(0));
            data[0] = 0xf0;
            data[size - 1] = 0xf7;
        }
        unsigned char off[]{0x80, 69, 0};
        jack_midi_event_write(extra, 1, off, 3);
    } else if (block == 64 || block == 256) {
        unsigned char message[]{uint8_t(block == 64 ? 0x90 : 0x80), 69, 100};
        jack_midi_event_write(m, 0, message, 3);
    }
    p.blocks.store(block + 1, std::memory_order_release);
    return 0;
}
int main(int argc, char **) {
    Probe p;
    p.overflow = argc > 1;
    jack_status_t status{};
    p.client = jack_client_open("NextProbe", JackNoStartServer, &status);
    if (!p.client)
        return 1;
    p.frames = jack_get_buffer_size(p.client);
    for (auto &c : p.captured)
        c.resize(size_t(p.frames) * 512);
    for (unsigned c = 0; c < 2; ++c) {
        p.send[c] = jack_port_register(p.client, c ? "send_2" : "send_1", JACK_DEFAULT_AUDIO_TYPE,
                                       JackPortIsOutput, 0);
        p.receive[c] = jack_port_register(p.client, c ? "receive_2" : "receive_1",
                                          JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        if (!p.send[c] || !p.receive[c])
            return 2;
    }
    p.midi = jack_port_register(p.client, "midi", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
    p.midi_extra =
        jack_port_register(p.client, "midi_extra", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
    if (!p.midi || !p.midi_extra)
        return 2;
    jack_set_process_callback(p.client, audio, &p);
    if (jack_activate(p.client))
        return 2;
    for (unsigned c = 0; c < 2; ++c) {
        const char *in = c ? "JerzyMultiFXNext:in_2" : "JerzyMultiFXNext:in_1";
        const char *out = c ? "JerzyMultiFXNext:out_2" : "JerzyMultiFXNext:out_1";
        if (jack_connect(p.client, jack_port_name(p.send[c]), in) ||
            jack_connect(p.client, out, jack_port_name(p.receive[c])))
            return 3;
    }
    if (jack_connect(p.client, jack_port_name(p.midi), "JerzyMultiFXNext:midi_in"))
        return 3;
    if (jack_connect(p.client, jack_port_name(p.midi_extra), "JerzyMultiFXNext:midi_in"))
        return 3;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (p.blocks.load(std::memory_order_acquire) < 512 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    jack_deactivate(p.client);
    jack_client_close(p.client);
    if (p.blocks < 512)
        return 4;
    double mean[2]{}, delta = 0, tail = 0;
    bool finite = true;
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned block = 320; block < 480; ++block)
            for (unsigned f = 0; f < p.frames; ++f) {
                float x = p.captured[c][size_t(block) * p.frames + f];
                mean[c] += x;
                finite = finite && std::isfinite(x);
            }
    for (auto &m : mean)
        m /= double(160 * p.frames);
    for (unsigned block = 96; block < 224; ++block)
        for (unsigned f = 0; f < p.frames; ++f)
            delta += std::abs(p.captured[0][size_t(block) * p.frames + f] - mean[0]);
    delta /= double(128 * p.frames);
    for (unsigned block = 320; block < 480; ++block)
        for (unsigned f = 0; f < p.frames; ++f)
            tail += std::abs(p.captured[0][size_t(block) * p.frames + f] - mean[0]);
    tail /= double(160 * p.frames);
    std::cout << "{\"tail_variation\":" << tail << ",\"left\":" << mean[0]
              << ",\"right\":" << mean[1] << ",\"note_energy\":" << delta
              << ",\"finite\":" << (finite ? "true" : "false") << "}\n";
    return 0;
}
