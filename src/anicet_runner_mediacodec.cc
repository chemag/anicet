// anicet_runner_mediacodec.cc
// Android MediaCodec encoder implementation

#include "anicet_runner_mediacodec.h"

#include <cstdio>

#include "android_mediacodec_lib.h"
#include "anicet_common.h"
#include "resource_profiler.h"

namespace anicet {
namespace runner {
namespace mediacodec {

// Global debug level (set in encode function)
static int g_debug_level __attribute__((unused)) = 0;

// Use unified DEBUG macro from anicet_common.h
#define DEBUG(level, ...) ANICET_DEBUG(g_debug_level, level, __VA_ARGS__)

// Map media types to file extensions
static const std::map<std::string, std::string> MEDIA_TYPE_EXTENSIONS = {
    {"video/apv", "apv"},           {"video/av01", "av1"},
    {"video/avc", "264"},           {"video/hevc", "265"},
    {"video/mp4v-es", "mp4v"},      {"video/x-vnd.on2.vp8", "vp8"},
    {"video/x-vnd.on2.vp9", "vp9"},
};

// Get file extension for a codec name by looking up its media type
std::string get_codec_extension(const std::string& codec_name) {
#ifdef __ANDROID__
  // Query device for codec's media type
  std::map<std::string, std::string> encoders =
      android_mediacodec_list_encoders(false);  // all codecs

  auto it = encoders.find(codec_name);
  if (it != encoders.end()) {
    const std::string& media_type = it->second;
    auto ext_it = MEDIA_TYPE_EXTENSIONS.find(media_type);
    if (ext_it != MEDIA_TYPE_EXTENSIONS.end()) {
      return ext_it->second;
    }
  }
#else
  (void)codec_name;
#endif
  // Default extension if media type not found
  return "bin";
}

// Get MediaCodec parameter descriptors with dynamically populated codec list
std::map<std::string, anicet::parameter::ParameterDescriptor>
get_mediacodec_parameters() {
  // Start with base parameters from MEDIACODEC_PARAMETERS
  std::map<std::string, anicet::parameter::ParameterDescriptor> params =
      MEDIACODEC_PARAMETERS;

#ifdef __ANDROID__
  // Query device for available encoders (returns map: codec_name -> media_type)
  std::map<std::string, std::string> encoders =
      android_mediacodec_list_encoders(true);  // image_only=true

  // Populate codec_name valid_values with available codecs
  if (!encoders.empty()) {
    // Extract just the codec names for valid_values
    std::list<std::string> codec_list;
    for (const auto& [codec_name, media_type] : encoders) {
      codec_list.push_back(codec_name);
    }
    params["codec_name"].valid_values = codec_list;
  }
#endif

  return params;
}

// Android MediaCodec encoder - wrapper that adapts
// android_mediacodec_encode_frame()
int anicet_run(const CodecInput* input, CodecSetup* setup,
               CodecOutput* output) {
  // Validate inputs
  if (!input || !input->input_buffer || !setup || !output) {
    return -1;
  }

  int num_runs __attribute__((unused)) = setup->num_runs;

  // Extract codec_name from parameter_map
  std::string codec_name_str = std::get<std::string>(
      anicet::runner::mediacodec::MEDIACODEC_PARAMETERS.at("codec_name")
          .default_value);
  auto codec_name_it = setup->parameter_map.find("codec_name");
  if (codec_name_it != setup->parameter_map.end()) {
    codec_name_str = std::get<std::string>(codec_name_it->second);
  } else {
    setup->parameter_map["codec_name"] = codec_name_str;
  }

  // Validate codec_name is not empty (it's a required parameter)
  if (codec_name_str.empty()) {
    fprintf(stderr,
            "mediacodec: codec_name parameter is required but not provided\n");
    fprintf(stderr, "Usage: --mediacodec codec_name=<encoder_name>\n");
    fprintf(stderr,
            "Example: --mediacodec codec_name=c2.android.hevc.encoder\n");
    return -1;
  }

  // Extract quality parameter
  int quality = anicet::runner::mediacodec::DEFAULT_QUALITY;
  auto quality_it = setup->parameter_map.find("quality");
  if (quality_it != setup->parameter_map.end()) {
    quality = std::get<int>(quality_it->second);
  } else {
    setup->parameter_map["quality"] = quality;
  }

  // Extract bitrate parameter
  int bitrate = anicet::runner::mediacodec::DEFAULT_BITRATE;
  auto bitrate_it = setup->parameter_map.find("bitrate");
  if (bitrate_it != setup->parameter_map.end()) {
    bitrate = std::get<int>(bitrate_it->second);
  } else {
    setup->parameter_map["bitrate"] = bitrate;
  }

  // Extract bitrate_mode parameter
  int bitrate_mode = anicet::runner::mediacodec::DEFAULT_BITRATE_MODE;
  auto bitrate_mode_it = setup->parameter_map.find("bitrate_mode");
  if (bitrate_mode_it != setup->parameter_map.end()) {
    bitrate_mode = std::get<int>(bitrate_mode_it->second);
  } else {
    setup->parameter_map["bitrate_mode"] = bitrate_mode;
  }

#ifdef __ANDROID__
  // Profile total memory (all 4 steps: setup + conversion + encode + cleanup)
  PROFILE_RESOURCES_START(profile_encode_mem);

  // (a) Codec setup - setup ONCE for all frames
  MediaCodecFormat format;
  format.width = input->width;
  format.height = input->height;
  format.codec_name = codec_name_str.c_str();
  format.color_format = input->color_format;
  format.quality = quality;
  format.bitrate = bitrate;
  format.bitrate_mode = bitrate_mode;
  // Use global debug level
  format.debug_level = android_mediacodec_get_debug_level();

  AMediaCodec* codec = nullptr;
  int setup_result = android_mediacodec_encode_setup(&format, &codec);
  if (setup_result != 0) {
    PROFILE_RESOURCES_END(profile_encode_mem);
    return setup_result;
  }

  // (b) Input conversion - none needed for MediaCodec, it accepts YUV420p
  // directly

  // (c) Actual encoding - encode all frames in one call with new API
  // CPU profiling is now done inside android_mediacodec_encode_frame()
  int result = android_mediacodec_encode_frame(
      codec, input->input_buffer, input->input_size, &format, num_runs, output);

  if (result == 0) {
    // Optionally print timing information
    for (size_t i = 0; i < output->num_frames(); i++) {
      if (format.debug_level > 1) {
        int64_t encode_time_us = output->timings[i].output_timestamp_us -
                                 output->timings[i].input_timestamp_us;
        DEBUG(2, "Frame %zu: encode time = %lld us\n", i,
              (long long)encode_time_us);
      }
    }
  } else {
    fprintf(stderr, "MediaCodec: Encoding failed\n");
  }

  // (d) Codec cleanup - cleanup ONCE at the end
  android_mediacodec_encode_cleanup(codec, format.debug_level);

  // Capture memory profiling data and store in output
  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;

  // Compute and store resource delta (without printing)
  compute_delta(&__profile_start_profile_encode_mem, &__profile_mem_end,
                &output->resource_delta);

  return result;
#else
  fprintf(stderr, "MediaCodec: Not available (Android only)\n");
  return -1;
#endif
}

}  // namespace mediacodec
}  // namespace runner
}  // namespace anicet
