// anicet_runner_jpegli.h
// jpegli encoder runner

#ifndef ANICET_RUNNER_JPEGLI_H
#define ANICET_RUNNER_JPEGLI_H

#include "anicet_runner.h"

#ifdef __cplusplus

#include <map>
#include <string>

#include "anicet_parameter.h"

namespace anicet {
namespace runner {
namespace jpegli {

// Default jpegli quality
constexpr int DEFAULT_QUALITY = 75;

// jpegli parameter descriptors
const std::map<std::string, anicet::parameter::ParameterDescriptor>
    JPEGLI_PARAMETERS = {
        {"quality",
         {.name = "quality",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "JPEG quality (0=worst, 100=best)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 100,
          .default_value = DEFAULT_QUALITY,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 0}},
        {"highway_target",
         {.name = "highway_target",
          .type = anicet::parameter::ParameterType::STRING_LIST,
          .description =
              "Highway SIMD target (all=auto-dispatch, none=scalar-only)",
          .valid_values = {"all", "none"},
          .min_value = 0,
          .max_value = 0,
          .default_value = std::string("all"),
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 1}}};

// jpegli encoder (JPEG XL's JPEG encoder)
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace jpegli
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_JPEGLI_H
