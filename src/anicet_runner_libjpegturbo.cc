// anicet_runner_libjpegturbo.cc
// libjpeg-turbo encoder implementation

#include "anicet_runner_libjpegturbo.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>

#include "anicet_common.h"
#include "resource_profiler.h"
#include "turbojpeg.h"

// libjpeg-turbo encoder - writes to caller-provided memory buffer only
int anicet_run_libjpegturbo(const uint8_t* input_buffer, size_t input_size,
                            int height, int width, const char* color_format,
                            int num_runs, bool dump_output,
                            CodecOutput* output) {
  // Unused
  (void)input_size;
  // Unused (yuv420p assumed)
  (void)color_format;

  // Validate inputs
  if (!input_buffer || !output) {
    return -1;
  }

  // Initialize output
  output->frame_buffers.clear();
  output->frame_buffers.resize(num_runs);
  output->frame_sizes.clear();
  output->frame_sizes.resize(num_runs);
  output->timings.clear();
  output->timings.resize(num_runs);

  // Profile total memory (all 4 steps: setup + conversion + encode + cleanup)
  PROFILE_RESOURCES_START(libjpegturbo_total_memory);

  // (a) Codec setup
  tjhandle handle = tjInitCompress();
  if (!handle) {
    fprintf(stderr, "TurboJPEG: Failed to initialize compressor\n");
    PROFILE_RESOURCES_END(libjpegturbo_total_memory);
    return -1;
  }

  // (b) Input conversion: None needed - TurboJPEG takes YUV420 directly

  // (c) Actual encoding - run num_runs times
  int result = 0;
  PROFILE_RESOURCES_START(libjpegturbo_encode_cpu);
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    output->timings[run].input_timestamp_us = anicet_get_timestamp();

    unsigned char* jpeg_buf = nullptr;
    unsigned long jpeg_size = 0;

    // Compress YUV to JPEG - tjCompressFromYUV allocates output buffer
    int ret =
        tjCompressFromYUV(handle, input_buffer, width, 1, height, TJSAMP_420,
                          &jpeg_buf, &jpeg_size, 75, TJFLAG_FASTDCT);
    if (ret != 0) {
      fprintf(stderr, "TurboJPEG: Encoding failed: %s\n",
              tjGetErrorStr2(handle));
      result = -1;
      break;
    }

    // Capture end timestamp
    output->timings[run].output_timestamp_us = anicet_get_timestamp();

    // Store output in vector (only copy buffer if dump_output is true)
    if (dump_output) {
      output->frame_buffers[run].assign(jpeg_buf, jpeg_buf + jpeg_size);
    }
    output->frame_sizes[run] = jpeg_size;

    tjFree(jpeg_buf);
  }
  PROFILE_RESOURCES_END(libjpegturbo_encode_cpu);

  // (d) Codec cleanup
  tjDestroy(handle);
  PROFILE_RESOURCES_END(libjpegturbo_total_memory);
  return result;
}

// libjpeg-turbo encoder (non-optimized) - uses dlopen to avoid symbol
// conflicts Dynamically loads libturbojpeg-nonopt.so with RTLD_LOCAL for
// symbol isolation
int anicet_run_libjpegturbo_nonopt(const uint8_t* input_buffer,
                                   size_t input_size, int height, int width,
                                   const char* color_format, int num_runs,
                                   bool dump_output, CodecOutput* output) {
  // Unused
  (void)input_size;
  // Unused (yuv420p assumed)
  (void)color_format;

  // Validate inputs
  if (!input_buffer || !output) {
    return -1;
  }

  // Initialize output
  output->frame_buffers.clear();
  output->frame_buffers.resize(num_runs);
  output->frame_sizes.clear();
  output->frame_sizes.resize(num_runs);
  output->timings.clear();
  output->timings.resize(num_runs);

  // Profile total memory (all 4 steps: setup + conversion + encode + cleanup)
  PROFILE_RESOURCES_START(libjpegturbo_nonopt_total_memory);

  // (a) Codec setup - Load libturbojpeg-nonopt.so with RTLD_LOCAL to isolate
  // symbols
  void* handle = dlopen("libturbojpeg-nonopt.so", RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "libjpeg-turbo-nonopt: Failed to load library: %s\n",
            dlerror());
    PROFILE_RESOURCES_END(libjpegturbo_nonopt_total_memory);
    return -1;
  }

  // Get function pointers
  typedef void* (*tjInitCompress_t)();
  typedef int (*tjCompressFromYUV_t)(void*, const unsigned char*, int, int, int,
                                     int, unsigned char**, unsigned long*, int,
                                     int);
  typedef char* (*tjGetErrorStr2_t)(void*);
  typedef void (*tjFree_t)(unsigned char*);
  typedef int (*tjDestroy_t)(void*);

  auto initCompress = (tjInitCompress_t)dlsym(handle, "tjInitCompress");
  auto compressFromYUV =
      (tjCompressFromYUV_t)dlsym(handle, "tjCompressFromYUV");
  auto getErrorStr2 = (tjGetErrorStr2_t)dlsym(handle, "tjGetErrorStr2");
  auto tjFreeFunc = (tjFree_t)dlsym(handle, "tjFree");
  auto tjDestroyFunc = (tjDestroy_t)dlsym(handle, "tjDestroy");

  if (!initCompress || !compressFromYUV || !getErrorStr2 || !tjFreeFunc ||
      !tjDestroyFunc) {
    fprintf(stderr, "libjpeg-turbo-nonopt: Failed to load symbols: %s\n",
            dlerror());
    dlclose(handle);
    PROFILE_RESOURCES_END(libjpegturbo_nonopt_total_memory);
    return -1;
  }

  // Now use the library exactly like the optimized version
  void* tj_handle = initCompress();
  if (!tj_handle) {
    fprintf(stderr, "libjpeg-turbo-nonopt: Failed to initialize compressor\n");
    dlclose(handle);
    PROFILE_RESOURCES_END(libjpegturbo_nonopt_total_memory);
    return -1;
  }

  // (b) Input conversion: None needed - TurboJPEG takes YUV420 directly

  // (c) Actual encoding - run num_runs times
  int result = 0;
  PROFILE_RESOURCES_START(libjpegturbo_nonopt_encode_cpu);
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    output->timings[run].input_timestamp_us = anicet_get_timestamp();

    unsigned char* jpeg_buf = nullptr;
    unsigned long jpeg_size = 0;

    // TJSAMP_420 = 2, TJFLAG_FASTDCT = 2048 (from turbojpeg.h)
    int ret = compressFromYUV(tj_handle, input_buffer, width, 1, height, 2,
                              &jpeg_buf, &jpeg_size, 75, 2048);
    if (ret != 0) {
      fprintf(stderr, "libjpeg-turbo-nonopt: Encoding failed: %s\n",
              getErrorStr2(tj_handle));
      result = -1;
      break;
    }

    // Capture end timestamp
    output->timings[run].output_timestamp_us = anicet_get_timestamp();

    // Store output in vector (only copy buffer if dump_output is true)
    if (dump_output) {
      output->frame_buffers[run].assign(jpeg_buf, jpeg_buf + jpeg_size);
    }
    output->frame_sizes[run] = jpeg_size;

    tjFreeFunc(jpeg_buf);
  }
  PROFILE_RESOURCES_END(libjpegturbo_nonopt_encode_cpu);

  // (d) Codec cleanup
  tjDestroyFunc(tj_handle);
  dlclose(handle);
  PROFILE_RESOURCES_END(libjpegturbo_nonopt_total_memory);
  return result;
}
