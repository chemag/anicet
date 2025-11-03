// anicet_runner_svtav1.h
// SVT-AV1 encoder runner

#ifndef ANICET_RUNNER_SVTAV1_H
#define ANICET_RUNNER_SVTAV1_H

#include "anicet_runner.h"

#ifdef __cplusplus

#include <list>
#include <map>
#include <string>

#include "anicet_parameter.h"

namespace anicet {
namespace runner {
namespace svtav1 {

// Default SVT-AV1 preset (speed/quality tradeoff)
constexpr int DEFAULT_PRESET = 8;

// Default SVT-AV1 QP (quantization parameter)
constexpr int DEFAULT_QP = 35;

// SVT-AV1 parameter descriptors
const std::map<std::string, anicet::parameter::ParameterDescriptor>
    SVTAV1_PARAMETERS = {
        {"preset",
         {.name = "preset",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Encoding preset (0=slowest/best, 13=fastest/worst)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 13,
          .default_value = DEFAULT_PRESET,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 0}},
        {"use_cpu_flags",
         {.name = "use_cpu_flags",
          .type = anicet::parameter::ParameterType::STRING_LIST,
          .description =
              "CPU optimization flags (all=auto detect, none=no SIMD)",
          .valid_values = {"all", "none"},
          .min_value = 0,
          .max_value = 0,
          .default_value = std::string("all"),
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 1}},
        {"tune",
         {.name = "tune",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Tuning mode (0=VQ, 1=PSNR, 2=SSIM)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 2,
          .default_value = 1,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 2}},
        {"rate_control_mode",
         {.name = "rate_control_mode",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Rate control mode (0=CQP, 1=VBR, 2=CBR)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 2,
          .default_value = 0,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 3}},
        {"qp",
         {.name = "qp",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Quantization parameter (0=best quality, 63=worst)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 63,
          .default_value = DEFAULT_QP,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 4}},
        {"target_bit_rate",
         {.name = "target_bit_rate",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Target bitrate in bits/sec (for VBR/CBR modes)",
          .valid_values = {},
          .min_value = 1000,
          .max_value = 100000000,
          .default_value = 2000000,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 5}},
        {"max_bit_rate",
         {.name = "max_bit_rate",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Maximum bitrate in bits/sec (for VBR mode)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 100000000,
          .default_value = 0,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 6}}};

// SVT-AV1 encoder
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace svtav1
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_SVTAV1_H
