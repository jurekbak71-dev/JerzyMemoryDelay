#include "dsp.h"
#include <jack/jack.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <algorithm>
#include <cmath>

namespace {
std::atomic<bool> running{true};
void stop(int) { running.store(false, std::memory_order_relaxed); }

struct App {
    jack_client_t* client{};
    jack_port_t* in{};
    jack_port_t* left{};
    jack_port_t* right{};
    Jerzy::Engine engine;
    Jerzy::Parameters params;

    static int process(jack_nframes_t frames, void* context) {
        auto* app = static_cast<App*>(context);
        auto* input = static_cast<const jack_default_audio_sample_t*>(jack_port_get_buffer(app->in, frames));
        auto* outL = static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(app->left, frames));
        auto* outR = static_cast<jack_default_audio_sample_t*>(jack_port_get_buffer(app->right, frames));
        if (!input || !outL || !outR) return 0;
        for (jack_nframes_t i = 0; i < frames; ++i) {
            const double sample = std::isfinite(input[i]) ? double(input[i]) : 0.0;
            const double dry[2]{sample, sample};
            double wet[2]{};
            app->engine.tick(dry, wet, 2, app->params);
            // Hard output guard for unexpectedly high feedback; do not alter the engine.
            outL[i] = float(std::clamp(wet[0], -1.0, 1.0));
            outR[i] = float(std::clamp(wet[1], -1.0, 1.0));
        }
        return 0;
    }
    static void shutdown(void*) { running.store(false, std::memory_order_relaxed); }
};

bool parseNumber(const char* arg, double& value) {
    try {
        const std::string s(arg);
        std::size_t used{};
        value = std::stod(s, &used);
        return used == s.size() && std::isfinite(value);
    } catch (...) { return false; }
}
void usage() {
    std::cout << "Usage: JerzyMemoryDelayJACK [--model 0..5] [--blend 0..1] "
                 "[--feedback 0..1] [--delay-ms 20..550] [--level 0..1]\n"
                 "Default model: 3 (Deluxe / MN). Connect ports using jack_connect.\n";
}
}

int main(int argc, char** argv) {
    App app;
    for (int i = 1; i < argc; i += 2) {
        if (std::string(argv[i]) == "--help") { usage(); return 0; }
        if (i + 1 >= argc) { usage(); return 2; }
        double value{};
        if (!parseNumber(argv[i + 1], value)) { usage(); return 2; }
        const std::string flag(argv[i]);
        if (flag == "--model" && value >= 0 && value <= 5 && std::floor(value) == value)
            app.params.set(Jerzy::Model, value / 5.0);
        else if (flag == "--blend" && value >= 0 && value <= 1)
            app.params.set(Jerzy::Blend, value);
        else if (flag == "--feedback" && value >= 0 && value <= 1)
            app.params.set(Jerzy::Feedback, value);
        else if (flag == "--level" && value >= 0 && value <= 1)
            app.params.set(Jerzy::Level, value);
        else if (flag == "--delay-ms" && value >= 20 && value <= 550)
            app.params.set(Jerzy::Delay, (value - 20.0) / 530.0);
        else { std::cerr << "Invalid option: " << flag << '\n'; usage(); return 2; }
    }
    jack_status_t status{};
    app.client = jack_client_open("JerzyMemoryDelay", JackNoStartServer, &status);
    if (!app.client) {
        std::cerr << "Cannot connect to JACK. Start jackd first.\n";
        return 1;
    }
    app.in = jack_port_register(app.client, "in_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    app.left = jack_port_register(app.client, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    app.right = jack_port_register(app.client, "out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!app.in || !app.left || !app.right) {
        std::cerr << "Cannot create JACK ports.\n";
        jack_client_close(app.client);
        return 1;
    }
    app.engine.prepare(jack_get_sample_rate(app.client));
    jack_set_process_callback(app.client, App::process, &app);
    jack_on_shutdown(app.client, App::shutdown, nullptr);
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    if (jack_activate(app.client)) {
        std::cerr << "Cannot activate JACK client.\n";
        jack_client_close(app.client);
        return 1;
    }
    std::cout << "Jerzy Memory Delay running at " << jack_get_sample_rate(app.client)
              << " Hz; MIDI and live controls will be added later.\n"
              << "Connect system:capture_1 -> JerzyMemoryDelay:in_1, "
              << "then JerzyMemoryDelay:out_1/2 -> system:playback_1/2.\n"
              << "Press Ctrl+C to stop.\n";
    while (running.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    jack_deactivate(app.client);
    jack_client_close(app.client);
}
