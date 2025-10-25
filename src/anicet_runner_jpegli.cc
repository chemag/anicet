// anicet_runner_jpegli.cc
// jpegli encoder implementation

#include "anicet_runner_jpegli.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "anicet_common.h"
#include "jpeglib.h"
#include "resource_profiler.h"

// jpegli encoder - writes to caller-provided memory buffer only
int anicet_run_jpegli(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      int num_runs, CodecOutput* output) {
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
  PROFILE_RESOURCES_START(jpegli_total_memory);

  // (a) Codec setup
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  // We convert YUV to RGB below
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 75, TRUE);

  // (b) Input conversion: YUV420 to RGB (done once)
  int row_stride = width * 3;
  JSAMPROW row_pointer[1];
  std::vector<unsigned char> rgb_buffer(height * row_stride);

  // Simple YUV420 to RGB conversion
  const uint8_t* y_plane = input_buffer;
  const uint8_t* u_plane = input_buffer + (width * height);
  const uint8_t* v_plane =
      input_buffer + (width * height) + (width * height / 4);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int y_val = y_plane[y * width + x];
      int u_val = u_plane[(y / 2) * (width / 2) + (x / 2)] - 128;
      int v_val = v_plane[(y / 2) * (width / 2) + (x / 2)] - 128;

      int r = y_val + (1.370705 * v_val);
      int g = y_val - (0.698001 * v_val) - (0.337633 * u_val);
      int b = y_val + (1.732446 * u_val);

      rgb_buffer[y * row_stride + x * 3 + 0] = (r < 0)     ? 0
                                               : (r > 255) ? 255
                                                           : r;
      rgb_buffer[y * row_stride + x * 3 + 1] = (g < 0)     ? 0
                                               : (g > 255) ? 255
                                                           : g;
      rgb_buffer[y * row_stride + x * 3 + 2] = (b < 0)     ? 0
                                               : (b > 255) ? 255
                                                           : b;
    }
  }

  // (c) Actual encoding - run num_runs times
  unsigned char* jpeg_buf = nullptr;
  unsigned long jpeg_size = 0;
  int result = 0;

  PROFILE_RESOURCES_START(jpegli_encode_cpu);
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    output->timings[run].input_timestamp_us = anicet_get_timestamp();

    // Free previous run's buffer if exists
    if (jpeg_buf) {
      free(jpeg_buf);
      jpeg_buf = nullptr;
    }

    jpeg_mem_dest(&cinfo, &jpeg_buf, &jpeg_size);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
      row_pointer[0] = &rgb_buffer[cinfo.next_scanline * row_stride];
      jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);

    // Capture end timestamp
    output->timings[run].output_timestamp_us = anicet_get_timestamp();

    // Store output in vector (only copy buffer if dump_output is true)
    if (output->dump_output) {
      output->frame_buffers[run].assign(jpeg_buf, jpeg_buf + jpeg_size);
    }
    output->frame_sizes[run] = jpeg_size;

    // For next iteration, need to reset scanline counter
    if (run < num_runs - 1) {
      jpeg_abort_compress(&cinfo);
    }
  }
  PROFILE_RESOURCES_END(jpegli_encode_cpu);

  // Cleanup the last jpeg_buf
  if (jpeg_buf) {
    free(jpeg_buf);
  }

  // (d) Codec cleanup
  jpeg_destroy_compress(&cinfo);

  PROFILE_RESOURCES_END(jpegli_total_memory);
  return result;
}
