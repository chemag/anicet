// anicet_runner.cc
// Encoder experiment runner implementation

#include "anicet_runner.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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
                    int width, const char* color_format, uint8_t* output_buffer,
                    size_t* output_size) {
  (void)input_size;    // Unused
  (void)color_format;  // Unused (yuv420p assumed)

  // Validate inputs
  if (!input_buffer || !output_buffer || !output_size) {
    return -1;
  }

  size_t max_output_size = *output_size;
  *output_size = 0;

  WebPConfig config;
  if (!WebPConfigInit(&config)) {
    fprintf(stderr, "WebP: Failed to initialize config\n");
    return -1;
  }

  config.quality = 75;
  config.method = 4;  // Speed/quality trade-off

  WebPPicture picture;
  if (!WebPPictureInit(&picture)) {
    fprintf(stderr, "WebP: Failed to initialize picture\n");
    return -1;
  }

  picture.width = width;
  picture.height = height;
  picture.use_argb = 0;  // Use YUV
  picture.colorspace = WEBP_YUV420;

  // Allocate picture
  if (!WebPPictureAlloc(&picture)) {
    fprintf(stderr, "WebP: Failed to allocate picture\n");
    WebPPictureFree(&picture);
    return -1;
  }

  // Import YUV420 data manually
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

  WebPMemoryWriter writer;
  WebPMemoryWriterInit(&writer);
  picture.writer = WebPMemoryWrite;
  picture.custom_ptr = &writer;

  int result = 0;
  if (!WebPEncode(&config, &picture)) {
    fprintf(stderr, "WebP: Encoding failed\n");
    result = -1;
  } else {
    // Check if output buffer is large enough
    if (writer.size > max_output_size) {
      fprintf(stderr,
              "WebP: Output buffer too small (%zu needed, %zu available)\n",
              writer.size, max_output_size);
      result = -1;
    } else {
      // Copy to caller's buffer
      memcpy(output_buffer, writer.mem, writer.size);
      *output_size = writer.size;
    }
  }

  WebPMemoryWriterClear(&writer);
  WebPPictureFree(&picture);
  return result;
}

// libjpeg-turbo encoder - writes to caller-provided memory buffer only
int anicet_run_libjpegturbo(const uint8_t* input_buffer, size_t input_size,
                            int height, int width, const char* color_format,
                            uint8_t* output_buffer, size_t* output_size) {
  (void)input_size;    // Unused
  (void)color_format;  // Unused (yuv420p assumed)

  // Validate inputs
  if (!input_buffer || !output_buffer || !output_size) {
    return -1;
  }

  size_t max_output_size = *output_size;
  *output_size = 0;

  tjhandle handle = tjInitCompress();
  if (!handle) {
    fprintf(stderr, "TurboJPEG: Failed to initialize compressor\n");
    return -1;
  }

  unsigned char* jpeg_buf = nullptr;
  unsigned long jpeg_size = 0;

  // Compress YUV to JPEG - tjCompressFromYUV allocates output buffer
  int ret =
      tjCompressFromYUV(handle, input_buffer, width, 1, height, TJSAMP_420,
                        &jpeg_buf, &jpeg_size, 75, TJFLAG_FASTDCT);
  if (ret != 0) {
    fprintf(stderr, "TurboJPEG: Encoding failed: %s\n", tjGetErrorStr2(handle));
    tjDestroy(handle);
    return -1;
  }

  int result = 0;
  if (jpeg_size > max_output_size) {
    fprintf(stderr,
            "TurboJPEG: Output buffer too small (%lu needed, %zu available)\n",
            jpeg_size, max_output_size);
    result = -1;
  } else {
    memcpy(output_buffer, jpeg_buf, jpeg_size);
    *output_size = jpeg_size;
  }

  tjFree(jpeg_buf);
  tjDestroy(handle);
  return result;
}

