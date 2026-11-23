#pragma once
#include "jfx.h"
#include <atomic>
#include <memory>
#include <string>
namespace nextfx {
class Library {
    void *handle_{};
    const JfxDescriptor *descriptor_{};
    std::string path_;
    explicit Library(std::string path);

  public:
    static std::shared_ptr<Library> open(const std::string &path);
    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
    const JfxDescriptor &descriptor() const noexcept { return *descriptor_; }
    const std::string &path() const noexcept { return path_; }
};
struct InstanceStatistics {
    uint64_t calls = 0, total_ns = 0, max_ns = 0;
};
class Instance {
    std::shared_ptr<Library> library_;
    void *instance_{};
    std::atomic<uint64_t> calls_{0}, total_ns_{0}, max_ns_{0};
    Instance(std::shared_ptr<Library> lib, double sr, uint32_t frames, const std::string &asset);

  public:
    static std::shared_ptr<Instance> create(std::shared_ptr<Library>, double, uint32_t,
                                            const std::string &);
    ~Instance();
    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;
    void process(const JfxBlock &b) noexcept;
    InstanceStatistics statistics() const noexcept;
    const JfxDescriptor &descriptor() const noexcept { return library_->descriptor(); }
    const std::shared_ptr<Library> &library() const noexcept { return library_; }
    uint64_t resource_bytes() const noexcept {
        return descriptor().resource_bytes ? descriptor().resource_bytes(instance_) : 0;
    }
};
} // namespace nextfx
