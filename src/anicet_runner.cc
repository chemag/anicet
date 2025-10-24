// anicet_runner.cc
// Encoder experiment runner implementation

#include "anicet_runner.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// POSIX headers for dynamic loading
#include <dlfcn.h>

// Resource profiling
#include "resource_profiler.h"

// Encoder library headers
#include "android_mediacodec_lib.h"
#include "jpeglib.h"
#include "turbojpeg.h"
#include "webp/encode.h"

// x265 must be included before SVT-AV1 due to DEFAULT macro conflict
#include "x265.h"

// Undefine DEFAULT from x265 to avoid conflict with SVT-AV1
#ifdef DEFAULT
#undef DEFAULT
#endif

#include "svt-av1/EbSvtAv1Enc.h"

// WebP encoder - writes to caller-provided memory buffer only
int anicet_run_webp(const uint8_t* input_buffer, size_t input_size, int height,
                    int width, const char* color_format, int num_runs,
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
  PROFILE_RESOURCES_START(webp_total_memory);

  // (a) Codec setup
  WebPConfig config;
  if (!WebPConfigInit(&config)) {
    fprintf(stderr, "WebP: Failed to initialize config\n");
    PROFILE_RESOURCES_END(webp_total_memory);
    return -1;
  }

  config.quality = 75;
  // Speed/quality trade-off
  config.method = 4;

  WebPPicture picture;
  if (!WebPPictureInit(&picture)) {
    fprintf(stderr, "WebP: Failed to initialize picture\n");
    PROFILE_RESOURCES_END(webp_total_memory);
    return -1;
  }

  picture.width = width;
  picture.height = height;
  // Use YUV
  picture.use_argb = 0;
  picture.colorspace = WEBP_YUV420;

  // Allocate picture
  if (!WebPPictureAlloc(&picture)) {
    fprintf(stderr, "WebP: Failed to allocate picture\n");
    WebPPictureFree(&picture);
    PROFILE_RESOURCES_END(webp_total_memory);
    return -1;
  }

  // (b) Input conversion: Import YUV420 data manually
  const uint8_t* y_plane = input_buffer;
  const uint8_t* u_plane = input_buffer + (width * height);
  const uint8_t* v_plane =
      input_buffer + (width * height) + (width * height / 4);

  // Copy Y plane
  for (int y = 0; y < height; y++) {
    memcpy(picture.y + y * picture.y_stride, y_plane + y * width, width);
  }
  // Copy U plane
  for (int y = 0; y < height / 2; y++) {
    memcpy(picture.u + y * picture.uv_stride, u_plane + y * (width / 2),
           width / 2);
  }
  // Copy V plane
  for (int y = 0; y < height / 2; y++) {
    memcpy(picture.v + y * picture.uv_stride, v_plane + y * (width / 2),
           width / 2);
  }

  // (c) Actual encoding - run num_runs times
  int result = 0;
  PROFILE_RESOURCES_START(webp_encode_cpu);
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].input_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

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

    // Capture end timestamp
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].output_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

    // Store output in vector
    output->frame_buffers[run].assign(writer.mem, writer.mem + writer.size);
    output->frame_sizes[run] = writer.size;

    // Save output for verification
    char filename[256];
    snprintf(filename, sizeof(filename),
             "/data/local/tmp/bin/out/output.webp.%d.bin", run);
    FILE* f = fopen(filename, "wb");
    if (f) {
      fwrite(writer.mem, 1, writer.size, f);
      fclose(f);
    }

    WebPMemoryWriterClear(&writer);
  }
  PROFILE_RESOURCES_END(webp_encode_cpu);

  // (d) Codec cleanup
  WebPPictureFree(&picture);
  PROFILE_RESOURCES_END(webp_total_memory);
  return result;
}

