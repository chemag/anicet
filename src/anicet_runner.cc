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

// Main experiment function - uses all sub-runners
int anicet_experiment(const uint8_t* buffer, size_t buf_size, int height,
                      int width, const char* color_format,
                      const char* codec_name, int num_runs, bool dump_output,
                      const char* dump_output_dir,
                      const char* dump_output_prefix) {
  // Validate inputs
  if (!buffer || buf_size == 0 || height <= 0 || width <= 0 || !color_format ||
      !codec_name) {
    fprintf(stderr, "Invalid input parameters\n");
    return -1;
  }

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

  printf("Encoding %dx%d %s image (%zu bytes) with codec: %s...\n", width,
         height, color_format, buf_size, codec_name);

  int errors = 0;

  // 1. WebP encoding
  if (run_webp) {
    printf("\n--- WebP ---\n");
    CodecOutput output;
    output.dump_output = dump_output;
    if (anicet_run_webp(buffer, buf_size, height, width, color_format, num_runs,
                        &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("WebP: Encoded to %zu bytes (%.2f%% of original)\n", last_size,
             (last_size * 100.0) / buf_size);

      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.webp.%02zu.webp",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(output.frame_buffers[i].data(), 1, output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }
    } else {
      fprintf(stderr, "WebP: Encoding failed\n");
      errors++;
    }
  }

  // 1b. WebP encoding (nonopt)
  if (run_webp_nonopt) {
    printf("\n--- WebP (nonopt) ---\n");
    CodecOutput output;
    output.dump_output = dump_output;
    if (anicet_run_webp_nonopt(buffer, buf_size, height, width, color_format,
                               num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("WebP (nonopt): Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);

      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.webp-nonopt.%02zu.webp",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(output.frame_buffers[i].data(), 1, output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }
    } else {
      fprintf(stderr, "WebP (nonopt): Encoding failed\n");
      errors++;
    }
  }

  // 2. libjpeg-turbo encoding (opt)
  if (run_libjpeg_turbo) {
    printf("\n--- libjpeg-turbo ---\n");
    CodecOutput output;
    output.dump_output = dump_output;
    if (anicet_run_libjpegturbo(buffer, buf_size, height, width, color_format,
                                num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("TurboJPEG: Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);

      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.libjpegturbo.%02zu.jpeg",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(output.frame_buffers[i].data(), 1, output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }
    } else {
      fprintf(stderr, "TurboJPEG: Encoding failed\n");
      errors++;
    }
  }

  // 2b. libjpeg-turbo encoding (nonopt)
  if (run_libjpeg_turbo_nonopt) {
    printf("\n--- libjpeg-turbo (nonopt) ---\n");
    CodecOutput output;
    output.dump_output = dump_output;
    if (anicet_run_libjpegturbo_nonopt(buffer, buf_size, height, width,
                                       color_format, num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("TurboJPEG (nonopt): Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);

      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename),
                   "%s/%s.libjpegturbo-nonopt.%02zu.jpeg", dump_output_dir,
                   dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(output.frame_buffers[i].data(), 1, output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }
    } else {
      fprintf(stderr, "TurboJPEG (nonopt): Encoding failed\n");
      errors++;
    }
  }

  // 3. jpegli encoding
  if (run_jpegli) {
    printf("\n--- jpegli ---\n");
    CodecOutput output;
    output.dump_output = dump_output;
    if (anicet_run_jpegli(buffer, buf_size, height, width, color_format,
                          num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("jpegli: Encoded to %zu bytes (%.2f%% of original)\n", last_size,
             (last_size * 100.0) / buf_size);

      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.jpegli.%02zu.jpeg",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(output.frame_buffers[i].data(), 1, output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }
    } else {
      fprintf(stderr, "jpegli: Encoding failed\n");
      errors++;
    }
  }

  // 4. x265 (H.265/HEVC) 8-bit encoding (opt)
  if (run_x265_8bit) {
    printf("\n--- x265 (H.265/HEVC) 8-bit ---\n");
    CodecOutput output;
    output.dump_output = dump_output;
    if (anicet_run_x265_8bit(buffer, buf_size, height, width, color_format,
                             num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("x265-8bit: Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);

      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.x265-8bit.%02zu.265",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(output.frame_buffers[i].data(), 1, output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }
    } else {
      fprintf(stderr, "x265-8bit: Encoding failed\n");
      errors++;
    }
  }

  // 4b. x265 (H.265/HEVC) 8-bit encoding (nonopt)
  if (run_x265_8bit_nonopt) {
    printf("\n--- x265 (H.265/HEVC) 8-bit (nonopt) ---\n");
    CodecOutput output;
    output.dump_output = dump_output;
    if (anicet_run_x265_8bit_nonopt(buffer, buf_size, height, width,
                                    color_format, num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("x265-8bit (nonopt): Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);

      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename),
                   "%s/%s.x265-8bit-nonopt.%02zu.265", dump_output_dir,
                   dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(output.frame_buffers[i].data(), 1, output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }
    } else {
      fprintf(stderr, "x265-8bit (nonopt): Encoding failed\n");
      errors++;
    }
  }

  // 5. SVT-AV1 encoding
  if (run_svtav1) {
    printf("\n--- SVT-AV1 ---\n");
    CodecOutput output;
    output.dump_output = dump_output;
    if (anicet_run_svtav1(buffer, buf_size, height, width, color_format,
                          num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("SVT-AV1: Encoded to %zu bytes (%.2f%% of original)\n", last_size,
             (last_size * 100.0) / buf_size);

      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.svtav1.%02zu.av1",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(output.frame_buffers[i].data(), 1, output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }
    } else {
      fprintf(stderr, "SVT-AV1: Encoding failed\n");
      errors++;
    }
  }

  // 6. Android MediaCodec encoding (only on Android)
  if (run_mediacodec) {
    printf("\n--- Android MediaCodec ---\n");
#ifdef __ANDROID__
    CodecOutput output;
    output.dump_output = dump_output;
    if (anicet_run_mediacodec(buffer, buf_size, height, width, color_format,
                              "c2.android.hevc.encoder", num_runs,
                              &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("MediaCodec: Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);

      // Write output files if requested
      if (dump_output) {
        for (size_t i = 0; i < output.num_frames(); i++) {
          char filename[512];
          snprintf(filename, sizeof(filename), "%s/%s.mediacodec.%02zu.bin",
                   dump_output_dir, dump_output_prefix, i);
          FILE* f = fopen(filename, "wb");
          if (f) {
            fwrite(output.frame_buffers[i].data(), 1, output.frame_sizes[i], f);
            fclose(f);
          }
        }
      }
    } else {
      fprintf(stderr, "MediaCodec: Encoding failed\n");
      errors++;
    }
#else
    printf("MediaCodec: Skipped (not on Android)\n");
#endif
  }

  printf("\n=== Encoding complete: %d errors ===\n", errors);
  return errors;
}
