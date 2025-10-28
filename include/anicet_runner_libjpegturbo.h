// anicet_runner_libjpegturbo.h
// libjpeg-turbo encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_LIBJPEGTURBO_H
#define ANICET_RUNNER_LIBJPEGTURBO_H

#include "anicet_runner.h"

#ifdef __cplusplus

namespace anicet {
namespace runner {
namespace libjpegturbo {

// Runner - dispatches to opt or nonopt based on setup parameters
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace libjpegturbo
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_LIBJPEGTURBO_H
