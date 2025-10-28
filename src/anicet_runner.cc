// anicet_runner.cc
// Encoder experiment runner implementation

#include "anicet_runner.h"

#include <cstdio>
#include <cstring>
#include <string>

// Individual codec runners
#include "anicet_runner_jpegli.h"
#include "anicet_runner_libjpegturbo.h"
#include "anicet_runner_mediacodec.h"
#include "anicet_runner_svtav1.h"
#include "anicet_runner_webp.h"
#include "anicet_runner_x265.h"
// MediaCodec library for debug level setting
#include "android_mediacodec_lib.h"

// Helper function to append one CodecOutput to another
static void append_codec_output(CodecOutput* dest, const CodecOutput& src) {
  // Append frame buffers
  dest->frame_buffers.insert(dest->frame_buffers.end(),
                             src.frame_buffers.begin(),
                             src.frame_buffers.end());
  // Append frame sizes
  dest->frame_sizes.insert(dest->frame_sizes.end(), src.frame_sizes.begin(),
                           src.frame_sizes.end());
  // Append timings
  dest->timings.insert(dest->timings.end(), src.timings.begin(),
                       src.timings.end());
  // Append output files
  dest->output_files.insert(dest->output_files.end(), src.output_files.begin(),
                            src.output_files.end());
  // Append CPU profiling
  dest->profile_encode_cpu_ms.insert(dest->profile_encode_cpu_ms.end(),
                                     src.profile_encode_cpu_ms.begin(),
                                     src.profile_encode_cpu_ms.end());
  // Keep dump_output setting
  if (!dest->dump_output) {
    dest->dump_output = src.dump_output;
  }
  // Accumulate memory usage (use maximum)
  if (src.profile_encode_mem_kb > dest->profile_encode_mem_kb) {
    dest->profile_encode_mem_kb = src.profile_encode_mem_kb;
  }
  // Accumulate resource delta
  dest->resource_delta.wall_time_ms += src.resource_delta.wall_time_ms;
  dest->resource_delta.cpu_time_ms += src.resource_delta.cpu_time_ms;
  dest->resource_delta.user_time_ms += src.resource_delta.user_time_ms;
  dest->resource_delta.system_time_ms += src.resource_delta.system_time_ms;
  dest->resource_delta.vm_rss_delta_kb += src.resource_delta.vm_rss_delta_kb;
  dest->resource_delta.vm_size_delta_kb += src.resource_delta.vm_size_delta_kb;
  dest->resource_delta.minor_faults += src.resource_delta.minor_faults;
  dest->resource_delta.major_faults += src.resource_delta.major_faults;
  dest->resource_delta.vol_ctx_switches += src.resource_delta.vol_ctx_switches;
  dest->resource_delta.invol_ctx_switches +=
      src.resource_delta.invol_ctx_switches;
}

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

  // Initialize output to empty state if provided
  if (output != nullptr) {
    output->frame_buffers.clear();
    output->frame_sizes.clear();
    output->timings.clear();
    output->output_files.clear();
    output->profile_encode_cpu_ms.clear();
    output->profile_encode_mem_kb = 0;
    output->dump_output = dump_output;
    memset(&output->resource_delta, 0, sizeof(output->resource_delta));
  }

  // Only support yuv420p for now
  if (strcmp(color_format, "yuv420p") != 0) {
    fprintf(stderr, "Only yuv420p format supported currently\n");
    return -1;
  }

  // Helper lambda to check if a codec is in the comma-separated list
  auto codec_in_list = [codec_name](const char* target) -> bool {
    if (strcmp(codec_name, target) == 0) {
      return true;  // Exact match
    }
    // Check if target appears in comma-separated list
    std::string codec_list(codec_name);
    std::string target_str(target);
    size_t pos = 0;
    while (pos < codec_list.length()) {
      // Find next comma or end of string
      size_t comma = codec_list.find(',', pos);
      if (comma == std::string::npos) {
        comma = codec_list.length();
      }
      // Extract codec name (trim spaces)
      size_t start = pos;
      while (start < comma && codec_list[start] == ' ') start++;
      size_t end = comma;
      while (end > start && codec_list[end - 1] == ' ') end--;

      std::string codec = codec_list.substr(start, end - start);
      if (codec == target_str) {
        return true;
      }
      pos = comma + 1;
    }
    return false;
  };

  // Determine which codecs to run
  bool run_all = codec_in_list("all");
  bool run_webp = run_all || codec_in_list("webp");
  bool run_webp_nonopt = codec_in_list("webp-nonopt");
  bool run_libjpeg_turbo = run_all || codec_in_list("libjpeg-turbo");
  bool run_libjpeg_turbo_nonopt = codec_in_list("libjpeg-turbo-nonopt");
  bool run_jpegli = run_all || codec_in_list("jpegli");
  bool run_x265_8bit = run_all || codec_in_list("x265-8bit");
  bool run_x265_8bit_nonopt = codec_in_list("x265-8bit-nonopt");
  bool run_svtav1 = run_all || codec_in_list("svt-av1");
  bool run_mediacodec = run_all || codec_in_list("mediacodec");

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
    if (anicet::runner::webp::anicet_run(&input, num_runs, &local_output) ==
            0 &&
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

      // Append results to output parameter if provided
      if (output != nullptr) {
        append_codec_output(output, local_output);
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
    if (anicet::runner::webp::anicet_run_nonopt(&input, num_runs,
                                                &local_output) == 0 &&
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

      // Append results to output parameter if provided
      if (output != nullptr) {
        append_codec_output(output, local_output);
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
    if (anicet::runner::libjpegturbo::anicet_run(&input, num_runs,
                                                 &local_output) == 0 &&
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

      // Append results to output parameter if provided
      if (output != nullptr) {
        append_codec_output(output, local_output);
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
    if (anicet::runner::libjpegturbo::anicet_run_nonopt(&input, num_runs,
                                                        &local_output) == 0 &&
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

      // Append results to output parameter if provided
      if (output != nullptr) {
        append_codec_output(output, local_output);
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
    if (anicet::runner::jpegli::anicet_run(&input, num_runs, &local_output) ==
            0 &&
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

      // Append results to output parameter if provided
      if (output != nullptr) {
        append_codec_output(output, local_output);
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
    if (anicet::runner::x265::anicet_run(&input, num_runs, &local_output) ==
            0 &&
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

      // Append results to output parameter if provided
      if (output != nullptr) {
        append_codec_output(output, local_output);
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
    if (anicet::runner::x265::anicet_run_nonopt(&input, num_runs,
                                                &local_output) == 0 &&
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

      // Append results to output parameter if provided
      if (output != nullptr) {
        append_codec_output(output, local_output);
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
    if (anicet::runner::svtav1::anicet_run(&input, num_runs, &local_output) ==
            0 &&
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

      // Append results to output parameter if provided
      if (output != nullptr) {
        append_codec_output(output, local_output);
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
    if (anicet::runner::mediacodec::anicet_run(
            &input, "c2.android.hevc.encoder", num_runs, &local_output) == 0 &&
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

      // Append results to output parameter if provided
      if (output != nullptr) {
        append_codec_output(output, local_output);
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
