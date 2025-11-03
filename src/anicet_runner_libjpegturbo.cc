// anicet_runner_libjpegturbo.cc
// libjpeg-turbo encoder implementation

#include "anicet_runner_libjpegturbo.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>

#include "anicet_common.h"
#include "resource_profiler.h"
#include "turbojpeg.h"

namespace anicet {
namespace runner {
namespace libjpegturbo {

using anicet::runner::libjpegturbo::DEFAULT_QUALITY;

// libjpeg-turbo encoder - uses dlopen to load library based on optimization
// parameter
int anicet_run(const CodecInput* input, CodecSetup* setup,
               CodecOutput* output) {
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
  } else {
    // Set default optimization in parameter_map so it gets reported
    setup->parameter_map["optimization"] = optimization;
  }

  const char* library_name = (optimization == "nonopt")
                                 ? "libturbojpeg-nonopt.so"
                                 : "libturbojpeg-opt.so";

  // (a) Codec setup - Load libturbojpeg library with RTLD_LOCAL to isolate
  // symbols
  void* handle = dlopen(library_name, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "libjpeg-turbo: Failed to load library %s: %s\n",
            library_name, dlerror());
    PROFILE_RESOURCES_END(profile_encode_mem);
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
    fprintf(stderr, "libjpeg-turbo: Failed to load symbols: %s\n", dlerror());
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // Now use the library
  void* tj_handle = initCompress();
  if (!tj_handle) {
    fprintf(stderr, "libjpeg-turbo: Failed to initialize compressor\n");
    dlclose(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // Get quality parameter
  int quality = DEFAULT_QUALITY;
  auto quality_it = setup->parameter_map.find("quality");
  if (quality_it != setup->parameter_map.end()) {
    quality = std::get<int>(quality_it->second);
  } else {
    setup->parameter_map["quality"] = quality;
  }

  // (b) Input conversion: None needed - TurboJPEG takes YUV420 directly

  // (c) Actual encoding - run num_runs times
  int result = 0;
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    output->timings[run].input_timestamp_us = anicet_get_timestamp();
    ResourceSnapshot frame_start;
    capture_resources(&frame_start);

    unsigned char* jpeg_buf = nullptr;
    unsigned long jpeg_size = 0;

    // Compress YUV to JPEG - tjCompressFromYUV allocates output buffer
    int ret = compressFromYUV(tj_handle, input->input_buffer, input->width, 1,
                              input->height, TJSAMP_420, &jpeg_buf, &jpeg_size,
                              quality, TJFLAG_FASTDCT);
    if (ret != 0) {
      fprintf(stderr, "libjpeg-turbo: Encoding failed: %s\n",
              getErrorStr2(tj_handle));
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

    // Store output in vector (only copy buffer if dump_output is true)
    if (output->dump_output) {
      output->frame_buffers[run].assign(jpeg_buf, jpeg_buf + jpeg_size);
    }
    output->frame_sizes[run] = jpeg_size;

    tjFreeFunc(jpeg_buf);
  }

  // (d) Codec cleanup
  tjDestroyFunc(tj_handle);
  dlclose(handle);

  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;

  // Compute and store resource delta (without printing)
  compute_delta(&__profile_start_profile_encode_mem, &__profile_mem_end,
                &output->resource_delta);

  return result;
}

}  // namespace libjpegturbo
}  // namespace runner
}  // namespace anicet
