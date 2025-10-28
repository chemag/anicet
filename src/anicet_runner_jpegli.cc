// anicet_runner_jpegli.cc
// jpegli encoder implementation

#include "anicet_runner_jpegli.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "anicet_common.h"
#include "jpeglib.h"
#include "resource_profiler.h"

namespace anicet {
namespace runner {
namespace jpegli {

// jpegli encoder - writes to caller-provided memory buffer only
int anicet_run(const CodecInput* input, int num_runs, CodecOutput* output) {
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
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  cinfo.image_width = input->width;
  cinfo.image_height = input->height;
  cinfo.input_components = 3;
  // Use YCbCr color space to avoid unnecessary conversion
  cinfo.in_color_space = JCS_YCbCr;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 75, TRUE);

  // Enable raw data mode for direct YUV420p input
  cinfo.raw_data_in = TRUE;

  // Configure sampling factors for YUV420p (4:2:0 subsampling)
  // Y component: 2x2 sampling (full resolution)
  cinfo.comp_info[0].h_samp_factor = 2;
  cinfo.comp_info[0].v_samp_factor = 2;
  // U component: 1x1 sampling (half resolution)
  cinfo.comp_info[1].h_samp_factor = 1;
  cinfo.comp_info[1].v_samp_factor = 1;
  // V component: 1x1 sampling (half resolution)
  cinfo.comp_info[2].h_samp_factor = 1;
  cinfo.comp_info[2].v_samp_factor = 1;

  // (b) Input conversion: Extract YUV420p plane pointers (no conversion needed)
  const uint8_t* y_plane = input->input_buffer;
  const uint8_t* u_plane = input->input_buffer + (input->width * input->height);
  const uint8_t* v_plane = input->input_buffer +
                           (input->width * input->height) +
                           (input->width * input->height / 4);

  // MCU rows: max_v_sample * DCTSIZE = 2 * 8 = 16 for Y plane
  // DCTSIZE is defined in jpeglib.h as 8
  const int max_lines = 2 * DCTSIZE;

  // Set up row pointers for raw data
  // We need 3 arrays of row pointers, one for each component (Y, U, V)
  std::vector<JSAMPROW> y_rows(max_lines);
  std::vector<JSAMPROW> u_rows(DCTSIZE);
  std::vector<JSAMPROW> v_rows(DCTSIZE);
  JSAMPARRAY plane_pointers[3] = {y_rows.data(), u_rows.data(), v_rows.data()};

  // (c) Actual encoding - run num_runs times
  unsigned char* jpeg_buf = nullptr;
  unsigned long jpeg_size = 0;
  int result = 0;

  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    output->timings[run].input_timestamp_us = anicet_get_timestamp();

    ResourceSnapshot frame_start;
    capture_resources(&frame_start);

    // Free previous run's buffer if exists
    if (jpeg_buf) {
      free(jpeg_buf);
      jpeg_buf = nullptr;
    }

    jpeg_mem_dest(&cinfo, &jpeg_buf, &jpeg_size);
    jpeg_start_compress(&cinfo, TRUE);

    // Write raw YUV data in MCU-sized blocks
    while (cinfo.next_scanline < cinfo.image_height) {
      // Current Y row (Y plane processes 16 rows at a time)
      JDIMENSION y_row = cinfo.next_scanline;
      // Current U/V row (U/V planes process 8 rows at a time, half of Y)
      JDIMENSION uv_row = y_row / 2;

      // Set up row pointers for Y plane (16 rows)
      for (int i = 0; i < max_lines; i++) {
        JDIMENSION row = y_row + i;
        if (row < cinfo.image_height) {
          y_rows[i] = const_cast<JSAMPROW>(y_plane + row * input->width);
        } else {
          // Padding for incomplete MCU
          y_rows[i] = const_cast<JSAMPROW>(y_plane + (cinfo.image_height - 1) *
                                                         input->width);
        }
      }

      // Set up row pointers for U plane (8 rows)
      for (int i = 0; i < DCTSIZE; i++) {
        JDIMENSION row = uv_row + i;
        JDIMENSION uv_height = (cinfo.image_height + 1) / 2;
        if (row < uv_height) {
          u_rows[i] = const_cast<JSAMPROW>(u_plane + row * (input->width / 2));
        } else {
          // Padding for incomplete MCU
          u_rows[i] = const_cast<JSAMPROW>(u_plane + (uv_height - 1) *
                                                         (input->width / 2));
        }
      }

      // Set up row pointers for V plane (8 rows)
      for (int i = 0; i < DCTSIZE; i++) {
        JDIMENSION row = uv_row + i;
        JDIMENSION uv_height = (cinfo.image_height + 1) / 2;
        if (row < uv_height) {
          v_rows[i] = const_cast<JSAMPROW>(v_plane + row * (input->width / 2));
        } else {
          // Padding for incomplete MCU
          v_rows[i] = const_cast<JSAMPROW>(v_plane + (uv_height - 1) *
                                                         (input->width / 2));
        }
      }

      // Write one MCU worth of data
      jpeg_write_raw_data(&cinfo, plane_pointers, max_lines);
    }

    jpeg_finish_compress(&cinfo);

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

    // For next iteration, need to reset scanline counter
    if (run < num_runs - 1) {
      jpeg_abort_compress(&cinfo);
    }
  }

  // Cleanup the last jpeg_buf
  if (jpeg_buf) {
    free(jpeg_buf);
  }

  // (d) Codec cleanup
  jpeg_destroy_compress(&cinfo);

  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;

  // Compute and store resource delta (without printing)
  compute_delta(&__profile_start_profile_encode_mem, &__profile_mem_end,
                &output->resource_delta);

  return result;
}

}  // namespace jpegli
}  // namespace runner
}  // namespace anicet
