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
  input.debug_level = debug_level;

  int errors = 0;

  // 1. WebP encoding
  if (run_webp) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    if (anicet_run_webp(&input, num_runs, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[512];
        snprintf(filename, sizeof(filename),
                 "%s/%s.webp.index_%zu.width_%d.height_%d.color_%s.webp",
                 dump_output_dir, dump_output_prefix, i, width, height,
                 color_format);
        local_output.output_files.push_back(filename);

        // Write output file if requested
        if (dump_output) {
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
      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[512];
        snprintf(filename, sizeof(filename),
                 "%s/%s.webp-nonopt.index_%zu.width_%d.height_%d.color_%s.webp",
                 dump_output_dir, dump_output_prefix, i, width, height,
                 color_format);
        local_output.output_files.push_back(filename);

        // Write output file if requested
        if (dump_output) {
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
      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[512];
        snprintf(
            filename, sizeof(filename),
            "%s/%s.libjpegturbo.index_%zu.width_%d.height_%d.color_%s.jpeg",
            dump_output_dir, dump_output_prefix, i, width, height,
            color_format);
        local_output.output_files.push_back(filename);

        // Write output file if requested
        if (dump_output) {
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
      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[512];
        snprintf(filename, sizeof(filename),
                 "%s/"
                 "%s.libjpegturbo-nonopt.index_%"
                 "zu.width_%d.height_%d.color_%s.jpeg",
                 dump_output_dir, dump_output_prefix, i, width, height,
                 color_format);
        local_output.output_files.push_back(filename);

        // Write output file if requested
        if (dump_output) {
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
      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[512];
        snprintf(filename, sizeof(filename),
                 "%s/%s.jpegli.index_%zu.width_%d.height_%d.color_%s.jpeg",
                 dump_output_dir, dump_output_prefix, i, width, height,
                 color_format);
        local_output.output_files.push_back(filename);

        // Write output file if requested
        if (dump_output) {
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
      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[512];
        snprintf(filename, sizeof(filename),
                 "%s/%s.x265-8bit.index_%zu.width_%d.height_%d.color_%s.265",
                 dump_output_dir, dump_output_prefix, i, width, height,
                 color_format);
        local_output.output_files.push_back(filename);

        // Write output file if requested
        if (dump_output) {
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
      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[512];
        snprintf(
            filename, sizeof(filename),
            "%s/"
            "%s.x265-8bit-nonopt.index_%zu.width_%d.height_%d.color_%s.265",
            dump_output_dir, dump_output_prefix, i, width, height,
            color_format);
        local_output.output_files.push_back(filename);

        // Write output file if requested
        if (dump_output) {
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
      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[512];
        snprintf(filename, sizeof(filename),
                 "%s/%s.svtav1.index_%zu.width_%d.height_%d.color_%s.av1",
                 dump_output_dir, dump_output_prefix, i, width, height,
                 color_format);
        local_output.output_files.push_back(filename);

        // Write output file if requested
        if (dump_output) {
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
      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[512];
        snprintf(filename, sizeof(filename),
                 "%s/%s.mediacodec.index_%zu.width_%d.height_%d.color_%s.bin",
                 dump_output_dir, dump_output_prefix, i, width, height,
                 color_format);
        local_output.output_files.push_back(filename);

        // Write output file if requested
        if (dump_output) {
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
