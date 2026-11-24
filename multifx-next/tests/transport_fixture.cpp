#include "jfx.h"
namespace {
int storage = 0;
void *create(double, uint32_t, const char *) { return &storage; }
void destroy(void *) {}
void process(void *, const JfxBlock *b) {
  for (unsigned c = 0; c < 2; c++)
    for (uint32_t i = 0; i < b->frames; i++)
      b->outputs[c][i] = float(b->transport.bpm / 300.);
}
JfxDescriptor d{JFX_MAGIC,
                sizeof(JfxDescriptor),
                3,
                0,
                "transport_fixture",
                "1",
                "BPM observation fixture",
                0,
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
