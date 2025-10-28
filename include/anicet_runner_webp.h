// anicet_runner_webp.h
// WebP encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_WEBP_H
#define ANICET_RUNNER_WEBP_H

#include "anicet_runner.h"

#ifdef __cplusplus

namespace anicet {
namespace runner {
namespace webp {

// Runner with optimization parameter - dispatches to opt or nonopt
// implementation
int anicet_run(const CodecInput* input, int num_runs, CodecOutput* output,
               const std::string& optimization);

}  // namespace webp
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_WEBP_H
