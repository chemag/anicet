// anicet_runner.cc
// Encoder experiment runner implementation

#include "anicet_runner.h"

#include <cstdio>
#include <cstring>

// Individual codec runners
#include "anicet_runner_jpegli.h"
#include "anicet_runner_libjpegturbo.h"
#include "anicet_runner_mediacodec.h"
#include "anicet_runner_svtav1.h"
#include "anicet_runner_webp.h"
#include "anicet_runner_x265.h"
// MediaCodec library for debug level setting
#include "android_mediacodec_lib.h"

// Main experiment function - uses all sub-runners
int anicet_experiment(const uint8_t* buffer, size_t buf_size, int height,
                      int width, const char* color_format,
                      const char* codec_name, int num_runs, bool dump_output,
                      const char* dump_output_dir,
                      const char* dump_output_prefix, int debug_level,
                      CodecOutput* output) {
  // Validate inputs
  if (!buffer || buf_size == 0 || height <= 0 || width <= 0 || !color_format ||
      !codec_name) {
    fprintf(stderr, "Invalid input parameters\n");
    return -1;
  }

  // Set debug level for MediaCodec
  android_mediacodec_set_debug_level(debug_level);

  // Only support yuv420p for now
  if (strcmp(color_format, "yuv420p") != 0) {
    fprintf(stderr, "Only yuv420p format supported currently\n");
    return -1;
  }

  // Determine which codecs to run
  bool run_all = (strcmp(codec_name, "all") == 0);
  bool run_webp = run_all || (strcmp(codec_name, "webp") == 0);
  bool run_webp_nonopt = (strcmp(codec_name, "webp-nonopt") == 0);
  bool run_libjpeg_turbo =
      run_all || (strcmp(codec_name, "libjpeg-turbo") == 0);
  bool run_libjpeg_turbo_nonopt =
      (strcmp(codec_name, "libjpeg-turbo-nonopt") == 0);
  bool run_jpegli = run_all || (strcmp(codec_name, "jpegli") == 0);
  bool run_x265_8bit = run_all || (strcmp(codec_name, "x265-8bit") == 0);
  bool run_x265_8bit_nonopt = (strcmp(codec_name, "x265-8bit-nonopt") == 0);
  bool run_svtav1 = run_all || (strcmp(codec_name, "svt-av1") == 0);
  bool run_mediacodec = run_all || (strcmp(codec_name, "mediacodec") == 0);

  // Create CodecInput struct for all encoders
  CodecInput input;
  input.input_buffer = buffer;
  input.input_size = buf_size;
  input.height = height;
  input.width = width;
  input.color_format = color_format;

  int errors = 0;

  // 1. WebP encoding
  if (run_webp) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_webp(&input, num_runs, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < local_output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.webp.%02zu.webp",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(local_output.frame_buffers[i].data(), 1,
                   local_output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }

      // Copy results to output parameter if provided
      if (output != nullptr) {
        *output = std::move(local_output);
      }
    } else {
      fprintf(stderr, "WebP: Encoding failed\n");
      errors++;
    }
  }

  // 1b. WebP encoding (nonopt)
  if (run_webp_nonopt) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_webp_nonopt(&input, num_runs, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < local_output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.webp-nonopt.%02zu.webp",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(local_output.frame_buffers[i].data(), 1,
                   local_output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }

      // Copy results to output parameter if provided
      if (output != nullptr) {
        *output = std::move(local_output);
      }
    } else {
      fprintf(stderr, "WebP (nonopt): Encoding failed\n");
      errors++;
    }
  }

  // 2. libjpeg-turbo encoding (opt)
  if (run_libjpeg_turbo) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_libjpegturbo(&input, num_runs, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < local_output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.libjpegturbo.%02zu.jpeg",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(local_output.frame_buffers[i].data(), 1,
                   local_output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }

      // Copy results to output parameter if provided
      if (output != nullptr) {
        *output = std::move(local_output);
      }
    } else {
      fprintf(stderr, "TurboJPEG: Encoding failed\n");
      errors++;
    }
  }

  // 2b. libjpeg-turbo encoding (nonopt)
  if (run_libjpeg_turbo_nonopt) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_libjpegturbo_nonopt(&input, num_runs, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < local_output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename),
                   "%s/%s.libjpegturbo-nonopt.%02zu.jpeg", dump_output_dir,
                   dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(local_output.frame_buffers[i].data(), 1,
                   local_output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }

      // Copy results to output parameter if provided
      if (output != nullptr) {
        *output = std::move(local_output);
      }
    } else {
      fprintf(stderr, "TurboJPEG (nonopt): Encoding failed\n");
      errors++;
    }
  }

  // 3. jpegli encoding
  if (run_jpegli) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_jpegli(&input, num_runs, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < local_output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.jpegli.%02zu.jpeg",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(local_output.frame_buffers[i].data(), 1,
                   local_output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }

      // Copy results to output parameter if provided
      if (output != nullptr) {
        *output = std::move(local_output);
      }
    } else {
      fprintf(stderr, "jpegli: Encoding failed\n");
      errors++;
    }
  }

  // 4. x265 (H.265/HEVC) 8-bit encoding (opt)
  if (run_x265_8bit) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_x265_8bit(&input, num_runs, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < local_output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.x265-8bit.%02zu.265",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(local_output.frame_buffers[i].data(), 1,
                   local_output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }

      // Copy results to output parameter if provided
      if (output != nullptr) {
        *output = std::move(local_output);
      }
    } else {
      fprintf(stderr, "x265-8bit: Encoding failed\n");
      errors++;
    }
  }

  // 4b. x265 (H.265/HEVC) 8-bit encoding (nonopt)
  if (run_x265_8bit_nonopt) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_x265_8bit_nonopt(&input, num_runs, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < local_output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename),
                   "%s/%s.x265-8bit-nonopt.%02zu.265", dump_output_dir,
                   dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(local_output.frame_buffers[i].data(), 1,
                   local_output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }

      // Copy results to output parameter if provided
      if (output != nullptr) {
        *output = std::move(local_output);
      }
    } else {
      fprintf(stderr, "x265-8bit (nonopt): Encoding failed\n");
      errors++;
    }
  }

  // 5. SVT-AV1 encoding
  if (run_svtav1) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_svtav1(&input, num_runs, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < local_output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.svtav1.%02zu.av1",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(local_output.frame_buffers[i].data(), 1,
                   local_output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }

      // Copy results to output parameter if provided
      if (output != nullptr) {
        *output = std::move(local_output);
      }
    } else {
      fprintf(stderr, "SVT-AV1: Encoding failed\n");
      errors++;
    }
  }

  // 6. Android MediaCodec encoding (only on Android)
  if (run_mediacodec) {
#ifdef __ANDROID__
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_mediacodec(&input, "c2.android.hevc.encoder", num_runs,
                              &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < local_output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.mediacodec.%02zu.bin",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(local_output.frame_buffers[i].data(), 1,
                   local_output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }

      // Copy results to output parameter if provided
      if (output != nullptr) {
        *output = std::move(local_output);
      }
    } else {
      fprintf(stderr, "MediaCodec: Encoding failed\n");
      errors++;
    }
#else
#endif
  }

  return errors;
}
