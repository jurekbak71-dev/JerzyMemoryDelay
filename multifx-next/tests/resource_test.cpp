#include "session.hpp"
#include <fstream>
#include <iostream>
#include <malloc.h>
#include <stdexcept>
using namespace nextfx;
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            return 2;
        std::string dir = argv[1];
        std::string wav = dir + "/long.wav";
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
        w32(36 + 65536);
        f.write("WAVEfmt ", 8);
        w32(16);
        w16(1);
        w16(1);
        w32(44100);
        w32(88200);
        w16(2);
        w16(16);
        f.write("data", 4);
        w32(65536);
        w16(16384);
        for (int i = 1; i < 32768; ++i)
            w16(0);
        f.close();
        Engine engine(44100, 64);
        Session session(engine);
        float x[64]{}, y[64]{}, z[64]{};
        BlockView b{64, {x, x}, {y, z}, nullptr, 0, {120, 0, 0, 1}, false};
        auto apply = [&](const std::string &command) {
            auto r = session.command(command);
            if (r.rfind("OK", 0) != 0)
                throw std::runtime_error(r);
            engine.process(b);
            session.collect();
        };
        auto before = session.command("STATS");
        apply("ADD " + dir + "/cab/plugin.so " + wav);
        auto loaded = session.command("STATS");
        auto tracked = engine.diagnostics().owned_bytes;
        auto heapLoaded = mallinfo2();
        apply("MODE 1 UNLOADED");
        auto after = session.command("STATS");
        auto released = tracked - engine.diagnostics().owned_bytes;
        auto heapAfter = mallinfo2();
        if (released < 1024 * 1024)
            throw std::runtime_error("CAB buffers not released from prepared plan");
        std::cout << "Before: " << before << "\nLoaded: " << loaded << "\nUnloaded: " << after
                  << "\nReleased tracked capacity bytes: " << released
                  << "\nHeap mmap before/after: " << heapLoaded.hblkhd << "/" << heapAfter.hblkhd
                  << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
