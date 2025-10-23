// Binder thread pool initialization via IPCThreadState
// Uses simpler approach to avoid ProcessState::init() crashes

#ifndef BINDER_INIT_H
#define BINDER_INIT_H

#ifdef __ANDROID__

#include <dlfcn.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>

// Get timestamp in seconds since start (matches main code)
static double binder_get_timestamp_s() {
  static struct timeval start_time = {0, 0};
  struct timeval now;
  gettimeofday(&now, nullptr);

  // Initialize start time on first call
  if (start_time.tv_sec == 0) {
    start_time = now;
  }

  double elapsed = (now.tv_sec - start_time.tv_sec) +
                   (now.tv_usec - start_time.tv_usec) / 1000000.0;
  return elapsed;
}

// Logging macro that matches the main DEBUG format
#define BINDER_DEBUG(debug_level, level, ...)                                \
  do {                                                                       \
    if (debug_level >= level) {                                              \
      fprintf(stderr, "[%8.3f][DEBUG%d] ", binder_get_timestamp_s(), level); \
      fprintf(stderr, __VA_ARGS__);                                          \
      fprintf(stderr, "\n");                                                 \
    }                                                                        \
  } while (0)

// Global thread handle for cleanup
static pthread_t g_binder_thread = 0;
static int g_binder_debug_level = 0;
static void* g_ipc_state = nullptr;

// Function pointer types
typedef void (*StopProcessFn)(void*);

// Try using IPCThreadState::self()->joinThreadPool() in a background thread
// This is simpler and avoids ProcessState::init() ABI issues
inline bool init_binder_thread_pool(int debug_level = 0) {
  BINDER_DEBUG(debug_level, 1,
               "Attempting alternative binder initialization...");
  g_binder_debug_level = debug_level;

  void* handle = dlopen("libbinder.so", RTLD_NOW | RTLD_GLOBAL);
  if (!handle) {
    BINDER_DEBUG(debug_level, 1, "Could not load libbinder.so: %s", dlerror());
    return false;
  }
  BINDER_DEBUG(debug_level, 2, "libbinder.so loaded");

  // Try IPCThreadState::self()->joinThreadPool() approach
  // This is safer as it doesn't require calling ProcessState::init()
  typedef void* (*IPCThreadStateSelfFn)();
  typedef void (*JoinThreadPoolFn)(void*, bool);

  IPCThreadStateSelfFn ipc_self =
      (IPCThreadStateSelfFn)dlsym(handle, "_ZN7android14IPCThreadState4selfEv");

  JoinThreadPoolFn join_pool = (JoinThreadPoolFn)dlsym(
      handle, "_ZN7android14IPCThreadState14joinThreadPoolEb");

  if (ipc_self && join_pool) {
    BINDER_DEBUG(debug_level, 2, "Using IPCThreadState approach");

    // Call IPCThreadState::self()->joinThreadPool(false) in background
    g_ipc_state = ipc_self();
    if (g_ipc_state) {
      BINDER_DEBUG(debug_level, 2, "IPCThreadState::self() = %p", g_ipc_state);

      // Start a background thread to handle binder callbacks
      auto thread_fn = [](void* arg) -> void* {
        auto fn = (JoinThreadPoolFn)(((void**)arg)[0]);
        void* ipc = ((void**)arg)[1];
        int dbg_level = (int)(long)(((void**)arg)[2]);
        BINDER_DEBUG(dbg_level, 2,
                     "Background thread calling joinThreadPool(false)...");
        fn(ipc, false);  // false = don't become main thread
        BINDER_DEBUG(dbg_level, 2,
                     "Background thread joinThreadPool() returned");
        return nullptr;
      };

      static void* args[3] = {(void*)join_pool, g_ipc_state,
                              (void*)(long)debug_level};
      if (pthread_create(&g_binder_thread, nullptr, thread_fn, args) == 0) {
        // Don't detach - we need to clean up later
        // Give thread minimal time to start (1ms is enough for thread
        // scheduling)
        usleep(1000);  // 1ms
        BINDER_DEBUG(debug_level, 2, "Binder thread started successfully");
        return true;
      }
    }
  }

  BINDER_DEBUG(debug_level, 1,
               "IPCThreadState approach failed, initialization skipped");
  return false;
}

// Stop binder thread - call stopProcess and join the thread
inline void stop_binder_thread_pool() {
  if (g_binder_thread != 0) {
    BINDER_DEBUG(g_binder_debug_level, 2, "Stopping binder thread...");

    // Call IPCThreadState::stopProcess() to signal the thread to exit
    void* handle = dlopen("libbinder.so", RTLD_NOW | RTLD_GLOBAL);
    if (handle && g_ipc_state) {
      StopProcessFn stop_process = (StopProcessFn)dlsym(
          handle, "_ZN7android14IPCThreadState11stopProcessEb");

      if (stop_process) {
        BINDER_DEBUG(g_binder_debug_level, 2,
                     "Calling IPCThreadState::stopProcess()...");
        stop_process(g_ipc_state);
        BINDER_DEBUG(g_binder_debug_level, 2, "stopProcess() returned");
      } else {
        BINDER_DEBUG(g_binder_debug_level, 2,
                     "stopProcess() not found, thread may not exit");
      }
    }

    // Join the thread to wait for it to finish
    // stopProcess() makes joinThreadPool() return, so this should be fast
    BINDER_DEBUG(g_binder_debug_level, 2, "Joining binder thread...");
    void* thread_result = nullptr;
    int join_result = pthread_join(g_binder_thread, &thread_result);
    if (join_result == 0) {
      BINDER_DEBUG(g_binder_debug_level, 2,
                   "Binder thread joined successfully");
    } else {
      BINDER_DEBUG(g_binder_debug_level, 1, "pthread_join failed: %d",
                   join_result);
    }

    g_binder_thread = 0;
    g_ipc_state = nullptr;
    BINDER_DEBUG(g_binder_debug_level, 2, "Binder thread cleanup complete");
  }
}

#endif  // __ANDROID__
#endif  // BINDER_INIT_H
