// A partially applied preset, stale bypass voice, wrong chain move or retained unloaded instance
// fails here.
#include "session.hpp"
#include <iostream>
#include <stdexcept>
using namespace nextfx;
void check(bool v, const char *text) {
    if (!v)
        throw std::runtime_error(text);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "plugin dir");
        std::string dir = argv[1];
        Engine e(44100, 64);
        Session s(e);
        float x[64], y[64]{}, z[64]{};
        for (auto &v : x)
            v = .2f;
        BlockView b{64, {x, x}, {y, z}, nullptr, 0, {120, 0, 0, 1}, false};
        auto step = [&] {
            e.process(b);
            s.collect();
        };
        auto command = [&](std::string line) {
            auto r = s.command(line);
            check(r.rfind("ERR", 0) != 0, r.c_str());
            step();
            return r;
        };
        check(command("ADD " + dir + "/gain/plugin.so") == "OK 1", "slot ID");
        command("SET 1 gain 0.5");
        check(y[0] == .1f, "gain command");
        command("ADD " + dir + "/gain/plugin.so");
        command("SET 2 offset 0.25");
        check(std::abs(y[0] - .35f) < 1e-6, "chain before move");
        command("MOVE 2 0");
        check(std::abs(y[0] - .225f) < 1e-6, "MOVE reconnects chain");
        std::string preset = dir + "/session.json";
        command("SAVE " + preset);
        command("MODE 1 UNLOADED");
        check(s.command("SET 1 gain 3").rfind("ERR", 0) == 0, "unloaded parameter range validated");
        check(std::abs(y[63] - .45f) < 1e-6, "unloaded dry");
        command("MODE 1 ACTIVE");
        check(std::abs(y[63] - .225f) < 1e-6, "reactivation restores parameters");
        command("REMOVE 2");
        check(y[0] == .1f, "remove");
        command("LOAD " + preset);
        check(std::abs(y[0] - .225f) < 1e-6, "preset restored audio and ordering");
        check(s.command("ADD " + dir + "/missing.so").rfind("ERR", 0) == 0, "bad plugin error");
        step();
        check(std::abs(y[0] - .225f) < 1e-6, "failed edit preserves signal");
        check(s.command("SET 1 gain nan").rfind("ERR", 0) == 0, "nonfinite rejected");
        command("MODE 1 BYPASS");
        step();
        command("MODE 1 ACTIVE");
        auto extreme = s.snapshot();
        extreme["slots"][0]["id"] = 2147483647;
        bool extreme_rejected = false;
        try {
            s.restore(extreme);
        } catch (const std::exception &) {
            extreme_rejected = true;
        }
        check(extreme_rejected, "extreme saved slot ID rejected");
        auto snapshot = s.snapshot();
        snapshot["slots"][0]["path"] = dir + "/missing.so";
        bool rejected = false;
        try {
            s.restore(snapshot);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "bad preset rejected transactionally");
        step();
        check(std::abs(y[0] - .225f) < 1e-6, "failed preset preserved live");
        command("CLEAR");
        auto weak = std::weak_ptr<Library>(Library::open(dir + "/gain/plugin.so"));
        check(weak.expired(), "last instance releases library");
        command("ADD " + dir + "/synth/plugin.so");
        command("ROUTE 0 3");
        JfxMidi note{0, 3, {0x90, 69, 127}};
        b.events = &note;
        b.event_count = 1;
        step();
        check(y[4] > .2f, "instrument sums with input");
        b.events = nullptr;
        b.event_count = 0;
        command("MODE 3 BYPASS");
        command("MODE 3 ACTIVE");
        check(y[4] == .2f, "bypass reactivation has no stuck voice");
        std::cout << "PASS session commands, reconnect MOVE, modes, preset atomic rollback, "
                     "library release and clean instrument reactivation\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
