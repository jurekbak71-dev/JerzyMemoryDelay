#include "engine.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace nextfx;
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            return 2;
        Engine engine(192000, 64);
        SlotSpec s;
        s.id = 1;
        s.instance =
            Instance::create(Library::open(std::string(argv[1]) + "/overload.so"), 192000, 64, "");
        GraphSpec g;
        g.slots = {s};
        g.edges = {{0, 1}, {1, -1}};
        if (!engine.publish(g))
            throw std::runtime_error("publication failed");
        float x[1]{.2f}, y[1]{}, z[1]{};
        BlockView b{1, {x, x}, {y, z}, nullptr, 0, {120, 0, 1, 1}, false};
        engine.process(b);
        engine.collect();
        auto d = engine.diagnostics();
        if (d.deadline_misses != 1 || d.slot_total_ns[0] < 5209 || !std::isfinite(y[0]) ||
            std::abs(y[0]) < .001f)
            throw std::runtime_error("overload not detected or audio invalid");
        std::cout << "PASS bounded heavy DSP overload detected: callback_ns=" << d.total_ns
                  << " budget_ns=5208 (192kHz, 1 frame); finite actual audio\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
