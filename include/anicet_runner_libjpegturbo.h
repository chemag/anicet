// anicet_runner_libjpegturbo.h
// libjpeg-turbo encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_LIBJPEGTURBO_H
#define ANICET_RUNNER_LIBJPEGTURBO_H

#include "anicet_runner.h"

#ifdef __cplusplus

// libjpeg-turbo encoder (using TurboJPEG API) - optimized
int anicet_run_libjpegturbo(const CodecInput* input, int num_runs,
                            CodecOutput* output);

// libjpeg-turbo encoder (using TurboJPEG API) - non-optimized (no SIMD)
int anicet_run_libjpegturbo_nonopt(const CodecInput* input, int num_runs,
                                   CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_LIBJPEGTURBO_H
