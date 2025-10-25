// anicet_runner_libjpegturbo.h
// libjpeg-turbo encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_LIBJPEGTURBO_H
#define ANICET_RUNNER_LIBJPEGTURBO_H

#include "anicet_runner.h"

#ifdef __cplusplus

// libjpeg-turbo encoder (using TurboJPEG API) - optimized
int anicet_run_libjpegturbo(const uint8_t* input_buffer, size_t input_size,
                            int height, int width, const char* color_format,
                            int num_runs, bool dump_output, CodecOutput* output);

// libjpeg-turbo encoder (using TurboJPEG API) - non-optimized (no SIMD)
int anicet_run_libjpegturbo_nonopt(const uint8_t* input_buffer,
                                   size_t input_size, int height, int width,
                                   const char* color_format, int num_runs,
                                   bool dump_output, CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_LIBJPEGTURBO_H
