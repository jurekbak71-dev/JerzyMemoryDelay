// Test-only glibc interposition: observes C allocation too, including code in loaded .so.
#include <atomic>
#include <cstddef>
extern "C" {
void *__libc_malloc(size_t);
void *__libc_calloc(size_t, size_t);
void *__libc_realloc(void *, size_t);
void __libc_free(void *);
void *__libc_memalign(size_t, size_t);
}
namespace {
thread_local bool audio = false;
thread_local long fail_after = -1;
bool fail() {
    if (fail_after < 0)
        return false;
    if (fail_after == 0) {
        fail_after = -1;
        return true;
    }
    --fail_after;
    return false;
}
std::atomic<unsigned long long> allocations{0}, frees{0};
} // namespace
extern "C" void jfx_test_rt(int enabled) { audio = enabled != 0; }
extern "C" unsigned long long jfx_test_allocations() { return allocations.load(); }
extern "C" unsigned long long jfx_test_frees() { return frees.load(); }
extern "C" void jfx_test_fail_after(long remaining) { fail_after = remaining; }
extern "C" void *malloc(size_t n) {
    if (fail())
        return nullptr;
    if (audio)
        ++allocations;
    return __libc_malloc(n);
}
extern "C" void *calloc(size_t n, size_t s) {
    if (fail())
        return nullptr;
    if (audio)
        ++allocations;
    return __libc_calloc(n, s);
}
extern "C" void *realloc(void *p, size_t n) {
    if (fail())
        return nullptr;
    if (audio)
        ++allocations;
    return __libc_realloc(p, n);
}
extern "C" void free(void *p) {
    if (audio && p)
        ++frees;
    __libc_free(p);
}
extern "C" void *aligned_alloc(size_t alignment, size_t size) {
    if (fail())
        return nullptr;
    if (audio)
        ++allocations;
    return __libc_memalign(alignment, size);
}
extern "C" int posix_memalign(void **p, size_t alignment, size_t size) {
    if (fail())
        return 12;
    if (audio)
        ++allocations;
    void *q = __libc_memalign(alignment, size);
    if (!q)
        return 12;
    *p = q;
    return 0;
}

extern "C" void jfx_test_observe(void *pointer) { asm volatile("" : : "g"(pointer) : "memory"); }
extern "C" void jfx_test_reset() {
    allocations.store(0);
    frees.store(0);
}