// libjpeg-turbo encoder - writes to caller-provided memory buffer only
int anicet_run_libjpegturbo(const uint8_t* input_buffer, size_t input_size,
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
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].input_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

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
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].output_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

    // Store output in vector
    output->frame_buffers[run].assign(jpeg_buf, jpeg_buf + jpeg_size);
    output->frame_sizes[run] = jpeg_size;

    // Save output for verification
    char filename[256];
    snprintf(filename, sizeof(filename),
             "/data/local/tmp/bin/out/output.libjpegturbo.%d.bin", run);
    FILE* f = fopen(filename, "wb");
    if (f) {
      fwrite(jpeg_buf, 1, jpeg_size, f);
      fclose(f);
    }

    tjFree(jpeg_buf);
  }
  PROFILE_RESOURCES_END(libjpegturbo_encode_cpu);

  // (d) Codec cleanup
  tjDestroy(handle);
  PROFILE_RESOURCES_END(libjpegturbo_total_memory);
  return result;
}

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
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].input_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

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
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].output_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

    // Store output in vector
    output->frame_buffers[run].assign(jpeg_buf, jpeg_buf + jpeg_size);
    output->frame_sizes[run] = jpeg_size;

    // Save this run's output for verification
    char filename[256];
    snprintf(filename, sizeof(filename),
             "/data/local/tmp/bin/out/output.jpegli.%d.bin", run);
    FILE* f = fopen(filename, "wb");
    if (f) {
      fwrite(jpeg_buf, 1, jpeg_size, f);
      fclose(f);
    }

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

// x265 encoder (8-bit) - writes to caller-provided memory buffer only
int anicet_run_x265_8bit(const uint8_t* input_buffer, size_t input_size,
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
  PROFILE_RESOURCES_START(x265_total_memory);

  // (a) Codec setup - setup ONCE for all frames
  x265_param* param = x265_param_alloc();
  if (!param) {
    fprintf(stderr, "x265: Failed to allocate parameters\n");
    PROFILE_RESOURCES_END(x265_total_memory);
    return -1;
  }

  x265_param_default_preset(param, "medium", "zerolatency");
  param->sourceWidth = width;
  param->sourceHeight = height;
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
    PROFILE_RESOURCES_END(x265_total_memory);
    return -1;
  }

  x265_picture* pic_in = x265_picture_alloc();
  x265_picture_init(param, pic_in);

  // (b) Input conversion - Set up picture planes for YUV420 (8-bit)
  pic_in->bitDepth = 8;
  pic_in->planes[0] = (void*)input_buffer;
  pic_in->planes[1] = (void*)(input_buffer + width * height);
  pic_in->planes[2] =
      (void*)(input_buffer + width * height + width * height / 4);
  pic_in->stride[0] = width;
  pic_in->stride[1] = width / 2;
  pic_in->stride[2] = width / 2;

  // (c) Actual encoding - run num_runs times through same encoder
  int result = 0;
  PROFILE_RESOURCES_START(x265_encode_cpu);

  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].input_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

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
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].output_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

    // Calculate total size for this frame
    size_t total_size = 0;
    for (uint32_t i = 0; i < num_nals; i++) {
      total_size += nals[i].sizeBytes;
    }

    // Copy all NAL units directly to output vector
    output->frame_buffers[run].resize(total_size);
    size_t offset = 0;
    for (uint32_t i = 0; i < num_nals; i++) {
      memcpy(output->frame_buffers[run].data() + offset, nals[i].payload,
             nals[i].sizeBytes);
      offset += nals[i].sizeBytes;
    }
    output->frame_sizes[run] = total_size;

    // Save this run's output for verification
    char filename[256];
    snprintf(filename, sizeof(filename),
             "/data/local/tmp/bin/out/output.x265-8bit.%d.bin", run);
    FILE* f = fopen(filename, "wb");
    if (f) {
      fwrite(output->frame_buffers[run].data(), 1, total_size, f);
      fclose(f);
    }
  }

  PROFILE_RESOURCES_END(x265_encode_cpu);

  // (d) Codec cleanup - cleanup ONCE at the end
  x265_picture_free(pic_in);
  x265_encoder_close(encoder);
  x265_param_free(param);

  PROFILE_RESOURCES_END(x265_total_memory);
  return result;
}

