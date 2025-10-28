// anicet_runner_webp.h
// WebP encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_WEBP_H
#define ANICET_RUNNER_WEBP_H

#include "anicet_runner.h"

#ifdef __cplusplus

namespace anicet {
namespace runner {
namespace webp {

// WebP encoder - optimized
int anicet_run(const CodecInput* input, int num_runs, CodecOutput* output);

// WebP encoder - non-optimized (no SIMD)
int anicet_run_nonopt(const CodecInput* input, int num_runs,
                      CodecOutput* output);

}  // namespace webp
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_WEBP_H
