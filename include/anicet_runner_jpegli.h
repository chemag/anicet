// anicet_runner_jpegli.h
// jpegli encoder runner

#ifndef ANICET_RUNNER_JPEGLI_H
#define ANICET_RUNNER_JPEGLI_H

#include "anicet_runner.h"

#ifdef __cplusplus

namespace anicet {
namespace runner {
namespace jpegli {

// jpegli encoder (JPEG XL's JPEG encoder)
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace jpegli
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_JPEGLI_H
