// anicet_runner_x265.h
// x265 encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_X265_H
#define ANICET_RUNNER_X265_H

#include "anicet_runner.h"

#ifdef __cplusplus

#include <list>
#include <map>
#include <string>

#include "anicet_parameter.h"

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

// x265 parameter descriptors
const std::map<std::string, anicet::parameter::ParameterDescriptor>
    X265_PARAMETERS = {
        {"optimization",
         {.name = "optimization",
          .type = anicet::parameter::ParameterType::STRING_LIST,
          .description = "Optimization level (opt=SIMD, nonopt=no SIMD)",
          .valid_values = {"opt", "nonopt"},
          .min_value = 0,
          .max_value = 0,
          .default_value = std::string("opt"),
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 0}},
        {"preset",
         {.name = "preset",
          .type = anicet::parameter::ParameterType::STRING_LIST,
          .description = "Encoding speed/quality preset",
          .valid_values = DEFAULT_CODEC_SETUP_PRESET_VALUES,
          .min_value = 0,
          .max_value = 0,
          .default_value = std::string(DEFAULT_CODEC_SETUP_PRESET),
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 1}},
        {"rate-control",
         {.name = "rate-control",
          .type = anicet::parameter::ParameterType::STRING_LIST,
          .description = "Rate control mode",
          .valid_values = DEFAULT_CODEC_SETUP_RATE_CONTROL_VALUES,
          .min_value = 0,
          .max_value = 0,
          .default_value = std::string(DEFAULT_CODEC_SETUP_RATE_CONTROL),
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 2}},
        {"tune",
         {.name = "tune",
          .type = anicet::parameter::ParameterType::STRING_LIST,
          .description = "Tune encoder for specific metric or use case",
          .valid_values = DEFAULT_CODEC_SETUP_TUNE_VALUES,
          .min_value = 0,
          .max_value = 0,
          .default_value = std::string(DEFAULT_CODEC_SETUP_TUNE),
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 3}},
        {"qp",
         {.name = "qp",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Constant quantization parameter (CQP mode)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 51,
          .default_value = 28,
          .requires_param = "rate-control",
          .requires_value = "cqp",
          .order = 4}},
        {"crf",
         {.name = "crf",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Constant rate factor (CRF mode)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 51,
          .default_value = 28,
          .requires_param = "rate-control",
          .requires_value = "crf",
          .order = 5}},
        {"bitrate",
         {.name = "bitrate",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Target bitrate in bits/second (ABR/CBR mode)",
          .valid_values = {},
          .min_value = 1,
          .max_value = 100000000,
          .default_value = 1000000,
          .requires_param = "rate-control",
          .requires_value = "abr",
          .order = 6}}};

// Runner - dispatches to opt or nonopt based on setup parameters
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace x265
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_X265_H