// SVT-AV1 encoder - writes to caller-provided memory buffer only
int anicet_run_svtav1(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      int num_runs, CodecOutput* output) {
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
  PROFILE_RESOURCES_START(svt_av1_total_memory);

  // (a) Codec setup
  EbComponentType* handle = nullptr;
  EbSvtAv1EncConfiguration config;

  // Initialize encoder handle - this loads default config
  EbErrorType res = svt_av1_enc_init_handle(&handle, &config);
  if (res != EB_ErrorNone || !handle) {
    fprintf(stderr, "SVT-AV1: Failed to initialize encoder handle\n");
    PROFILE_RESOURCES_END(svt_av1_total_memory);
    return -1;
  }

  // Modify parameters after getting defaults
  config.source_width = width;
  config.source_height = height;
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
    PROFILE_RESOURCES_END(svt_av1_total_memory);
    return -1;
  }

  res = svt_av1_enc_init(handle);
  if (res != EB_ErrorNone) {
    fprintf(stderr, "SVT-AV1: Failed to initialize encoder\n");
    svt_av1_enc_deinit_handle(handle);
    PROFILE_RESOURCES_END(svt_av1_total_memory);
    return -1;
  }

  // (b) Input conversion: SVT-AV1 requires EbSvtIOFormat with separate Y/Cb/Cr
  // pointers
  EbSvtIOFormat input_picture;
  memset(&input_picture, 0, sizeof(input_picture));

  // YUV420p layout: Y plane, then U (Cb), then V (Cr)
  size_t y_size = width * height;
  size_t uv_size = y_size / 4;

  input_picture.luma = (uint8_t*)input_buffer;
  input_picture.cb = (uint8_t*)input_buffer + y_size;
  input_picture.cr = (uint8_t*)input_buffer + y_size + uv_size;
  input_picture.y_stride = width;
  input_picture.cb_stride = width / 2;
  input_picture.cr_stride = width / 2;

  EbBufferHeaderType input_buf;
  memset(&input_buf, 0, sizeof(input_buf));
  // Required for version check
  input_buf.size = sizeof(EbBufferHeaderType);
  input_buf.p_buffer = (uint8_t*)&input_picture;
  input_buf.n_filled_len = input_size;
  input_buf.n_alloc_len = input_size;
  input_buf.pic_type = EB_AV1_KEY_PICTURE;

  // (c) Actual encoding - run num_runs times
  int result = 0;

  PROFILE_RESOURCES_START(svt_av1_encode_cpu);

  // Step 1: Send all input pictures (I-frame only, no EOS between them)
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp when sending input
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].input_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

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
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        output->timings[run].output_timestamp_us =
            ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

        // Store output in vector
        output->frame_buffers[run].assign(
            output_buf->p_buffer,
            output_buf->p_buffer + output_buf->n_filled_len);
        output->frame_sizes[run] = output_buf->n_filled_len;

        // Save this run's output for verification
        char filename[256];
        snprintf(filename, sizeof(filename),
                 "/data/local/tmp/bin/out/output.svtav1.%d.bin", run);
        FILE* f = fopen(filename, "wb");
        if (f) {
          fwrite(output_buf->p_buffer, 1, output_buf->n_filled_len, f);
          fclose(f);
        }

        svt_av1_enc_release_out_buffer(&output_buf);
      } else {
        fprintf(stderr, "SVT-AV1: Failed to get output packet (run %d)\n", run);
        result = -1;
        break;
      }
    }
  }

  PROFILE_RESOURCES_END(svt_av1_encode_cpu);

  // (d) Codec cleanup
  svt_av1_enc_deinit(handle);
  svt_av1_enc_deinit_handle(handle);

  PROFILE_RESOURCES_END(svt_av1_total_memory);
  return result;
}