// jpegli encoder - writes to caller-provided memory buffer only
int anicet_run_jpegli(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      uint8_t* output_buffer, size_t* output_size) {
  (void)input_size;    // Unused
  (void)color_format;  // Unused (yuv420p assumed)

  // Validate inputs
  if (!input_buffer || !output_buffer || !output_size) {
    return -1;
  }

  size_t max_output_size = *output_size;
  *output_size = 0;

  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  unsigned char* jpeg_buf = nullptr;
  unsigned long jpeg_size = 0;
  jpeg_mem_dest(&cinfo, &jpeg_buf, &jpeg_size);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_YCbCr;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 75, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  // For YUV420, convert to RGB for JPEG encoding
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

  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = &rgb_buffer[cinfo.next_scanline * row_stride];
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);

  int result = 0;
  if (jpeg_size > max_output_size) {
    fprintf(stderr,
            "jpegli: Output buffer too small (%lu needed, %zu available)\n",
            jpeg_size, max_output_size);
    result = -1;
    free(jpeg_buf);
  } else {
    memcpy(output_buffer, jpeg_buf, jpeg_size);
    *output_size = jpeg_size;
    free(jpeg_buf);
  }

  jpeg_destroy_compress(&cinfo);
  return result;
}

// x265 encoder - writes to caller-provided memory buffer only
int anicet_run_x265(const uint8_t* input_buffer, size_t input_size, int height,
                    int width, const char* color_format, uint8_t* output_buffer,
                    size_t* output_size) {
  (void)input_size;    // Unused
  (void)color_format;  // Unused (yuv420p assumed)

  // Validate inputs
  if (!input_buffer || !output_buffer || !output_size) {
    return -1;
  }

  size_t max_output_size = *output_size;
  *output_size = 0;

  x265_param* param = x265_param_alloc();
  if (!param) {
    fprintf(stderr, "x265: Failed to allocate parameters\n");
    return -1;
  }

  x265_param_default_preset(param, "medium", "zerolatency");
  param->sourceWidth = width;
  param->sourceHeight = height;
  param->fpsNum = 30;
  param->fpsDenom = 1;
  param->internalCsp = X265_CSP_I420;

  x265_encoder* encoder = x265_encoder_open(param);
  if (!encoder) {
    fprintf(stderr, "x265: Failed to open encoder\n");
    x265_param_free(param);
    return -1;
  }

  x265_picture* pic_in = x265_picture_alloc();
  x265_picture_init(param, pic_in);

  // Set up picture planes for YUV420
  pic_in->planes[0] = (void*)input_buffer;
  pic_in->planes[1] = (void*)(input_buffer + width * height);
  pic_in->planes[2] =
      (void*)(input_buffer + width * height + width * height / 4);
  pic_in->stride[0] = width;
  pic_in->stride[1] = width / 2;
  pic_in->stride[2] = width / 2;

  x265_nal* nals = nullptr;
  uint32_t num_nals = 0;
  int frame_size =
      x265_encoder_encode(encoder, &nals, &num_nals, pic_in, nullptr);

  int result = -1;
  if (frame_size > 0) {
    // Calculate total size
    size_t total_size = 0;
    for (uint32_t i = 0; i < num_nals; i++) {
      total_size += nals[i].sizeBytes;
    }

    if (total_size > max_output_size) {
      fprintf(stderr,
              "x265: Output buffer too small (%zu needed, %zu available)\n",
              total_size, max_output_size);
    } else {
      // Copy all NAL units into output buffer
      size_t offset = 0;
      for (uint32_t i = 0; i < num_nals; i++) {
        memcpy(output_buffer + offset, nals[i].payload, nals[i].sizeBytes);
        offset += nals[i].sizeBytes;
      }
      *output_size = total_size;
      result = 0;
    }
  } else {
    fprintf(stderr, "x265: Encoding failed\n");
  }

  x265_picture_free(pic_in);
  x265_encoder_close(encoder);
  x265_param_free(param);
  return result;
}

