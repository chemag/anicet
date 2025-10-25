// anicet_runner_svtav1.h
// SVT-AV1 encoder runner

#ifndef ANICET_RUNNER_SVTAV1_H
#define ANICET_RUNNER_SVTAV1_H

#include "anicet_runner.h"

#ifdef __cplusplus

// SVT-AV1 encoder
int anicet_run_svtav1(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      int num_runs, CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_SVTAV1_H
