/*
 * mjpeg_decoder.h — baseline JPEG decoder for Motion JPEG frames.
 *
 * A small, dependency-free baseline JPEG decoder used by the AVI/MJPEG
 * backend in bink_compat. Decodes one Motion JPEG frame at a time into a
 * 32-bit BGRA buffer with caller-supplied pitch, matching the pixel
 * layout the engine's TIG video buffer already expects (SDL surface with
 * SDL_PIXELFORMAT_ARGB8888 on little-endian hosts).
 *
 * Supported subset:
 *   - Baseline DCT (SOF0), 8-bit precision, 1 or 3 components
 *   - YCbCr 4:4:4 / 4:2:2 / 4:2:0 / 4:1:1 chroma subsampling
 *   - Standard Huffman + quantisation tables carried in-stream (DHT/DQT)
 *   - Restart markers (RST0..RST7) and DRI
 *
 * Unsupported (rejected at parse time):
 *   - Progressive, hierarchical, arithmetic, lossless modes
 *   - 12-bit precision, more than 3 components, CMYK
 *
 * Origin: clean-room baseline implementation modelled on NanoJPEG
 * (public domain, Martin J. Fiedler). No external dependencies.
 */

#ifndef MJPEG_DECODER_H_
#define MJPEG_DECODER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MjpegDecoder MjpegDecoder;

/* Allocate a decoder. Returns NULL on allocation failure. */
MjpegDecoder* mjpeg_decoder_create(void);

/* Free a decoder created by mjpeg_decoder_create(). Safe to pass NULL. */
void mjpeg_decoder_destroy(MjpegDecoder* dec);

/*
 * Decode a single JPEG-encoded frame.
 *
 *   dec       - decoder handle
 *   data,size - JPEG bitstream (from one AVI ##dc chunk)
 *   dst       - destination pixel buffer, 32-bit BGRA
 *   dst_pitch - bytes per destination row (typically >= 4*width)
 *   dst_w/h   - destination buffer dimensions. Output is clipped to
 *               min(jpeg_w, dst_w) x min(jpeg_h, dst_h).
 *
 * The decoder reuses Huffman / quantisation tables across calls when
 * subsequent frames omit DHT/DQT segments (a common Motion JPEG
 * encoding). The first frame in a stream must carry its tables.
 *
 * Returns true on success. On failure, the destination buffer is left
 * in an unspecified state and the decoder is reset.
 */
bool mjpeg_decoder_decode(MjpegDecoder* dec,
    const uint8_t* data, size_t size,
    uint8_t* dst, int dst_pitch, int dst_w, int dst_h);

/* Inspect the dimensions of the most recently decoded frame. Returns
 * false if no frame has been decoded yet. */
bool mjpeg_decoder_get_dimensions(const MjpegDecoder* dec,
    int* out_w, int* out_h);

#ifdef __cplusplus
}
#endif

#endif /* MJPEG_DECODER_H_ */
