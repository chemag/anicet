// anicet_runner_x265.h
// x265 encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_X265_H
#define ANICET_RUNNER_X265_H

#include "anicet_runner.h"

#ifdef __cplusplus

namespace anicet {
namespace runner {
namespace x265 {

// x265 encoder (H.265/HEVC) 8-bit - optimized
int anicet_run(const CodecInput* input, int num_runs, CodecOutput* output);

// x265 encoder (H.265/HEVC) 8-bit - non-optimized (no assembly)
int anicet_run_nonopt(const CodecInput* input, int num_runs,
                      CodecOutput* output);

}  // namespace x265
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_X265_H
