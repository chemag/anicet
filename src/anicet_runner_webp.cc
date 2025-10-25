// anicet_runner_webp.cc
// WebP encoder runners implementation

#include "anicet_runner_webp.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>

#include "anicet_common.h"
#include "resource_profiler.h"
#include "webp/encode.h"

// WebP encoder - writes to caller-provided memory buffer only
int anicet_run_webp(const CodecInput* input, int num_runs,
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

  // (a) Codec setup
  WebPConfig config;
  if (!WebPConfigInit(&config)) {
    fprintf(stderr, "WebP: Failed to initialize config\n");
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  config.quality = 75;
  // Speed/quality trade-off
  config.method = 4;

  WebPPicture picture;
  if (!WebPPictureInit(&picture)) {
    fprintf(stderr, "WebP: Failed to initialize picture\n");
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  picture.width = input->width;
  picture.height = input->height;
  // Use YUV
  picture.use_argb = 0;
  picture.colorspace = WEBP_YUV420;

  // Allocate picture
  if (!WebPPictureAlloc(&picture)) {
    fprintf(stderr, "WebP: Failed to allocate picture\n");
    WebPPictureFree(&picture);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // (b) Input conversion: Import YUV420 data manually
  const uint8_t* y_plane = input->input_buffer;
  const uint8_t* u_plane = input->input_buffer + (input->width * input->height);
  const uint8_t* v_plane = input->input_buffer +
                           (input->width * input->height) +
                           (input->width * input->height / 4);

  // Copy Y plane
  for (int y = 0; y < input->height; y++) {
    memcpy(picture.y + y * picture.y_stride, y_plane + y * input->width,
           input->width);
  }
  // Copy U plane
  for (int y = 0; y < input->height / 2; y++) {
    memcpy(picture.u + y * picture.uv_stride, u_plane + y * (input->width / 2),
           input->width / 2);
  }
  // Copy V plane
  for (int y = 0; y < input->height / 2; y++) {
    memcpy(picture.v + y * picture.uv_stride, v_plane + y * (input->width / 2),
           input->width / 2);
  }

  // (c) Actual encoding - run num_runs times
  int result = 0;
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp and resources
    output->timings[run].input_timestamp_us = anicet_get_timestamp();
    ResourceSnapshot frame_start;
    capture_resources(&frame_start);

    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;

    if (!WebPEncode(&config, &picture)) {
      fprintf(stderr, "WebP: Encoding failed\n");
      WebPMemoryWriterClear(&writer);
      result = -1;
      break;
    }

    // Capture end timestamp and resources
    output->timings[run].output_timestamp_us = anicet_get_timestamp();
    ResourceSnapshot frame_end;
    capture_resources(&frame_end);
    ResourceDelta frame_delta;
    compute_delta(&frame_start, &frame_end, &frame_delta);
    output->profile_encode_cpu_ms[run] = frame_delta.cpu_time_ms;

    // Store output in vector (only copy buffer if dump_output is true)
    if (output->dump_output) {
      output->frame_buffers[run].assign(writer.mem, writer.mem + writer.size);
    }
    output->frame_sizes[run] = writer.size;

    WebPMemoryWriterClear(&writer);
  }

  // (d) Codec cleanup
  WebPPictureFree(&picture);

  // Capture memory profiling data and store in output
  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;
  PROFILE_RESOURCES_END(profile_encode_mem);
  return result;
}

// WebP encoder (non-optimized) - uses dlopen to avoid symbol conflicts
// Dynamically loads libwebp-nonopt.so with RTLD_LOCAL for symbol isolation
int anicet_run_webp_nonopt(const CodecInput* input, int num_runs,
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

  // (a) Codec setup - Load libwebp-nonopt.so with RTLD_LOCAL to isolate symbols
  void* handle = dlopen("libwebp-nonopt.so", RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "webp-nonopt: Failed to load library: %s\n", dlerror());
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // Get function pointers
  // Note: WebPConfigInit and WebPPictureInit are macros that call *Internal
  // functions
  typedef int (*WebPConfigInitInternal_t)(WebPConfig*, int, int);
  typedef int (*WebPPictureInitInternal_t)(WebPPicture*, int);
  typedef int (*WebPPictureAlloc_t)(WebPPicture*);
  typedef void (*WebPPictureFree_t)(WebPPicture*);
  typedef void (*WebPMemoryWriterInit_t)(WebPMemoryWriter*);
  typedef int (*WebPMemoryWrite_t)(const uint8_t*, size_t, const WebPPicture*);
  typedef void (*WebPMemoryWriterClear_t)(WebPMemoryWriter*);
  typedef int (*WebPEncode_t)(const WebPConfig*, WebPPicture*);

  auto configInitInternal =
      (WebPConfigInitInternal_t)dlsym(handle, "WebPConfigInitInternal");
  auto pictureInitInternal =
      (WebPPictureInitInternal_t)dlsym(handle, "WebPPictureInitInternal");
  auto pictureAlloc = (WebPPictureAlloc_t)dlsym(handle, "WebPPictureAlloc");
  auto pictureFree = (WebPPictureFree_t)dlsym(handle, "WebPPictureFree");
  auto memoryWriterInit =
      (WebPMemoryWriterInit_t)dlsym(handle, "WebPMemoryWriterInit");
  auto memoryWrite = (WebPMemoryWrite_t)dlsym(handle, "WebPMemoryWrite");
  auto memoryWriterClear =
      (WebPMemoryWriterClear_t)dlsym(handle, "WebPMemoryWriterClear");
  auto encode = (WebPEncode_t)dlsym(handle, "WebPEncode");

  if (!configInitInternal || !pictureInitInternal || !pictureAlloc ||
      !pictureFree || !memoryWriterInit || !memoryWrite || !memoryWriterClear ||
      !encode) {
    fprintf(stderr, "webp-nonopt: Failed to load symbols: %s\n", dlerror());
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // Now use the library exactly like the optimized version
  WebPConfig config;
  // Call the internal function directly with version parameters
  if (!configInitInternal(&config, WEBP_ENCODER_ABI_VERSION,
                          WEBP_ENCODER_ABI_VERSION)) {
    fprintf(stderr, "webp-nonopt: Failed to initialize config\n");
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  config.quality = 75;
  // Speed/quality trade-off
  config.method = 4;

  WebPPicture picture;
  // Call the internal function directly with version parameter
  if (!pictureInitInternal(&picture, WEBP_ENCODER_ABI_VERSION)) {
    fprintf(stderr, "webp-nonopt: Failed to initialize picture\n");
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  picture.width = input->width;
  picture.height = input->height;
  // Use YUV
  picture.use_argb = 0;
  picture.colorspace = WEBP_YUV420;

  // Allocate picture
  if (!pictureAlloc(&picture)) {
    fprintf(stderr, "webp-nonopt: Failed to allocate picture\n");
    pictureFree(&picture);
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // (b) Input conversion: Import YUV420 data manually
  const uint8_t* y_plane = input->input_buffer;
  const uint8_t* u_plane = input->input_buffer + (input->width * input->height);
  const uint8_t* v_plane = input->input_buffer +
                           (input->width * input->height) +
                           (input->width * input->height / 4);

  // Copy Y plane
  for (int y = 0; y < input->height; y++) {
    memcpy(picture.y + y * picture.y_stride, y_plane + y * input->width,
           input->width);
  }
  // Copy U plane
  for (int y = 0; y < input->height / 2; y++) {
    memcpy(picture.u + y * picture.uv_stride, u_plane + y * (input->width / 2),
           input->width / 2);
  }
  // Copy V plane
  for (int y = 0; y < input->height / 2; y++) {
    memcpy(picture.v + y * picture.uv_stride, v_plane + y * (input->width / 2),
           input->width / 2);
  }

  // (c) Actual encoding - run num_runs times
  int result = 0;
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp and resources
    output->timings[run].input_timestamp_us = anicet_get_timestamp();
    ResourceSnapshot frame_start;
    capture_resources(&frame_start);

    WebPMemoryWriter writer;
    memoryWriterInit(&writer);
    picture.writer = memoryWrite;
    picture.custom_ptr = &writer;

    if (!encode(&config, &picture)) {
      fprintf(stderr, "webp-nonopt: Encoding failed\n");
      memoryWriterClear(&writer);
      result = -1;
      break;
    }

    // Capture end timestamp and resources
    output->timings[run].output_timestamp_us = anicet_get_timestamp();
    ResourceSnapshot frame_end;
    capture_resources(&frame_end);
    ResourceDelta frame_delta;
    compute_delta(&frame_start, &frame_end, &frame_delta);
    output->profile_encode_cpu_ms[run] = frame_delta.cpu_time_ms;

    // Store output in vector (only copy buffer if dump_output is true)
    if (output->dump_output) {
      output->frame_buffers[run].assign(writer.mem, writer.mem + writer.size);
    }
    output->frame_sizes[run] = writer.size;

    memoryWriterClear(&writer);
  }

  // (d) Codec cleanup
  pictureFree(&picture);
  dlclose(handle);

  // Capture memory profiling data and store in output
  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;
  PROFILE_RESOURCES_END(profile_encode_mem);
  return result;
}
