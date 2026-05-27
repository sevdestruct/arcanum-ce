/*
 * bink1_decoder.h -- from-scratch Bink1 (BIK[a-i]) video container +
 * video + audio decoder.
 *
 * This is the v2 of the cross-platform video work, alongside the
 * MJPEG/AVI backend. The MJPEG path requires users to pre-convert
 * their .bik files; this path decodes .bik directly so playback "just
 * works" from a stock install on every supported target.
 *
 * Scope:
 *   - Container parsing: Bink1 header + per-frame index table.
 *   - Bink Video decoder: Y/U/V plane reconstruction. Block types
 *     SKIP / MOTION / RUN / RESIDUE / INTRA / FILL / INTER /
 *     PATTERN / RAW / SCALED, bundle-coded Huffman, custom 8x8 DCT,
 *     half-pel motion compensation.
 *   - Bink Audio decoder: RDFT + DCT modes, overlap-add, stereo and
 *     mono.
 *
 * Bink2 is intentionally out of scope -- Arcanum's cutscenes are all
 * Bink1, and Bink2 is a much larger and more recent codec.
 *
 * The format is RAD Game Tools' proprietary container. Layout
 * documentation comes from the publicly available Multimedia Wiki
 * reverse-engineering writeup; this implementation is clean-room
 * (no code derived from FFmpeg's LGPL libavcodec/binkdec.c or
 * ScummVM's GPL video/bink_decoder.cpp). The intent is interop with
 * legitimate .bik content the user already owns, in the same spirit
 * as the project's existing binkw32.dll runtime-load on Windows x86.
 *
 * Public API contract: the bink_compat shim layer dispatches the
 * legacy HBINK calls (BinkOpen / BinkDoFrame / BinkCopyToBuffer /
 * BinkWait / BinkClose) through this decoder when the input path is
 * a .bik file; the AVI/MJPEG backend remains the fallback for files
 * already converted under the v1 path.
 */

#ifndef BINK1_DECODER_H_
#define BINK1_DECODER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Bink1Decoder Bink1Decoder;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t frame_duration_us;  /* microseconds per video frame */
    uint8_t  video_version;      /* 'a'..'i' from the BIK<x> magic */
    uint8_t  has_alpha;
    uint8_t  has_grayscale;
    uint8_t  audio_track_count;
    /* Only first audio track is reported here (we mix down to the
     * primary track for BINKSND). */
    int32_t  audio_sample_rate;
    int32_t  audio_channels;
    int32_t  audio_is_dct;       /* 1 = DCT mode, 0 = RDFT (FFT) mode */
} Bink1Info;

typedef enum {
    BINK1_CHUNK_NONE = 0,
    BINK1_CHUNK_VIDEO,           /* video bitstream for one frame */
    BINK1_CHUNK_AUDIO,           /* PCM s16le decoded audio for one frame */
    BINK1_CHUNK_EOF
} Bink1ChunkType;

typedef struct {
    Bink1ChunkType type;
    const uint8_t* data;         /* owned by decoder; valid until next call */
    size_t size;
    unsigned frame_index;        /* 0-based; meaningful for VIDEO */
} Bink1Chunk;

/* Open a Bink1 file. Returns NULL if the file isn't a Bink1 stream
 * (magic check), or on I/O / allocation failure. */
Bink1Decoder* bink1_decoder_open(const char* path);

/* Free a decoder. Safe to pass NULL. */
void bink1_decoder_close(Bink1Decoder* d);

/* Read parsed file info. */
bool bink1_decoder_get_info(const Bink1Decoder* d, Bink1Info* out);

/* Decode the next frame's video into a caller-supplied BGRA buffer
 * (4 bytes per pixel, pitch in bytes). The destination must be
 * dst_w x dst_h; output is clipped if smaller than the video. */
bool bink1_decoder_decode_video(Bink1Decoder* d,
    uint8_t* dst, int dst_pitch, int dst_w, int dst_h);

/* Decode the next frame's audio for the primary track into PCM s16le
 * stereo samples. *out_bytes is set to the byte length written. Pass
 * a buffer at least bink1_decoder_max_audio_bytes(d) long. */
bool bink1_decoder_decode_audio(Bink1Decoder* d,
    uint8_t* dst, size_t dst_capacity, size_t* out_bytes);

/* Worst-case audio bytes per frame for the primary track. */
size_t bink1_decoder_max_audio_bytes(const Bink1Decoder* d);

/* Advance to the next frame. Returns false at EOF. */
bool bink1_decoder_next_frame(Bink1Decoder* d);

/* Restart playback from frame 0. */
bool bink1_decoder_rewind(Bink1Decoder* d);

/* Inspect current decoder state. */
unsigned bink1_decoder_current_frame(const Bink1Decoder* d);
bool bink1_decoder_at_eof(const Bink1Decoder* d);

#ifdef __cplusplus
}
#endif

#endif /* BINK1_DECODER_H_ */
