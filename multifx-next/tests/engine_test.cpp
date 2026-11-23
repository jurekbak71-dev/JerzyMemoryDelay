// Removing synchronization, bypass skipping, compensation or routing changes real output here.
#include "engine.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#ifdef NEXT_ALLOC_GUARD
extern "C" void jfx_test_rt(int);
extern "C" void jfx_test_observe(void *);
extern "C" void jfx_test_reset();
extern "C" unsigned long long jfx_test_allocations();
extern "C" unsigned long long jfx_test_frees();
#else
void jfx_test_rt(int) {}
#endif
using namespace nextfx;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "plugins path");
        std::string dir = argv[1];
#ifdef NEXT_ALLOC_GUARD
        jfx_test_rt(1);
        auto *calibration = new int(7);
        jfx_test_observe(calibration);
        delete calibration;
        jfx_test_rt(0);
        check(jfx_test_allocations() > 0 && jfx_test_frees() > 0,
              "allocation audit observes real C++ allocation/free");
        jfx_test_reset();
#endif
        Engine e(44100, 64);
        float l[128]{}, r[128]{}, ol[128]{}, orr[128]{};
        for (int i = 0; i < 64; ++i) {
            l[i] = .2f;
            r[i] = -.4f;
        }
        BlockView b{64, {l, r}, {ol, orr}, nullptr, 0, {120, 0, 0, 1}, false};
        auto process = [&] {
            jfx_test_rt(1);
            e.process(b);
            jfx_test_rt(0);
        };
        process();
        check(ol[5] == .2f && orr[30] == -.4f, "stereo passthrough");
        auto slot = [&](int id, const std::string &name) {
            SlotSpec s;
            s.id = id;
            s.mode = Mode::Active;
            s.instance =
                Instance::create(Library::open(dir + "/" + name + "/plugin.so"), 44100, 64, "");
            auto &d = s.instance->descriptor();
            s.inputs = d.audio_inputs;
            s.latency = d.latency_frames;
            s.midi = (d.flags & JFX_MIDI_INPUT) != 0;
            for (unsigned i = 0; i < d.parameter_count; ++i)
                s.parameters.push_back(d.parameters[i].initial);
            return s;
        };
        auto a = slot(1, "gain"), c = slot(2, "gain");
        a.parameters = {.5f, 0};
        c.parameters = {1, .25f};
        GraphSpec g;
        g.slots = {a, c};
        g.edges = {{0, 1}, {1, 2}, {2, -1}};
        check(e.publish(g), "publish graph");
        check(!e.publish(g), "bounded busy");
        process();
        check(e.collect(), "acknowledged");
        check(std::abs(ol[0] - .35f) < 1e-6 && std::abs(orr[0] - .05f) < 1e-6, "serial signal");
        g.edges = {{0, 2}, {2, 1}, {1, -1}};
        check(e.publish(g), "reorder");
        process();
        e.collect();
        check(std::abs(ol[0] - .225f) < 1e-6, "reordered graph signal");
        g.edges = {{0, 1}, {0, 2}, {1, -1}, {2, -1}};
        check(e.publish(g), "parallel");
        process();
        e.collect();
        check(std::abs(ol[0] - .55f) < 1e-6, "parallel sum");
        g.slots[0].mode = Mode::Bypass;
        check(e.publish(g), "bypass");
        process();
        e.collect();
        auto before = e.diagnostics().slot_calls[0];
        process();
        check(e.diagnostics().slot_calls[0] == before, "bypass no DSP");
        check(std::abs(ol[0] - .65f) < 1e-6, "bypass dry audio");
        g.slots[0].mode = Mode::Unloaded;
        g.slots[0].instance.reset();
        check(e.publish(g), "unload");
        process();
        e.collect();
        check(std::abs(ol[0] - .65f) < 1e-6, "unloaded route");
        auto inst = slot(3, "synth");
        GraphSpec music;
        music.slots = {inst, a};
        music.slots[1].parameters = {.5f, 0};
        music.edges = {{0, 1}, {3, 1}, {1, -1}};
        music.midi_routes = {{0, 3}};
        JfxMidi note{0, 3, {0x90, 69, 127}};
        b.events = &note;
        b.event_count = 1;
        check(e.publish(music), "instrument and FX");
        process();
        e.collect();
        check(ol[4] > .1f, "instrument mixed through FX");
        b.events = nullptr;
        b.event_count = 0;
        b.midi_overflow = true;
        process();
        b.midi_overflow = false;
        process();
        check(std::abs(ol[4] - .1f) < 1e-6, "overflow panic ends voices");
        GraphSpec invalid = music;
        invalid.edges.push_back({1, 1});
        bool rejected = false;
        try {
            e.publish(invalid);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "cycle rejected");
        process();
        check(std::abs(ol[4] - .1f) < 1e-6, "invalid graph preserves audio");
        // A wrapped channel index must be rejected before it can index audio buffers.
        GraphSpec badmap = music;
        badmap.edges[0].source_channel = 0xffffffffu;
        rejected = false;
        try {
            e.publish(badmap);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "wrapped audio channel rejected");
        // Compare delayed CAB branch and dry branch: both impulses at sample 64.
        auto ir = slot(4, "gain");
        ir.instance = Instance::create(Library::open(dir + "/cab/plugin.so"), 44100, 64,
                                       dir + "/impulse.wav");
        ir.parameters = {1};
        ir.latency = 64;
        ir.inputs = 2;
        GraphSpec aligned;
        aligned.slots = {ir};
        aligned.edges = {{0, -1}, {0, 4}, {4, -1}};
        check(e.publish(aligned), "compensated graph");
        for (int block = 0; block < 3; ++block) {
            for (int i = 0; i < 64; ++i)
                l[i] = r[i] = (block == 0 && i == 0) ? 1.f : 0.f;
            process();
            for (int i = 0; i < 64; ++i)
                check(std::abs(ol[i] - ((block == 1 && i == 0) ? 1.5f : 0.f)) < 1e-6,
                      "graph latency compensation");
        }
        e.collect();
        b.frames = 65;
        process();
        check(e.diagnostics().oversize_blocks == 1, "oversize detected");
        b.frames = 64;

        // Real concurrent reader/controller, bounded pending plans, repeated shared library
        // ownership.
        {
            Engine stress(44100, 64);
            std::atomic<bool> stop{false};
            std::thread audio([&] {
                float x[64]{}, y[64]{}, z[64]{};
                BlockView v{64, {x, x}, {y, z}, nullptr, 0, {120, 0, 0, 1}, false};
                while (!stop.load()) {
                    jfx_test_rt(1);
                    stress.process(v);
                    jfx_test_rt(0);
                }
            });
            for (int i = 0; i < 2000; ++i) {
                GraphSpec spec;
                if (i % 3) {
                    auto n = slot(1, "gain");
                    n.parameters = {.5f, 0};
                    spec.slots = {std::move(n)};
                    spec.edges = {{0, 1}, {1, -1}};
                }
                while (!stress.publish(spec)) {
                    stress.collect();
                    std::this_thread::yield();
                }
                while (!stress.collect())
                    std::this_thread::yield();
            }
            stop = true;
            audio.join();
            GraphSpec pending;
            check(stress.publish(pending), "pending shutdown");
        }

#ifdef NEXT_ALLOC_GUARD
        check(jfx_test_allocations() == 0 && jfx_test_frees() == 0,
              "no C allocation/free on audio");
        std::cout << "PASS audio allocation audit: C and C++ allocation/free calls = 0\n";
#endif
        std::cout << "PASS graph audio/MIDI, bypass, compensation, bounded exchange and 2000 "
                     "concurrent changes\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
