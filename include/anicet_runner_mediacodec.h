// anicet_runner_mediacodec.h
// Android MediaCodec encoder runner

#ifndef ANICET_RUNNER_MEDIACODEC_H
#define ANICET_RUNNER_MEDIACODEC_H

#include "anicet_runner.h"

#ifdef __cplusplus

#include <map>
#include <string>

#include "anicet_parameter.h"

namespace anicet {
namespace runner {
namespace mediacodec {

// Default MediaCodec parameters
constexpr int DEFAULT_QUALITY = 75;
constexpr int DEFAULT_BITRATE = -1;      // Auto-calculate from quality
constexpr int DEFAULT_BITRATE_MODE = 1;  // VBR

// MediaCodec parameter descriptors
const std::map<std::string, anicet::parameter::ParameterDescriptor>
    MEDIACODEC_PARAMETERS = {
        {"codec_name",
         {.name = "codec_name",
          .type = anicet::parameter::ParameterType::STRING_LIST,
          .description = "MediaCodec encoder name (required, no default)",
          .valid_values = {},  // Empty list = accept any string
          .min_value = 0,
          .max_value = 0,
          .default_value = std::string(""),  // Empty default = no default
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 0}},
        {"quality",
         {.name = "quality",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Quality (0=worst, 100=best)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 100,
          .default_value = DEFAULT_QUALITY,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 1}},
        {"bitrate",
         {.name = "bitrate",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Target bitrate in bps (-1=auto from quality)",
          .valid_values = {},
          .min_value = -1,
          .max_value = 100000000,  // 100 Mbps max
          .default_value = DEFAULT_BITRATE,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 2}},
        {"bitrate_mode",
         {.name = "bitrate_mode",
          .type = anicet::parameter::ParameterType::INTEGER_RANGE,
          .description = "Bitrate mode (0=CQ, 1=VBR, 2=CBR)",
          .valid_values = {},
          .min_value = 0,
          .max_value = 2,
          .default_value = DEFAULT_BITRATE_MODE,
          .requires_param = std::nullopt,
          .requires_value = std::nullopt,
          .order = 3}}};

// Hardcoded MediaCodec parameters (not exposed as CLI parameters)
#define MEDIACODEC_FRAME_RATE 30
#define MEDIACODEC_I_FRAME_INTERVAL 0
#define MEDIACODEC_MAX_BFRAMES 0

// Get MediaCodec parameter descriptors with dynamically populated codec list
// This function queries the device for available encoders and populates
// the valid_values list for codec_name parameter
std::map<std::string, anicet::parameter::ParameterDescriptor>
get_mediacodec_parameters();

// Get file extension for a codec name based on its media type
// Returns proper extension (e.g., "av1", "264", "265") or "bin" as fallback
std::string get_codec_extension(const std::string& codec_name);

// Android MediaCodec encoder (Android only)
// Expects parameters in setup->parameter_map: codec_name, quality, bitrate,
// bitrate_mode
int anicet_run(const CodecInput* input, CodecSetup* setup, CodecOutput* output);

}  // namespace mediacodec
}  // namespace runner
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_RUNNER_MEDIACODEC_H
