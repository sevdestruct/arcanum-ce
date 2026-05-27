/*
 * avi_reader.c — minimal AVI 1.0 RIFF demuxer (see avi_reader.h).
 *
 * Single-pass header parse + sequential chunk iteration. No index use;
 * we trust the avih frame count and stream sequentially, which keeps
 * memory bounded to one chunk-size buffer regardless of file length.
 *
 * All multi-byte fields in AVI are little-endian by the RIFF spec.
 * We read them via byte-shift to avoid host endianness assumptions.
 */

#include "avi_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) \
    | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define FCC_RIFF FCC('R','I','F','F')
#define FCC_LIST FCC('L','I','S','T')
#define FCC_AVI  FCC('A','V','I',' ')
#define FCC_HDRL FCC('h','d','r','l')
#define FCC_AVIH FCC('a','v','i','h')
#define FCC_STRL FCC('s','t','r','l')
#define FCC_STRH FCC('s','t','r','h')
#define FCC_STRF FCC('s','t','r','f')
#define FCC_MOVI FCC('m','o','v','i')
#define FCC_VIDS FCC('v','i','d','s')
#define FCC_AUDS FCC('a','u','d','s')
#define FCC_JUNK FCC('J','U','N','K')

struct AviReader {
    FILE* fp;
    AviInfo info;

    long movi_offset;   /* first byte after the "movi" fourcc */
    long movi_end;      /* one past last byte of LIST movi content */

    uint8_t* buf;
    size_t buf_cap;

    unsigned video_stream_id;
    unsigned audio_stream_id;
    int have_video_stream;
    int have_audio_stream;

    unsigned video_frame_counter;
};

