// anicet_runner_svtav1.cc
// SVT-AV1 encoder implementation

#include "anicet_runner_svtav1.h"

#include <cstdio>
#include <cstring>

#include "anicet_common.h"
#include "resource_profiler.h"

// Undefine DEFAULT from x265 to avoid conflict with SVT-AV1
#ifdef DEFAULT
#undef DEFAULT
#endif

#include "svt-av1/EbSvtAv1Enc.h"

// SVT-AV1 encoder - writes to caller-provided memory buffer only
int anicet_run_svtav1(const CodecInput* input, int num_runs,
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
  EbComponentType* handle = nullptr;
  EbSvtAv1EncConfiguration config;

  // Initialize encoder handle - this loads default config
  EbErrorType res = svt_av1_enc_init_handle(&handle, &config);
  if (res != EB_ErrorNone || !handle) {
    fprintf(stderr, "SVT-AV1: Failed to initialize encoder handle\n");
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // Modify parameters after getting defaults
  config.source_width = input->width;
  config.source_height = input->height;
  config.frame_rate_numerator = 30;
  config.frame_rate_denominator = 1;
  config.encoder_bit_depth = 8;
  // I-frame only
  config.intra_period_length = -1;
  // Key frame refresh
  config.intra_refresh_type = SVT_AV1_KF_REFRESH;

  res = svt_av1_enc_set_parameter(handle, &config);
  if (res != EB_ErrorNone) {
    fprintf(stderr, "SVT-AV1: Failed to set parameters\n");
    svt_av1_enc_deinit_handle(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  res = svt_av1_enc_init(handle);
  if (res != EB_ErrorNone) {
    fprintf(stderr, "SVT-AV1: Failed to initialize encoder\n");
    svt_av1_enc_deinit_handle(handle);
    PROFILE_RESOURCES_END(profile_encode_mem);
    return -1;
  }

  // (b) Input conversion: SVT-AV1 requires EbSvtIOFormat with separate Y/Cb/Cr
  // pointers
  EbSvtIOFormat input_picture;
  memset(&input_picture, 0, sizeof(input_picture));

  // YUV420p layout: Y plane, then U (Cb), then V (Cr)
  size_t y_size = input->width * input->height;
  size_t uv_size = y_size / 4;

  input_picture.luma = (uint8_t*)input->input_buffer;
  input_picture.cb = (uint8_t*)input->input_buffer + y_size;
  input_picture.cr = (uint8_t*)input->input_buffer + y_size + uv_size;
  input_picture.y_stride = input->width;
  input_picture.cb_stride = input->width / 2;
  input_picture.cr_stride = input->width / 2;

  EbBufferHeaderType input_buf;
  memset(&input_buf, 0, sizeof(input_buf));
  // Required for version check
  input_buf.size = sizeof(EbBufferHeaderType);
  input_buf.p_buffer = (uint8_t*)&input_picture;
  input_buf.n_filled_len = input->input_size;
  input_buf.n_alloc_len = input->input_size;
  input_buf.pic_type = EB_AV1_KEY_PICTURE;

  // (c) Actual encoding - run num_runs times
  int result = 0;

  // Store frame start snapshots for per-frame CPU tracking
  std::vector<ResourceSnapshot> frame_starts(num_runs);

  // Step 1: Send all input pictures (I-frame only, no EOS between them)
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp when sending input
    output->timings[run].input_timestamp_us = anicet_get_timestamp();

    capture_resources(&frame_starts[run]);

    res = svt_av1_enc_send_picture(handle, &input_buf);
    if (res != EB_ErrorNone) {
      fprintf(stderr, "SVT-AV1: Failed to send picture (run %d)\n", run);
      result = -1;
      break;
    }
  }

  // Step 2: Send EOS once at the end to flush all frames
  if (result == 0) {
    EbBufferHeaderType eos_buffer;
    memset(&eos_buffer, 0, sizeof(eos_buffer));
    eos_buffer.size = sizeof(EbBufferHeaderType);
    eos_buffer.flags = EB_BUFFERFLAG_EOS;
    res = svt_av1_enc_send_picture(handle, &eos_buffer);
    if (res != EB_ErrorNone) {
      fprintf(stderr, "SVT-AV1: Failed to send EOS\n");
      result = -1;
    }
  }

  // Step 3: Collect all output packets
  if (result == 0) {
    for (int run = 0; run < num_runs; run++) {
      EbBufferHeaderType* output_buf = nullptr;
      res = svt_av1_enc_get_packet(handle, &output_buf, 1);
      if (res == EB_ErrorNone && output_buf && output_buf->n_filled_len > 0) {
        // Capture end timestamp when receiving output
        output->timings[run].output_timestamp_us = anicet_get_timestamp();

        ResourceSnapshot frame_end;
        capture_resources(&frame_end);
        ResourceDelta frame_delta;
        compute_delta(&frame_starts[run], &frame_end, &frame_delta);
        output->profile_encode_cpu_ms[run] = frame_delta.cpu_time_ms;

        // Store output in vector (only copy buffer if dump_output is true)
        if (output->dump_output) {
          output->frame_buffers[run].assign(
              output_buf->p_buffer,
              output_buf->p_buffer + output_buf->n_filled_len);
        }
        output->frame_sizes[run] = output_buf->n_filled_len;

        svt_av1_enc_release_out_buffer(&output_buf);
      } else {
        fprintf(stderr, "SVT-AV1: Failed to get output packet (run %d)\n", run);
        result = -1;
        break;
      }
    }
  }

  // (d) Codec cleanup
  svt_av1_enc_deinit(handle);
  svt_av1_enc_deinit_handle(handle);

  ResourceSnapshot __profile_mem_end;
  capture_resources(&__profile_mem_end);
  output->profile_encode_mem_kb = __profile_mem_end.rss_peak_kb;

  PROFILE_RESOURCES_END(profile_encode_mem);
  return result;
}
