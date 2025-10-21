// android_mediacodec.cpp
// Android MediaCodec hardware encoder wrapper for image/video encoding
// Build: Part of anicet CMake build system (Android only)

#include <fcntl.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaError.h>
#include <media/NdkMediaFormat.h>

#include "android-binder_init.h"
#endif

#define DEFAULT_QUALITY 80

// Android MediaCodec color-format constants (subset)
// These match MediaCodecInfo.CodecCapabilities color formats.
#ifndef COLOR_FormatYUV420Planar
#define COLOR_FormatYUV420Planar 19  // I420
#endif
#ifndef COLOR_FormatYUV420SemiPlanar
#define COLOR_FormatYUV420SemiPlanar \
  21  // NV12 or NV21 (depends on UV order you pack)
#endif
#ifndef COLOR_FormatYUV420PackedPlanar
#define COLOR_FormatYUV420PackedPlanar 0x14  // 20
#endif
#ifndef COLOR_FormatYUV420PackedSemiPlanar
#define COLOR_FormatYUV420PackedSemiPlanar 0x27  // 39
#endif
#ifndef COLOR_FormatYUV420Flexible
#define COLOR_FormatYUV420Flexible 0x7F420888
#endif

struct Options {
  std::string codec_name;
  std::string input_file;
  std::string output_file;
  int width = 0;
  int height = 0;
  // yuv420p, nv12, nv21
  std::string color_format = "yuv420p";
  // 0-100, translates to bitrate
  int quality = -1;
  // bps, overrides quality
  int bitrate = -1;
  // number of frames to encode
  int frame_count = 1;
  bool list_codecs = false;
  bool list_image_codecs = false;
  // debug verbosity level
  int debug_level = 0;
};

// Global debug level (set from Options in main)
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

static void print_usage(const char* argv0) {
  fprintf(
      stderr,
      "Usage: %s [options]\n\n"
      "Options:\n"
      "  --codec-name NAME        Codec to use (e.g., c2.qti.heic.encoder)\n"
      "  --input FILE             Input YUV file\n"
      "  --output FILE            Output encoded file\n"
      "  --width N                Frame width (required)\n"
      "  --height N               Frame height (required)\n"
      "  --format FMT             Color format: yuv420p, nv12, nv21 (default: "
      "yuv420p)\n"
      "  --quality N              Quality 0-100 (default: translates to "
      "bitrate)\n"
      "  --bitrate N              Target bitrate in bps (overrides quality)\n"
      "  --frame-count N          Number of frames to encode (default: 1)\n"
      "  --list-codecs            List all available encoders\n"
      "  --list-image-codecs      List image encoders (HEIC, jpeg, etc.)\n"
      "  -d, --debug              Increase debug verbosity (can be repeated)\n"
      "  --quiet                  Suppress debug output\n"
      "  --help                   Show this help\n\n"
      "Examples:\n"
      "  # List image encoders\n"
      "  %s --list-image-codecs\n\n"
      "  # Encode single frame (image)\n"
      "  %s --codec-name c2.qti.heic.encoder \\\n"
      "    --input /sdcard/input.yuv --output /sdcard/output.heic \\\n"
      "    --width 1920 --height 1080 --quality 90\n\n"
      "  # Encode video\n"
      "  %s --codec-name c2.qti.hevc.encoder \\\n"
      "    --input /sdcard/video.yuv --output /sdcard/video.hevc \\\n"
      "    --width 3840 --height 2160 --bitrate 20000000 \\\n"
      "    --frame-count 300\n",
      argv0, argv0, argv0, argv0);
}

