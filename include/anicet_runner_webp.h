// anicet_runner_webp.h
// WebP encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_WEBP_H
#define ANICET_RUNNER_WEBP_H

#include "anicet_runner.h"

#ifdef __cplusplus

// WebP encoder - optimized
int anicet_run_webp(const uint8_t* input_buffer, size_t input_size, int height,
                    int width, const char* color_format, int num_runs,
                    bool dump_output, CodecOutput* output);

// WebP encoder - non-optimized (no SIMD)
int anicet_run_webp_nonopt(const uint8_t* input_buffer, size_t input_size,
                           int height, int width, const char* color_format,
                           int num_runs, bool dump_output, CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_WEBP_H
