#pragma once
#include <stdexcept>
#ifdef NEXT_ALLOC_GUARD
extern "C" void jfx_test_rt(int);
extern "C" void jfx_test_observe(void *);
extern "C" void jfx_test_reset();
extern "C" unsigned long long jfx_test_allocations();
extern "C" unsigned long long jfx_test_frees();
#endif
struct AudioScope {
  AudioScope() {
#ifdef NEXT_ALLOC_GUARD
    jfx_test_rt(1);
#endif
  }
  ~AudioScope() {
#ifdef NEXT_ALLOC_GUARD
    jfx_test_rt(0);
#endif
  }
};
struct AudioAudit {
  AudioAudit() {
#ifdef NEXT_ALLOC_GUARD
    {
      AudioScope scope;
      auto *p = new int(7);
      jfx_test_observe(p);
      delete p;
    }
    if (!jfx_test_allocations() || !jfx_test_frees())
      throw std::runtime_error("allocation guard calibration failed");
    jfx_test_reset();
#endif
  }
  void clean() const {
#ifdef NEXT_ALLOC_GUARD
    if (jfx_test_allocations() || jfx_test_frees())
      throw std::runtime_error("allocation/free in audio processing");
#endif
  }
};
template <class Fn> void audited_audio(Fn &&fn) {
  AudioScope scope;
  fn();
}