static bool parse_args(int argc, char** argv, Options& opt) {
  static struct option long_options[] = {
      {"help", no_argument, 0, 'h'},
      {"list-codecs", no_argument, 0, 'l'},
      {"list-image-codecs", no_argument, 0, 'L'},
      {"codec-name", required_argument, 0, 'c'},
      {"input", required_argument, 0, 'i'},
      {"output", required_argument, 0, 'o'},
      {"width", required_argument, 0, 'w'},
      {"height", required_argument, 0, 'H'},
      {"format", required_argument, 0, 'f'},
      {"quality", required_argument, 0, 'q'},
      {"bitrate", required_argument, 0, 'b'},
      {"frame-count", required_argument, 0, 'n'},
      {"debug", no_argument, 0, 'd'},
      {"quiet", no_argument, 0, 'Q'},
      {0, 0, 0, 0}};

  int c;
  int option_index = 0;

  while ((c = getopt_long(argc, argv, "hlLc:i:o:w:H:f:q:b:n:dQ", long_options,
                          &option_index)) != -1) {
    switch (c) {
      case 'h':
        print_usage(argv[0]);
        exit(0);
      case 'l':
        opt.list_codecs = true;
        break;
      case 'L':
        opt.list_image_codecs = true;
        break;
      case 'c':
        opt.codec_name = optarg;
        break;
      case 'i':
        opt.input_file = optarg;
        break;
      case 'o':
        opt.output_file = optarg;
        break;
      case 'w':
        opt.width = atoi(optarg);
        break;
      case 'H':
        opt.height = atoi(optarg);
        break;
      case 'f':
        opt.color_format = optarg;
        break;
      case 'q':
        opt.quality = atoi(optarg);
        break;
      case 'b':
        opt.bitrate = atoi(optarg);
        break;
      case 'n':
        opt.frame_count = atoi(optarg);
        break;
      case 'd':
        opt.debug_level++;
        break;
      case 'Q':
        opt.debug_level = 0;
        break;
      case '?':
        // getopt_long already printed an error message
        return false;
      default:
        fprintf(stderr, "Error: Unknown option\n");
        return false;
    }
  }

  // Check for non-option arguments
  if (optind < argc) {
    fprintf(stderr, "Error: Unexpected argument: %s\n", argv[optind]);
    return false;
  }

  // Validation
  if (opt.list_codecs || opt.list_image_codecs) {
    return true;  // Listing mode, no other validation needed
  }

  if (opt.codec_name.empty()) {
    fprintf(stderr, "Error: --codec-name is required\n");
    return false;
  }
  if (opt.input_file.empty()) {
    fprintf(stderr, "Error: --input is required\n");
    return false;
  }
  if (opt.output_file.empty()) {
    fprintf(stderr, "Error: --output is required\n");
    return false;
  }
  if (opt.width <= 0 || opt.height <= 0) {
    fprintf(stderr,
            "Error: --width and --height are required and must be > 0\n");
    return false;
  }

  return true;
}

static size_t get_frame_size(const std::string& color_format, const int& width,
                             const int& height) {
  // Calculate frame size based on color format
  if (color_format == "yuv420p" || color_format == "nv12" ||
      color_format == "nv21") {
    // YUV420: 1.5 bytes per pixel (Y + U/4 + V/4)
    return width * height * 3 / 2;
  }
  // Add more formats as needed
  return 0;
}

#ifdef __ANDROID__

// Codec listing using dumpsys media.player
static int list_codecs_cmd(const Options& opt) {
  // Run dumpsys media.player to get codec list
  FILE* pipe = popen("/system/bin/dumpsys media.player 2>/dev/null", "r");
  if (!pipe) {
    fprintf(stderr, "Error: Could not run dumpsys command\n");
    return 1;
  }

  std::vector<std::string> encoders;
  char buffer[1024];

  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    std::string line(buffer);

    // Look for encoder lines: '  Encoder "codec.name" supports'
    if (line.find("Encoder \"") != std::string::npos) {
      size_t start = line.find("\"");
      size_t end = line.find("\"", start + 1);
      if (start != std::string::npos && end != std::string::npos) {
        std::string codec_name = line.substr(start + 1, end - start - 1);

        // Filter for image codecs if requested
        if (opt.list_image_codecs) {
          // Include HEVC, HEIC, AVC, H264, VP9, AV1 codecs
          if (codec_name.find("hevc") != std::string::npos ||
              codec_name.find("heic") != std::string::npos ||
              codec_name.find("avc") != std::string::npos ||
              codec_name.find("h264") != std::string::npos ||
              codec_name.find("vp9") != std::string::npos ||
              codec_name.find("av1") != std::string::npos) {
            encoders.push_back(codec_name);
          }
        } else {
          encoders.push_back(codec_name);
        }
      }
    }
  }

  pclose(pipe);

  if (encoders.empty()) {
    fprintf(stderr, "No %sencoders found.\n",
            opt.list_image_codecs ? "image/video " : "");
    return 1;
  }

  printf("Available %sencoders:\n",
         opt.list_image_codecs ? "image/video " : "");
  printf("======================\n\n");

  for (const auto& enc : encoders) {
    printf("  %s\n", enc.c_str());
  }

  printf("\n");
  if (opt.list_image_codecs) {
    printf("For single-frame encoding (images), use --frame-count 1\n");
  }

  return 0;
}

