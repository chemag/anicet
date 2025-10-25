// anicet_runner_x265.h
// x265 encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_X265_H
#define ANICET_RUNNER_X265_H

#include "anicet_runner.h"

#ifdef __cplusplus

// x265 encoder (H.265/HEVC) 8-bit - optimized
int anicet_run_x265_8bit(const uint8_t* input_buffer, size_t input_size,
                         int height, int width, const char* color_format,
                         int num_runs, CodecOutput* output);

// x265 encoder (H.265/HEVC) 8-bit - non-optimized (no assembly)
int anicet_run_x265_8bit_nonopt(const uint8_t* input_buffer, size_t input_size,
                                int height, int width, const char* color_format,
                                int num_runs, CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_X265_H
