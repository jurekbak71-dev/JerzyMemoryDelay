#include "plugin.hpp"
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <time.h>
namespace nextfx {
namespace {
bool key(const char *s) {
    if (!s || !*s || std::strlen(s) > 128)
        return false;
    for (; *s; ++s)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '.' ||
              *s == '-'))
            return false;
    return true;
}
void elf(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    Elf64_Ehdr h{};
    f.read(reinterpret_cast<char *>(&h), sizeof h);
#if defined(__aarch64__)
    constexpr int machine = EM_AARCH64;
#elif defined(__x86_64__)
    constexpr int machine = EM_X86_64;
#else
#error Unsupported architecture
#endif
    if (!f || std::memcmp(h.e_ident, ELFMAG, SELFMAG) || h.e_ident[EI_CLASS] != ELFCLASS64 ||
        h.e_ident[EI_DATA] != ELFDATA2LSB || h.e_type != ET_DYN || h.e_machine != machine)
        throw std::runtime_error("incompatible ELF plugin: " + path);
}
} // namespace
Library::Library(std::string path) : path_(std::move(path)) {
    elf(path_);
    handle_ = dlopen(path_.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        const char *err = dlerror();
        throw std::runtime_error(err ? err : "dlopen failed");
    }
    try {
        auto entry = reinterpret_cast<JfxEntryV3>(dlsym(handle_, "jfx_entry_v3"));
        const auto *d = entry ? entry() : nullptr;
        if (!d || d->magic != JFX_MAGIC || d->struct_size < sizeof(JfxDescriptor) ||
            d->abi_major != JFX_MAJOR)
            throw std::runtime_error("incompatible JFX ABI");
        if (!key(d->id) || !d->name || !d->version ||
            (d->audio_inputs != 0 && d->audio_inputs != 2) || d->audio_outputs != 2 ||
            d->parameter_count > 48 || d->latency_frames > 131072 || !d->create || !d->destroy ||
            !d->process || (d->parameter_count && !d->parameters) ||
            (d->flags & ~(JFX_MIDI_INPUT | JFX_STATE)))
            throw std::runtime_error("invalid plugin descriptor");
        if ((d->flags & JFX_STATE) && (!d->state_size || !d->save_state || !d->load_state))
            throw std::runtime_error("incomplete state API");
        for (uint32_t i = 0; i < d->parameter_count; ++i) {
            const auto &p = d->parameters[i];
            if (!key(p.key) || !p.name || !std::isfinite(p.minimum) || !std::isfinite(p.maximum) ||
                !std::isfinite(p.initial) || p.minimum > p.maximum || p.initial < p.minimum ||
                p.initial > p.maximum)
                throw std::runtime_error("invalid plugin parameter");
            for (uint32_t j = 0; j < i; ++j)
                if (!std::strcmp(p.key, d->parameters[j].key))
                    throw std::runtime_error("duplicate parameter key");
        }
        descriptor_ = d;
    } catch (...) {
        dlclose(handle_);
        handle_ = nullptr;
        throw;
    }
}
std::shared_ptr<Library> Library::open(const std::string &path) {
    // Single controller only. Weak registry does not keep unused code mapped.
    static std::map<std::string, std::weak_ptr<Library>> registry;
    const auto canonical = std::filesystem::canonical(path).string();
    for (auto it = registry.begin(); it != registry.end();)
        if (it->second.expired())
            it = registry.erase(it);
        else
            ++it;
    if (auto it = registry.find(canonical); it != registry.end())
        if (auto lib = it->second.lock())
            return lib;
    auto lib = std::shared_ptr<Library>(new Library(canonical));
    registry[canonical] = lib;
    return lib;
}
Library::~Library() {
    if (handle_)
        dlclose(handle_);
}
Instance::Instance(std::shared_ptr<Library> lib, double sr, uint32_t frames,
                   const std::string &asset)
    : library_(std::move(lib)) {
    if (!std::isfinite(sr) || sr < 8000 || sr > 192000 || frames == 0 || frames > 8192)
        throw std::runtime_error("invalid plugin preparation");
    instance_ = descriptor().create(sr, frames, asset.c_str());
    if (!instance_)
        throw std::runtime_error("plugin create failed: " + std::string(descriptor().id));
}
std::shared_ptr<Instance> Instance::create(std::shared_ptr<Library> lib, double sr, uint32_t frames,
                                           const std::string &asset) {
    if (!lib)
        throw std::runtime_error("missing library");
    return std::shared_ptr<Instance>(new Instance(std::move(lib), sr, frames, asset));
}
void Instance::process(const JfxBlock &block) noexcept {
    timespec before{}, after{};
    clock_gettime(CLOCK_MONOTONIC, &before);
    descriptor().process(instance_, &block);
    clock_gettime(CLOCK_MONOTONIC, &after);
    uint64_t elapsed = (uint64_t(after.tv_sec) * 1000000000 + uint64_t(after.tv_nsec)) -
                       (uint64_t(before.tv_sec) * 1000000000 + uint64_t(before.tv_nsec));
    calls_.fetch_add(1, std::memory_order_relaxed);
    total_ns_.fetch_add(elapsed, std::memory_order_relaxed);
    if (elapsed > max_ns_.load(std::memory_order_relaxed))
        max_ns_.store(elapsed, std::memory_order_relaxed);
}
InstanceStatistics Instance::statistics() const noexcept {
    return {calls_.load(std::memory_order_relaxed), total_ns_.load(std::memory_order_relaxed),
            max_ns_.load(std::memory_order_relaxed)};
}
Instance::~Instance() {
    if (instance_)
        descriptor().destroy(instance_);
}
} // namespace nextfx
