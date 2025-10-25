// anicet_runner_mediacodec.h
// Android MediaCodec encoder runner

#ifndef ANICET_RUNNER_MEDIACODEC_H
#define ANICET_RUNNER_MEDIACODEC_H

#include "anicet_runner.h"

#ifdef __cplusplus

// Android MediaCodec encoder (Android only)
int anicet_run_mediacodec(const uint8_t* input_buffer, size_t input_size,
                          int height, int width, const char* color_format,
                          const char* codec_name, int num_runs,
                          CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_MEDIACODEC_H