// Convert color format string to MediaCodec color format constant
static int32_t get_color_format(const std::string& format) {
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

// Calculate bitrate from quality (simple heuristic)
static int calculate_bitrate(int quality, int width, int height) {
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

void set_amediaformat(AMediaFormat* format, const char* mime_type,
                      const int& width, const int& height,
                      const std::string& color_format, int* bitrate,
                      const int& quality) {
  // Set basic parameters
  AMediaFormat_setString(format, "mime", mime_type);
  DEBUG(2, "AMediaFormat_setString(format, \"mime\", \"%s\");", mime_type);

  AMediaFormat_setInt32(format, "width", width);
  DEBUG(2, "AMediaFormat_setInt32(format, \"width\", %d);", width);
#if 0
  AMediaFormat_setInt32(format, "stride", width);
  DEBUG(2, "AMediaFormat_setInt32(format, \"stride\", %d);", width);
#endif

  AMediaFormat_setInt32(format, "height", height);
  DEBUG(2, "AMediaFormat_setInt32(format, \"height\", %d);", height);
#if 0
  AMediaFormat_setInt32(format, "slice-height", height);
  DEBUG(2, "AMediaFormat_setInt32(format, \"slice-height\", %d);", height);
#endif

  int32_t color_fmt = get_color_format(color_format);
  DEBUG(1, "Setting color-format to %d (%s)", color_fmt, color_format.c_str());
  AMediaFormat_setInt32(format, "color-format", color_fmt);
  DEBUG(2, "AMediaFormat_setInt32(format, \"color-format\", %d);", color_fmt);

  // Do not set stride/slice-height - let encoder determine proper values
  // Setting these can cause issues with some codecs

  // TODO(chema): reconsider this
  int frame_rate = 30;
  AMediaFormat_setInt32(format, "frame-rate", frame_rate);
  DEBUG(2, "AMediaFormat_setInt32(format, \"frame-rate\", %d);", frame_rate);

  // set the key frame interval (GoP) to all-key frames
  int i_frame_interval = 0;
  AMediaFormat_setInt32(format, "i-frame-interval", i_frame_interval);
  DEBUG(2, "AMediaFormat_setInt32(format, \"i-frame-interval\", %d);",
        i_frame_interval);

  // TODO(chema): reconsider this
  // Set bitrate
  if (*bitrate < 0) {
    *bitrate = calculate_bitrate((quality >= 0) ? quality : DEFAULT_QUALITY,
                                 width, height);
  }
  AMediaFormat_setInt32(format, "bitrate", *bitrate);
  DEBUG(2, "AMediaFormat_setInt32(format, \"bitrate\", %d);", *bitrate);

  // set bitrate mode
  // TODO(chema): reconsider this
  // 0=CQ, 1=VBR, 2=CBR  (same as Java constants)
  int bitrate_mode = 1;
  AMediaFormat_setInt32(format, "bitrate-mode", bitrate_mode);
  DEBUG(2, "AMediaFormat_setInt32(format, \"bitrate-mode\", %i);",
        bitrate_mode);

  int max_b_frames = 0;
  AMediaFormat_setInt32(format, "max-bframes", max_b_frames);
  DEBUG(2, "AMediaFormat_setInt32(format, \"max-bframes\", %d);", max_b_frames);

#if 0
  // some encoders (e.g. c2.android.hevc.encoder) have problems with this
  // logcat:
  // [timestamp] E CCodec  : Failed to set KEY_PREPEND_HEADER_TO_SYNC_FRAMES
  // ask the encoder to prepend a config frame to first IDR (raw .hevc)
  AMediaFormat_setInt32(format, "prepend-sps-pps-to-idr-frames", 1);
  DEBUG(2, "AMediaFormat_setInt32(format, \"prepend-sps-pps-to-idr-frames\", %d);", 1);
#endif
}

// Encode frames using MediaCodec
static int encode_frames(const Options& opt) {
  media_status_t status;

  // initialize Binder thread pool for MediaCodec callbacks
  // The binder thread pool needs to be started, otherwise the application will
  // not receive callbacks from the MediaCodec service. Unless we add it,
  // - input buffers are queued successfully (one-way calls), but
  // - output buffers never come back (callbacks never arrive because there
  //   are no threads listening)
  if (!init_binder_thread_pool(g_debug_level)) {
    fprintf(stderr, "Warning: Failed to initialize Binder thread pool\n");
    fprintf(stderr, "MediaCodec may not work correctly\n");
    // Continue anyway - might work in some configurations
  } else {
    DEBUG(1, "Binder thread pool initialized successfully");
  }

  // 1. open input file
  DEBUG(1, "Opening input file: %s", opt.input_file.c_str());
  int input_fd = open(opt.input_file.c_str(), O_RDONLY);
  if (input_fd < 0) {
    fprintf(stderr, "Error: Cannot open input file: %s\n",
            opt.input_file.c_str());
    return 1;
  }
  DEBUG(1, "Input file opened successfully");

  // 2. open output file
  DEBUG(1, "Opening output file: %s", opt.output_file.c_str());
  FILE* output_fp = fopen(opt.output_file.c_str(), "wb");
  if (output_fp == nullptr) {
    fprintf(stderr, "Error: Cannot create output file: %s\n",
            opt.output_file.c_str());
    close(input_fd);
    return 1;
  }
  DEBUG(1, "Output file opened successfully");

  // 3. set codec format
  // determine MIME type from codec name
  const char* mime_type = "video/hevc";  // Default to HEVC
  if (opt.codec_name.find("heic") != std::string::npos) {
    mime_type = "image/vnd.android.heic";
  } else if (opt.codec_name.find("hevc") != std::string::npos) {
    mime_type = "video/hevc";
  } else if (opt.codec_name.find("avc") != std::string::npos ||
             opt.codec_name.find("h264") != std::string::npos) {
    mime_type = "video/avc";
  } else if (opt.codec_name.find("vp9") != std::string::npos) {
    mime_type = "video/x-vnd.on2.vp9";
  } else if (opt.codec_name.find("vp8") != std::string::npos) {
    mime_type = "video/x-vnd.on2.vp8";
  } else if (opt.codec_name.find("av1") != std::string::npos) {
    mime_type = "video/av01";
  }
  // create format
  AMediaFormat* format = AMediaFormat_new();
  // Make a local copy of bitrate since set_amediaformat may modify it
  int bitrate = opt.bitrate;
  set_amediaformat(format, mime_type, opt.width, opt.height, opt.color_format,
                   &bitrate, opt.quality);
  DEBUG(1, "Encoding with: %s", opt.codec_name.c_str());
  DEBUG(1, "MIME type: %s", mime_type);
  DEBUG(1, "resolution: %dx%d bitrate: %d frames: %d", opt.width, opt.height,
        bitrate, opt.frame_count);

  // 4. create codec
#if 1
  DEBUG(1, "Creating codec: AMediaCodec_createCodecByName(%s)",
        opt.codec_name.c_str());
  AMediaCodec* codec = AMediaCodec_createCodecByName(opt.codec_name.c_str());
  if (!codec) {
    fprintf(stderr, "Error: Cannot create codec: %s\n", opt.codec_name.c_str());
    close(input_fd);
    fclose(output_fp);
    return 1;
  }
  DEBUG(1, "Codec created successfully");
#else
  DEBUG(1, "Creating codec: AMediaCodec_createCodecByType(%s)", mime_type);
  AMediaCodec* codec = AMediaCodec_createCodecByType(mime_type);
  if (!codec) {
    fprintf(stderr, "Error: Cannot create codec: %s\n", mime_type);
    close(input_fd);
    fclose(output_fp);
    return 1;
  }
  DEBUG(1, "Codec created successfully");
#endif

  // 5. configure codec with format
  DEBUG(1, "Configuring codec...");
  DEBUG(2,
        "AMediaCodec_configure(codec, format, nullptr, nullptr, "
        "AMEDIACODEC_CONFIGURE_FLAG_ENCODE);");
  status = AMediaCodec_configure(codec, format, nullptr, nullptr,
                                 AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  // TODO: move to the end?
  AMediaFormat_delete(format);

  if (status != AMEDIA_OK) {
    fprintf(stderr, "Error: Cannot configure codec: %d\n", status);
    AMediaCodec_delete(codec);
    close(input_fd);
    fclose(output_fp);
    return 1;
  }
  DEBUG(1, "Codec configured successfully");

  // 6. start codec
  DEBUG(1, "Starting codec...");
  DEBUG(2, "AMediaCodec_start(codec);");
  status = AMediaCodec_start(codec);
  if (status != AMEDIA_OK) {
    fprintf(stderr, "Error: Cannot start codec: %d\n", status);
    AMediaCodec_delete(codec);
    close(input_fd);
    fclose(output_fp);
    return 1;
  }
  DEBUG(1, "Codec started successfully");

  // 7. read frame data once before encoding loop
  size_t frame_size = get_frame_size(opt.color_format, opt.width, opt.height);
  std::vector<uint8_t> frame_data(frame_size);
  DEBUG(1, "Reading %zu bytes from input file...", frame_size);
  ssize_t bytes_read = read(input_fd, frame_data.data(), frame_size);
  DEBUG(1, "Read %zd bytes", bytes_read);
  if (bytes_read != (ssize_t)frame_size) {
    fprintf(
        stderr,
        "Error: Could not read complete frame (got %zd bytes, expected %zu)\n",
        bytes_read, frame_size);
    AMediaCodec_delete(codec);
    close(input_fd);
    fclose(output_fp);
    return 1;
  }

  // 8. encoding loop
  AMediaCodecBufferInfo info;
  int frames_sent = 0;
  int frames_recv = 0;
  bool input_eos_sent = false;
  bool output_eos_recv = false;
  int64_t timeout_us = 500000;

  while (!output_eos_recv) {
    if (!input_eos_sent) {
      // 8.1. get (dequeue) input buffer(s)
      ssize_t input_buffer_index =
          AMediaCodec_dequeueInputBuffer(codec, timeout_us);

      if (input_buffer_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        // no output buffer available
        DEBUG(2,
              "AMediaCodec_dequeueInputBuffer() -> "
              "AMEDIACODEC_INFO_TRY_AGAIN_LATER");

      } else if (input_buffer_index >= 0) {
        DEBUG(1,
              "AMediaCodec_dequeueInputBuffer(codec, timeout_us: %zu) -> "
              "input_buffer_index: %zu",
              timeout_us, input_buffer_index);
        if (input_buffer_index >= 0) {
          // got an input buffer
          size_t input_buffer_size;
          // 8.2. get the input buffer
          uint8_t* input_buffer = AMediaCodec_getInputBuffer(
              codec, (size_t)input_buffer_index, &input_buffer_size);
          DEBUG(1,
                "AMediaCodec_getInputBuffer(codec, input_buffer_index: %zu, "
                "&input_buffer_size: %zu) -> input_buffer: %p",
                input_buffer_index, input_buffer_size, input_buffer);
          if (frames_sent < opt.frame_count) {
            memcpy(input_buffer, frame_data.data(), frame_size);
            uint64_t pts_timestamp_us = frames_sent * 33'000;
            // 8.3. re-queue input buffer with actual frame (reusing same frame
            // data)
            AMediaCodec_queueInputBuffer(codec, (size_t)input_buffer_index, 0,
                                         frame_size, pts_timestamp_us, 0);
            DEBUG(1,
                  "AMediaCodec_queueInputBuffer(codec, input_buffer_index: "
                  "%zu, 0, "
                  "frame_size: %zu, pts_timestamp_us: %zu, flags: 0)",
                  input_buffer_index, frame_size, pts_timestamp_us);
            frames_sent++;
          } else {
            // 8.3. re-queue input buffer with EOS
            DEBUG(1,
                  "AMediaCodec_queueInputBuffer(codec, input_buffer_index: "
                  "%zu, 0, "
                  "frame_size: 0, pts_timestamp_us: 0, flags: "
                  "AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)",
                  input_buffer_index);
            AMediaCodec_queueInputBuffer(codec, (size_t)input_buffer_index, 0,
                                         0, 0,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            input_eos_sent = true;
          }
        }
      }
    }

    // 8.4. get (dequeue) an output buffer
    ssize_t output_buffer_index =
        AMediaCodec_dequeueOutputBuffer(codec, &info, timeout_us);

    // check whether the output buffer is valid (info is
    // valid iff the output buffer is valid)
    if (output_buffer_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      // no output buffer available
      DEBUG(2,
            "AMediaCodec_dequeueOutputBuffer() -> "
            "AMEDIACODEC_INFO_TRY_AGAIN_LATER");

    } else if (output_buffer_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      DEBUG(2,
            "AMediaCodec_dequeueOutputBuffer() -> "
            "AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED");
      // handle the new format
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
      // buffer is valid
      size_t output_buffer_size;
      // 8.5. get output buffer
      uint8_t* output_buffer = AMediaCodec_getOutputBuffer(
          codec, (size_t)output_buffer_index, &output_buffer_size);
      DEBUG(1,
            "AMediaCodec_getOutputBuffer(codec, output_buffer_index: %zi, "
            "&output_buffer_size: %zu)",
            output_buffer_index, output_buffer_size);

      if (info.size > 0 && output_buffer) {
        const bool is_config =
            (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
        if (is_config) {
          // this is a config frame ([VPS/]SPS/PPS)
          DEBUG(1, "... this is a config frame");
        } else {
          DEBUG(1, "... this is a buffer frame");
          frames_recv++;
        }
        // there was an actual output buffer
        fwrite(output_buffer + info.offset, 1, info.size, output_fp);
      }
      const bool is_eos =
          (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
      if (is_eos) {
        // there are no more output buffers
        output_eos_recv = true;
      }

      // 8.6. re-queue output buffer
      AMediaCodec_releaseOutputBuffer(codec, (size_t)output_buffer_index,
                                      false);
      DEBUG(1,
            "AMediaCodec_releaseOutputBuffer(codec, output_buffer_index: %zi, "
            "false)",
            output_buffer_index);
    }
  }

  DEBUG(1, "Encoded %d frames, received %d frames", frames_sent, frames_recv);

  // clean up
  // Flush codec to clear any pending operations
  DEBUG(2, "Flushing codec...");
  AMediaCodec_flush(codec);
  // Stop codec to signal no more work
  DEBUG(2, "Stopping codec...");
  AMediaCodec_stop(codec);
  // Give binder thread time to finish processing before deleting codec
  stop_binder_thread_pool();
  // Now safe to delete codec
  DEBUG(2, "Deleting codec...");
  AMediaCodec_delete(codec);
  close(input_fd);
  fclose(output_fp);

  return 0;

#if 0

  // 8. encoding loop
  DEBUG(1, "Entering encoding loop (will encode same frame %d times)", opt.frame_count);
  bool input_done = false;
  bool output_done = false;
  int frames_sent = 0;
  int frames_recv = 0;
  int64_t timestamp_us = 0;
  const int64_t frame_duration_us = 1000000 / opt.frame_rate;
  int loop_iterations = 0;
  const int max_iterations = 1000;  // Safety limit to prevent infinite loops

  while (!output_done) {
    loop_iterations++;
    if (loop_iterations > max_iterations) {
      fprintf(
          stderr,
          "Error: Encoding timeout after %d iterations (fed=%d, received=%d)\n",
          loop_iterations, frames_sent, frames_recv);
      break;
    }
    DEBUG(3,
          "Loop iteration: fed=%d, received=%d, input_done=%d, output_done=%d",
          frames_sent, frames_recv, input_done, output_done);

    // 8.1. feed input
    if (!input_done && frames_sent < opt.frame_count) {
      DEBUG(2, "Dequeuing input buffer (frame %d/%d)...", frames_sent + 1,
            opt.frame_count);
      DEBUG(2, "AMediaCodec_dequeueInputBuffer(codec, 10000);");
      ssize_t buf_idx =
          AMediaCodec_dequeueInputBuffer(codec, 10000);  // 10ms timeout

      if (buf_idx >= 0) {
        DEBUG(2, "Got input buffer %zd", buf_idx);
        size_t buf_size = 0;
        DEBUG(2,
              "AMediaCodec_getInputBuffer(codec, buf_idx=%zu, "
              "&buf_size)(codec, 10000);",
              buf_idx);
        uint8_t* buf = AMediaCodec_getInputBuffer(codec, buf_idx, &buf_size);
        DEBUG(2, "Input buffer: ptr=%p, size=%zu, needed=%zu", buf, buf_size,
              frame_size);

        if (buf && buf_size >= frame_size) {
          // Copy frame data to encoder buffer (reusing the same frame data)
          DEBUG(2, "Copying %zu bytes to encoder buffer...", frame_size);
          memcpy(buf, frame_data.data(), frame_size);

          // No special flags for regular frames (encoder decides keyframes)
          uint32_t flags = 0;

          DEBUG(
              2,
              "Queuing input buffer %zd (size=%zu, timestamp=%lld, flags=0x%x)",
              buf_idx, frame_size, (long long)timestamp_us, flags);
          DEBUG(2,
                "AMediaCodec_queueInputBuffer(codec, buf_idx=%zu, 0, "
                "frame_size=%zu, timestamp_us=%li, flags=%i);",
                buf_idx, frame_size, timestamp_us, flags);
          status = AMediaCodec_queueInputBuffer(codec, buf_idx, 0, frame_size,
                                                timestamp_us, flags);
          if (status == AMEDIA_OK) {
            frames_sent++;
            timestamp_us += frame_duration_us;
            DEBUG(1, "Input frame %d queued successfully", frames_sent);
          } else {
            fprintf(stderr, "Error: queueInputBuffer failed: %d\n", status);
          }
        } else {
          fprintf(stderr, "Error: Input buffer too small (got %zu, need %zu)\n",
                  buf_size, frame_size);
          DEBUG(2,
                "AMediaCodec_queueInputBuffer(codec, buf_idx=%zu, 0, 0, "
                "timestamp_us=%li, flags=%i);",
                buf_idx, timestamp_us, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
          AMediaCodec_queueInputBuffer(codec, buf_idx, 0, 0, timestamp_us,
                                       AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
          input_done = true;
        }
      } else {
        DEBUG(2, "No input buffer available (returned %zd)", buf_idx);
      }
    } else if (!input_done) {
      // All frames fed, send EOS
      DEBUG(2, "All frames fed (%d/%d), sending EOS...", frames_sent,
            opt.frame_count);
      DEBUG(2, "AMediaCodec_dequeueInputBuffer(codec, 10000);");
      ssize_t buf_idx = AMediaCodec_dequeueInputBuffer(codec, 10000);
      if (buf_idx >= 0) {
        DEBUG(2, "Got input buffer %zd for EOS", buf_idx);
        DEBUG(2,
              "AMediaCodec_queueInputBuffer(codec, buf_idx=%zu, 0, 0, "
              "timestamp_us=%li, flags=%i);",
              buf_idx, timestamp_us, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        status =
            AMediaCodec_queueInputBuffer(codec, buf_idx, 0, 0, timestamp_us,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        if (status == AMEDIA_OK) {
          input_done = true;
          DEBUG(1, "EOS sent successfully");
        } else {
          DEBUG(1, "EOS queue failed with status %d", status);
        }
      } else {
        DEBUG(2, "Could not get input buffer for EOS (returned %zd)", buf_idx);
      }
    }

    // Retrieve output
    DEBUG(2, "Dequeuing output buffer...");
    AMediaCodecBufferInfo info;
    // Use longer timeout for output (100ms instead of 10ms) since encoding can
    // take time
    DEBUG(2, "AMediaCodec_dequeueOutputBuffer(codec, &info, 100000);");
    ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec, &info, 100000);
    DEBUG(2, "Dequeue output returned: %zd", out_idx);

    if (out_idx >= 0) {
      DEBUG(2, "Got output buffer %zd (size=%d, flags=0x%x)", out_idx,
            info.size, info.flags);
      if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        DEBUG(1, "Received END_OF_STREAM flag");
        output_done = true;
      }

      if (info.size > 0) {
        size_t out_size = 0;
        DEBUG(2, "AMediaCodec_getOutputBuffer(codec, out_idx=%zi, &out_size);",
              out_idx);
        uint8_t* out_buf =
            AMediaCodec_getOutputBuffer(codec, out_idx, &out_size);

        if (out_buf) {
          // Write to output file
          DEBUG(2, "Writing %d bytes to output", info.size);
          ssize_t written = write(output_fd, out_buf + info.offset, info.size);
          if (written != info.size) {
            fprintf(stderr, "Error: Write failed\n");
          } else {
            frames_recv++;
            DEBUG(1, "Frame %d encoded (%d bytes)", frames_recv, info.size);
          }
        }
      }

      DEBUG(2, "AMediaCodec_releaseOutputBuffer(codec, out_idx=%zi, false);",
            out_idx);
      AMediaCodec_releaseOutputBuffer(codec, out_idx, false);
    } else if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      DEBUG(2, "AMediaCodec_getOutputFormat(codec);");
      AMediaFormat* out_format = AMediaCodec_getOutputFormat(codec);
      DEBUG(1, "Output format changed: %s", AMediaFormat_toString(out_format));
      fprintf(stderr, "Output format changed: %s\n",
              AMediaFormat_toString(out_format));
      AMediaFormat_delete(out_format);
    } else if (out_idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      DEBUG(3, "No output available yet");
      // No output available yet, continue
      if (input_done && frames_recv >= frames_sent) {
        // All frames processed
        DEBUG(1, "All frames processed, ending");
        output_done = true;
      }
    } else {
      // Error
      fprintf(stderr, "Error: dequeueOutputBuffer returned %zd\n", out_idx);
    }
  }

  fprintf(stderr, "Encoded %d frames, received %d frames\n", frames_sent,
          frames_recv);

  // Cleanup
  DEBUG(2, "AMediaCodec_stop(codec);");
  AMediaCodec_stop(codec);
  DEBUG(2, "AMediaCodec_delete(codec);");
  AMediaCodec_delete(codec);
  close(input_fd);
  close(output_fd);
#endif
}

#else  // !__ANDROID__

static int list_codecs_cmd(const Options&) {
  fprintf(stderr, "Error: This tool only works on Android\n");
  return 1;
}

static int encode_frames(const Options&) {
  fprintf(stderr, "Error: This tool only works on Android\n");
  return 1;
}

#endif  // __ANDROID__

int main(int argc, char** argv) {
  Options opt;

  if (!parse_args(argc, argv, opt)) {
    print_usage(argv[0]);
    return 2;
  }

  // Set global debug level
  g_debug_level = opt.debug_level;

  if (opt.list_codecs || opt.list_image_codecs) {
    return list_codecs_cmd(opt);
  }

  return encode_frames(opt);
}
