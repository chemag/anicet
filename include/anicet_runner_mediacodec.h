// anicet_runner_mediacodec.h
// Android MediaCodec encoder runner

#ifndef ANICET_RUNNER_MEDIACODEC_H
#define ANICET_RUNNER_MEDIACODEC_H

#include "anicet_runner.h"

#ifdef __cplusplus

namespace anicet {
namespace runner {
namespace mediacodec {

// Android MediaCodec encoder (Android only)
// Expects "codec_name" parameter in setup->parameter_map
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace mediacodec
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_MEDIACODEC_H