// x265 encoder (8-bit, non-optimized) - uses dlopen to avoid symbol conflicts
// Dynamically loads libx265-8bit-nonopt.so with RTLD_LOCAL for symbol isolation
int anicet_run_x265_8bit_nonopt(const uint8_t* input_buffer, size_t input_size,
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
  PROFILE_RESOURCES_START(x265_8bit_nonopt_total_memory);

  // (a) Codec setup - Load libx265-8bit-nonopt.so with RTLD_LOCAL to isolate
  // symbols
  void* handle = dlopen("libx265-8bit-nonopt.so", RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "x265-8bit-nonopt: Failed to load library: %s\n",
            dlerror());
    PROFILE_RESOURCES_END(x265_8bit_nonopt_total_memory);
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
  auto encoder_open = (x265_encoder_open_t)dlsym(handle, "x265_encoder_open");
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
    PROFILE_RESOURCES_END(x265_8bit_nonopt_total_memory);
    return -1;
  }

  // Now use the library exactly like the optimized version
  x265_param* param = param_alloc();
  if (!param) {
    fprintf(stderr, "x265-8bit-nonopt: Failed to allocate parameters\n");
    dlclose(handle);
    PROFILE_RESOURCES_END(x265_8bit_nonopt_total_memory);
    return -1;
  }

  param_default_preset(param, "medium", "zerolatency");
  param->sourceWidth = width;
  param->sourceHeight = height;
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
    PROFILE_RESOURCES_END(x265_8bit_nonopt_total_memory);
    return -1;
  }

  x265_picture* pic_in = picture_alloc();
  picture_init(param, pic_in);

  // (b) Input conversion - Set up picture planes for YUV420 (8-bit)
  pic_in->bitDepth = 8;
  pic_in->planes[0] = (void*)input_buffer;
  pic_in->planes[1] = (void*)(input_buffer + width * height);
  pic_in->planes[2] =
      (void*)(input_buffer + width * height + width * height / 4);
  pic_in->stride[0] = width;
  pic_in->stride[1] = width / 2;
  pic_in->stride[2] = width / 2;

  // (c) Actual encoding - run num_runs times through same encoder
  int result = 0;
  PROFILE_RESOURCES_START(x265_8bit_nonopt_encode_cpu);

  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].input_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

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
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].output_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

    // Calculate total size for this frame
    size_t total_size = 0;
    for (uint32_t i = 0; i < num_nals; i++) {
      total_size += nals[i].sizeBytes;
    }

    // Copy all NAL units directly to output vector
    output->frame_buffers[run].resize(total_size);
    size_t offset = 0;
    for (uint32_t i = 0; i < num_nals; i++) {
      memcpy(output->frame_buffers[run].data() + offset, nals[i].payload,
             nals[i].sizeBytes);
      offset += nals[i].sizeBytes;
    }
    output->frame_sizes[run] = total_size;

    // Save this run's output for verification
    char filename[256];
    snprintf(filename, sizeof(filename),
             "/data/local/tmp/bin/out/output.x265-8bit-nonopt.%d.bin", run);
    FILE* f = fopen(filename, "wb");
    if (f) {
      fwrite(output->frame_buffers[run].data(), 1, total_size, f);
      fclose(f);
    }
  }

  PROFILE_RESOURCES_END(x265_8bit_nonopt_encode_cpu);

  // (d) Codec cleanup - cleanup ONCE at the end
  picture_free(pic_in);
  encoder_close(encoder);
  param_free(param);
  dlclose(handle);
  PROFILE_RESOURCES_END(x265_8bit_nonopt_total_memory);
  return result;
}

// libjpeg-turbo encoder (non-optimized) - uses dlopen to avoid symbol
// conflicts Dynamically loads libturbojpeg-nonopt.so with RTLD_LOCAL for
// symbol isolation
int anicet_run_libjpegturbo_nonopt(const uint8_t* input_buffer,
                                   size_t input_size, int height, int width,
                                   const char* color_format, int num_runs,
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
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].input_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

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
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].output_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

    // Store output in vector
    output->frame_buffers[run].assign(jpeg_buf, jpeg_buf + jpeg_size);
    output->frame_sizes[run] = jpeg_size;

    // Save output for verification
    char filename[256];
    snprintf(filename, sizeof(filename),
             "/data/local/tmp/bin/out/output.libjpegturbo-nonopt.%d.bin", run);
    FILE* f = fopen(filename, "wb");
    if (f) {
      fwrite(jpeg_buf, 1, jpeg_size, f);
      fclose(f);
    }

    tjFreeFunc(jpeg_buf);
  }
  PROFILE_RESOURCES_END(libjpegturbo_nonopt_encode_cpu);

  // (d) Codec cleanup
  tjDestroyFunc(tj_handle);
  dlclose(handle);
  PROFILE_RESOURCES_END(libjpegturbo_nonopt_total_memory);
  return result;
}

