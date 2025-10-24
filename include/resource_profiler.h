// resource_profiler.h
// Function-level resource usage profiler for Linux/Android

#ifndef RESOURCE_PROFILER_H
#define RESOURCE_PROFILER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/time.h>

struct ResourceSnapshot {
  // Time
  struct timespec wall_time;
  struct timespec cpu_time;

  // Memory (from /proc/self/status)
  long vm_size_kb;   // Virtual memory size
  long vm_rss_kb;    // Resident set size (physical memory)
  long vm_peak_kb;   // Peak virtual memory
  long rss_peak_kb;  // Peak RSS

  // CPU time
  long user_time_us;    // User CPU time (microseconds)
  long system_time_us;  // System CPU time (microseconds)

  // Page faults
  long minor_faults;  // Minor page faults
  long major_faults;  // Major page faults (disk I/O)

  // Context switches
  long vol_ctx_switches;    // Voluntary context switches
  long invol_ctx_switches;  // Involuntary context switches
};

// Read memory stats from /proc/self/status
static void read_proc_status(ResourceSnapshot* snap) {
  FILE* f = fopen("/proc/self/status", "r");
  if (!f) return;

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "VmSize:", 7) == 0) {
      sscanf(line + 7, "%ld", &snap->vm_size_kb);
    } else if (strncmp(line, "VmRSS:", 6) == 0) {
      sscanf(line + 6, "%ld", &snap->vm_rss_kb);
    } else if (strncmp(line, "VmPeak:", 7) == 0) {
      sscanf(line + 7, "%ld", &snap->vm_peak_kb);
    } else if (strncmp(line, "VmHWM:", 6) == 0) {
      sscanf(line + 6, "%ld", &snap->rss_peak_kb);
    }
  }
  fclose(f);
}

// Capture current resource usage
static void capture_resources(ResourceSnapshot* snap) {
  // Wall clock time
  clock_gettime(CLOCK_MONOTONIC, &snap->wall_time);

  // CPU time
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &snap->cpu_time);

  // Memory from /proc
  read_proc_status(snap);

  // rusage for CPU time and page faults
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);
  snap->user_time_us = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec;
  snap->system_time_us =
      usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;
  snap->minor_faults = usage.ru_minflt;
  snap->major_faults = usage.ru_majflt;
  snap->vol_ctx_switches = usage.ru_nvcsw;
  snap->invol_ctx_switches = usage.ru_nivcsw;
}

// Calculate difference between two snapshots
struct ResourceDelta {
  double wall_time_ms;
  double cpu_time_ms;
  long vm_size_delta_kb;
  long vm_rss_delta_kb;
  long user_time_ms;
  long system_time_ms;
  long minor_faults;
  long major_faults;
  long vol_ctx_switches;
  long invol_ctx_switches;
};

static void compute_delta(const ResourceSnapshot* start,
                          const ResourceSnapshot* end, ResourceDelta* delta) {
  // Wall time
  delta->wall_time_ms =
      (end->wall_time.tv_sec - start->wall_time.tv_sec) * 1000.0 +
      (end->wall_time.tv_nsec - start->wall_time.tv_nsec) / 1000000.0;

  // CPU time
  delta->cpu_time_ms =
      (end->cpu_time.tv_sec - start->cpu_time.tv_sec) * 1000.0 +
      (end->cpu_time.tv_nsec - start->cpu_time.tv_nsec) / 1000000.0;

  // Memory deltas
  delta->vm_size_delta_kb = end->vm_size_kb - start->vm_size_kb;
  delta->vm_rss_delta_kb = end->vm_rss_kb - start->vm_rss_kb;

  // CPU time breakdown
  delta->user_time_ms = (end->user_time_us - start->user_time_us) / 1000;
  delta->system_time_ms = (end->system_time_us - start->system_time_us) / 1000;

  // Page faults
  delta->minor_faults = end->minor_faults - start->minor_faults;
  delta->major_faults = end->major_faults - start->major_faults;

  // Context switches
  delta->vol_ctx_switches = end->vol_ctx_switches - start->vol_ctx_switches;
  delta->invol_ctx_switches =
      end->invol_ctx_switches - start->invol_ctx_switches;
}

static void print_resource_delta(const char* label,
                                 const ResourceDelta* delta) {
  printf("\n=== Resource Usage: %s ===\n", label);
  printf("Wall time:        %.2f ms\n", delta->wall_time_ms);
  printf("CPU time:         %.2f ms (%.1f%% CPU utilization)\n",
         delta->cpu_time_ms,
         delta->wall_time_ms > 0
             ? (delta->cpu_time_ms / delta->wall_time_ms * 100.0)
             : 0);
  printf("  User time:      %ld ms\n", delta->user_time_ms);
  printf("  System time:    %ld ms\n", delta->system_time_ms);
  printf("Memory RSS:       %+ld KB (physical memory used)\n",
         delta->vm_rss_delta_kb);
  printf("Memory VSS:       %+ld KB (virtual memory)\n",
         delta->vm_size_delta_kb);
  printf("Page faults:\n");
  printf("  Minor:          %ld (memory already in RAM)\n",
         delta->minor_faults);
  printf("  Major:          %ld (disk I/O required)\n", delta->major_faults);
  printf("Context switches:\n");
  printf("  Voluntary:      %ld (yielded CPU)\n", delta->vol_ctx_switches);
  printf("  Involuntary:    %ld (preempted)\n", delta->invol_ctx_switches);
}

// RAII-style profiler for C++
class ScopedResourceProfiler {
 private:
  const char* label_;
  ResourceSnapshot start_;

 public:
  explicit ScopedResourceProfiler(const char* label) : label_(label) {
    capture_resources(&start_);
  }

  ~ScopedResourceProfiler() {
    ResourceSnapshot end;
    capture_resources(&end);

    ResourceDelta delta;
    compute_delta(&start_, &end, &delta);
    print_resource_delta(label_, &delta);
  }
};

// C-style macros for easy instrumentation
#define PROFILE_RESOURCES_START(name)      \
  ResourceSnapshot __profile_start_##name; \
  capture_resources(&__profile_start_##name);

#define PROFILE_RESOURCES_END(name)                                           \
  do {                                                                        \
    ResourceSnapshot __profile_end;                                           \
    capture_resources(&__profile_end);                                        \
    ResourceDelta __profile_delta;                                            \
    compute_delta(&__profile_start_##name, &__profile_end, &__profile_delta); \
    print_resource_delta(#name, &__profile_delta);                            \
  } while (0)

#else
// Stub for non-Linux platforms
struct ResourceSnapshot {
  int dummy;
};
struct ResourceDelta {
  int dummy;
};
static void capture_resources(ResourceSnapshot* snap) { (void)snap; }
static void compute_delta(const ResourceSnapshot* s, const ResourceSnapshot* e,
                          ResourceDelta* d) {
  (void)s;
  (void)e;
  (void)d;
}
static void print_resource_delta(const char* l, const ResourceDelta* d) {
  (void)l;
  (void)d;
}
class ScopedResourceProfiler {
 public:
  explicit ScopedResourceProfiler(const char* label) { (void)label; }
};
#define PROFILE_RESOURCES_START(name)
#define PROFILE_RESOURCES_END(name)
#endif

#endif  // RESOURCE_PROFILER_H
