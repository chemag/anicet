// android_mediacodec_lib.cc
// Android MediaCodec library implementation
// Provides buffer-based encoding API (no file I/O)

#include "android_mediacodec_lib.h"

#include <sys/time.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaError.h>
#include <media/NdkMediaFormat.h>

#include "android_binder_init.h"
#endif

#define DEFAULT_QUALITY 80

// Color format constants are now defined in android_mediacodec_lib.h

// Global debug level (set in encode function)
static int g_debug_level = 0;

// Get timestamp in seconds since start
static double get_timestamp_s() {
  static struct timeval start_time = {0, 0};
  struct timeval now;
  gettimeofday(&now, nullptr);

  // Initialize start time on first call
  if (start_time.tv_sec == 0) {
    start_time = now;
  }

  double elapsed = (now.tv_sec - start_time.tv_sec) +
                   (now.tv_usec - start_time.tv_usec) / 1000000.0;
  return elapsed;
}

#define DEBUG(level, ...)                                             \
  do {                                                                \
    if (g_debug_level >= level) {                                     \
      fprintf(stderr, "[%8.3f][DEBUG%d] ", get_timestamp_s(), level); \
      fprintf(stderr, __VA_ARGS__);                                   \
      fprintf(stderr, "\n");                                          \
    }                                                                 \
  } while (0)

#ifdef __ANDROID__
#define LOGE(...) \
  __android_log_print(ANDROID_LOG_ERROR, "MediaCodec", __VA_ARGS__)
#else
#define LOGE(...) fprintf(stderr, "ERROR: " __VA_ARGS__)
#endif

// Helper: Calculate frame size based on color format
// Public helper: Calculate frame size based on color format and dimensions
size_t android_mediacodec_get_frame_size(const char* color_format, int width,
                                         int height) {
  std::string fmt(color_format);
  // Calculate frame size based on color format
  if (fmt == "yuv420p" || fmt == "nv12" || fmt == "nv21") {
    // YUV420: 1.5 bytes per pixel (Y + U/4 + V/4)
    return width * height * 3 / 2;
  }
  // Add more formats as needed
  return 0;
}

// Internal wrapper for C++ code
static size_t get_frame_size(const std::string& color_format, int width,
                             int height) {
  return android_mediacodec_get_frame_size(color_format.c_str(), width, height);
}

// Public helper: Convert color format string to MediaCodec color format
// constant
int android_mediacodec_get_color_format(const char* color_format) {
  std::string format(color_format);
  // Planar 4:2:0 (I420, aka yuv420p)
  if (format == "yuv420p" || format == "i420" || format == "iyuv") {
    return COLOR_FormatYUV420Planar;  // 19
  }

  // Semiplanar 4:2:0 (NV12 or NV21 share the SAME id; you control the UV order
  // in your bytes)
  // - NV12 -> UVUV...
  // - NV21 -> VUVU...
  if (format == "nv12" || format == "nv21" || format == "yuv420sp" ||
      format == "yuv420spsemi") {
    return COLOR_FormatYUV420SemiPlanar;  // 21
  }

  // Packed variants (rare; only use if dumpsys says the encoder supports them)
  if (format == "yuv420packedplanar") {
    return COLOR_FormatYUV420PackedPlanar;  // 20
  }
  if (format == "yuv420packedsemiplanar") {
    return COLOR_FormatYUV420PackedSemiPlanar;  // 39
  }

  // Only choose Flexible if you explicitly asked for it.
  if (format == "yuv420flexible" || format == "flex" || format == "flexible") {
    return COLOR_FormatYUV420Flexible;  // 0x7F420888
  }

  // Safe default for buffer input: planar I420
  return COLOR_FormatYUV420Planar;  // 19
}

// Internal wrapper for C++ code
static int32_t get_color_format(const std::string& format) {
  return android_mediacodec_get_color_format(format.c_str());
}

