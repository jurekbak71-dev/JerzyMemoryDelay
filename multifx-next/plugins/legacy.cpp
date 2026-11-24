#include "jfx.h"
#define JfxParameter LegacyParameter
#define JfxDescriptor LegacyDescriptor
#define JfxEntryPoint LegacyEntryPoint
#include "../../multifx2/api.h"
#undef JfxParameter
#undef JfxDescriptor
#undef JfxEntryPoint
#include <array>
#include <dlfcn.h>
#include <new>
#include <string>
namespace {
struct State {
    void *library = nullptr;
    const LegacyDescriptor *descriptor = nullptr;
    void *instance = nullptr;
    uint32_t max = 0;
    ~State() {
        if (instance)
            descriptor->destroy(instance);
        if (library)
            dlclose(library);
    }
};
void *create(double sr, uint32_t frames, const char *asset) {
    try {
        auto s = new State;
        s->max = frames;
        s->library = dlopen(asset, RTLD_NOW | RTLD_LOCAL);
        if (!s->library) {
            delete s;
            return nullptr;
        }
        auto entry = reinterpret_cast<LegacyEntryPoint>(dlsym(s->library, "jfx_plugin"));
        auto *d = entry ? entry() : nullptr;
        if (!d || d->abi_version != 2 || d->parameter_count > 48 || !d->create || !d->destroy ||
            !d->process || (d->parameter_count && !d->parameters)) {
            delete s;
            return nullptr;
        }
        s->descriptor = d;
        try {
            s->instance = d->create(sr);
        } catch (...) {
            delete s;
            return nullptr;
        }
        if (!s->instance) {
            delete s;
            return nullptr;
        }
        return s;
    } catch (...) {
        return nullptr;
    }
}
void destroy(void *p) { delete static_cast<State *>(p); }
void process(void *p, const JfxBlock *b) {
    auto &s = *static_cast<State *>(p);
    if (b->frames > s.max)
        return;
    for (uint32_t i = 0; i < b->frames; ++i) {
        float x[]{b->inputs[0][i], b->inputs[1][i]}, y[2]{};
        s.descriptor->process(s.instance, x, y, b->parameters, b->transport.bpm);
        b->outputs[0][i] = y[0];
        b->outputs[1][i] = y[1];
    }
}
// ABI2 cannot report DSP allocations. This reports only the known bridge object.
uint64_t bytes(void *) { return sizeof(State); }
struct Descriptor {
    std::array<std::string, 48> keys;
    std::array<JfxParameter, 48> parameters;
    JfxDescriptor d;
    Descriptor() {
        for (unsigned i = 0; i < 48; ++i) {
            keys[i] = "p" + std::to_string(i);
            parameters[i] = {keys[i].c_str(), keys[i].c_str(), 0, 1, .5f};
        }
        d = {JFX_MAGIC,
             sizeof(JfxDescriptor),
             3,
             0,
             "legacy",
             "1.0.0",
             "JFX ABI2 FX migration bridge",
             2,
             2,
             0,
             48,
             parameters.data(),
             0,
             0,
             create,
             destroy,
             process,
             nullptr,
             nullptr,
             nullptr,
             bytes};
    }
};
} // namespace
extern "C" JFX_EXPORT const JfxDescriptor *jfx_entry_v3() {
    static Descriptor d;
    return &d.d;
}
