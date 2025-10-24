// anicet_common.h
// Common utility functions for anicet

#ifndef ANICET_COMMON_H
#define ANICET_COMMON_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get current timestamp in microseconds using CLOCK_MONOTONIC
int64_t anicet_get_timestamp();

// Get timestamp in seconds since start (for debug output)
double anicet_get_timestamp_s();

#ifdef __cplusplus
}
#endif

// Unified DEBUG macro
// Usage: ANICET_DEBUG(debug_level_var, level, format, ...)
#define ANICET_DEBUG(debug_level, level, ...)                                \
  do {                                                                       \
    if (debug_level >= level) {                                              \
      fprintf(stderr, "[%8.3f][DEBUG%d] ", anicet_get_timestamp_s(), level); \
      fprintf(stderr, __VA_ARGS__);                                          \
      fprintf(stderr, "\n");                                                 \
    }                                                                        \
  } while (0)

#endif
