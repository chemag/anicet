// anicet_runner_x265.cc
// x265 encoder implementation

#include "anicet_runner_x265.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>

#include "anicet_common.h"
#include "resource_profiler.h"
#include "x265.h"

namespace anicet {
namespace runner {
namespace x265 {

// x265 encoder (8-bit) - uses dlopen to load library based on optimization
// parameter
int anicet_run(const CodecInput* input, CodecSetup* setup,
               CodecOutput* output) {
// Local DEBUG macro for cleaner debug statements
#define DEBUG(level, ...) ANICET_DEBUG(input->debug_level, level, __VA_ARGS__)
  // Validate inputs
  if (!input || !input->input_buffer || !setup || !output) {
    return -1;
  }

  int num_runs = setup->num_runs;

  // Initialize output
  output->frame_buffers.clear();
  output->frame_buffers.resize(num_runs);
  output->frame_sizes.clear();
  output->frame_sizes.resize(num_runs);
  output->timings.clear();
  output->timings.resize(num_runs);
  output->profile_encode_cpu_ms.clear();
  output->profile_encode_cpu_ms.resize(num_runs);

  // Profile total memory (all 4 steps: setup + conversion + encode + cleanup)
  PROFILE_RESOURCES_START(profile_encode_mem);

  // Determine library name from optimization parameter
  std::string optimization = "opt";
  auto opt_it = setup->parameter_map.find("optimization");
  if (opt_it != setup->parameter_map.end()) {
    optimization = std::get<std::string>(opt_it->second);
  }

  const char* library_name = (optimization == "nonopt")
                                 ? "libx265-8bit-nonopt.so"
                                 : "libx265-8bit-opt.so";

  DEBUG(2, "x265: Loading library %s (optimization=%s)", library_name,
        optimization.c_str());

  // (a) Codec setup - Load x265 library with RTLD_LOCAL to isolate symbols
  void* handle = dlopen(library_name, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "x265: Failed to load library %s: %s\n", library_name,
            dlerror());
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // Get function pointers
  typedef x265_param* (*x265_param_alloc_t)();
  typedef int (*x265_param_default_preset_t)(x265_param*, const char*,
                                             const char*);
  typedef x265_encoder* (*x265_encoder_open_t)(x265_param*);
  typedef x265_picture* (*x265_picture_alloc_t)();
  typedef void (*x265_picture_init_t)(x265_param*, x265_picture*);
  typedef int (*x265_encoder_encode_t)(x265_encoder*, x265_nal**, uint32_t*,
                                       x265_picture*, x265_picture*);
  typedef void (*x265_picture_free_t)(x265_picture*);
  typedef void (*x265_encoder_close_t)(x265_encoder*);
  typedef void (*x265_param_free_t)(x265_param*);

  auto param_alloc = (x265_param_alloc_t)dlsym(handle, "x265_param_alloc");
  auto param_default_preset =
      (x265_param_default_preset_t)dlsym(handle, "x265_param_default_preset");
  auto encoder_open =
      (x265_encoder_open_t)dlsym(handle, "x265_encoder_open_215");
  auto picture_alloc =
      (x265_picture_alloc_t)dlsym(handle, "x265_picture_alloc");
  auto picture_init = (x265_picture_init_t)dlsym(handle, "x265_picture_init");
  auto encoder_encode =
      (x265_encoder_encode_t)dlsym(handle, "x265_encoder_encode");
  auto picture_free = (x265_picture_free_t)dlsym(handle, "x265_picture_free");
  auto encoder_close =
      (x265_encoder_close_t)dlsym(handle, "x265_encoder_close");
  auto param_free = (x265_param_free_t)dlsym(handle, "x265_param_free");

  if (!param_alloc || !param_default_preset || !encoder_open ||
      !picture_alloc || !picture_init || !encoder_encode || !picture_free ||
      !encoder_close || !param_free) {
    fprintf(stderr, "x265: Failed to load symbols: %s\n", dlerror());
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // Now use the library
  x265_param* param = param_alloc();
  if (!param) {
    fprintf(stderr, "x265: Failed to allocate parameters\n");
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // Determine preset and tune (use defaults if not specified)
  std::string preset = DEFAULT_CODEC_SETUP_PRESET;  // "medium"
  std::string tune = DEFAULT_CODEC_SETUP_TUNE;      // "zerolatency"

  // Check if preset is specified and validate it
  auto preset_it = setup->parameter_map.find("preset");
  if (preset_it != setup->parameter_map.end()) {
    preset = std::get<std::string>(preset_it->second);

    if (!validate_parameter_list("x265", "preset", preset,
                                 DEFAULT_CODEC_SETUP_PRESET_VALUES)) {
      param_free(param);
      dlclose(handle);
      PROFILE_RESOURCES_END(profile_encode_mem);
      return -1;
    }
  } else {
    // Set default preset in parameter_map so it gets reported
    setup->parameter_map["preset"] = preset;
  }

  // Check for tune parameter override
  auto tune_it = setup->parameter_map.find("tune");
  if (tune_it != setup->parameter_map.end()) {
    tune = std::get<std::string>(tune_it->second);

    if (!validate_parameter_list("x265", "tune", tune,
                                 DEFAULT_CODEC_SETUP_TUNE_VALUES)) {
      param_free(param);
      dlclose(handle);
      PROFILE_RESOURCES_END(profile_encode_mem);
      return -1;
    }
  } else {
    // Set default tune in parameter_map so it gets reported
    setup->parameter_map["tune"] = tune;
  }

  // ALWAYS call param_default_preset to initialize the param structure properly
  DEBUG(2, "x265: Applying preset '%s' with tune '%s'", preset.c_str(),
        tune.c_str());
  int preset_ret = param_default_preset(param, preset.c_str(), tune.c_str());
  if (preset_ret < 0) {
    fprintf(
        stderr,
        "x265: Failed to apply preset '%s' with tune '%s' (error code %d)\n",
        preset.c_str(), tune.c_str(), preset_ret);
    param_free(param);
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }
  DEBUG(2, "x265: Successfully applied preset");

  // Check for rate-control parameter and configure it
  auto rate_control_it = setup->parameter_map.find("rate-control");
  std::string rate_control =
      DEFAULT_CODEC_SETUP_RATE_CONTROL;  // default: "crf"

  if (rate_control_it != setup->parameter_map.end()) {
    rate_control = std::get<std::string>(rate_control_it->second);

    if (!validate_parameter_list("x265", "rate-control", rate_control,
                                 DEFAULT_CODEC_SETUP_RATE_CONTROL_VALUES)) {
      param_free(param);
      dlclose(handle);
      PROFILE_RESOURCES_END(profile_encode_mem);
      return -1;
    }
  } else {
    // Set default rate-control in parameter_map so it gets reported
    setup->parameter_map["rate-control"] = rate_control;
  }

  // Configure rate control mode and associated parameters
  if (rate_control == "crf") {
    param->rc.rateControlMode = X265_RC_CRF;
    // Check for crf parameter
    auto crf_it = setup->parameter_map.find("crf");
    if (crf_it != setup->parameter_map.end()) {
      param->rc.rfConstant = std::get<int>(crf_it->second);
    } else {
      // Store the default value (from x265_param_default: 28) in parameter_map
      setup->parameter_map["crf"] = static_cast<int>(param->rc.rfConstant);
    }
    DEBUG(2, "x265: Using CRF mode with crf=%d",
          static_cast<int>(param->rc.rfConstant));
  } else if (rate_control == "cqp") {
    param->rc.rateControlMode = X265_RC_CQP;
    // Check for qp parameter
    auto qp_it = setup->parameter_map.find("qp");
    if (qp_it != setup->parameter_map.end()) {
      param->rc.qp = std::get<int>(qp_it->second);
    } else {
      // Store the default value (from x265_param_default: 32) in parameter_map
      setup->parameter_map["qp"] = static_cast<int>(param->rc.qp);
    }
    DEBUG(2, "x265: Using CQP mode with qp=%d", static_cast<int>(param->rc.qp));
  } else if (rate_control == "abr" || rate_control == "cbr") {
    param->rc.rateControlMode = X265_RC_ABR;
    // Check for bitrate parameter
    auto bitrate_it = setup->parameter_map.find("bitrate");
    if (bitrate_it != setup->parameter_map.end()) {
      param->rc.bitrate = std::get<int>(bitrate_it->second);
    } else if (param->rc.bitrate == 0) {
      // If no bitrate set, we need one for ABR/CBR
      fprintf(stderr, "x265: bitrate parameter required for %s mode\n",
              rate_control.c_str());
      param_free(param);
      dlclose(handle);
      PROFILE_RESOURCES_END(profile_encode_mem);
      return -1;
    }
    // Store the bitrate value in parameter_map
    setup->parameter_map["bitrate"] = static_cast<int>(param->rc.bitrate);

    // For CBR, also enable VBV with tight constraints
    if (rate_control == "cbr") {
      param->rc.vbvBufferSize = param->rc.bitrate;
      param->rc.vbvMaxBitrate = param->rc.bitrate;
    }
    DEBUG(2, "x265: Using %s mode with bitrate=%d", rate_control.c_str(),
          static_cast<int>(param->rc.bitrate));
  } else if (rate_control == "2-pass") {
    // 2-pass requires separate encode passes with stats file
    // For now, just use CRF as fallback
    fprintf(stderr, "x265: 2-pass encoding not yet supported, using CRF\n");
    param->rc.rateControlMode = X265_RC_CRF;
    setup->parameter_map["rate-control"] = std::string("crf");
    setup->parameter_map["crf"] = static_cast<int>(param->rc.rfConstant);
  }

  param->sourceWidth = input->width;
  param->sourceHeight = input->height;
  param->fpsNum = 30;
  param->fpsDenom = 1;
  param->internalCsp = X265_CSP_I420;
  // Library is compiled for 8-bit
  param->internalBitDepth = 8;
  // I-frame only
  param->keyframeMax = 1;
  param->bframes = 0;
  // Set log level based on debug_level (only show messages if debug_level > 1)
  param->logLevel = (input->debug_level > 1) ? X265_LOG_INFO : X265_LOG_NONE;

  DEBUG(2,
        "x265: Opening encoder (width=%d, height=%d, csp=I420, "
        "keyframeMax=%d, bframes=%d)",
        param->sourceWidth, param->sourceHeight, param->keyframeMax,
        param->bframes);

  x265_encoder* encoder = encoder_open(param);
  if (!encoder) {
    fprintf(stderr, "x265: Failed to open encoder\n");
    param_free(param);
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  DEBUG(2, "x265: Encoder opened successfully");
  DEBUG(2, "x265: Allocating picture");
  x265_picture* pic_in = picture_alloc();
  DEBUG(2, "x265: Initializing picture");
  picture_init(param, pic_in);
  DEBUG(2, "x265: Picture initialized");

  // (b) Input conversion - Set up picture planes for YUV420 (8-bit)
  pic_in->bitDepth = 8;
  pic_in->planes[0] = (void*)input->input_buffer;
  pic_in->planes[1] =
      (void*)(input->input_buffer + input->width * input->height);
  pic_in->planes[2] =
      (void*)(input->input_buffer + input->width * input->height +
              input->width * input->height / 4);
  pic_in->stride[0] = input->width;
  pic_in->stride[1] = input->width / 2;
  pic_in->stride[2] = input->width / 2;

  // (c) Actual encoding - run num_runs times through same encoder
  int result = 0;

  DEBUG(2, "x265: Starting encoding loop (num_runs=%d)", num_runs);

  for (int run = 0; run < num_runs; run++) {
    DEBUG(2, "x265: Encoding run %d/%d", run + 1, num_runs);

    // Capture start timestamp
    output->timings[run].input_timestamp_us = anicet_get_timestamp();
    ResourceSnapshot frame_start;
    capture_resources(&frame_start);

    // Force this frame to be IDR
    pic_in->sliceType = X265_TYPE_IDR;

    x265_nal* nals = nullptr;
    uint32_t num_nals = 0;
    int frame_size = encoder_encode(encoder, &nals, &num_nals, pic_in, nullptr);

    if (frame_size <= 0) {
      fprintf(stderr, "x265: Encoding failed (run %d)\n", run);
      result = -1;
      break;
    }

    // Capture end timestamp
    output->timings[run].output_timestamp_us = anicet_get_timestamp();
    ResourceSnapshot frame_end;
    capture_resources(&frame_end);
    ResourceDelta frame_delta;
    compute_delta(&frame_start, &frame_end, &frame_delta);
    output->profile_encode_cpu_ms[run] = frame_delta.cpu_time_ms;

    // Calculate total size for this frame
    size_t total_size = 0;
    for (uint32_t i = 0; i < num_nals; i++) {
      total_size += nals[i].sizeBytes;
    }

    // Copy all NAL units directly to output vector (only if dump_output is
    // true)
    if (output->dump_output) {
      output->frame_buffers[run].resize(total_size);
      size_t offset = 0;
      for (uint32_t i = 0; i < num_nals; i++) {
        memcpy(output->frame_buffers[run].data() + offset, nals[i].payload,
               nals[i].sizeBytes);
        offset += nals[i].sizeBytes;
      }
    }
    output->frame_sizes[run] = total_size;
    DEBUG(2, "x265: Run %d complete (output size=%zu bytes)", run + 1,
          total_size);
  }

  DEBUG(2, "x265: All encoding runs complete, cleaning up");

  // (d) Codec cleanup - cleanup ONCE at the end
  picture_free(pic_in);
  encoder_close(encoder);
  param_free(param);
  dlclose(handle);

  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;

  // Compute and store resource delta (without printing)
  compute_delta(&__profile_start_profile_encode_mem, &__profile_mem_end,
                &output->resource_delta);

#undef DEBUG
  return result;
}

}  // namespace x265
}  // namespace runner
}  // namespace anicet
