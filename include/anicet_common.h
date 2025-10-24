// anicet_common.h
// Common utility functions for anicet

#ifndef ANICET_COMMON_H
#define ANICET_COMMON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get current timestamp in microseconds using CLOCK_MONOTONIC
int64_t anicet_get_timestamp();

#ifdef __cplusplus
}
#endif

#endif
