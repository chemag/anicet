// Minimal shim for android::ProcessState
// The C++ Binder API is not part of the NDK, but libbinder.so is available on all Android devices
// This header declares just the functions we need to initialize the Binder thread pool

#ifndef PROCESSSTATE_SHIM_H
#define PROCESSSTATE_SHIM_H

#ifdef __ANDROID__

namespace android {

// Forward declaration - minimal interface we need
class ProcessState {
public:
    static ProcessState* self();
    void startThreadPool();
    void setThreadPoolMaxThreadCount(size_t maxThreads);
};

}  // namespace android

#endif  // __ANDROID__
#endif  // PROCESSSTATE_SHIM_H