// SVT-AV1 encoder - writes to caller-provided memory buffer only
int anicet_run_svtav1(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      uint8_t* output_buffer, size_t* output_size) {
  (void)color_format;  // Unused (yuv420p assumed)

  // Validate inputs
  if (!input_buffer || !output_buffer || !output_size) {
    return -1;
  }

  size_t max_output_size = *output_size;
  *output_size = 0;

  EbComponentType* handle = nullptr;
  EbSvtAv1EncConfiguration config;

  // Initialize encoder handle - this loads default config
  EbErrorType res = svt_av1_enc_init_handle(&handle, &config);
  if (res != EB_ErrorNone || !handle) {
    fprintf(stderr, "SVT-AV1: Failed to initialize encoder handle\n");
    return -1;
  }

  // Modify parameters after getting defaults
  config.source_width = width;
  config.source_height = height;
  config.frame_rate_numerator = 30;
  config.frame_rate_denominator = 1;
  config.encoder_bit_depth = 8;
  config.intra_period_length = -1;                 // I-frame only
  config.intra_refresh_type = SVT_AV1_KF_REFRESH;  // Key frame refresh

  res = svt_av1_enc_set_parameter(handle, &config);
  if (res != EB_ErrorNone) {
    fprintf(stderr, "SVT-AV1: Failed to set parameters\n");
    svt_av1_enc_deinit_handle(handle);
    return -1;
  }

  res = svt_av1_enc_init(handle);
  if (res != EB_ErrorNone) {
    fprintf(stderr, "SVT-AV1: Failed to initialize encoder\n");
    svt_av1_enc_deinit_handle(handle);
    return -1;
  }

  EbBufferHeaderType input_buf;
  memset(&input_buf, 0, sizeof(input_buf));
  input_buf.p_buffer = (uint8_t*)input_buffer;
  input_buf.n_filled_len = input_size;
  input_buf.n_alloc_len = input_size;
  input_buf.pic_type = EB_AV1_KEY_PICTURE;

  res = svt_av1_enc_send_picture(handle, &input_buf);
  int result = -1;
  if (res != EB_ErrorNone) {
    fprintf(stderr, "SVT-AV1: Failed to send picture\n");
  } else {
    // Send EOS to flush the encoder
    EbBufferHeaderType eos_buffer;
    memset(&eos_buffer, 0, sizeof(eos_buffer));
    eos_buffer.flags = EB_BUFFERFLAG_EOS;
    svt_av1_enc_send_picture(handle, &eos_buffer);

    EbBufferHeaderType* output_buf = nullptr;
    res = svt_av1_enc_get_packet(handle, &output_buf, 1);
    if (res == EB_ErrorNone && output_buf && output_buf->n_filled_len > 0) {
      if (output_buf->n_filled_len > max_output_size) {
        fprintf(stderr,
                "SVT-AV1: Output buffer too small (%u needed, %zu available)\n",
                output_buf->n_filled_len, max_output_size);
      } else {
        memcpy(output_buffer, output_buf->p_buffer, output_buf->n_filled_len);
        *output_size = output_buf->n_filled_len;
        result = 0;
      }
      svt_av1_enc_release_out_buffer(&output_buf);
    } else {
      fprintf(stderr, "SVT-AV1: Failed to get output packet\n");
    }
  }

  svt_av1_enc_deinit(handle);
  svt_av1_enc_deinit_handle(handle);
  return result;
}

// x265 encoder (non-optimized) - stub implementation
// Note: Both libx265.a and libx265-noopt.a export the same symbols,
// so we cannot link both into the same binary. Use the x265-noopt CLI tool
// instead.
int anicet_run_x265_noopt(const uint8_t* input_buffer, size_t input_size,
                          int height, int width, const char* color_format,
                          uint8_t* output_buffer, size_t* output_size) {
  (void)input_buffer;
  (void)input_size;
  (void)height;
  (void)width;
  (void)color_format;
  (void)output_buffer;
  (void)output_size;

  fprintf(stderr, "x265-noopt: Library API not available (symbol conflict)\n");
  fprintf(stderr,
          "x265-noopt: Use CLI tool 'x265-noopt' instead for non-optimized "
          "encoding\n");
  return -1;
}

// libjpeg-turbo encoder (non-optimized) - stub implementation
int anicet_run_libjpegturbo_noopt(const uint8_t* input_buffer,
                                  size_t input_size, int height, int width,
                                  const char* color_format,
                                  uint8_t* output_buffer, size_t* output_size) {
  (void)input_buffer;
  (void)input_size;
  (void)height;
  (void)width;
  (void)color_format;
  (void)output_buffer;
  (void)output_size;

  fprintf(stderr,
          "libjpeg-turbo-noopt: Library API not available (symbol conflict)\n");
  fprintf(stderr,
          "libjpeg-turbo-noopt: Use CLI tool 'cjpeg-noopt' instead for "
          "non-optimized encoding\n");
  return -1;
}

