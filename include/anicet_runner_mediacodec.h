// anicet_runner_mediacodec.h
// Android MediaCodec encoder runner

#ifndef ANICET_RUNNER_MEDIACODEC_H
#define ANICET_RUNNER_MEDIACODEC_H

#include "anicet_runner.h"

#ifdef __cplusplus

// Android MediaCodec encoder (Android only)
int anicet_run_mediacodec(const CodecInput* input, const char* codec_name,
                          int num_runs, CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_MEDIACODEC_H
