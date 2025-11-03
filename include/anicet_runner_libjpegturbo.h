// anicet_runner_libjpegturbo.h
// libjpeg-turbo encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_LIBJPEGTURBO_H
#define ANICET_RUNNER_LIBJPEGTURBO_H

#include "anicet_runner.h"

#ifdef __cplusplus

#include <list>
#include <map>
#include <string>

#include "anicet_parameter.h"

namespace anicet {
namespace runner {
namespace libjpegturbo {

// Default libjpeg-turbo quality
constexpr int DEFAULT_QUALITY = 75;

// libjpeg-turbo parameter descriptors
const std::map<std::string, anicet::parameter::ParameterDescriptor>
    LIBJPEGTURBO_PARAMETERS = {
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
        {"quality",
         {.name = "quality",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Quality factor (1=worst, 100=best)",
          .valid_values = {},
          .min_value = 1,
          .max_value = 100,
          .default_value = DEFAULT_QUALITY,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 1}}};

// Runner - dispatches to opt or nonopt based on setup parameters
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace libjpegturbo
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_LIBJPEGTURBO_H
