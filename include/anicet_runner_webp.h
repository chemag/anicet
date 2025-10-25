// anicet_runner_webp.h
// WebP encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_WEBP_H
#define ANICET_RUNNER_WEBP_H

#include "anicet_runner.h"

#ifdef __cplusplus

// WebP encoder - optimized
int anicet_run_webp(const CodecInput* input, int num_runs, CodecOutput* output);

// WebP encoder - non-optimized (no SIMD)
int anicet_run_webp_nonopt(const CodecInput* input, int num_runs,
                           CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_WEBP_H
