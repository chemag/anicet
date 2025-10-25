// anicet_runner_x265.cc
// x265 encoder implementation

#include "anicet_runner_x265.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>

#include "anicet_common.h"
#include "resource_profiler.h"
#include "x265.h"

// x265 encoder (8-bit) - writes to caller-provided memory buffer only
int anicet_run_x265_8bit(const CodecInput* input, int num_runs,
                         CodecOutput* output) {
  // Validate inputs
  if (!input || !input->input_buffer || !output) {
    return -1;
  }

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

  // (a) Codec setup - setup ONCE for all frames
  x265_param* param = x265_param_alloc();
  if (!param) {
    fprintf(stderr, "x265: Failed to allocate parameters\n");
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  x265_param_default_preset(param, "medium", "zerolatency");
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

  x265_encoder* encoder = x265_encoder_open(param);
  if (!encoder) {
    fprintf(stderr, "x265: Failed to open encoder\n");
    x265_param_free(param);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  x265_picture* pic_in = x265_picture_alloc();
  x265_picture_init(param, pic_in);

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

  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    output->timings[run].input_timestamp_us = anicet_get_timestamp();
    ResourceSnapshot frame_start;
    capture_resources(&frame_start);

    // Force this frame to be IDR
    pic_in->sliceType = X265_TYPE_IDR;

    x265_nal* nals = nullptr;
    uint32_t num_nals = 0;
    int frame_size =
        x265_encoder_encode(encoder, &nals, &num_nals, pic_in, nullptr);

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
  }

  // (d) Codec cleanup - cleanup ONCE at the end
  x265_picture_free(pic_in);
  x265_encoder_close(encoder);
  x265_param_free(param);

  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;

  PROFILE_RESOURCES_END(profile_encode_mem);
  return result;
}

// x265 encoder (8-bit, non-optimized) - uses dlopen to avoid symbol conflicts
// Dynamically loads libx265-8bit-nonopt.so with RTLD_LOCAL for symbol isolation
int anicet_run_x265_8bit_nonopt(const CodecInput* input, int num_runs,
                                CodecOutput* output) {
  // Validate inputs
  if (!input || !input->input_buffer || !output) {
    return -1;
  }

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

  // (a) Codec setup - Load libx265-8bit-nonopt.so with RTLD_LOCAL to isolate
  // symbols
  void* handle = dlopen("libx265-8bit-nonopt.so", RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "x265-8bit-nonopt: Failed to load library: %s\n",
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
    fprintf(stderr, "x265-8bit-nonopt: Failed to load symbols: %s\n",
            dlerror());
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // Now use the library exactly like the optimized version
  x265_param* param = param_alloc();
  if (!param) {
    fprintf(stderr, "x265-8bit-nonopt: Failed to allocate parameters\n");
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  param_default_preset(param, "medium", "zerolatency");
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

  x265_encoder* encoder = encoder_open(param);
  if (!encoder) {
    fprintf(stderr, "x265-8bit-nonopt: Failed to open encoder\n");
    param_free(param);
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  x265_picture* pic_in = picture_alloc();
  picture_init(param, pic_in);

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

  for (int run = 0; run < num_runs; run++) {
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
      fprintf(stderr, "x265-8bit-nonopt: Encoding failed (run %d)\n", run);
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
  }

  // (d) Codec cleanup - cleanup ONCE at the end
  picture_free(pic_in);
  encoder_close(encoder);
  param_free(param);
  dlclose(handle);

  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;

  PROFILE_RESOURCES_END(profile_encode_mem);
  return result;
}
