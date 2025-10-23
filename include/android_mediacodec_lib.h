// android_mediacodec_lib.h
// Library interface for Android MediaCodec hardware encoder wrapper
// Provides buffer-based encoding API (no file I/O)

#ifndef ANDROID_MEDIACODEC_LIB_H
#define ANDROID_MEDIACODEC_LIB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// MediaCodec encoding format configuration
typedef struct {
  int width;                 // Frame width in pixels
  int height;                // Frame height in pixels
  const char* codec_name;    // MediaCodec name (e.g., "c2.qti.heic.encoder")
  const char* color_format;  // Color format string ("yuv420p", "nv12", "nv21")
  int quality;      // Quality 0-100 (used to calculate bitrate if bitrate < 0)
  int bitrate;      // Target bitrate in bps (if < 0, calculated from quality)
  int frame_count;  // Number of frames to encode (reuses input_buffer)
  int debug_level;  // Debug verbosity (0 = quiet, 1+ = verbose)
} MediaCodecFormat;

// Forward declaration for Android MediaCodec
struct AMediaCodec;

// Setup MediaCodec encoder
//
// Parameters:
//   format: Encoding configuration (codec, dimensions, quality, etc.)
//   codec:  Output parameter to receive AMediaCodec handle
//
// Returns:
//   0 on success, non-zero error code on failure
//   On success, *codec contains the encoder handle
//   Caller must call android_mediacodec_encode_cleanup() to free resources
int android_mediacodec_encode_setup(const MediaCodecFormat* format,
                                    struct AMediaCodec** codec);

// Encode frames using pre-configured MediaCodec encoder
//
// Parameters:
//   codec:         Codec handle from android_mediacodec_encode_setup()
//   input_buffer:  Raw YUV frame data (single frame, reused frame_count times)
//   input_size:    Size of input buffer in bytes
//   format:        Encoding configuration (for frame_count, color_format,
//   dimensions) output_buffer: Pointer to receive allocated output buffer
//   (caller must free()) output_size:   Pointer to receive output buffer size
//
// Returns:
//   0 on success, non-zero error code on failure
//   On success, *output_buffer contains encoded data (caller must free())
//   On failure, *output_buffer is NULL
int android_mediacodec_encode_frame(struct AMediaCodec* codec,
                                    const uint8_t* input_buffer,
                                    size_t input_size,
                                    const MediaCodecFormat* format,
                                    uint8_t** output_buffer,
                                    size_t* output_size);

// Cleanup MediaCodec encoder and free resources
//
// Parameters:
//   codec: Codec handle from android_mediacodec_encode_setup()
//   debug_level: Debug verbosity level
void android_mediacodec_encode_cleanup(struct AMediaCodec* codec,
                                       int debug_level);

// Full all-in-one encode function (convenience wrapper)
//
// Parameters:
//   input_buffer:  Raw YUV frame data (single frame, reused frame_count times)
//   input_size:    Size of input buffer in bytes
//   format:        Encoding configuration (codec, dimensions, quality, etc.)
//   output_buffer: Pointer to receive allocated output buffer (caller must
//   free()) output_size:   Pointer to receive output buffer size
//
// Returns:
//   0 on success, non-zero error code on failure
//   On success, *output_buffer contains encoded data (caller must free())
//   On failure, *output_buffer is NULL
int android_mediacodec_encode_frame_full(const uint8_t* input_buffer,
                                         size_t input_size,
                                         const MediaCodecFormat* format,
                                         uint8_t** output_buffer,
                                         size_t* output_size);

#ifdef __cplusplus
}
#endif

#endif  // ANDROID_MEDIACODEC_LIB_H
