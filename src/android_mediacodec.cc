// android_mediacodec.cc
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

#include "android_binder_init.h"
#include "android_mediacodec_lib.h"
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

// File I/O wrapper: Read input file, encode, write output file
static int encode_frames(const Options& opt) {
  // Set global debug level
  g_debug_level = opt.debug_level;

  // 1. Read input file into buffer
  DEBUG(1, "Opening input file: %s", opt.input_file.c_str());
  int input_fd = open(opt.input_file.c_str(), O_RDONLY);
  if (input_fd < 0) {
    fprintf(stderr, "Error: Cannot open input file: %s\n",
            opt.input_file.c_str());
    return 1;
  }

  // Get file size
  struct stat st;
  if (fstat(input_fd, &st) != 0) {
    fprintf(stderr, "Error: Cannot get file size\n");
    close(input_fd);
    return 1;
  }
  size_t file_size = st.st_size;

  // Calculate size for one frame (we reuse it for all frames)
  size_t frame_size = get_frame_size(opt.color_format, opt.width, opt.height);
  size_t read_size = frame_size;
  if (file_size < read_size) {
    fprintf(stderr,
            "Error: Input file too small (got %zu bytes, need %zu for one "
            "frame)\n",
            file_size, read_size);
    close(input_fd);
    return 1;
  }

  DEBUG(1, "Reading %zu bytes from input file...", read_size);
  uint8_t* input_buffer = (uint8_t*)malloc(read_size);
  if (!input_buffer) {
    fprintf(stderr, "Error: Cannot allocate input buffer\n");
    close(input_fd);
    return 1;
  }

  ssize_t bytes_read = read(input_fd, input_buffer, read_size);
  close(input_fd);

  if (bytes_read != (ssize_t)read_size) {
    fprintf(stderr,
            "Error: Could not read complete frame (got %zd bytes, expected "
            "%zu)\n",
            bytes_read, read_size);
    free(input_buffer);
    return 1;
  }
  DEBUG(1, "Input file read successfully (%zu bytes)", read_size);

  // 2. Call library function to encode
  DEBUG(1, "Calling android_mediacodec_encode_frame()...");

  // Prepare format configuration
  MediaCodecFormat format;
  format.width = opt.width;
  format.height = opt.height;
  format.codec_name = opt.codec_name.c_str();
  format.color_format = opt.color_format.c_str();
  format.quality = opt.quality;
  format.bitrate = opt.bitrate;
  format.frame_count = opt.frame_count;
  format.debug_level = opt.debug_level;

  uint8_t* output_buffer = nullptr;
  size_t output_size = 0;
  int result = android_mediacodec_encode_frame(input_buffer, read_size, &format,
                                               &output_buffer, &output_size);

  // Free input buffer
  free(input_buffer);

  // Check encoding result
  if (result != 0) {
    fprintf(stderr, "Error: Encoding failed with status %d\n", result);
    if (output_buffer) {
      free(output_buffer);
    }
    return 1;
  }

  if (!output_buffer || output_size == 0) {
    fprintf(stderr, "Error: No output produced\n");
    if (output_buffer) {
      free(output_buffer);
    }
    return 1;
  }

  DEBUG(1, "Encoding completed successfully, output size: %zu bytes",
        output_size);

  // 3. Write output to file
  DEBUG(1, "Writing output to: %s", opt.output_file.c_str());
  FILE* output_fp = fopen(opt.output_file.c_str(), "wb");
  if (!output_fp) {
    fprintf(stderr, "Error: Cannot create output file: %s\n",
            opt.output_file.c_str());
    free(output_buffer);
    return 1;
  }

  size_t bytes_written = fwrite(output_buffer, 1, output_size, output_fp);
  fclose(output_fp);

  if (bytes_written != output_size) {
    fprintf(stderr, "Error: Could not write complete output\n");
    free(output_buffer);
    return 1;
  }

  DEBUG(1, "Output file written successfully (%zu bytes)", bytes_written);

  // 4. Clean up
  free(output_buffer);

  return 0;
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