// WebP encoder (non-optimized) - uses dlopen to avoid symbol conflicts
// Dynamically loads libwebp-nonopt.so with RTLD_LOCAL for symbol isolation
int anicet_run_webp_nonopt(const uint8_t* input_buffer, size_t input_size,
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
  PROFILE_RESOURCES_START(webp_nonopt_total_memory);

  // (a) Codec setup - Load libwebp-nonopt.so with RTLD_LOCAL to isolate symbols
  void* handle = dlopen("libwebp-nonopt.so", RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "webp-nonopt: Failed to load library: %s\n", dlerror());
    PROFILE_RESOURCES_END(webp_nonopt_total_memory);
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
    PROFILE_RESOURCES_END(webp_nonopt_total_memory);
    return -1;
  }

  // Now use the library exactly like the optimized version
  WebPConfig config;
  // Call the internal function directly with version parameters
  if (!configInitInternal(&config, WEBP_ENCODER_ABI_VERSION,
                          WEBP_ENCODER_ABI_VERSION)) {
    fprintf(stderr, "webp-nonopt: Failed to initialize config\n");
    dlclose(handle);
    PROFILE_RESOURCES_END(webp_nonopt_total_memory);
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
    PROFILE_RESOURCES_END(webp_nonopt_total_memory);
    return -1;
  }

  picture.width = width;
  picture.height = height;
  // Use YUV
  picture.use_argb = 0;
  picture.colorspace = WEBP_YUV420;

  // Allocate picture
  if (!pictureAlloc(&picture)) {
    fprintf(stderr, "webp-nonopt: Failed to allocate picture\n");
    pictureFree(&picture);
    dlclose(handle);
    PROFILE_RESOURCES_END(webp_nonopt_total_memory);
    return -1;
  }

  // (b) Input conversion: Import YUV420 data manually
  const uint8_t* y_plane = input_buffer;
  const uint8_t* u_plane = input_buffer + (width * height);
  const uint8_t* v_plane =
      input_buffer + (width * height) + (width * height / 4);

  // Copy Y plane
  for (int y = 0; y < height; y++) {
    memcpy(picture.y + y * picture.y_stride, y_plane + y * width, width);
  }
  // Copy U plane
  for (int y = 0; y < height / 2; y++) {
    memcpy(picture.u + y * picture.uv_stride, u_plane + y * (width / 2),
           width / 2);
  }
  // Copy V plane
  for (int y = 0; y < height / 2; y++) {
    memcpy(picture.v + y * picture.uv_stride, v_plane + y * (width / 2),
           width / 2);
  }

  // (c) Actual encoding - run num_runs times
  int result = 0;
  PROFILE_RESOURCES_START(webp_nonopt_encode_cpu);
  for (int run = 0; run < num_runs; run++) {
    // Capture start timestamp
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].input_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

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

    // Capture end timestamp
    clock_gettime(CLOCK_MONOTONIC, &ts);
    output->timings[run].output_timestamp_us =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;

    // Store output in vector
    output->frame_buffers[run].assign(writer.mem, writer.mem + writer.size);
    output->frame_sizes[run] = writer.size;

    // Save output for verification
    char filename[256];
    snprintf(filename, sizeof(filename),
             "/data/local/tmp/bin/out/output.webp-nonopt.%d.bin", run);
    FILE* f = fopen(filename, "wb");
    if (f) {
      fwrite(writer.mem, 1, writer.size, f);
      fclose(f);
    }

    memoryWriterClear(&writer);
  }
  PROFILE_RESOURCES_END(webp_nonopt_encode_cpu);

  // (d) Codec cleanup
  pictureFree(&picture);
  dlclose(handle);
  PROFILE_RESOURCES_END(webp_nonopt_total_memory);
  return result;
}

