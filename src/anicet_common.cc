// anicet_common.cc
// Common utility functions implementation

#include "anicet_common.h"

#include <time.h>

int64_t anicet_get_timestamp() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
