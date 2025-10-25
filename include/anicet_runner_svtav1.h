// anicet_runner_svtav1.h
// SVT-AV1 encoder runner

#ifndef ANICET_RUNNER_SVTAV1_H
#define ANICET_RUNNER_SVTAV1_H

#include "anicet_runner.h"

#ifdef __cplusplus

// SVT-AV1 encoder
int anicet_run_svtav1(const CodecInput* input, int num_runs,
                      CodecOutput* output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_SVTAV1_H