// Public helper: Calculate bitrate from quality (0-100) and frame dimensions
int android_mediacodec_calculate_bitrate(int quality, int width, int height) {
  // ensure quality makes sense
  if (quality < 0 || quality > 100) {
    quality = DEFAULT_QUALITY;
  }

  // Pixels per second
  // TODO(chema): fix bitrate usage for images
  // * default is quality parameter
  // * try CQ (advertised or not)
  int64_t pps = (int64_t)width * height * 30 /* frame_rate */;

  // Bits per pixel based on quality
  // Low quality: ~0.05 bpp, High quality: ~0.25 bpp
  double bpp = 0.05 + (quality / 100.0) * 0.20;

  return (int)(pps * bpp);
}

// Internal wrapper for C++ code
static int calculate_bitrate(int quality, int width, int height) {
  return android_mediacodec_calculate_bitrate(quality, width, height);
}

// Note: android_mediacodec_cleanup_binder() is implemented in
// android_binder_init.h

// Public helper: Configure AMediaFormat with encoding parameters
void android_mediacodec_set_format(AMediaFormat* format, const char* mime_type,
                                   int width, int height,
                                   const char* color_format, int* bitrate,
                                   int quality) {
  std::string color_format_str(color_format);
  int32_t color_fmt = get_color_format(color_format_str);
  // Set basic parameters
  AMediaFormat_setString(format, "mime", mime_type);
  DEBUG(2, "AMediaFormat_setString(format, \"mime\", \"%s\");", mime_type);

  AMediaFormat_setInt32(format, "width", width);
  DEBUG(2, "AMediaFormat_setInt32(format, \"width\", %d);", width);

  AMediaFormat_setInt32(format, "height", height);
  DEBUG(2, "AMediaFormat_setInt32(format, \"height\", %d);", height);

  DEBUG(1, "Setting color-format to %d (%s)", color_fmt, color_format);
  AMediaFormat_setInt32(format, "color-format", color_fmt);
  DEBUG(2, "AMediaFormat_setInt32(format, \"color-format\", %d);", color_fmt);

  // TODO(chema): reconsider this
  int frame_rate = 30;
  AMediaFormat_setInt32(format, "frame-rate", frame_rate);
  DEBUG(2, "AMediaFormat_setInt32(format, \"frame-rate\", %d);", frame_rate);

  // set the key frame interval (GoP) to all-key frames
  int i_frame_interval = 0;
  AMediaFormat_setInt32(format, "i-frame-interval", i_frame_interval);
  DEBUG(2, "AMediaFormat_setInt32(format, \"i-frame-interval\", %d);",
        i_frame_interval);

  // Set bitrate
  if (*bitrate < 0) {
    *bitrate = calculate_bitrate((quality >= 0) ? quality : DEFAULT_QUALITY,
                                 width, height);
  }
  AMediaFormat_setInt32(format, "bitrate", *bitrate);
  DEBUG(2, "AMediaFormat_setInt32(format, \"bitrate\", %d);", *bitrate);

  // set bitrate mode (0=CQ, 1=VBR, 2=CBR)
  int bitrate_mode = 1;
  AMediaFormat_setInt32(format, "bitrate-mode", bitrate_mode);
  DEBUG(2, "AMediaFormat_setInt32(format, \"bitrate-mode\", %i);",
        bitrate_mode);

  int max_b_frames = 0;
  AMediaFormat_setInt32(format, "max-bframes", max_b_frames);
  DEBUG(2, "AMediaFormat_setInt32(format, \"max-bframes\", %d);", max_b_frames);
}

#ifdef __ANDROID__

