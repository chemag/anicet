// anicet_runner_webp.h
// WebP encoder runners (optimized and non-optimized)

#ifndef ANICET_RUNNER_WEBP_H
#define ANICET_RUNNER_WEBP_H

#include "anicet_runner.h"

#ifdef __cplusplus

#include <list>
#include <map>
#include <string>

#include "anicet_parameter.h"

namespace anicet {
namespace runner {
namespace webp {

// Default WebP quality
constexpr int DEFAULT_QUALITY = 75;

// Default WebP method (speed/quality trade-off)
constexpr int DEFAULT_METHOD = 4;

// WebP parameter descriptors
const std::map<std::string, anicet::parameter::ParameterDescriptor>
    WEBP_PARAMETERS = {
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
          .description = "Quality factor (0=smallest file, 100=best quality)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 100,
          .default_value = DEFAULT_QUALITY,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 1}},
        {"method",
         {.name = "method",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Compression method (0=fast, 6=slowest/best)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 6,
          .default_value = DEFAULT_METHOD,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 2}}};

// Runner - dispatches to opt or nonopt based on setup parameters
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace webp
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_WEBP_H
