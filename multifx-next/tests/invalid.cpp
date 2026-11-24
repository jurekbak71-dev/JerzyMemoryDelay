// Deliberately malformed test fixture; never shipped as a usable plugin.
#include "jfx.h"
#include <cstdlib>
#include <cstring>
#include <limits>
namespace {
void *create(double, uint32_t, const char *) { return nullptr; }
void destroy(void *) {}
void process(void *, const JfxBlock *) {}
} // namespace
extern "C" JFX_EXPORT const JfxDescriptor *jfx_entry_v3() {
    static JfxParameter p{"value", "Value", 0, 1, .5f};
    static JfxDescriptor d;
    p.initial = .5f;
    d = {JFX_MAGIC,
         sizeof(JfxDescriptor),
         3,
         0,
         "invalid",
         "1.0.0",
         "Invalid fixture",
         2,
         2,
         0,
         1,
         &p,
         0,
         0,
         create,
         destroy,
         process,
         nullptr,
         nullptr,
         nullptr,
         nullptr};
    const char *kind = std::getenv("JFX_INVALID_CASE");
    if (!kind)
        return nullptr;
    if (!std::strcmp(kind, "major"))
        d.abi_major = 99;
    if (!std::strcmp(kind, "short"))
        d.struct_size = 16;
    if (!std::strcmp(kind, "process"))
        d.process = nullptr;
    if (!std::strcmp(kind, "nan"))
        p.initial = std::numeric_limits<float>::quiet_NaN();
    if (!std::strcmp(kind, "ports"))
        d.audio_outputs = 3;
    if (!std::strcmp(kind, "count"))
        d.parameter_count = 49;
    if (!std::strcmp(kind, "flags"))
        d.flags = 128;
    return &d;
}
