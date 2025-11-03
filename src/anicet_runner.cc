// anicet_runner.cc
// Encoder experiment runner implementation

#include "anicet_runner.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
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
  // Copy codec name and params if dest is empty (first codec)
  if (dest->codec_name.empty() && !src.codec_name.empty()) {
    dest->codec_name = src.codec_name;
    dest->codec_params = src.codec_params;
  }
  // Note: If mixing multiple codecs (e.g., --codec all), codec_name will be
  // from the first codec. Individual frames still have correct filenames.
}

// Helper function to validate parameter against a list of valid values
bool validate_parameter_list(const std::string& label,
                             const std::string& param_name,
                             const std::string& param_value,
                             const std::list<std::string>& valid_values) {
  // Empty list means accept any value (no validation)
  if (valid_values.empty()) {
    return true;
  }

  for (const auto& valid : valid_values) {
    if (param_value == valid) {
      return true;
    }
  }

  // Invalid - print error message with valid options
  fprintf(stderr, "%s: Invalid %s '%s'. Valid values are: ", label.c_str(),
          param_name.c_str(), param_value.c_str());
  bool first = true;
  for (const auto& valid : valid_values) {
    fprintf(stderr, "%s%s", first ? "" : ", ", valid.c_str());
    first = false;
  }
  fprintf(stderr, "\n");
  return false;
}

// Helper function to convert parameter_map to string map
static std::map<std::string, std::string> convert_params_to_strings(
    const CodecSetupParameterMap& parameter_map) {
  std::map<std::string, std::string> result;
  for (const auto& [key, value] : parameter_map) {
    // Convert variant to string
    if (std::holds_alternative<std::string>(value)) {
      result[key] = std::get<std::string>(value);
    } else if (std::holds_alternative<int>(value)) {
      result[key] = std::to_string(std::get<int>(value));
    } else if (std::holds_alternative<double>(value)) {
      result[key] = std::to_string(std::get<double>(value));
    }
  }
  return result;
}

// Helper function to get parameter ordering from descriptors
// Returns the order value, or 100 if not found
static int get_param_order(
    const std::string& key,
    const std::map<std::string, anicet::parameter::ParameterDescriptor>&
        descriptors) {
  auto it = descriptors.find(key);
  if (it != descriptors.end()) {
    return it->second.order;
  }
  return 100;  // Default order for parameters not in descriptors
}

// Comparator for custom parameter ordering using descriptors
struct ParamComparator {
  const std::map<std::string, anicet::parameter::ParameterDescriptor>&
      descriptors;

  ParamComparator(
      const std::map<std::string, anicet::parameter::ParameterDescriptor>& desc)
      : descriptors(desc) {}

  bool operator()(const std::pair<std::string, std::string>& a,
                  const std::pair<std::string, std::string>& b) const {
    int order_a = get_param_order(a.first, descriptors);
    int order_b = get_param_order(b.first, descriptors);

    if (order_a != order_b) {
      return order_a < order_b;
    }
    // For parameters with same order, sort alphabetically
    return a.first < b.first;
  }
};

// Helper function to populate codec name and parameters in CodecOutput
static void populate_codec_info(CodecOutput& output,
                                const std::string& codec_name,
                                const CodecSetup& setup) {
  output.codec_name = codec_name;
  output.codec_params = convert_params_to_strings(setup.parameter_map);
}

