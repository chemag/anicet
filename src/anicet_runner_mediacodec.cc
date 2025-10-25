// anicet_runner_mediacodec.cc
// Android MediaCodec encoder implementation

#include "anicet_runner_mediacodec.h"

#include <cstdio>

#include "anicet_common.h"
#include "android_mediacodec_lib.h"
#include "resource_profiler.h"

// Android MediaCodec encoder - wrapper that adapts
// android_mediacodec_encode_frame()
int anicet_run_mediacodec(const uint8_t* input_buffer, size_t input_size,
                          int height, int width, const char* color_format,
                          const char* codec_name, int num_runs,
                          bool dump_output, CodecOutput* output) {
  // Validate inputs
  if (!input_buffer || !output || !codec_name) {
    return -1;
  }

#ifdef __ANDROID__
  // Profile total memory (all 4 steps: setup + conversion + encode + cleanup)
  PROFILE_RESOURCES_START(mediacodec_total_memory);

  // (a) Codec setup - setup ONCE for all frames
  MediaCodecFormat format;
  format.width = width;
  format.height = height;
  format.codec_name = codec_name;
  format.color_format = color_format;
  format.quality = 75;
  // Auto-calculate from quality
  format.bitrate = -1;
  // Quiet
  format.debug_level = 0;

  AMediaCodec* codec = nullptr;
  int setup_result = android_mediacodec_encode_setup(&format, &codec);
  if (setup_result != 0) {
    PROFILE_RESOURCES_END(mediacodec_total_memory);
    return setup_result;
  }

  // (b) Input conversion - none needed for MediaCodec, it accepts YUV420p
  // directly

  // (c) Actual encoding - encode all frames in one call with new API
  int result = 0;
  PROFILE_RESOURCES_START(mediacodec_encode_cpu);

  // Call new API - encodes all num_runs frames in single session
  result = android_mediacodec_encode_frame(
      codec, input_buffer, input_size, &format, num_runs, dump_output, output);

  if (result == 0) {
    // Optionally print timing information
    for (size_t i = 0; i < output->num_frames(); i++) {
      // Optionally print timing information
      if (format.debug_level > 0) {
        int64_t encode_time_us = output->timings[i].output_timestamp_us -
                                 output->timings[i].input_timestamp_us;
        printf("Frame %zu: encode time = %lld us\n", i,
               (long long)encode_time_us);
      }
    }
  } else {
    fprintf(stderr, "MediaCodec: Encoding failed\n");
  }

  // Vectors will be automatically freed when output goes out of scope

  PROFILE_RESOURCES_END(mediacodec_encode_cpu);

  // (d) Codec cleanup - cleanup ONCE at the end
  android_mediacodec_encode_cleanup(codec, format.debug_level);

  PROFILE_RESOURCES_END(mediacodec_total_memory);
  return result;
#else
  // Unused on non-Android
  (void)num_runs;
  fprintf(stderr, "MediaCodec: Not available (Android only)\n");
  return -1;
#endif
}