// Context structure for MediaCodec encoder
// Setup MediaCodec encoder
int android_mediacodec_encode_setup(const MediaCodecFormat* fmt,
                                    AMediaCodec** codec_out) {
  // Initialize output parameter
  *codec_out = nullptr;

  // Set global debug level for this encoding session
  g_debug_level = fmt->debug_level;

  // initialize Binder thread pool for MediaCodec callbacks
  if (!init_binder_thread_pool(g_debug_level)) {
    fprintf(stderr, "Warning: Failed to initialize Binder thread pool\n");
    fprintf(stderr, "MediaCodec may not work correctly\n");
  } else {
    DEBUG(1, "Binder thread pool initialized successfully");
    // Give the media server time to stabilize after binder connection
    // This helps prevent aborts when the media server is still cleaning up from
    // previous client disconnections. Testing shows: 18% at 20ms, 16% at 50ms,
    // 12% at 100ms - media server needs substantial recovery time.
    usleep(150000);  // 150ms delay
  }

  // 1. set codec format
  // determine MIME type from codec name
  const char* mime_type = "video/hevc";  // Default to HEVC
  std::string codec_str(fmt->codec_name);
  if (codec_str.find("heic") != std::string::npos) {
    mime_type = "image/vnd.android.heic";
  } else if (codec_str.find("hevc") != std::string::npos) {
    mime_type = "video/hevc";
  } else if (codec_str.find("avc") != std::string::npos ||
             codec_str.find("h264") != std::string::npos) {
    mime_type = "video/avc";
  } else if (codec_str.find("vp9") != std::string::npos) {
    mime_type = "video/x-vnd.on2.vp9";
  } else if (codec_str.find("vp8") != std::string::npos) {
    mime_type = "video/x-vnd.on2.vp8";
  } else if (codec_str.find("av1") != std::string::npos) {
    mime_type = "video/av01";
  }

  // create format
  AMediaFormat* format = AMediaFormat_new();
  int bitrate_local =
      fmt->bitrate;  // Make local copy since function may modify
  android_mediacodec_set_format(format, mime_type, fmt->width, fmt->height,
                                fmt->color_format, &bitrate_local,
                                fmt->quality);
  DEBUG(1, "Encoding with: %s", fmt->codec_name);
  DEBUG(1, "MIME type: %s", mime_type);
  DEBUG(1, "resolution: %dx%d bitrate: %d", fmt->width, fmt->height,
        bitrate_local);

  // 2. create codec
  // Retry codec creation to handle transient media server issues
  // Sometimes the media server is in a bad state from previous client
  // disconnections
  AMediaCodec* codec = nullptr;
  const int max_retries = 3;
  for (int attempt = 0; attempt < max_retries && !codec; attempt++) {
    if (attempt > 0) {
      DEBUG(1, "Retry %d/%d: Waiting 50ms before retrying codec creation...",
            attempt, max_retries - 1);
      usleep(50000);  // 50ms delay before retry
    }
    DEBUG(1,
          "Creating codec: AMediaCodec_createCodecByName(%s) (attempt %d/%d)",
          fmt->codec_name, attempt + 1, max_retries);
    codec = AMediaCodec_createCodecByName(fmt->codec_name);
  }

  if (!codec) {
    fprintf(stderr, "Error: Cannot create codec after %d attempts: %s\n",
            max_retries, fmt->codec_name);
    AMediaFormat_delete(format);
    return 1;
  }
  DEBUG(1, "Codec created successfully");

  // 3. configure codec
  DEBUG(1, "Configuring codec...");
  DEBUG(2,
        "AMediaCodec_configure(codec, format, nullptr, nullptr, "
        "AMEDIACODEC_CONFIGURE_FLAG_ENCODE);");
  media_status_t status = AMediaCodec_configure(
      codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  AMediaFormat_delete(format);

  if (status != AMEDIA_OK) {
    fprintf(stderr, "Error: Cannot configure codec: %d\n", status);
    AMediaCodec_delete(codec);
    return 2;
  }
  DEBUG(1, "Codec configured successfully");

  // 4. start codec
  DEBUG(1, "Starting codec...");
  DEBUG(2, "AMediaCodec_start(codec);");
  status = AMediaCodec_start(codec);
  if (status != AMEDIA_OK) {
    fprintf(stderr, "Error: Cannot start codec: %d\n", status);
    AMediaCodec_delete(codec);
    return 3;
  }
  DEBUG(1, "Codec started successfully");

  *codec_out = codec;
  return 0;
}

// Encode frames using pre-configured MediaCodec encoder
int android_mediacodec_encode_frame(AMediaCodec* codec,
                                    const uint8_t* input_buffer,
                                    size_t input_size,
                                    const MediaCodecFormat* fmt, int num_runs,
                                    CodecOutput* output) {
  // Initialize output - clear vectors and pre-allocate space
  output->frame_buffers.clear();
  output->frame_buffers.resize(num_runs);
  output->frame_sizes.clear();
  output->frame_sizes.resize(num_runs);
  output->timings.clear();
  output->timings.resize(num_runs);

  // Set debug level for this encoding session
  g_debug_level = fmt->debug_level;

  // Calculate frame size
  std::string color_format_str(fmt->color_format);
  size_t frame_size = get_frame_size(color_format_str, fmt->width, fmt->height);

  // Validate input size
  if (input_size < frame_size) {
    fprintf(stderr,
            "Error: Input buffer too small (got %zu, need %zu for one frame)\n",
            input_size, frame_size);
    return 4;
  }

  // Per-frame buffers (will accumulate data for each frame separately)
  std::vector<std::vector<uint8_t>> frame_buffers(num_runs);

  // Encoding loop
  AMediaCodecBufferInfo info;
  int frames_sent = 0;
  int frames_recv = 0;
  int current_frame_idx = -1;  // Which frame we're currently receiving
  bool input_eos_sent = false;
  bool output_eos_recv = false;
  int64_t timeout_us = 10000;  // 10ms timeout

  while (!output_eos_recv) {
    if (!input_eos_sent) {
      // Get (dequeue) input buffer(s)
      ssize_t input_buffer_index =
          AMediaCodec_dequeueInputBuffer(codec, timeout_us);

      if (input_buffer_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        DEBUG(2,
              "AMediaCodec_dequeueInputBuffer() -> "
              "AMEDIACODEC_INFO_TRY_AGAIN_LATER");

      } else if (input_buffer_index >= 0) {
        DEBUG(1,
              "AMediaCodec_dequeueInputBuffer(codec, timeout_us: %zu) -> "
              "input_buffer_index: %zu",
              timeout_us, input_buffer_index);

        size_t input_buffer_size;
        uint8_t* codec_input_buffer = AMediaCodec_getInputBuffer(
            codec, (size_t)input_buffer_index, &input_buffer_size);
        DEBUG(1,
              "AMediaCodec_getInputBuffer(codec, input_buffer_index: %zu, "
              "&input_buffer_size: %zu) -> input_buffer: %p",
              input_buffer_index, input_buffer_size, codec_input_buffer);

        if (frames_sent < num_runs) {
          // Copy frame from input buffer (reuse same frame data each time)
          memcpy(codec_input_buffer, input_buffer, frame_size);
          uint64_t pts_timestamp_us = frames_sent * 33'000;

          // Capture timing BEFORE queueInputBuffer
          struct timespec ts;
          clock_gettime(CLOCK_MONOTONIC, &ts);
          output->timings[frames_sent].input_timestamp_us =
              ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

          AMediaCodec_queueInputBuffer(codec, (size_t)input_buffer_index, 0,
                                       frame_size, pts_timestamp_us, 0);
          DEBUG(1,
                "AMediaCodec_queueInputBuffer(codec, input_buffer_index: %zu, "
                "0, frame_size: %zu, pts_timestamp_us: %zu, flags: 0)",
                input_buffer_index, frame_size, pts_timestamp_us);
          frames_sent++;
        } else {
          // Send EOS
          DEBUG(1,
                "AMediaCodec_queueInputBuffer(codec, input_buffer_index: %zu, "
                "0, frame_size: 0, pts_timestamp_us: 0, flags: "
                "AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)",
                input_buffer_index);
          AMediaCodec_queueInputBuffer(codec, (size_t)input_buffer_index, 0, 0,
                                       0,
                                       AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
          input_eos_sent = true;
        }
      }
    }

    // Get (dequeue) an output buffer
    ssize_t output_buffer_index =
        AMediaCodec_dequeueOutputBuffer(codec, &info, timeout_us);

    if (output_buffer_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      DEBUG(2,
            "AMediaCodec_dequeueOutputBuffer() -> "
            "AMEDIACODEC_INFO_TRY_AGAIN_LATER");

    } else if (output_buffer_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      DEBUG(2,
            "AMediaCodec_dequeueOutputBuffer() -> "
            "AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED");
      AMediaFormat* ofmt = AMediaCodec_getOutputFormat(codec);
      DEBUG(1, "Output format changed: %s", AMediaFormat_toString(ofmt));
      AMediaFormat_delete(ofmt);

    } else if (output_buffer_index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      DEBUG(2,
            "AMediaCodec_dequeueOutputBuffer() -> "
            "AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED");

    } else if (output_buffer_index >= 0) {
      DEBUG(
          1,
          "AMediaCodec_dequeueOutputBuffer(codec, &info {.offset: 0x%x .size: "
          "%u .presentationTimeUs: %lu .flags: %u}, timeout_us: %zu) -> %zu",
          info.offset, info.size, info.presentationTimeUs, info.flags,
          timeout_us, output_buffer_index);

      size_t codec_output_buffer_size;
      uint8_t* codec_output_buffer = AMediaCodec_getOutputBuffer(
          codec, (size_t)output_buffer_index, &codec_output_buffer_size);
      DEBUG(1,
            "AMediaCodec_getOutputBuffer(codec, output_buffer_index: %zi, "
            "&output_buffer_size: %zu)",
            output_buffer_index, codec_output_buffer_size);

      // Capture timing AFTER getOutputBuffer
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      int64_t get_output_ts = ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

      if (info.size > 0 && codec_output_buffer) {
        const bool is_config =
            (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
        if (is_config) {
          DEBUG(1, "... this is a config frame");
        } else {
          DEBUG(1, "... this is a buffer frame");
          current_frame_idx = frames_recv;
          frames_recv++;

          // Store timing for this frame
          if (current_frame_idx < num_runs) {
            output->timings[current_frame_idx].output_timestamp_us =
                get_output_ts;
          }
        }

        // Append to appropriate frame buffer
        if (current_frame_idx >= 0 && current_frame_idx < num_runs) {
          std::vector<uint8_t>& buf = frame_buffers[current_frame_idx];
          size_t old_size = buf.size();
          buf.resize(old_size + info.size);
          memcpy(buf.data() + old_size, codec_output_buffer + info.offset,
                 info.size);
        }
      }

      const bool is_eos =
          (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
      if (is_eos) {
        output_eos_recv = true;
      }

      AMediaCodec_releaseOutputBuffer(codec, (size_t)output_buffer_index,
                                      false);
      DEBUG(1,
            "AMediaCodec_releaseOutputBuffer(codec, output_buffer_index: %zi, "
            "false)",
            output_buffer_index);
    }
  }

  DEBUG(1, "Encoded %d frames, received %d frames", frames_sent, frames_recv);

  // Copy frame buffers to output structure using vectors
  // Resize to actual number of frames received (may be less than num_runs)
  output->frame_buffers.resize(frames_recv);
  output->frame_sizes.resize(frames_recv);
  output->timings.resize(frames_recv);

  for (int i = 0; i < frames_recv; i++) {
    output->frame_buffers[i] = frame_buffers[i];  // Vector copy
    output->frame_sizes[i] = frame_buffers[i].size();
  }

  return 0;  // Success
}

// Cleanup MediaCodec encoder and free resources
void android_mediacodec_encode_cleanup(AMediaCodec* codec, int debug_level) {
  if (!codec) {
    return;
  }

  // Set debug level for cleanup
  g_debug_level = debug_level;

  // Stop and delete codec
  // NOTE: Do NOT call flush() here - it's for reset/reuse, not cleanup
  // After EOS is sent/received, go straight to stop then delete
  DEBUG(2, "Stopping codec...");
  AMediaCodec_stop(codec);
  DEBUG(2, "Deleting codec...");
  AMediaCodec_delete(codec);

  // NOTE: We do NOT stop the binder thread pool here!
  // The binder thread pool is a process-wide resource that should remain
  // active for the lifetime of the application. Stopping it after each encode
  // would break subsequent encode operations in the same process.
  //
  // The binder thread will be automatically cleaned up by the OS when the
  // process exits. Attempting to manually stop it (e.g., via atexit handlers)
  // creates race conditions where the media server or lingering callbacks try
  // to access already-destroyed mutexes, leading to crashes or corruption.
  // Letting the kernel handle cleanup is the safest approach.
}

// Full all-in-one encode function (convenience wrapper)
int android_mediacodec_encode_frame_full(const uint8_t* input_buffer,
                                         size_t input_size,
                                         const MediaCodecFormat* format,
                                         uint8_t** output_buffer,
                                         size_t* output_size) {
  // Setup
  AMediaCodec* codec = nullptr;
  int setup_result = android_mediacodec_encode_setup(format, &codec);
  if (setup_result != 0) {
    return setup_result;
  }

  // Create CodecOutput structure for single frame (num_runs=1)
  // Vectors will be initialized by the encode function
  CodecOutput mediacodec_output;

  // Encode single frame (num_runs=1)
  int result = android_mediacodec_encode_frame(codec, input_buffer, input_size,
                                               format, 1, &mediacodec_output);

  // Copy first frame's output to caller's buffer (malloc for caller)
  if (result == 0 && mediacodec_output.num_frames() > 0) {
    *output_size = mediacodec_output.frame_sizes[0];
    *output_buffer = (uint8_t*)malloc(*output_size);
    if (*output_buffer) {
      memcpy(*output_buffer, mediacodec_output.frame_buffers[0].data(),
             *output_size);
    } else {
      *output_size = 0;
      result = -1;
    }
  } else {
    *output_buffer = nullptr;
    *output_size = 0;
  }

  // Vectors will be automatically freed when mediacodec_output goes out of
  // scope

  // Cleanup
  android_mediacodec_encode_cleanup(codec, format->debug_level);

  return result;
}

#else  // !__ANDROID__

// Stub implementations for non-Android platforms

// Forward declaration for stub
struct AMediaCodec;

int android_mediacodec_encode_setup(const MediaCodecFormat* format,
                                    AMediaCodec** codec) {
  (void)format;
  *codec = nullptr;
  fprintf(stderr, "Error: MediaCodec encoding only works on Android\n");
  return 1;
}

int android_mediacodec_encode_frame(AMediaCodec* codec,
                                    const uint8_t* input_buffer,
                                    size_t input_size,
                                    const MediaCodecFormat* format,
                                    int num_runs, CodecOutput* output) {
  (void)codec;
  (void)input_buffer;
  (void)input_size;
  (void)format;
  (void)num_runs;
  output->frame_buffers.clear();
  output->frame_sizes.clear();
  output->timings.clear();
  fprintf(stderr, "Error: MediaCodec encoding only works on Android\n");
  return 1;
}

void android_mediacodec_encode_cleanup(AMediaCodec* codec, int debug_level) {
  (void)codec;
  (void)debug_level;
  // Stub - nothing to do on non-Android platforms
}

void android_mediacodec_set_format(AMediaFormat* format, const char* mime_type,
                                   int width, int height,
                                   const char* color_format, int* bitrate,
                                   int quality) {
  (void)format;
  (void)mime_type;
  (void)width;
  (void)height;
  (void)color_format;
  (void)bitrate;
  (void)quality;
  // Stub - nothing to do on non-Android platforms
}

int android_mediacodec_encode_frame_full(const uint8_t* input_buffer,
                                         size_t input_size,
                                         const MediaCodecFormat* format,
                                         uint8_t** output_buffer,
                                         size_t* output_size) {
  (void)input_buffer;
  (void)input_size;
  (void)format;
  *output_buffer = nullptr;
  *output_size = 0;
  fprintf(stderr, "Error: MediaCodec encoding only works on Android\n");
  return 1;
}

#endif  // __ANDROID__
