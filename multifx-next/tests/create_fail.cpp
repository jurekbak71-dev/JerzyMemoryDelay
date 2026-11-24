#include "jfx.h"
namespace {
void *create(double, uint32_t, const char *) { return nullptr; }
void destroy(void *) {}
void process(void *, const JfxBlock *) {}
JfxDescriptor d{JFX_MAGIC,
                sizeof(JfxDescriptor),
                3,
                0,
                "create_fail",
                "1",
                "Expected failed factory",
                2,
                2,
                0,
                0,
                nullptr,
                0,
                0,
                create,
                destroy,
                process,
                nullptr,
                nullptr,
                nullptr,
                nullptr};
} // namespace
extern "C" JFX_EXPORT const JfxDescriptor *jfx_entry_v3() { return &d; }