// WebP encoder (non-optimized) - stub implementation
int anicet_run_webp_noopt(const uint8_t* input_buffer, size_t input_size,
                          int height, int width, const char* color_format,
                          uint8_t* output_buffer, size_t* output_size) {
  (void)input_buffer;
  (void)input_size;
  (void)height;
  (void)width;
  (void)color_format;
  (void)output_buffer;
  (void)output_size;

  fprintf(stderr, "webp-noopt: Library API not available (symbol conflict)\n");
  fprintf(stderr,
          "webp-noopt: Use CLI tool 'cwebp-noopt' instead for non-optimized "
          "encoding\n");
  return -1;
}

// Android MediaCodec encoder - wrapper that adapts
// android_mediacodec_encode_frame()
int anicet_run_mediacodec(const uint8_t* input_buffer, size_t input_size,
                          int height, int width, const char* color_format,
                          const char* codec_name, uint8_t* output_buffer,
                          size_t* output_size) {
  // Validate inputs
  if (!input_buffer || !output_buffer || !output_size || !codec_name) {
    return -1;
  }

  size_t max_output_size = *output_size;
  *output_size = 0;

#ifdef __ANDROID__
  MediaCodecFormat format;
  format.width = width;
  format.height = height;
  format.codec_name = codec_name;
  format.color_format = color_format;
  format.quality = 75;
  format.bitrate = -1;  // Auto-calculate from quality
  format.frame_count = 1;
  format.debug_level = 0;  // Quiet

  // android_mediacodec_encode_frame allocates its own buffer
  uint8_t* mediacodec_buffer = nullptr;
  size_t mediacodec_size = 0;

  int ret = android_mediacodec_encode_frame(
      input_buffer, input_size, &format, &mediacodec_buffer, &mediacodec_size);
  if (ret != 0) {
    return ret;
  }

  // Check if it fits in caller's buffer
  if (mediacodec_size > max_output_size) {
    fprintf(stderr,
            "MediaCodec: Output buffer too small (%zu needed, %zu available)\n",
            mediacodec_size, max_output_size);
    free(mediacodec_buffer);
    return -1;
  }

  // Copy to caller's buffer
  memcpy(output_buffer, mediacodec_buffer, mediacodec_size);
  *output_size = mediacodec_size;
  free(mediacodec_buffer);
  return 0;
#else
  fprintf(stderr, "MediaCodec: Not available (Android only)\n");
  return -1;
#endif
}

