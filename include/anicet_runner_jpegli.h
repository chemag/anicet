// anicet_runner_jpegli.h
// jpegli encoder runner

#ifndef ANICET_RUNNER_JPEGLI_H
#define ANICET_RUNNER_JPEGLI_H

#include "anicet_runner.h"

#ifdef __cplusplus

// jpegli encoder (JPEG XL's JPEG encoder)
int anicet_run_jpegli(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      int num_runs, bool dump_output, CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_JPEGLI_H
