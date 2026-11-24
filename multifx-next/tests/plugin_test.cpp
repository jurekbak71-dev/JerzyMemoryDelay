// Missing block processing, timestamps, note-off or latency alignment must fail these checks.
#include "../../multifx4/ir_cab.h"
#include "plugin.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace nextfx;
void check(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "plugin directory argument");
        std::string dir = argv[1];
        auto gain = Instance::create(Library::open(dir + "/gain/plugin.so"), 44100, 64, "");
        float l[64], r[64], ol[64]{}, orr[64]{};
        for (int i = 0; i < 64; ++i) {
            l[i] = .25f;
            r[i] = -.5f;
        }
        const float *inputs[]{l, r};
        float *outputs[]{ol, orr};
        float p[]{.5f, 0.f};
        JfxBlock b{64, inputs, outputs, p, nullptr, 0, {120, 0, 0, 1}};
        gain->process(b);
        check(ol[5] == .125f && orr[63] == -.25f, "gain stereo audio");
        auto synth = Instance::create(Library::open(dir + "/synth/plugin.so"), 44100, 64, "");
        JfxMidi events[]{{8, 3, {0x90, 69, 127}}, {40, 3, {0x80, 69, 0}}};
        p[0] = .2f;
        b.inputs = nullptr;
        b.events = events;
        b.event_count = 2;
        synth->process(b);
        double energy = 0;
        for (int i = 0; i < 64; ++i) {
            check(std::isfinite(ol[i]), "finite instrument");
            if (i < 8 || i >= 40)
                check(ol[i] == 0, "sample accurate note boundary");
            else
                energy += std::abs(ol[i]);
        }
        check(energy > 1, "real instrument audio");
        JfxMidi cc[]{{0, 3, {0x90, 69, 127}}, {16, 3, {0xb0, 7, 0}}};
        b.events = cc;
        synth->process(b);
        for (int i = 16; i < 64; ++i)
            check(ol[i] == 0, "CC volume");
        JfxMidi pc[]{{0, 2, {0xc0, 1, 0}}, {0, 3, {0xb0, 7, 127}}, {32, 3, {0x90, 69, 0}}};
        b.events = pc;
        b.event_count = 3;
        synth->process(b);
        check(std::abs(ol[0]) > .001f, "program waveform");
        for (int i = 32; i < 64; ++i)
            check(ol[i] == 0, "velocity zero note off");
        for (auto name : {"/missing.so", "/bad.txt", "/wrong.so", "/noentry.so"}) {
            bool rejected = false;
            try {
                Library::open(dir + name);
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected, "invalid plugin rejection");
        }
        check(std::filesystem::exists(dir + "/invalid.so"), "invalid ABI fixture exists");
        for (auto kind : {"major", "short", "process", "nan", "ports", "count", "flags"}) {
            setenv("JFX_INVALID_CASE", kind, 1);
            bool rejected = false;
            try {
                Library::open(dir + "/invalid.so");
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected, "incompatible ABI descriptor rejected");
        }
        unsetenv("JFX_INVALID_CASE");
        auto again = Library::open(dir + "/gain/plugin.so");
        check(again == gain->library(), "shared library ownership");
        // WAV impulse with independently known h[0]=0.5 and 64-frame wet latency.
        std::string wav = dir + "/impulse.wav";
        std::ofstream f(wav, std::ios::binary);
        auto w16 = [&](unsigned x) {
            f.put(char(x));
            f.put(char(x >> 8));
        };
        auto w32 = [&](unsigned x) {
            w16(x);
            w16(x >> 16);
        };
        f.write("RIFF", 4);
        w32(36 + 256);
        f.write("WAVEfmt ", 8);
        w32(16);
        w16(1);
        w16(1);
        w32(44100);
        w32(88200);
        w16(2);
        w16(16);
        f.write("data", 4);
        w32(256);
        w16(16384);
        for (int i = 1; i < 128; ++i)
            w16(0);
        f.close();
        auto cab = Instance::create(Library::open(dir + "/cab/plugin.so"), 44100, 64, wav);
        check(cab->descriptor().latency_frames == 64, "CAB declared latency");
        std::string error;
        auto reference = mfx4::CabinetIR::load(wav, 44100, error);
        check(bool(reference), "reference IR");
        b.inputs = inputs;
        b.events = nullptr;
        b.event_count = 0;
        p[0] = 1;
        p[1] = 0;
        for (int block = 0; block < 4; ++block) {
            for (int i = 0; i < 64; ++i)
                l[i] = r[i] = (block == 0 && i == 0) ? 1.f : 0.f;
            cab->process(b);
            for (int i = 0; i < 64; ++i) {
                float x[]{l[i], r[i]}, y[2]{};
                reference->tick(x, y);
                check(std::abs(ol[i] - y[0]) < 1e-6, "CAB matches original wet");
            }
        }
        cab = Instance::create(Library::open(dir + "/cab/plugin.so"), 44100, 64, wav);
        p[0] = .5f;
        for (int block = 0; block < 3; ++block) {
            for (int i = 0; i < 64; ++i)
                l[i] = r[i] = (block == 0 && i == 0) ? 1.f : 0.f;
            cab->process(b);
            for (int i = 0; i < 64; ++i)
                check(std::abs(ol[i] - ((block == 1 && i == 0) ? .75f : 0.f)) < 1e-6,
                      "aligned dry wet impulse");
        }
        check(cab->resource_bytes() > 1000, "CAB reports owned memory");
        std::cout << "PASS plugin ABI, stereo FX, MIDI instrument, shared libraries, CAB reference "
                     "and dry alignment\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