// Android MediaCodec encoder - wrapper that adapts
// android_mediacodec_encode_frame()
int anicet_run_mediacodec(const uint8_t* input_buffer, size_t input_size,
                          int height, int width, const char* color_format,
                          const char* codec_name, int num_runs,
                          CodecOutput* output) {
  // Validate inputs
  if (!input_buffer || !output || !codec_name) {
    return -1;
  }

#ifdef __ANDROID__
  // Profile total memory (all 4 steps: setup + conversion + encode + cleanup)
  PROFILE_RESOURCES_START(mediacodec_total_memory);

  // (a) Codec setup - setup ONCE for all frames
  MediaCodecFormat format;
  format.width = width;
  format.height = height;
  format.codec_name = codec_name;
  format.color_format = color_format;
  format.quality = 75;
  // Auto-calculate from quality
  format.bitrate = -1;
  // Quiet
  format.debug_level = 0;

  AMediaCodec* codec = nullptr;
  int setup_result = android_mediacodec_encode_setup(&format, &codec);
  if (setup_result != 0) {
    PROFILE_RESOURCES_END(mediacodec_total_memory);
    return setup_result;
  }

  // (b) Input conversion - none needed for MediaCodec, it accepts YUV420p
  // directly

  // (c) Actual encoding - encode all frames in one call with new API
  int result = 0;
  PROFILE_RESOURCES_START(mediacodec_encode_cpu);

  // Call new API - encodes all num_runs frames in single session
  result = android_mediacodec_encode_frame(codec, input_buffer, input_size,
                                           &format, num_runs, output);

  if (result == 0) {
    // Save each frame to separate file
    for (size_t i = 0; i < output->num_frames(); i++) {
      char filename[256];
      snprintf(filename, sizeof(filename),
               "/data/local/tmp/bin/out/output.mediacodec.%zu.bin", i);
      FILE* f = fopen(filename, "wb");
      if (f) {
        fwrite(output->frame_buffers[i].data(), 1, output->frame_sizes[i], f);
        fclose(f);
      }

      // Optionally print timing information
      if (format.debug_level > 0) {
        int64_t encode_time_us = output->timings[i].output_timestamp_us -
                                 output->timings[i].input_timestamp_us;
        printf("Frame %zu: encode time = %lld us\n", i,
               (long long)encode_time_us);
      }
    }
  } else {
    fprintf(stderr, "MediaCodec: Encoding failed\n");
  }

  // Vectors will be automatically freed when output goes out of scope

  PROFILE_RESOURCES_END(mediacodec_encode_cpu);

  // (d) Codec cleanup - cleanup ONCE at the end
  android_mediacodec_encode_cleanup(codec, format.debug_level);

  PROFILE_RESOURCES_END(mediacodec_total_memory);
  return result;
#else
  // Unused on non-Android
  (void)num_runs;
  fprintf(stderr, "MediaCodec: Not available (Android only)\n");
  return -1;
#endif
}

