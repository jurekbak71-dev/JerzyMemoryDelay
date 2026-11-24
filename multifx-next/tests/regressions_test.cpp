#include "session.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace nextfx;
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    std::string dir = argv[1];
    int failures = 0;
    for (int which : {0, 1})
        try {
            Engine e(44100, 64);
            Session s(e);
            float x[64]{}, y[64]{}, z[64]{};
            BlockView b{64, {x, x}, {y, z}, nullptr, 0, {120, 0, 0, 1}, false};
            auto step = [&] {
                e.process(b);
                s.collect();
            };
            auto cmd = [&](const std::string &text) {
                auto r = s.command(text);
                check(r.rfind("OK", 0) == 0, r.c_str());
                step();
            };
            if (which == 0) {
                cmd("ADD " + dir + "/synth/plugin.so");
                JfxMidi on{0, 3, {0x90, 69, 127}}, off{0, 3, {0x80, 69, 0}};
                b.events = &on;
                b.event_count = 1;
                step();
                b.events = nullptr;
                b.event_count = 0;
                cmd("ROUTE 0 1 2");
                b.events = &off;
                b.event_count = 1;
                step();
                b.events = nullptr;
                b.event_count = 0;
                for (int i = 0; i < 10; ++i) {
                    step();
                    for (float v : y)
                        check(v == 0, "removed MIDI route must release old channel voices");
                }
            } else {
                cmd("ADD " + dir + "/gain/plugin.so");
                cmd("ADD " + dir + "/gain/plugin.so");
                cmd("MODE 2 BYPASS");
                auto sleeping_calls = Json::parse(s.command("STATS").substr(3)).at("slot_calls")[1].get<uint64_t>();
                for (int i = 0; i < 5; ++i)
                    step();
                cmd("MOVE 2 0");
                auto stats = Json::parse(s.command("STATS").substr(3));
                check(stats.at("slot_ids")[0] == 2, "statistics have stable slot IDs");
                check(stats.at("slot_calls")[0].get<uint64_t>() == sleeping_calls,
                      "bypassed statistics remain attached to the instance and stop increasing after ramp");
            }
            std::cout << "PASS review regression " << which << '\n';
        } catch (const std::exception &ex) {
            std::cerr << "FAIL review regression " << which << ": " << ex.what() << '\n';
            ++failures;
        }
    return failures ? 1 : 0;
}