// Main experiment function - uses all sub-runners
int anicet_experiment(const uint8_t* buffer, size_t buf_size, int height,
                      int width, const char* color_format,
                      const char* codec_name, int num_runs, bool dump_output,
                      const char* dump_output_dir,
                      const char* dump_output_prefix, int debug_level,
                      CodecOutput* output, CodecSetup* codec_setup) {
  // Local DEBUG macro for cleaner debug statements
#define DEBUG(level, ...) ANICET_DEBUG(debug_level, level, __VA_ARGS__)
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
  bool run_libjpeg_turbo = run_all || codec_in_list("libjpeg-turbo");
  bool run_jpegli = run_all || codec_in_list("jpegli");
  bool run_x265 = run_all || codec_in_list("x265");
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
    CodecSetup setup;
    setup.num_runs = num_runs;

    // Use codec_setup if provided, otherwise use defaults
    if (codec_setup) {
      setup = *codec_setup;
    } else {
      // Set default optimization
      setup.parameter_map["optimization"] = "opt";
    }
    if (anicet::runner::webp::anicet_run(&input, &setup, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Store codec name and parameters in output
      populate_codec_info(local_output, "webp", setup);

      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[1024];

        // Build filename with all parameters
        std::stringstream ss;
        ss << dump_output_dir << "/" << dump_output_prefix;
        ss << ".codec_webp";

        // Convert parameters to string map
        std::map<std::string, std::string> params =
            convert_params_to_strings(setup.parameter_map);

        // Convert to vector and sort with custom ordering from descriptors
        std::vector<std::pair<std::string, std::string>> sorted_params(
            params.begin(), params.end());
        std::sort(sorted_params.begin(), sorted_params.end(),
                  ParamComparator(anicet::runner::webp::WEBP_PARAMETERS));

        for (const auto& [key, value] : sorted_params) {
          ss << "." << key << "_" << value;
        }

        ss << ".index_" << i << ".webp";

        snprintf(filename, sizeof(filename), "%s", ss.str().c_str());
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

  // 2. libjpeg-turbo encoding
  if (run_libjpeg_turbo) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    CodecSetup setup;
    setup.num_runs = num_runs;

    // Use codec_setup if provided, otherwise use defaults
    if (codec_setup) {
      setup = *codec_setup;
    } else {
      // Set default optimization
      setup.parameter_map["optimization"] = "opt";
    }
    if (anicet::runner::libjpegturbo::anicet_run(&input, &setup,
                                                 &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Store codec name and parameters in output
      populate_codec_info(local_output, "libjpeg-turbo", setup);

      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[1024];

        // Build filename with all parameters
        std::stringstream ss;
        ss << dump_output_dir << "/" << dump_output_prefix;
        ss << ".codec_libjpeg-turbo";

        // Convert parameters to string map
        std::map<std::string, std::string> params =
            convert_params_to_strings(setup.parameter_map);

        // Convert to vector and sort with custom ordering from descriptors
        std::vector<std::pair<std::string, std::string>> sorted_params(
            params.begin(), params.end());
        std::sort(sorted_params.begin(), sorted_params.end(),
                  ParamComparator(
                      anicet::runner::libjpegturbo::LIBJPEGTURBO_PARAMETERS));

        for (const auto& [key, value] : sorted_params) {
          ss << "." << key << "_" << value;
        }

        ss << ".index_" << i << ".jpeg";

        snprintf(filename, sizeof(filename), "%s", ss.str().c_str());
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
      fprintf(stderr, "libjpeg-turbo: Encoding failed\n");
      errors++;
    }
  }

  // 3. jpegli encoding
  if (run_jpegli) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    CodecSetup setup;
    setup.num_runs = num_runs;

    // Use codec_setup if provided, otherwise use defaults
    if (codec_setup) {
      setup = *codec_setup;
    }

    if (anicet::runner::jpegli::anicet_run(&input, &setup, &local_output) ==
            0 &&
        local_output.num_frames() > 0) {
      // Store codec name and parameters in output
      populate_codec_info(local_output, "jpegli", setup);

      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[1024];

        // Build filename with all parameters
        std::stringstream ss;
        ss << dump_output_dir << "/" << dump_output_prefix;
        ss << ".codec_jpegli";

        // Convert parameters to string map
        std::map<std::string, std::string> params =
            convert_params_to_strings(setup.parameter_map);

        // Convert to vector and sort with custom ordering from descriptors
        std::vector<std::pair<std::string, std::string>> sorted_params(
            params.begin(), params.end());
        std::sort(sorted_params.begin(), sorted_params.end(),
                  ParamComparator(anicet::runner::jpegli::JPEGLI_PARAMETERS));

        for (const auto& [key, value] : sorted_params) {
          // Replace underscores with hyphens for consistency
          std::string formatted_key = key;
          std::replace(formatted_key.begin(), formatted_key.end(), '_', '-');
          ss << "." << formatted_key << "_" << value;
        }

        ss << ".index_" << i << ".jpeg";

        snprintf(filename, sizeof(filename), "%s", ss.str().c_str());
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

  // 4. x265 (H.265/HEVC) 8-bit encoding
  if (run_x265) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;

    // Use provided setup or create default one
    CodecSetup setup;
    if (codec_setup != nullptr) {
      // Use the provided setup
      setup = *codec_setup;
      // Override num_runs if not already set
      if (setup.num_runs == 0) {
        setup.num_runs = num_runs;
      }
      // Ensure "optimization" is always set (needed for filename generation)
      if (setup.parameter_map.find("optimization") ==
          setup.parameter_map.end()) {
        setup.parameter_map["optimization"] = "opt";  // default
      }
    } else {
      // Create default setup
      setup.num_runs = num_runs;
      setup.parameter_map["optimization"] = "opt";
      setup.parameter_map["preset"] = "medium";
      setup.parameter_map["tune"] = "zerolatency";
      setup.parameter_map["rate-control"] = "crf";
    }

    if (anicet::runner::x265::anicet_run(&input, &setup, &local_output) == 0 &&
        local_output.num_frames() > 0) {
      // Store codec name and parameters in output
      populate_codec_info(local_output, "x265", setup);

      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[1024];

        // Build filename with all parameters using the shared helper
        std::stringstream ss;
        ss << dump_output_dir << "/" << dump_output_prefix;
        ss << ".codec_x265";

        // Convert parameters to string map
        std::map<std::string, std::string> params =
            convert_params_to_strings(setup.parameter_map);

        // Convert to vector and sort with custom ordering from descriptors
        std::vector<std::pair<std::string, std::string>> sorted_params(
            params.begin(), params.end());
        std::sort(sorted_params.begin(), sorted_params.end(),
                  ParamComparator(anicet::runner::x265::X265_PARAMETERS));

        for (const auto& [key, value] : sorted_params) {
          ss << "." << key << "_" << value;
        }

        ss << ".index_" << i << ".265";

        snprintf(filename, sizeof(filename), "%s", ss.str().c_str());
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
      fprintf(stderr, "x265: Encoding failed\n");
      errors++;
    }
  }

  // 5. SVT-AV1 encoding
  if (run_svtav1) {
    CodecOutput local_output;
    local_output.dump_output = dump_output;
    CodecSetup setup;
    setup.num_runs = num_runs;

    // Use codec_setup if provided, otherwise use defaults
    if (codec_setup) {
      setup = *codec_setup;
    }

    if (anicet::runner::svtav1::anicet_run(&input, &setup, &local_output) ==
            0 &&
        local_output.num_frames() > 0) {
      // Store codec name and parameters in output
      populate_codec_info(local_output, "svt-av1", setup);

      // Generate filenames and optionally write files
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[1024];

        // Build filename with all parameters
        std::stringstream ss;
        ss << dump_output_dir << "/" << dump_output_prefix;
        ss << ".codec_svt-av1";

        // Convert parameters to string map
        std::map<std::string, std::string> params =
            convert_params_to_strings(setup.parameter_map);

        // Convert to vector and sort with custom ordering from descriptors
        std::vector<std::pair<std::string, std::string>> sorted_params(
            params.begin(), params.end());
        std::sort(sorted_params.begin(), sorted_params.end(),
                  ParamComparator(anicet::runner::svtav1::SVTAV1_PARAMETERS));

        for (const auto& [key, value] : sorted_params) {
          ss << "." << key << "_" << value;
        }

        ss << ".index_" << i << ".av1";

        snprintf(filename, sizeof(filename), "%s", ss.str().c_str());
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
    CodecSetup setup;
    setup.num_runs = num_runs;

    // Use codec_setup if provided, otherwise use defaults
    if (codec_setup) {
      setup = *codec_setup;
    }

    if (anicet::runner::mediacodec::anicet_run(&input, &setup, &local_output) ==
            0 &&
        local_output.num_frames() > 0) {
      // Store codec name and parameters in output
      populate_codec_info(local_output, "mediacodec", setup);

      // Generate filenames with all parameters
      for (size_t i = 0; i < local_output.num_frames(); i++) {
        char filename[1024];

        // Build filename with all parameters
        std::stringstream ss;
        ss << dump_output_dir << "/" << dump_output_prefix;
        ss << ".codec_mediacodec";

        // Convert parameters to string map
        std::map<std::string, std::string> params =
            convert_params_to_strings(setup.parameter_map);

        // Convert to vector and sort with custom ordering from descriptors
        std::vector<std::pair<std::string, std::string>> sorted_params(
            params.begin(), params.end());
        std::sort(
            sorted_params.begin(), sorted_params.end(),
            ParamComparator(anicet::runner::mediacodec::MEDIACODEC_PARAMETERS));

        for (const auto& [key, value] : sorted_params) {
          // Replace underscores with hyphens for consistency
          std::string formatted_key = key;
          std::replace(formatted_key.begin(), formatted_key.end(), '_', '-');
          ss << "." << formatted_key << "_" << value;
        }

        ss << ".index_" << i << ".bin";

        snprintf(filename, sizeof(filename), "%s", ss.str().c_str());
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

#undef DEBUG
  return errors;
}
