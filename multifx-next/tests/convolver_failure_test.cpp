#include "pffft_cab.hpp"
#include <cmath>
#include <iostream>
#include <new>
#include <stdexcept>
extern "C" void jfx_test_fail_after(long);
int main() {
    try {
        const std::vector<float> original{1.f}, replacement(1024, .01f);
        for (long point : {0, 1, 2, 3}) {
            nextfx::PartitionedConvolver live;
            live.prepare(original);
            for (unsigned i = 0; i < 16; ++i)
                live.tick(i == 0 ? 1.f : 0.f);
            std::cerr << "Injecting allocation failure " << point << '\n';
            jfx_test_fail_after(point);
            bool failed = false;
            try {
                live.prepare(replacement);
            } catch (const std::bad_alloc &) {
                failed = true;
            }
            jfx_test_fail_after(-1);
            if (!failed)
                throw std::runtime_error("injected allocation failure was not handled");
            for (unsigned i = 16; i < 160; ++i)
                if (std::abs(live.tick(0) - (i == 64 ? 1.f : 0.f)) > 1e-6)
                    throw std::runtime_error("failed prepare changed active convolution state");
        }
        std::cout << "PASS setup struct, twiddle storage and both aligned spectrum allocation "
                     "failures preserve live state\n";
        return 0;
    } catch (const std::exception &e) {
        jfx_test_fail_after(-1);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