static uint16_t rd_u16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd_u32(const uint8_t* p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static bool read_exact(FILE* fp, void* dst, size_t n)
{
    return fread(dst, 1, n, fp) == n;
}

static bool skip_bytes(FILE* fp, long n)
{
    return fseek(fp, n, SEEK_CUR) == 0;
}

static bool grow_buf(AviReader* r, size_t need)
{
    if (need <= r->buf_cap) return true;
    size_t cap = r->buf_cap ? r->buf_cap : 4096;
    while (cap < need) cap *= 2;
    uint8_t* nb = (uint8_t*)realloc(r->buf, cap);
    if (!nb) return false;
    r->buf = nb;
    r->buf_cap = cap;
    return true;
}

/* Parse "avih" payload (AVIMAINHEADER, 56 bytes). We only need
 * micros-per-frame and total-frames; everything else is informational. */
static bool parse_avih(AviReader* r, uint32_t size)
{
    if (size < 32) return false;
    uint8_t hdr[56];
    size_t n = size < sizeof(hdr) ? size : sizeof(hdr);
    if (!read_exact(r->fp, hdr, n)) return false;
    if (size > n) {
        if (!skip_bytes(r->fp, (long)(size - n))) return false;
    }
    /* Layout: micros/frame, max_bytes/sec, padding, flags, total_frames, ... */
    r->info.frame_duration_us = rd_u32(hdr + 0);
    if (n >= 20) r->info.frame_count = rd_u32(hdr + 16);
    /* width/height in avih are unreliable for some encoders; we use
     * BITMAPINFOHEADER values instead. */
    return true;
}

/* Parse "strh" payload (AVISTREAMHEADER, 56 bytes). Returns the stream
 * type fourcc in *out_type, or 0 on failure. */
static bool parse_strh(AviReader* r, uint32_t size, uint32_t* out_type)
{
    if (size < 8) return false;
    uint8_t hdr[64];
    size_t n = size < sizeof(hdr) ? size : sizeof(hdr);
    if (!read_exact(r->fp, hdr, n)) return false;
    if (size > n) {
        if (!skip_bytes(r->fp, (long)(size - n))) return false;
    }
    *out_type = rd_u32(hdr + 0);    /* fccType */
    return true;
}

/* Parse "strf" for the video stream: BITMAPINFOHEADER (40 bytes
 * minimum). Extract width/height/biCompression. */
static bool parse_strf_video(AviReader* r, uint32_t size)
{
    if (size < 24) return false;
    uint8_t hdr[40];
    size_t n = size < sizeof(hdr) ? size : sizeof(hdr);
    if (!read_exact(r->fp, hdr, n)) return false;
    if (size > n) {
        if (!skip_bytes(r->fp, (long)(size - n))) return false;
    }
    r->info.width  = rd_u32(hdr + 4);
    r->info.height = rd_u32(hdr + 8);
    if (n >= 20) r->info.video_fourcc = rd_u32(hdr + 16);
    return true;
}

/* Parse "strf" for the audio stream: WAVEFORMATEX (18 bytes minimum). */
static bool parse_strf_audio(AviReader* r, uint32_t size)
{
    if (size < 14) return false;
    uint8_t hdr[40];
    size_t n = size < sizeof(hdr) ? size : sizeof(hdr);
    if (!read_exact(r->fp, hdr, n)) return false;
    if (size > n) {
        if (!skip_bytes(r->fp, (long)(size - n))) return false;
    }
    r->info.audio_format = rd_u16(hdr + 0);
    r->info.audio_channels = rd_u16(hdr + 2);
    r->info.audio_freq = (int)rd_u32(hdr + 4);
    if (n >= 16) r->info.audio_bits_per_sample = rd_u16(hdr + 14);
    return true;
}

/* Walk a strl LIST: strh + strf (+ ignored siblings). Assigns the
 * stream index based on parse order. */
static bool parse_strl(AviReader* r, uint32_t list_size, unsigned stream_index)
{
    long end = ftell(r->fp);
    if (end < 0) return false;
    end += (long)list_size - 4;  /* list_size includes the "strl" fourcc */

    uint32_t stream_type = 0;
    int saw_strh = 0;
    int saw_strf = 0;

    while (ftell(r->fp) < end) {
        uint8_t hdr[8];
        if (!read_exact(r->fp, hdr, 8)) return false;
        uint32_t fcc = rd_u32(hdr);
        uint32_t sz = rd_u32(hdr + 4);

        if (fcc == FCC_STRH) {
            if (!parse_strh(r, sz, &stream_type)) return false;
            saw_strh = 1;
        } else if (fcc == FCC_STRF) {
            if (!saw_strh) return false;
            if (stream_type == FCC_VIDS) {
                if (!parse_strf_video(r, sz)) return false;
                r->video_stream_id = stream_index;
                r->have_video_stream = 1;
            } else if (stream_type == FCC_AUDS) {
                if (!parse_strf_audio(r, sz)) return false;
                r->audio_stream_id = stream_index;
                r->have_audio_stream = 1;
                r->info.has_audio = 1;
            } else {
                if (!skip_bytes(r->fp, (long)sz)) return false;
            }
            saw_strf = 1;
        } else {
            /* JUNK / strn / strd / etc. — skip. */
            if (!skip_bytes(r->fp, (long)sz)) return false;
        }
        /* RIFF chunks are word-aligned; skip pad byte if size is odd. */
        if (sz & 1) {
            if (!skip_bytes(r->fp, 1)) return false;
        }
    }
    (void)saw_strf;
    return true;
}

/* Walk LIST hdrl: avih + per-stream strl + ignored chunks. */
static bool parse_hdrl(AviReader* r, uint32_t list_size)
{
    long end = ftell(r->fp);
    if (end < 0) return false;
    end += (long)list_size - 4;

    unsigned stream_index = 0;

    while (ftell(r->fp) < end) {
        uint8_t hdr[8];
        if (!read_exact(r->fp, hdr, 8)) return false;
        uint32_t fcc = rd_u32(hdr);
        uint32_t sz = rd_u32(hdr + 4);

        if (fcc == FCC_AVIH) {
            if (!parse_avih(r, sz)) return false;
        } else if (fcc == FCC_LIST) {
            uint8_t sub[4];
            if (!read_exact(r->fp, sub, 4)) return false;
            uint32_t sub_fcc = rd_u32(sub);
            if (sub_fcc == FCC_STRL) {
                if (!parse_strl(r, sz, stream_index++)) return false;
            } else {
                if (!skip_bytes(r->fp, (long)(sz - 4))) return false;
            }
        } else {
            if (!skip_bytes(r->fp, (long)sz)) return false;
        }
        if (sz & 1) {
            if (!skip_bytes(r->fp, 1)) return false;
        }
    }
    return true;
}

/* Top-level parser: read RIFF header, walk LIST chunks until we locate
 * "hdrl" and "movi". Stops at the start of "movi" (records offset and
 * end-of-list bound for sequential streaming). */
static bool parse_header(AviReader* r)
{
    uint8_t riff[12];
    if (!read_exact(r->fp, riff, 12)) return false;
    if (rd_u32(riff) != FCC_RIFF) return false;
    /* riff_size = rd_u32(riff + 4); */
    if (rd_u32(riff + 8) != FCC_AVI) return false;

    while (1) {
        uint8_t hdr[8];
        if (!read_exact(r->fp, hdr, 8)) return false;
        uint32_t fcc = rd_u32(hdr);
        uint32_t sz = rd_u32(hdr + 4);

        if (fcc == FCC_LIST) {
            uint8_t sub[4];
            if (!read_exact(r->fp, sub, 4)) return false;
            uint32_t sub_fcc = rd_u32(sub);
            if (sub_fcc == FCC_HDRL) {
                if (!parse_hdrl(r, sz)) return false;
            } else if (sub_fcc == FCC_MOVI) {
                long here = ftell(r->fp);
                if (here < 0) return false;
                r->movi_offset = here;
                r->movi_end = here + (long)sz - 4;
                return true;
            } else {
                if (!skip_bytes(r->fp, (long)(sz - 4))) return false;
            }
        } else {
            /* JUNK and similar — skip. */
            if (!skip_bytes(r->fp, (long)sz)) return false;
        }
        if (sz & 1) {
            if (!skip_bytes(r->fp, 1)) return false;
        }
    }
}

AviReader* avi_reader_open(const char* path)
{
    if (!path) return NULL;
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;

    AviReader* r = (AviReader*)calloc(1, sizeof(AviReader));
    if (!r) {
        fclose(fp);
        return NULL;
    }
    r->fp = fp;
    r->info.frame_duration_us = 33333;   /* fallback ~30fps */

    if (!parse_header(r)) {
        avi_reader_close(r);
        return NULL;
    }
    if (!r->have_video_stream) {
        avi_reader_close(r);
        return NULL;
    }
    return r;
}

void avi_reader_close(AviReader* r)
{
    if (!r) return;
    if (r->fp) fclose(r->fp);
    free(r->buf);
    free(r);
}

bool avi_reader_get_info(const AviReader* r, AviInfo* out)
{
    if (!r || !out) return false;
    *out = r->info;
    return true;
}

bool avi_reader_rewind(AviReader* r)
{
    if (!r) return false;
    if (fseek(r->fp, r->movi_offset, SEEK_SET) != 0) return false;
    r->video_frame_counter = 0;
    return true;
}

/* Decode a chunk fourcc into (stream_index, kind). Returns 1 if the
 * fourcc is a per-stream data chunk we recognise, 0 otherwise.
 *
 * Layout: "NNkk" where NN is two ASCII digits (stream index 0..99) and
 * kk is the two-letter chunk kind ("dc"/"db" = video, "wb" = audio). */
static int classify_chunk(uint32_t fcc, unsigned* out_stream, int* out_kind)
{
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(fcc & 0xFF);
    bytes[1] = (uint8_t)((fcc >> 8) & 0xFF);
    bytes[2] = (uint8_t)((fcc >> 16) & 0xFF);
    bytes[3] = (uint8_t)((fcc >> 24) & 0xFF);
    if (bytes[0] < '0' || bytes[0] > '9') return 0;
    if (bytes[1] < '0' || bytes[1] > '9') return 0;
    unsigned idx = (unsigned)((bytes[0] - '0') * 10 + (bytes[1] - '0'));
    if ((bytes[2] == 'd' && (bytes[3] == 'c' || bytes[3] == 'b'))) {
        *out_stream = idx;
        *out_kind = 1;  /* video */
        return 1;
    }
    if (bytes[2] == 'w' && bytes[3] == 'b') {
        *out_stream = idx;
        *out_kind = 2;  /* audio */
        return 1;
    }
    return 0;
}

bool avi_reader_next_chunk(AviReader* r, AviChunk* out)
{
    if (!r || !out) return false;
    memset(out, 0, sizeof(*out));

    while (1) {
        long here = ftell(r->fp);
        if (here < 0) return false;
        if (here >= r->movi_end) {
            out->type = AVI_CHUNK_EOF;
            return true;
        }

        uint8_t hdr[8];
        if (!read_exact(r->fp, hdr, 8)) {
            out->type = AVI_CHUNK_EOF;
            return true;
        }
        uint32_t fcc = rd_u32(hdr);
        uint32_t sz = rd_u32(hdr + 4);

        /* Skip nested LISTs (some encoders emit rec lists inside movi). */
        if (fcc == FCC_LIST) {
            if (!skip_bytes(r->fp, 4)) return false;
            continue;
        }
        if (fcc == FCC_JUNK) {
            if (!skip_bytes(r->fp, (long)sz)) return false;
            if (sz & 1) { if (!skip_bytes(r->fp, 1)) return false; }
            continue;
        }

        unsigned stream_idx = 0;
        int kind = 0;
        if (!classify_chunk(fcc, &stream_idx, &kind)) {
            /* Unknown chunk in movi — skip and keep going. */
            if (!skip_bytes(r->fp, (long)sz)) return false;
            if (sz & 1) { if (!skip_bytes(r->fp, 1)) return false; }
            continue;
        }

        bool is_video = (kind == 1) && r->have_video_stream
            && stream_idx == r->video_stream_id;
        bool is_audio = (kind == 2) && r->have_audio_stream
            && stream_idx == r->audio_stream_id;

        if (!is_video && !is_audio) {
            if (!skip_bytes(r->fp, (long)sz)) return false;
            if (sz & 1) { if (!skip_bytes(r->fp, 1)) return false; }
            continue;
        }

        if (sz == 0) {
            /* Zero-byte chunks are legal (drop frames / silent audio). */
            out->type = is_video ? AVI_CHUNK_VIDEO : AVI_CHUNK_AUDIO;
            out->data = NULL;
            out->size = 0;
            if (is_video) out->frame_index = r->video_frame_counter++;
            if (sz & 1) { if (!skip_bytes(r->fp, 1)) return false; }
            return true;
        }

        if (!grow_buf(r, sz)) return false;
        if (!read_exact(r->fp, r->buf, sz)) return false;
        if (sz & 1) { if (!skip_bytes(r->fp, 1)) return false; }

        out->type = is_video ? AVI_CHUNK_VIDEO : AVI_CHUNK_AUDIO;
        out->data = r->buf;
        out->size = sz;
        if (is_video) out->frame_index = r->video_frame_counter++;
        return true;
    }
}
