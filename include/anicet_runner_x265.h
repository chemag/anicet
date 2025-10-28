// anicet_runner_x265.h
// x265 encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_X265_H
#define ANICET_RUNNER_X265_H

#include "anicet_runner.h"

#ifdef __cplusplus

#include <list>
#include <string>

namespace anicet {
namespace runner {
namespace x265 {

// Default x265 preset
constexpr const char* DEFAULT_CODEC_SETUP_PRESET = "medium";

// Valid x265 preset values
const std::list<std::string> DEFAULT_CODEC_SETUP_PRESET_VALUES = {
    "ultrafast", "superfast", "veryfast", "faster",   "fast",
    "medium",    "slow",      "slower",   "veryslow", "placebo"};

// Default x265 tune
constexpr const char* DEFAULT_CODEC_SETUP_TUNE = "zerolatency";

// Valid x265 tune values
const std::list<std::string> DEFAULT_CODEC_SETUP_TUNE_VALUES = {
    "psnr", "ssim", "grain", "zerolatency", "fastdecode"};

// Default x265 rate-control
constexpr const char* DEFAULT_CODEC_SETUP_RATE_CONTROL = "crf";

// Valid x265 rate-control values
const std::list<std::string> DEFAULT_CODEC_SETUP_RATE_CONTROL_VALUES = {
    "crf", "cqp", "abr", "cbr", "2-pass"};

// Runner - dispatches to opt or nonopt based on setup parameters
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace x265
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_X265_H
