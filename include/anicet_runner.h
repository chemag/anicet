// anicet_runner.h
// Encoder experiment runner that tests multiple encoder libraries

#ifndef ANICET_RUNNER_H
#define ANICET_RUNNER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Individual encoder sub-runners
// All sub-runners follow the same pattern:
//   - Input: raw YUV420p buffer, dimensions, color format, output buffer
//   - Output: encoded data written to output_buffer, size updated
//   - output_size: INPUT = max buffer capacity, OUTPUT = actual bytes written
//   - Return: 0 on success, -1 on error (including buffer too small)
//
// NOTE: All encoders write to memory buffers only, no file I/O

// WebP encoder - optimized
int anicet_run_webp(const uint8_t* input_buffer, size_t input_size, int height,
                    int width, const char* color_format, uint8_t* output_buffer,
                    size_t* output_size);

// WebP encoder - non-optimized (no SIMD)
int anicet_run_webp_nonopt(const uint8_t* input_buffer, size_t input_size,
                           int height, int width, const char* color_format,
                           uint8_t* output_buffer, size_t* output_size);

// libjpeg-turbo encoder (using TurboJPEG API) - optimized
int anicet_run_libjpegturbo(const uint8_t* input_buffer, size_t input_size,
                            int height, int width, const char* color_format,
                            uint8_t* output_buffer, size_t* output_size);

// libjpeg-turbo encoder (using TurboJPEG API) - non-optimized (no SIMD)
int anicet_run_libjpegturbo_nonopt(const uint8_t* input_buffer,
                                   size_t input_size, int height, int width,
                                   const char* color_format,
                                   uint8_t* output_buffer, size_t* output_size);

// jpegli encoder (JPEG XL's JPEG encoder)
int anicet_run_jpegli(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      uint8_t* output_buffer, size_t* output_size);

// x265 encoder (H.265/HEVC) - optimized
int anicet_run_x265(const uint8_t* input_buffer, size_t input_size, int height,
                    int width, const char* color_format, uint8_t* output_buffer,
                    size_t* output_size);

// x265 encoder (H.265/HEVC) - non-optimized (no assembly)
int anicet_run_x265_nonopt(const uint8_t* input_buffer, size_t input_size,
                           int height, int width, const char* color_format,
                           uint8_t* output_buffer, size_t* output_size);

// SVT-AV1 encoder
int anicet_run_svtav1(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      uint8_t* output_buffer, size_t* output_size);

// Android MediaCodec encoder (Android only)
// This is a wrapper around android_mediacodec_encode_frame()
// NOTE: This function still allocates memory internally via
// android_mediacodec_encode_frame
//       The allocated buffer is copied to output_buffer if it fits
int anicet_run_mediacodec(const uint8_t* input_buffer, size_t input_size,
                          int height, int width, const char* color_format,
                          const char* codec_name, uint8_t* output_buffer,
                          size_t* output_size);

// Run encoding experiment with multiple encoders
// Encodes the same raw YUV420p image using specified encoder(s)
// and reports the compressed size for each
//
// Parameters:
//   buffer:       Raw YUV420p image data
//   buf_size:     Size of buffer in bytes
//   height:       Image height in pixels
//   width:        Image width in pixels
//   color_format: Color format string (currently only "yuv420p" supported)
//   codec_name:   Codec to use: "x265", "x265-nonopt", "svt-av1",
//                 "libjpeg-turbo", "libjpeg-turbo-nonopt", "jpegli",
//                 "webp", "mediacodec", "all" (default: all encoders)
//
// Returns:
//   Number of encoding errors (0 = all succeeded)
int anicet_experiment(const uint8_t* buffer, size_t buf_size, int height,
                      int width, const char* color_format,
                      const char* codec_name);

#ifdef __cplusplus
}
#endif

#endif  // ANICET_RUNNER_H