// Main experiment function - uses all sub-runners
int anicet_experiment(const uint8_t* buffer, size_t buf_size, int height,
                      int width, const char* color_format,
                      const char* codec_name, int num_runs) {
  // Validate inputs
  if (!buffer || buf_size == 0 || height <= 0 || width <= 0 || !color_format ||
      !codec_name) {
    fprintf(stderr, "Invalid input parameters\n");
    return -1;
  }

  // Only support yuv420p for now
  if (strcmp(color_format, "yuv420p") != 0) {
    fprintf(stderr, "Only yuv420p format supported currently\n");
    return -1;
  }

  // Determine which codecs to run
  bool run_all = (strcmp(codec_name, "all") == 0);
  bool run_webp = run_all || (strcmp(codec_name, "webp") == 0);
  bool run_webp_nonopt = (strcmp(codec_name, "webp-nonopt") == 0);
  bool run_libjpeg_turbo =
      run_all || (strcmp(codec_name, "libjpeg-turbo") == 0);
  bool run_libjpeg_turbo_nonopt =
      (strcmp(codec_name, "libjpeg-turbo-nonopt") == 0);
  bool run_jpegli = run_all || (strcmp(codec_name, "jpegli") == 0);
  bool run_x265_8bit = run_all || (strcmp(codec_name, "x265-8bit") == 0);
  bool run_x265_8bit_nonopt = (strcmp(codec_name, "x265-8bit-nonopt") == 0);
  bool run_svtav1 = run_all || (strcmp(codec_name, "svt-av1") == 0);
  bool run_mediacodec = run_all || (strcmp(codec_name, "mediacodec") == 0);

  printf("Encoding %dx%d %s image (%zu bytes) with codec: %s...\n", width,
         height, color_format, buf_size, codec_name);

  int errors = 0;

  // 1. WebP encoding
  if (run_webp) {
    printf("\n--- WebP ---\n");
    CodecOutput output;
    if (anicet_run_webp(buffer, buf_size, height, width, color_format, num_runs,
                        &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("WebP: Encoded to %zu bytes (%.2f%% of original)\n", last_size,
             (last_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "WebP: Encoding failed\n");
      errors++;
    }
  }

  // 1b. WebP encoding (nonopt)
  if (run_webp_nonopt) {
    printf("\n--- WebP (nonopt) ---\n");
    CodecOutput output;
    if (anicet_run_webp_nonopt(buffer, buf_size, height, width, color_format,
                               num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("WebP (nonopt): Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "WebP (nonopt): Encoding failed\n");
      errors++;
    }
  }

  // 2. libjpeg-turbo encoding (opt)
  if (run_libjpeg_turbo) {
    printf("\n--- libjpeg-turbo ---\n");
    CodecOutput output;
    if (anicet_run_libjpegturbo(buffer, buf_size, height, width, color_format,
                                num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("TurboJPEG: Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "TurboJPEG: Encoding failed\n");
      errors++;
    }
  }

  // 2b. libjpeg-turbo encoding (nonopt)
  if (run_libjpeg_turbo_nonopt) {
    printf("\n--- libjpeg-turbo (nonopt) ---\n");
    CodecOutput output;
    if (anicet_run_libjpegturbo_nonopt(buffer, buf_size, height, width,
                                       color_format, num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("TurboJPEG (nonopt): Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "TurboJPEG (nonopt): Encoding failed\n");
      errors++;
    }
  }

  // 3. jpegli encoding
  if (run_jpegli) {
    printf("\n--- jpegli ---\n");
    CodecOutput output;
    if (anicet_run_jpegli(buffer, buf_size, height, width, color_format,
                          num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("jpegli: Encoded to %zu bytes (%.2f%% of original)\n", last_size,
             (last_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "jpegli: Encoding failed\n");
      errors++;
    }
  }

  // 4. x265 (H.265/HEVC) 8-bit encoding (opt)
  if (run_x265_8bit) {
    printf("\n--- x265 (H.265/HEVC) 8-bit ---\n");
    CodecOutput output;
    if (anicet_run_x265_8bit(buffer, buf_size, height, width, color_format,
                             num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("x265-8bit: Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "x265-8bit: Encoding failed\n");
      errors++;
    }
  }

  // 4b. x265 (H.265/HEVC) 8-bit encoding (nonopt)
  if (run_x265_8bit_nonopt) {
    printf("\n--- x265 (H.265/HEVC) 8-bit (nonopt) ---\n");
    CodecOutput output;
    if (anicet_run_x265_8bit_nonopt(buffer, buf_size, height, width,
                                    color_format, num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("x265-8bit (nonopt): Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "x265-8bit (nonopt): Encoding failed\n");
      errors++;
    }
  }

  // 5. SVT-AV1 encoding
  if (run_svtav1) {
    printf("\n--- SVT-AV1 ---\n");
    CodecOutput output;
    if (anicet_run_svtav1(buffer, buf_size, height, width, color_format,
                          num_runs, &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("SVT-AV1: Encoded to %zu bytes (%.2f%% of original)\n", last_size,
             (last_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "SVT-AV1: Encoding failed\n");
      errors++;
    }
  }

  // 6. Android MediaCodec encoding (only on Android)
  if (run_mediacodec) {
    printf("\n--- Android MediaCodec ---\n");
#ifdef __ANDROID__
    CodecOutput output;
    if (anicet_run_mediacodec(buffer, buf_size, height, width, color_format,
                              "c2.android.hevc.encoder", num_runs,
                              &output) == 0 &&
        output.num_frames() > 0) {
      size_t last_size = output.frame_sizes[output.num_frames() - 1];
      printf("MediaCodec: Encoded to %zu bytes (%.2f%% of original)\n",
             last_size, (last_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "MediaCodec: Encoding failed\n");
      errors++;
    }
#else
    printf("MediaCodec: Skipped (not on Android)\n");
#endif
  }

  printf("\n=== Encoding complete: %d errors ===\n", errors);
  return errors;
}
