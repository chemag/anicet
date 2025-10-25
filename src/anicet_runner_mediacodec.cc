// anicet_runner_mediacodec.cc
// Android MediaCodec encoder implementation

#include "anicet_runner_mediacodec.h"

#include <cstdio>

#include "android_mediacodec_lib.h"
#include "anicet_common.h"
#include "resource_profiler.h"

// Android MediaCodec encoder - wrapper that adapts
// android_mediacodec_encode_frame()
int anicet_run_mediacodec(const CodecInput* input, const char* codec_name,
                          int num_runs, CodecOutput* output) {
  // Validate inputs
  if (!input || !input->input_buffer || !output || !codec_name) {
    return -1;
  }

#ifdef __ANDROID__
  // Profile total memory (all 4 steps: setup + conversion + encode + cleanup)
  PROFILE_RESOURCES_START(profile_encode_mem);

  // (a) Codec setup - setup ONCE for all frames
  MediaCodecFormat format;
  format.width = input->width;
  format.height = input->height;
  format.codec_name = codec_name;
  format.color_format = input->color_format;
  format.quality = 75;
  // Auto-calculate from quality
  format.bitrate = -1;
  // Quiet
  format.debug_level = 0;

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

  // (d) Codec cleanup - cleanup ONCE at the end
  android_mediacodec_encode_cleanup(codec, format.debug_level);

  // Capture memory profiling data and store in output
  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;
  PROFILE_RESOURCES_END(profile_encode_mem);
  return result;
#else
  fprintf(stderr, "MediaCodec: Not available (Android only)\n");
  return -1;
#endif
}
