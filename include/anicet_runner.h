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

// Codec input data (C++ only)
// This structure holds all input parameters for encoding
struct CodecInput {
  const uint8_t* input_buffer;  // Raw input data
  size_t input_size;            // Size of input buffer in bytes
  int height;                   // Image height in pixels
  int width;                    // Image width in pixels
  const char* color_format;     // Color format string (e.g., "yuv420p")
};

// Codec encoding output with timing data (C++ only)
// This structure uses C++ vectors for automatic memory management
struct CodecOutput {
  std::vector<std::vector<uint8_t>>
      frame_buffers;                // Output buffers (one per frame)
  std::vector<size_t> frame_sizes;  // Output sizes (one per frame) - redundant
                                    // but kept for compatibility
  std::vector<CodecFrameTiming> timings;  // Timing data (one per frame)
  bool dump_output;  // Whether to copy encoded data to frame_buffers

  // Resource consumption statistics
  std::vector<double>
      profile_encode_cpu_ms;   // CPU time per frame (milliseconds)
  long profile_encode_mem_kb;  // Peak memory usage (kilobytes)

  // Helper method to get number of frames
  size_t num_frames() const { return frame_buffers.size(); }
};

// Individual encoder sub-runners (C++ only)
// All sub-runners follow the same pattern:
//   - Input: CodecInput structure with raw data and parameters, num_runs
//   - Output: CodecOutput structure with encoded frames and timing data
//   - Return: 0 on success, -1 on error
//
// NOTE: All encoders write to memory buffers only, no file I/O
// NOTE: If output->dump_output is false, encoders skip copying frame_buffers
//       to save memory but still populate frame_sizes and timings for
//       statistics

// WebP encoder - optimized
int anicet_run_webp(const CodecInput* input, int num_runs, CodecOutput* output);

// WebP encoder - non-optimized (no SIMD)
int anicet_run_webp_nonopt(const CodecInput* input, int num_runs,
                           CodecOutput* output);

// libjpeg-turbo encoder (using TurboJPEG API) - optimized
int anicet_run_libjpegturbo(const CodecInput* input, int num_runs,
                            CodecOutput* output);

// libjpeg-turbo encoder (using TurboJPEG API) - non-optimized (no SIMD)
int anicet_run_libjpegturbo_nonopt(const CodecInput* input, int num_runs,
                                   CodecOutput* output);

// jpegli encoder (JPEG XL's JPEG encoder)
int anicet_run_jpegli(const CodecInput* input, int num_runs,
                      CodecOutput* output);

// x265 encoder (H.265/HEVC) 8-bit - optimized
int anicet_run_x265_8bit(const CodecInput* input, int num_runs,
                         CodecOutput* output);

// x265 encoder (H.265/HEVC) 8-bit - non-optimized (no assembly)
int anicet_run_x265_8bit_nonopt(const CodecInput* input, int num_runs,
                                CodecOutput* output);

// SVT-AV1 encoder
int anicet_run_svtav1(const CodecInput* input, int num_runs,
                      CodecOutput* output);

// Android MediaCodec encoder (Android only)
int anicet_run_mediacodec(const CodecInput* input, const char* codec_name,
                          int num_runs, CodecOutput* output);

// Run encoding experiment with multiple encoders
// Encodes the same raw YUV420p image using specified encoder(s)
// and reports the compressed size for each
//
// Parameters:
//   buffer:             Raw YUV420p image data
//   buf_size:           Size of buffer in bytes
//   height:             Image height in pixels
//   width:              Image width in pixels
//   color_format:       Color format string (currently only "yuv420p"
//   supported) codec_name:         Codec to use: "x265", "x265-nonopt",
//   "svt-av1",
//                       "libjpeg-turbo", "libjpeg-turbo-nonopt", "jpegli",
//                       "webp", "mediacodec", "all" (default: all encoders)
//   num_runs:           Number of times to encode the same frame
//   dump_output:        Write output files to disk (default: false)
//   dump_output_dir:    Directory for output files (default: executable
//   directory) dump_output_prefix: Prefix for output files (default:
//   "anicet.output")
//
// Returns:
//   Number of encoding errors (0 = all succeeded)
int anicet_experiment(const uint8_t* buffer, size_t buf_size, int height,
                      int width, const char* color_format,
                      const char* codec_name, int num_runs, bool dump_output,
                      const char* dump_output_dir,
                      const char* dump_output_prefix);

#endif  // __cplusplus

#endif  // ANICET_RUNNER_H
