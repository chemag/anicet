// anicet_runner_svtav1.h
// SVT-AV1 encoder runner

#ifndef ANICET_RUNNER_SVTAV1_H
#define ANICET_RUNNER_SVTAV1_H

#include "anicet_runner.h"

#ifdef __cplusplus

namespace anicet {
namespace runner {
namespace svtav1 {

// SVT-AV1 encoder
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace svtav1
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_SVTAV1_H
