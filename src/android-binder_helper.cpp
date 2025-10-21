// Minimal C++ helper to initialize Binder thread pool
// This needs to be compiled as a separate shared library with proper C++
// support

#if defined(__ANDROID__) && defined(USE_BINDER_INIT)

// These headers are from Android platform, not NDK
// We'll use weak symbols to avoid link-time dependencies
namespace android {
class ProcessState;
}

// Weak declarations - will resolve at runtime if libbinder.so is available
extern "C" {
// Declare as weak symbols so the binary still links even without libbinder
__attribute__((weak)) android::ProcessState* _ZN7android12ProcessState4selfEv();
__attribute__((weak)) void _ZN7android12ProcessState15startThreadPoolEv(
    android::ProcessState*);
__attribute__((weak)) void
_ZN7android12ProcessState27setThreadPoolMaxThreadCountEm(android::ProcessState*,
                                                         unsigned long);
}

extern "C" int android_init_binder_thread_pool() {
  // Check if symbols are available
  if (!_ZN7android12ProcessState4selfEv ||
      !_ZN7android12ProcessState15startThreadPoolEv ||
      !_ZN7android12ProcessState27setThreadPoolMaxThreadCountEm) {
    return 0;  // Symbols not available
  }

  try {
    android::ProcessState* ps = _ZN7android12ProcessState4selfEv();
    if (!ps) {
      return 0;
    }

    _ZN7android12ProcessState27setThreadPoolMaxThreadCountEm(ps, 1);
    _ZN7android12ProcessState15startThreadPoolEv(ps);

    return 1;  // Success
  } catch (...) {
    return 0;  // Failed
  }
}

#else

extern "C" int android_init_binder_thread_pool() {
  return 0;  // Not Android or disabled
}

#endif
