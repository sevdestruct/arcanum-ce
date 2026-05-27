/*
 * avi_reader.h — minimal AVI 1.0 RIFF demuxer.
 *
 * Parses the container layout produced by mainstream encoders (ffmpeg,
 * VirtualDub, Adobe Premiere) for the MJPEG-video + PCM-audio profile
 * the bink_compat backend consumes. Streams chunks one at a time so the
 * caller can pace decode against wall-clock A/V sync.
 *
 * Layout we accept:
 *   RIFF ... AVI
 *     LIST hdrl
 *       avih
 *       LIST strl  (one or more)
 *         strh       'vids' or 'auds'
 *         strf       BITMAPINFOHEADER or WAVEFORMATEX
 *         [JUNK/strn — skipped]
 *     [LIST INFO / JUNK — skipped]
 *     LIST movi
 *       NNdc / NNdb (video) and NNwb (audio) chunks, optional 1-byte
 *       pad after each odd-sized chunk
 *     [idx1 — index (ignored; we stream sequentially)]
 *
 * The reader uses stdio FILE*. All supported targets either expose a
 * normal filesystem at the .avi file path (Windows, Linux, macOS, iOS
 * app sandbox, Android app data dir) or sideload the files there before
 * the game runs (per the project's installation docs).
 */

#ifndef AVI_READER_H_
#define AVI_READER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AviReader AviReader;

typedef struct {
    unsigned width;
    unsigned height;
    unsigned frame_count;
    unsigned frame_duration_us;     /* microseconds per video frame */

    int has_audio;
    int audio_freq;                 /* Hz */
    int audio_channels;
    int audio_bits_per_sample;      /* 8 or 16 */
    int audio_format;               /* WAVE_FORMAT_* (1 = PCM) */

    uint32_t video_fourcc;          /* e.g. 'MJPG' stored little-endian */
} AviInfo;

typedef enum {
    AVI_CHUNK_NONE = 0,
    AVI_CHUNK_VIDEO,
    AVI_CHUNK_AUDIO,
    AVI_CHUNK_EOF
} AviChunkType;

typedef struct {
    AviChunkType type;
    const uint8_t* data;            /* owned by reader; valid until next call */
    size_t size;
    unsigned frame_index;           /* 0-based; meaningful for VIDEO */
} AviChunk;

/* Open an AVI file. Returns NULL on failure. */
AviReader* avi_reader_open(const char* path);

/* Close and free the reader. Safe to pass NULL. */
void avi_reader_close(AviReader* r);

/* Copy the parsed file info. Returns false if reader is NULL. */
bool avi_reader_get_info(const AviReader* r, AviInfo* out);

/* Read the next video or audio chunk from the movi list. On EOF the
 * call still returns true with out->type == AVI_CHUNK_EOF. Returns
 * false on I/O or parse error. */
bool avi_reader_next_chunk(AviReader* r, AviChunk* out);

/* Seek back to the first chunk of the movi list. Used by menu video
 * loops. Returns false on I/O error. */
bool avi_reader_rewind(AviReader* r);

#ifdef __cplusplus
}
#endif

#endif /* AVI_READER_H_ */
