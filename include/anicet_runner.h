// anicet_runner.h
// Encoder experiment runner that tests multiple encoder libraries

#ifndef ANICET_RUNNER_H
#define ANICET_RUNNER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <vector>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Per-frame timing information
typedef struct {
  int64_t input_timestamp_us;   // Before encoding starts
  int64_t output_timestamp_us;  // After encoding completes
} CodecFrameTiming;

#ifdef __cplusplus
}

// Codec encoding output with timing data (C++ only)
// This structure uses C++ vectors for automatic memory management
struct CodecOutput {
  std::vector<std::vector<uint8_t>>
      frame_buffers;                // Output buffers (one per frame)
  std::vector<size_t> frame_sizes;  // Output sizes (one per frame) - redundant
                                    // but kept for compatibility
  std::vector<CodecFrameTiming> timings;  // Timing data (one per frame)

  // Helper method to get number of frames
  size_t num_frames() const { return frame_buffers.size(); }
};

// Individual encoder sub-runners (C++ only)
// All sub-runners follow the same pattern:
//   - Input: raw YUV420p buffer, dimensions, color format, num_runs
//   - Output: CodecOutput structure with encoded frames and timing data
//   - Return: 0 on success, -1 on error
//
// NOTE: All encoders write to memory buffers only, no file I/O
// NOTE: If dump_output is false, encoders skip copying frame_buffers to save
//       memory but still populate frame_sizes and timings for statistics

// WebP encoder - optimized
int anicet_run_webp(const uint8_t* input_buffer, size_t input_size, int height,
                    int width, const char* color_format, int num_runs,
                    bool dump_output, CodecOutput* output);

// WebP encoder - non-optimized (no SIMD)
int anicet_run_webp_nonopt(const uint8_t* input_buffer, size_t input_size,
                           int height, int width, const char* color_format,
                           int num_runs, bool dump_output, CodecOutput* output);

// libjpeg-turbo encoder (using TurboJPEG API) - optimized
int anicet_run_libjpegturbo(const uint8_t* input_buffer, size_t input_size,
                            int height, int width, const char* color_format,
                            int num_runs, bool dump_output,
                            CodecOutput* output);

// libjpeg-turbo encoder (using TurboJPEG API) - non-optimized (no SIMD)
int anicet_run_libjpegturbo_nonopt(const uint8_t* input_buffer,
                                   size_t input_size, int height, int width,
                                   const char* color_format, int num_runs,
                                   bool dump_output, CodecOutput* output);

// jpegli encoder (JPEG XL's JPEG encoder)
int anicet_run_jpegli(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      int num_runs, bool dump_output, CodecOutput* output);

// x265 encoder (H.265/HEVC) 8-bit - optimized
int anicet_run_x265_8bit(const uint8_t* input_buffer, size_t input_size,
                         int height, int width, const char* color_format,
                         int num_runs, bool dump_output, CodecOutput* output);

// x265 encoder (H.265/HEVC) 8-bit - non-optimized (no assembly)
int anicet_run_x265_8bit_nonopt(const uint8_t* input_buffer, size_t input_size,
                                int height, int width, const char* color_format,
                                int num_runs, bool dump_output,
                                CodecOutput* output);

// SVT-AV1 encoder
int anicet_run_svtav1(const uint8_t* input_buffer, size_t input_size,
                      int height, int width, const char* color_format,
                      int num_runs, bool dump_output, CodecOutput* output);

// Android MediaCodec encoder (Android only)
int anicet_run_mediacodec(const uint8_t* input_buffer, size_t input_size,
                          int height, int width, const char* color_format,
                          const char* codec_name, int num_runs,
                          bool dump_output, CodecOutput* output);

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
//   num_runs:     Number of times to encode the same frame
//   dump_output:  Write output files to disk (default: false)
//
// Returns:
//   Number of encoding errors (0 = all succeeded)
int anicet_experiment(const uint8_t* buffer, size_t buf_size, int height,
                      int width, const char* color_format,
                      const char* codec_name, int num_runs, bool dump_output);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_H