// Main experiment function - uses all sub-runners
int anicet_experiment(const uint8_t* buffer, size_t buf_size, int height,
                      int width, const char* color_format,
                      const char* codec_name) {
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
  bool run_webp_noopt = (strcmp(codec_name, "webp-noopt") == 0);
  bool run_libjpeg_turbo =
      run_all || (strcmp(codec_name, "libjpeg-turbo") == 0);
  bool run_libjpeg_turbo_noopt =
      (strcmp(codec_name, "libjpeg-turbo-noopt") == 0);
  bool run_jpegli = run_all || (strcmp(codec_name, "jpegli") == 0);
  bool run_x265 = run_all || (strcmp(codec_name, "x265") == 0);
  bool run_x265_noopt = (strcmp(codec_name, "x265-noopt") == 0);
  bool run_svtav1 = run_all || (strcmp(codec_name, "svt-av1") == 0);
  bool run_mediacodec = run_all || (strcmp(codec_name, "mediacodec") == 0);

  printf("Encoding %dx%d %s image (%zu bytes) with codec: %s...\n", width,
         height, color_format, buf_size, codec_name);

  int errors = 0;
  // Allocate output buffer - assume worst case (raw size)
  size_t output_buffer_size = buf_size * 2;  // Generous estimate
  uint8_t* output_buffer = (uint8_t*)malloc(output_buffer_size);
  if (!output_buffer) {
    fprintf(stderr, "Failed to allocate output buffer\n");
    return -1;
  }

  // 1. WebP encoding
  if (run_webp) {
    printf("\n--- WebP ---\n");
    size_t output_size = output_buffer_size;
    if (anicet_run_webp(buffer, buf_size, height, width, color_format,
                        output_buffer, &output_size) == 0) {
      printf("WebP: Encoded to %zu bytes (%.2f%% of original)\n", output_size,
             (output_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "WebP: Encoding failed\n");
      errors++;
    }
  }

  // 1b. WebP encoding (noopt)
  if (run_webp_noopt) {
    printf("\n--- WebP (noopt) ---\n");
    size_t output_size = output_buffer_size;
    if (anicet_run_webp_noopt(buffer, buf_size, height, width, color_format,
                              output_buffer, &output_size) == 0) {
      printf("WebP (noopt): Encoded to %zu bytes (%.2f%% of original)\n",
             output_size, (output_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "WebP (noopt): Encoding failed\n");
      errors++;
    }
  }

  // 2. libjpeg-turbo encoding (opt)
  if (run_libjpeg_turbo) {
    printf("\n--- libjpeg-turbo ---\n");
    size_t output_size = output_buffer_size;
    if (anicet_run_libjpegturbo(buffer, buf_size, height, width, color_format,
                                output_buffer, &output_size) == 0) {
      printf("TurboJPEG: Encoded to %zu bytes (%.2f%% of original)\n",
             output_size, (output_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "TurboJPEG: Encoding failed\n");
      errors++;
    }
  }

  // 2b. libjpeg-turbo encoding (noopt)
  if (run_libjpeg_turbo_noopt) {
    printf("\n--- libjpeg-turbo (noopt) ---\n");
    size_t output_size = output_buffer_size;
    if (anicet_run_libjpegturbo_noopt(buffer, buf_size, height, width,
                                      color_format, output_buffer,
                                      &output_size) == 0) {
      printf("TurboJPEG (noopt): Encoded to %zu bytes (%.2f%% of original)\n",
             output_size, (output_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "TurboJPEG (noopt): Encoding failed\n");
      errors++;
    }
  }

  // 3. jpegli encoding
  if (run_jpegli) {
    printf("\n--- jpegli ---\n");
    size_t output_size = output_buffer_size;
    if (anicet_run_jpegli(buffer, buf_size, height, width, color_format,
                          output_buffer, &output_size) == 0) {
      printf("jpegli: Encoded to %zu bytes (%.2f%% of original)\n", output_size,
             (output_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "jpegli: Encoding failed\n");
      errors++;
    }
  }

  // 4. x265 (H.265/HEVC) encoding (opt)
  if (run_x265) {
    printf("\n--- x265 (H.265/HEVC) ---\n");
    size_t output_size = output_buffer_size;
    if (anicet_run_x265(buffer, buf_size, height, width, color_format,
                        output_buffer, &output_size) == 0) {
      printf("x265: Encoded to %zu bytes (%.2f%% of original)\n", output_size,
             (output_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "x265: Encoding failed\n");
      errors++;
    }
  }

  // 4b. x265 (H.265/HEVC) encoding (noopt)
  if (run_x265_noopt) {
    printf("\n--- x265 (H.265/HEVC) (noopt) ---\n");
    size_t output_size = output_buffer_size;
    if (anicet_run_x265_noopt(buffer, buf_size, height, width, color_format,
                              output_buffer, &output_size) == 0) {
      printf("x265 (noopt): Encoded to %zu bytes (%.2f%% of original)\n",
             output_size, (output_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "x265 (noopt): Encoding failed\n");
      errors++;
    }
  }

  // 5. SVT-AV1 encoding
  if (run_svtav1) {
    printf("\n--- SVT-AV1 ---\n");
    size_t output_size = output_buffer_size;
    if (anicet_run_svtav1(buffer, buf_size, height, width, color_format,
                          output_buffer, &output_size) == 0) {
      printf("SVT-AV1: Encoded to %zu bytes (%.2f%% of original)\n",
             output_size, (output_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "SVT-AV1: Encoding failed\n");
      errors++;
    }
  }

  // 6. Android MediaCodec encoding (only on Android)
  if (run_mediacodec) {
    printf("\n--- Android MediaCodec ---\n");
#ifdef __ANDROID__
    size_t output_size = output_buffer_size;
    if (anicet_run_mediacodec(buffer, buf_size, height, width, color_format,
                              "c2.android.hevc.encoder", output_buffer,
                              &output_size) == 0) {
      printf("MediaCodec: Encoded to %zu bytes (%.2f%% of original)\n",
             output_size, (output_size * 100.0) / buf_size);
    } else {
      fprintf(stderr, "MediaCodec: Encoding failed\n");
      errors++;
    }
#else
    printf("MediaCodec: Skipped (not on Android)\n");
#endif
  }

  free(output_buffer);
  printf("\n=== Encoding complete: %d errors ===\n", errors);
  return errors;
}
