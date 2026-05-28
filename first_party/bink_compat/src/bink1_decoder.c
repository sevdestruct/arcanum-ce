/*
 * bink1_decoder.c -- clean-room Bink1 (.bik) container + video +
 * audio decoder. See header for scope and format notes.
 *
 * Reference layout sources (used for spec only; no third-party code
 * has been copied into this file):
 *   - Multimedia Wiki: https://wiki.multimedia.cx/index.php/Bink_Container
 *   - Multimedia Wiki: https://wiki.multimedia.cx/index.php/Bink_Video
 *   - Multimedia Wiki: https://wiki.multimedia.cx/index.php/Bink_Audio
 *
 * The decoder is intentionally split into self-contained sections:
 *
 *     1. Bitstream reader              -- LE bit reads, byte align
 *     2. Container parser              -- header, audio tracks, index
 *     3. Per-frame demuxer             -- splits a frame into audio
 *                                         packets and a video payload
 *     4. Huffman tree builder          -- shared by audio and video
 *     5. Bink Audio decoder            -- RDFT/DCT, overlap-add, s16
 *     6. Bink Video decoder            -- bundles, block dispatch,
 *                                         DCT, motion compensation
 *     7. YCbCr -> BGRA composer        -- plane to caller buffer
 *     8. Public API
 *
 * The video decoder reconstructs Y/U/V planes for the current frame
 * and converts to BGRA at composition time. Previous-frame planes are
 * retained so MOTION / RESIDUE blocks can reference them.
 */

#include "bink1_decoder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Diagnostic instrumentation                                         */
/* ------------------------------------------------------------------ */
/* All tracing is opt-in via ARCANUM_BINK_TRACE=1 (env var read once
 * at module init). When off the macros compile to no-ops so there's
 * zero overhead in production. */

static int g_bink_trace_enabled = -1;
static int g_bink_trace_frame = 0;

static int bink_trace_check(void)
{
    if (g_bink_trace_enabled < 0) {
        const char* v = getenv("ARCANUM_BINK_TRACE");
        g_bink_trace_enabled = (v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y'))
            ? 1 : 0;
    }
    return g_bink_trace_enabled;
}

#define BINK_TRACE(...) do { if (bink_trace_check()) { \
        fprintf(stderr, "bink1: " __VA_ARGS__); fflush(stderr); \
    } } while (0)

/* Detailed trace: only first N frames to keep log readable. */
#define BINK_TRACE_FRAME_LIMIT 3
#define BINK_TRACE_DETAIL(...) do { \
    if (bink_trace_check() && g_bink_trace_frame < BINK_TRACE_FRAME_LIMIT) { \
        fprintf(stderr, "bink1: " __VA_ARGS__); fflush(stderr); \
    } } while (0)

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define BINK1_MAGIC0  'B'
#define BINK1_MAGIC1  'I'
#define BINK1_MAGIC2  'K'
/* Bink1 versions span 'a' through 'i' across the RAD releases. The
 * stream format is largely consistent across versions; per-version
 * quirks are handled inline where they matter. */

#define BINK1_MAX_AUDIO_TRACKS 256
#define BINK1_MAX_PLANES 3              /* Y, Cb, Cr -- alpha handled separately if needed */

/* Block-type enumeration as it appears in the BLOCK_TYPES bundle. */
typedef enum {
    BINK_BLOCK_SKIP    = 0,
    BINK_BLOCK_SCALED  = 1,
    BINK_BLOCK_MOTION  = 2,
    BINK_BLOCK_RUN     = 3,
    BINK_BLOCK_RESIDUE = 4,
    BINK_BLOCK_INTRA   = 5,
    BINK_BLOCK_FILL    = 6,
    BINK_BLOCK_INTER   = 7,
    BINK_BLOCK_PATTERN = 8,
    BINK_BLOCK_RAW     = 9,
    BINK_BLOCK_COUNT   = 10
} BinkBlockType;

/* Per-frame bundle identifiers. Each bundle is a separate
 * Huffman-coded stream prepended to the block-by-block decode pass. */
typedef enum {
    BINK_SRC_BLOCK_TYPES = 0,
    BINK_SRC_SUB_BLOCK_TYPES,
    BINK_SRC_COLORS,
    BINK_SRC_PATTERN,
    BINK_SRC_X_OFF,
    BINK_SRC_Y_OFF,
    BINK_SRC_INTRA_DC,
    BINK_SRC_INTER_DC,
    BINK_SRC_RUN,
    BINK_SRC_COUNT
} BinkSrcBundle;

/* Zig-zag scan order for 8x8 coefficients (same as JPEG). */
static const uint8_t kBinkZigZag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* Bink's intra-DC and inter-DC magnitude-length bit-counts. */
static const uint8_t kBinkDcLengths[2][16] = {
    /* intra: */ { 4, 4, 4, 4, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 },
    /* inter: */ { 3, 3, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }
};

/* Run-length thresholds used by the COLORS bundle when emitting runs. */
static const uint8_t kBinkRleLengths[16] = {
    2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 32, 48, 64, 128
};

/* ------------------------------------------------------------------ */
/* 1. Bitstream reader                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint8_t* base;
    size_t total_bits;
    size_t pos_bits;
} BitReader;

static void br_init(BitReader* br, const uint8_t* data, size_t size_bytes)
{
    br->base = data;
    br->total_bits = size_bytes * 8;
    br->pos_bits = 0;
}

/* Number of bits still readable. */
static size_t br_remaining(const BitReader* br)
{
    return br->pos_bits < br->total_bits ? br->total_bits - br->pos_bits : 0;
}

/* Read n bits (n <= 32), LE byte order, LSB-first within each byte. */
static uint32_t br_read(BitReader* br, int n)
{
    if (n <= 0 || n > 32) return 0;
    if ((size_t)n > br_remaining(br)) {
        br->pos_bits = br->total_bits;
        return 0;
    }

    uint32_t out = 0;
    int produced = 0;
    while (produced < n) {
        size_t byte_idx = br->pos_bits >> 3;
        int bit_in_byte = (int)(br->pos_bits & 7);
        int take = 8 - bit_in_byte;
        if (take > n - produced) take = n - produced;
        uint32_t bits = (uint32_t)(br->base[byte_idx] >> bit_in_byte)
            & ((1u << take) - 1u);
        out |= bits << produced;
        produced += take;
        br->pos_bits += take;
    }
    return out;
}

/* Peek up to 32 bits without advancing. */
static uint32_t br_peek(BitReader* br, int n)
{
    size_t saved = br->pos_bits;
    uint32_t v = br_read(br, n);
    br->pos_bits = saved;
    return v;
}

static void br_skip(BitReader* br, int n)
{
    br->pos_bits += (size_t)n;
    if (br->pos_bits > br->total_bits) br->pos_bits = br->total_bits;
}

static void br_byte_align(BitReader* br)
{
    size_t r = br->pos_bits & 7;
    if (r) br->pos_bits += 8 - r;
}

/* ------------------------------------------------------------------ */
/* 2. Container parser                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t flags;             /* bit 13: stereo (older), bit 12: DCT mode */
    int      is_dct;
    int      is_stereo;
    uint32_t track_id;
} Bink1AudioTrackInfo;

typedef struct {
    uint64_t offset;            /* file byte offset of frame start */
    uint32_t size;              /* total bytes in this frame */
    int      keyframe;
} Bink1FrameEntry;

/* Internal audio decoder state for one track. */
typedef struct {
    Bink1AudioTrackInfo info;

    /* Band layout for the FFT/DCT coder. Set on first frame. */
    int frame_len;              /* samples per coded frame (power of 2) */
    int overlap_len;            /* half-window samples carried to next frame */
    int band_count;
    int band_bounds[26];        /* up to 25 critical bands + sentinel */

    /* Working buffers: one float frame's worth per channel. */
    float* coeffs[2];           /* coefficient buffer */
    float* prev_tail[2];        /* overlap-add tail from previous frame */
    int    initialized;
} Bink1AudioState;

/* Bink Huffman tree state -- mirrored in the full definition further
 * down the file. Declared here so Bink1Decoder can hold one. */
typedef struct {
    uint8_t  symbols[16];
    uint8_t  code_lengths[16];
    uint32_t code_min[17];
    int16_t  code_offset[17];
    uint8_t  permutation[16];
    int      built;
} BinkHuffTreeData;

/* Bundle decode state (fully defined below). Forward-declare the
 * minimal shape needed by the decoder struct. */
typedef struct {
    BinkHuffTreeData tree;
    uint8_t* entries;
    size_t   capacity;
    size_t   write_pos;
    size_t   read_pos;
    int      remaining;          /* run quota left for this plane */
    int      runsize;            /* bit-width of chunk-length prefix */
    int16_t* values;
    /* For BLOCK_TYPES / SUB_BLOCK_TYPES: last emitted symbol so the
     * 4-bit RLE expansion (codes 12..15 -> repeat last) can fire. */
    int      last_value;
} BundleData;

struct Bink1Decoder {
    FILE* fp;
    Bink1Info info;

    /* Audio tracks. We decode the first track for BINKSND; secondary
     * tracks are demuxed but discarded. */
    Bink1AudioTrackInfo* tracks;
    int track_count;

    /* Per-frame index table. */
    Bink1FrameEntry* frames;
    uint32_t frame_count;
    uint32_t current_frame;
    int      at_eof;

    /* Reusable I/O buffer for the current frame's raw bytes. */
    uint8_t* frame_buf;
    size_t   frame_buf_capacity;
    size_t   frame_buf_size;     /* bytes valid in frame_buf */

    /* The current frame's video bitstream slice (points into frame_buf). */
    const uint8_t* video_data;
    size_t         video_size;

    /* The current frame's audio packet slices (one per track). */
    struct {
        const uint8_t* data;
        size_t size;
    } audio_packets[BINK1_MAX_AUDIO_TRACKS];

    /* Decoded plane buffers: planes[0..2][0..1] = current and previous
     * frame Y/Cb/Cr planes. Each plane is stored row-major with stride
     * == padded width (rounded up to 8). */
    uint8_t* planes[BINK1_MAX_PLANES][2];
    int      plane_widths[BINK1_MAX_PLANES];
    int      plane_heights[BINK1_MAX_PLANES];
    int      plane_strides[BINK1_MAX_PLANES];
    int      cur_plane_set;     /* 0 or 1: which set holds the current frame */


    /* Decoded BGRA output for the most recent decode_video call. */
    int      have_decoded_frame;

    /* Audio decoder state (one entry per track up to track_count). */
    Bink1AudioState* audio_state;

    /* Worst-case decoded audio bytes per frame (16-bit stereo). */
    size_t   audio_bytes_per_frame;

    /* Decoded audio scratch for decode_audio_chunk. */
    int16_t* audio_out_scratch;
    size_t   audio_out_scratch_capacity;
    size_t   audio_out_scratch_bytes;
};

static uint16_t rd_u16le(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd_u32le(const uint8_t* p)
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

static bool parse_header(Bink1Decoder* d)
{
    uint8_t hdr[44];
    if (!read_exact(d->fp, hdr, sizeof(hdr))) return false;
    if (hdr[0] != BINK1_MAGIC0 || hdr[1] != BINK1_MAGIC1
        || hdr[2] != BINK1_MAGIC2) {
        return false;
    }
    /* Reject Bink2 (BIKf 'KB2*' etc. — Bink2 has its own header). */
    if (hdr[3] < 'a' || hdr[3] > 'i') return false;

    d->info.video_version = hdr[3];

    /* hdr[4..7]   = file size - 8 (informational; we don't trust it)
     * hdr[8..11]  = num frames
     * hdr[12..15] = max frame size
     * hdr[16..19] = duplicate of max-frame-size
     * hdr[20..23] = width
     * hdr[24..27] = height
     * hdr[28..31] = fps_num
     * hdr[32..35] = fps_den
     * hdr[36..39] = video flags
     * hdr[40..43] = num audio tracks */
    d->info.frame_count = rd_u32le(hdr + 8);
    d->info.width  = rd_u32le(hdr + 20);
    d->info.height = rd_u32le(hdr + 24);

    uint32_t fps_num = rd_u32le(hdr + 28);
    uint32_t fps_den = rd_u32le(hdr + 32);
    if (fps_num == 0) fps_num = 30;
    if (fps_den == 0) fps_den = 1;
    /* frame_duration_us = (fps_den / fps_num) * 1e6 with rounding. */
    d->info.frame_duration_us = (uint32_t)(((uint64_t)fps_den * 1000000u
        + fps_num / 2) / fps_num);

    uint32_t vflags = rd_u32le(hdr + 36);
    d->info.has_alpha = (vflags & 0x00100000) != 0;
    d->info.has_grayscale = (vflags & 0x00020000) != 0;

    uint32_t track_count = rd_u32le(hdr + 40);
    if (track_count > BINK1_MAX_AUDIO_TRACKS) return false;
    d->info.audio_track_count = (uint8_t)track_count;
    d->track_count = (int)track_count;
    if (track_count > 0) {
        d->tracks = (Bink1AudioTrackInfo*)calloc(track_count, sizeof(*d->tracks));
        if (!d->tracks) return false;
    }
    return true;
}

static bool parse_audio_tracks(Bink1Decoder* d)
{
    if (d->track_count == 0) return true;

    /* Each track's metadata is split across three small tables in the
     * header: (a) track_size[4] per track, (b) sample_rate[2] +
     * channels[2] + flags[2] per track, (c) track_id[4] per track. */

    /* (a) Per-track packet size hints (unused -- per-frame sizes are
     * encoded in the audio packets themselves). Skip these 4-byte
     * entries. */
    for (int i = 0; i < d->track_count; ++i) {
        uint8_t buf[4];
        if (!read_exact(d->fp, buf, 4)) return false;
    }

    /* (b) Per-track audio_format, 8 bytes each. Layout:
     *   2 bytes: sample_rate (little-endian, e.g. 0xAC44 = 44100)
     *   2 bytes: flags
     *       bit 12 (0x1000): DCT codec mode (Bink Audio newer)
     *       bit 13 (0x2000): stereo
     *       bit 14, 15: other format bits, not parsed here
     *   4 bytes: reserved / zero
     * Channel count is derived from the stereo flag; Bink1 has no
     * support for >2 channels per track. */
    for (int i = 0; i < d->track_count; ++i) {
        uint8_t buf[8];
        if (!read_exact(d->fp, buf, 8)) return false;
        d->tracks[i].sample_rate = rd_u16le(buf + 0);
        d->tracks[i].flags = rd_u16le(buf + 2);
        d->tracks[i].is_stereo = (d->tracks[i].flags & (1 << 13)) != 0;
        d->tracks[i].is_dct    = (d->tracks[i].flags & (1 << 12)) != 0;
        d->tracks[i].channels  = d->tracks[i].is_stereo ? 2 : 1;
    }

    /* (c) Track IDs. */
    for (int i = 0; i < d->track_count; ++i) {
        uint8_t buf[4];
        if (!read_exact(d->fp, buf, 4)) return false;
        d->tracks[i].track_id = rd_u32le(buf);
    }

    /* Publish first track's basics. */
    d->info.audio_sample_rate = (int32_t)d->tracks[0].sample_rate;
    d->info.audio_channels = d->tracks[0].channels;
    d->info.audio_is_dct = d->tracks[0].is_dct;
    return true;
}

static bool parse_frame_index(Bink1Decoder* d)
{
    if (d->info.frame_count == 0) return false;
    size_t entries = (size_t)d->info.frame_count + 1;
    d->frames = (Bink1FrameEntry*)calloc(entries, sizeof(*d->frames));
    if (!d->frames) return false;

    uint32_t* raw = (uint32_t*)malloc(entries * sizeof(uint32_t));
    if (!raw) return false;
    if (!read_exact(d->fp, raw, entries * sizeof(uint32_t))) {
        free(raw);
        return false;
    }
    for (size_t i = 0; i < entries; ++i) {
        uint32_t v = raw[i];
        /* The low bit marks keyframes. The actual byte offset has that
         * bit masked off. */
        int kf = (v & 1) != 0;
        uint64_t offs = (uint64_t)(v & ~1u);
        d->frames[i].offset = offs;
        d->frames[i].keyframe = kf;
    }
    free(raw);

    /* Compute per-frame sizes from offset deltas. The sentinel entry
     * at index frame_count holds the end-of-frames offset. */
    for (uint32_t i = 0; i < d->info.frame_count; ++i) {
        uint64_t next = d->frames[i + 1].offset & ~1ull;
        uint64_t cur  = d->frames[i].offset & ~1ull;
        d->frames[i].size = (uint32_t)(next - cur);
    }
    d->frame_count = d->info.frame_count;

    BINK_TRACE("frame index: %u frames, first 4 offsets/sizes/kf: "
        "(%llu,%u,%d) (%llu,%u,%d) (%llu,%u,%d) (%llu,%u,%d)\n",
        d->frame_count,
        (unsigned long long)d->frames[0].offset, d->frames[0].size, d->frames[0].keyframe,
        (unsigned long long)d->frames[1].offset, d->frames[1].size, d->frames[1].keyframe,
        (unsigned long long)d->frames[2].offset, d->frames[2].size, d->frames[2].keyframe,
        (unsigned long long)d->frames[3].offset, d->frames[3].size, d->frames[3].keyframe);
    return true;
}

static bool load_frame(Bink1Decoder* d, uint32_t index)
{
    if (index >= d->frame_count) return false;
    Bink1FrameEntry* fe = &d->frames[index];
    if (fseeko(d->fp, (off_t)fe->offset, SEEK_SET) != 0) return false;

    if (fe->size > d->frame_buf_capacity) {
        size_t cap = d->frame_buf_capacity ? d->frame_buf_capacity : 65536;
        while (cap < fe->size) cap *= 2;
        uint8_t* nb = (uint8_t*)realloc(d->frame_buf, cap);
        if (!nb) return false;
        d->frame_buf = nb;
        d->frame_buf_capacity = cap;
    }

    if (!read_exact(d->fp, d->frame_buf, fe->size)) return false;
    d->frame_buf_size = fe->size;

    /* Demux the frame. Per the Bink container spec, the per-track
     * audio packet is structured as:
     *   4 bytes: stored_length = (audio_data_bytes + 4); 0 = silence
     *   4 bytes: sample_count (only present if stored_length != 0)
     *   N bytes: audio bitstream (N = stored_length - 4)
     * After all audio tracks, the remainder is the video bitstream. */
    size_t pos = 0;
    for (int t = 0; t < d->track_count; ++t) {
        if (pos + 4 > d->frame_buf_size) return false;
        uint32_t stored_len = rd_u32le(d->frame_buf + pos);
        pos += 4;
        if (stored_len == 0) {
            /* Silence packet -- no sample_count word follows. */
            d->audio_packets[t].data = NULL;
            d->audio_packets[t].size = 0;
            continue;
        }
        /* stored_len includes the 4-byte sample_count word + the
         * raw audio bytes. Skip the sample count and point the
         * packet at the audio data proper. */
        if (pos + stored_len > d->frame_buf_size) return false;
        if (stored_len < 4) return false;
        /* d->frame_buf[pos..pos+3] is the sample_count -- skip. */
        d->audio_packets[t].data = d->frame_buf + pos + 4;
        d->audio_packets[t].size = stored_len - 4;
        pos += stored_len;
    }
    d->video_data = d->frame_buf + pos;
    d->video_size = d->frame_buf_size - pos;
    return true;
}

/* ------------------------------------------------------------------ */
/* 4. Huffman tree builder (shared by audio + video)                  */
/* ------------------------------------------------------------------ */

/* Bink uses a small fixed Huffman alphabet (16 symbols) plus a per-
 * bundle symbol permutation. The 16 prebuilt code-length tables are
 * documented on the Multimedia Wiki -- here they are stored as code
 * lengths in bits, indexed by symbol. Codes are LSB-first (matches
 * our BitReader). */
static const uint8_t kBinkHuffLengths[16][16] = {
    /*  0: 4-bit fixed -- identity tree (used when length 0)
     *     Encoded as 16 codes of length 4 in the prebuilt table. */
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
    {1, 3, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6},
    {1, 2, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6},
    {2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6},
    {2, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6},
    {1, 2, 4, 4, 4, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {1, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6},
    {3, 3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 6},
    {2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6},
    {2, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6},
    {3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6},
    {2, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 6, 6, 6, 6},
    {1, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6},
    {2, 2, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6},
    {2, 3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6},
    {1, 3, 4, 4, 4, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6},
};

/* Alias for the forward-declared BinkHuffTreeData up top so the
 * implementation code reads naturally as "BinkHuffTree". */
typedef BinkHuffTreeData BinkHuffTree;

/* Build the canonical Huffman lookup tables from the per-symbol code
 * lengths in `lengths`. Symbols are placed in code-order so a given
 * (length, code) pair maps to symbols[code_offset[length]+(code-min)]. */
static bool bink_huff_build(BinkHuffTree* t, const uint8_t* lengths)
{
    /* Count codes of each length. */
    int count[17] = { 0 };
    for (int i = 0; i < 16; ++i) {
        if (lengths[i] < 1 || lengths[i] > 16) return false;
        count[lengths[i]]++;
    }

    /* First canonical code value at each length. */
    uint32_t code = 0;
    int symbol_idx = 0;
    for (int len = 1; len <= 16; ++len) {
        t->code_min[len] = code;
        t->code_offset[len] = (int16_t)(symbol_idx - (int)code);
        code = (code + count[len]) << 1;
    }

    /* Place symbols in canonical code order. */
    int placed[17] = { 0 };
    for (int sym = 0; sym < 16; ++sym) {
        int len = lengths[sym];
        int slot = 0;
        for (int l = 1; l < len; ++l) slot += count[l];
        slot += placed[len]++;
        t->symbols[slot] = (uint8_t)sym;
        t->code_lengths[slot] = (uint8_t)len;
    }
    return true;
}

/* Read a Huffman-coded symbol from the bitstream. Returns the index
 * into the tree's symbols[] array; callers then map through the
 * permutation if they care about the user-visible value. */
static int bink_huff_read_raw(BitReader* br, const BinkHuffTree* t)
{
    uint32_t acc = 0;
    for (int len = 1; len <= 16; ++len) {
        acc = (acc << 1) | br_read(br, 1);
        if (acc < t->code_min[len] + 0u) continue;
        /* Check if this code falls within length `len`'s range. */
        int next_min = (len < 16) ? (int)t->code_min[len + 1] >> 1 : -1;
        if (next_min < 0 || acc < (uint32_t)next_min) {
            int slot = (int)acc + t->code_offset[len];
            if (slot < 0 || slot >= 16) return 0;
            return (int)t->symbols[slot];
        }
    }
    return 0;
}

/* Read a permutation-mapped symbol value (0..15). */
static int bink_huff_read_sym(BitReader* br, const BinkHuffTree* t)
{
    int raw = bink_huff_read_raw(br, t);
    return t->permutation[raw];
}

/* Bink Huffman tree permutation merge step (recursive). Given a
 * working array of size 2^depth, repeatedly merge adjacent runs by
 * interleaving them via swap pairs read from the stream until the
 * tree's full 16-symbol permutation is reconstructed. This matches
 * the "shuffle" branch of the Bink tree specifier. */
static void bink_huff_merge_perm(BitReader* br, uint8_t* a, int run_len)
{
    if (run_len >= 16) return;
    uint8_t merged[16];
    for (int i = 0; i < 16; i += run_len * 2) {
        int p = 0;
        int q = run_len;
        while (p < run_len && q < run_len * 2) {
            /* 1-bit: 0 = take from left run, 1 = take from right. */
            if (br_read(br, 1)) {
                merged[i + p + q - run_len] = a[i + q];
                ++q;
            } else {
                merged[i + p + q - run_len] = a[i + p];
                ++p;
            }
        }
        while (p < run_len) {
            merged[i + p + q - run_len] = a[i + p];
            ++p;
        }
        while (q < run_len * 2) {
            merged[i + p + q - run_len] = a[i + q];
            ++q;
        }
    }
    memcpy(a, merged, 16);
    bink_huff_merge_perm(br, a, run_len * 2);
}

/* Decode a per-bundle Huffman tree specifier from the bitstream:
 *   - 4 bits: tree index (0..15) selecting a prebuilt code-length
 *     table from kBinkHuffLengths.
 *   - If index == 0: identity permutation (raw 4-bit nibbles).
 *   - Otherwise: 1-bit mode selector:
 *       1 = "explicit": read 3 bits for count, then `count` 4-bit
 *           symbols listing which value goes where, remaining
 *           symbols fall through in natural order.
 *       0 = "shuffle": read 2 bits for depth, swap adjacent pairs
 *           at that depth, then merge() the pair runs upward.
 * Returns true on success. */
static bool bink_huff_read_tree(BitReader* br, BinkHuffTree* t)
{
    int idx = (int)br_read(br, 4);
    if (idx >= 16) return false;
    if (!bink_huff_build(t, kBinkHuffLengths[idx])) return false;
    if (idx == 0) {
        for (int i = 0; i < 16; ++i) t->permutation[i] = (uint8_t)i;
        t->built = 1;
        return true;
    }

    int mode = (int)br_read(br, 1);
    if (mode == 1) {
        /* Explicit selection. */
        int count = (int)br_read(br, 3);
        if (count > 16) count = 16;
        uint8_t taken[16] = { 0 };
        uint8_t perm[16];
        memset(perm, 0xFF, sizeof(perm));
        for (int i = 0; i < count; ++i) {
            int v = (int)br_read(br, 4);
            if (v >= 16) v = 15;
            perm[i] = (uint8_t)v;
            taken[v] = 1;
        }
        /* Fill remaining slots in natural order with unused values. */
        int slot = count;
        for (int v = 0; v < 16 && slot < 16; ++v) {
            if (!taken[v]) perm[slot++] = (uint8_t)v;
        }
        memcpy(t->permutation, perm, 16);
    } else {
        /* Shuffle mode. Initial layout is identity. Pair-swap at
         * the specified depth, then merge upward. */
        int depth = (int)br_read(br, 2);
        uint8_t perm[16];
        for (int i = 0; i < 16; ++i) perm[i] = (uint8_t)i;
        /* Apply per-pair swap bits at the base depth. */
        int base_run = 1 << depth;
        for (int i = 0; i < 16; i += base_run * 2) {
            if (br_read(br, 1)) {
                uint8_t tmp[8];
                memcpy(tmp, perm + i, base_run);
                memcpy(perm + i, perm + i + base_run, base_run);
                memcpy(perm + i + base_run, tmp, base_run);
            }
        }
        bink_huff_merge_perm(br, perm, base_run * 2);
        memcpy(t->permutation, perm, 16);
    }
    t->built = 1;
    return true;
}

/* ------------------------------------------------------------------ */
/* 5. Bink Audio decoder                                              */
/* ------------------------------------------------------------------ */

/* Critical-band edges (Hz) shared by all Bink Audio sample rates.
 * Each frame's coefficient buffer is partitioned at the band positions
 * corresponding to the sample rate -- the decoder builds band_bounds
 * lazily on first frame using these reference edges. */
static const float kBinkAudioBandHz[25] = {
        0.0f,   100.0f,  200.0f,  300.0f,  400.0f,
      510.0f,   630.0f,  770.0f,  920.0f, 1080.0f,
     1270.0f,  1480.0f, 1720.0f, 2000.0f, 2320.0f,
     2700.0f,  3150.0f, 3700.0f, 4400.0f, 5300.0f,
     6400.0f,  7700.0f, 9500.0f, 12000.0f, 15500.0f
};

static int audio_frame_len_for_rate(uint32_t rate)
{
    /* Bink picks the FFT size from sample rate, rounded to a power
     * of two. For 44100 the frame is 2048 samples (overlap 1024). */
    if (rate < 22050) return 1024;
    if (rate < 44100) return 1024;
    return 2048;
}

static bool audio_init_track(Bink1AudioState* a, const Bink1AudioTrackInfo* info)
{
    a->info = *info;
    int n = audio_frame_len_for_rate(info->sample_rate);
    a->frame_len = n;
    a->overlap_len = n / 2;

    /* Compute band layout from the reference edges. */
    a->band_count = 0;
    float hz_per_bin = (float)info->sample_rate / (float)n;
    for (int i = 0; i < 25 && a->band_count < 25; ++i) {
        int bin = (int)(kBinkAudioBandHz[i] / hz_per_bin + 0.5f);
        if (bin > n / 2) bin = n / 2;
        a->band_bounds[a->band_count++] = bin;
    }
    a->band_bounds[a->band_count] = n / 2;
    if (a->band_bounds[a->band_count] != a->band_bounds[a->band_count - 1]) {
        ++a->band_count;
    }

    int channels = info->is_stereo ? 2 : 1;
    for (int c = 0; c < channels; ++c) {
        a->coeffs[c] = (float*)calloc((size_t)n, sizeof(float));
        a->prev_tail[c] = (float*)calloc((size_t)a->overlap_len, sizeof(float));
        if (!a->coeffs[c] || !a->prev_tail[c]) return false;
    }
    a->initialized = 1;
    return true;
}

/* In-place iterative radix-2 decimation-in-time FFT. Input is N
 * complex samples laid out as { re[0], im[0], re[1], im[1], ... }.
 * `inverse` selects the direction (1 = inverse, 0 = forward). N
 * must be a power of two. */
static void audio_fft_complex(float* data, int n, int inverse)
{
    /* Bit-reversal permutation. */
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = data[2*i];     data[2*i]     = data[2*j];     data[2*j]     = tr;
            float ti = data[2*i + 1]; data[2*i + 1] = data[2*j + 1]; data[2*j + 1] = ti;
        }
    }
    /* Butterflies. */
    const double PI = 3.14159265358979323846;
    for (int len = 2; len <= n; len <<= 1) {
        double ang = (inverse ? 1.0 : -1.0) * 2.0 * PI / (double)len;
        float wlen_re = (float)cos(ang);
        float wlen_im = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float w_re = 1.0f, w_im = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                int a = 2 * (i + k);
                int b = 2 * (i + k + len / 2);
                float u_re = data[a], u_im = data[a + 1];
                float v_re = data[b] * w_re - data[b + 1] * w_im;
                float v_im = data[b] * w_im + data[b + 1] * w_re;
                data[a]     = u_re + v_re;
                data[a + 1] = u_im + v_im;
                data[b]     = u_re - v_re;
                data[b + 1] = u_im - v_im;
                float nw_re = w_re * wlen_re - w_im * wlen_im;
                float nw_im = w_re * wlen_im + w_im * wlen_re;
                w_re = nw_re; w_im = nw_im;
            }
        }
    }
    if (inverse) {
        float inv_n = 1.0f / (float)n;
        for (int i = 0; i < 2 * n; ++i) data[i] *= inv_n;
    }
}

/* Inverse Real DFT (length n, n even). The Bink coefficient layout
 * stores the spectrum as n real values where:
 *   coeffs[0] = DC bin (real)
 *   coeffs[1] = Nyquist bin (real) -- libavcodec packs it here
 *   coeffs[2k], coeffs[2k+1] for k = 1..n/2-1 form (real, imag)
 *     pairs of the kth complex bin
 * After the inverse transform, coeffs[] holds n real time-domain
 * samples in-place. */
static void audio_inverse_rdft(float* coeffs, int n)
{
    if (n <= 0 || (n & (n - 1)) != 0) return;  /* require power of 2 */
    /* Expand the packed real spectrum into a length-n complex
     * spectrum suitable for a complex inverse FFT. The complex
     * layout enforces Hermitian symmetry (X[N-k] = conj(X[k])) so
     * the output is purely real. */
    float* tmp = (float*)calloc((size_t)n * 2, sizeof(float));
    if (!tmp) return;
    tmp[0] = coeffs[0];                         /* DC */
    tmp[1] = 0.0f;
    tmp[2 * (n / 2)] = coeffs[1];               /* Nyquist (real) */
    tmp[2 * (n / 2) + 1] = 0.0f;
    for (int k = 1; k < n / 2; ++k) {
        float re = coeffs[2 * k];
        float im = coeffs[2 * k + 1];
        tmp[2 * k]     = re;
        tmp[2 * k + 1] = im;
        tmp[2 * (n - k)]     = re;
        tmp[2 * (n - k) + 1] = -im;
    }
    audio_fft_complex(tmp, n, 1);
    /* Take real parts. */
    for (int i = 0; i < n; ++i) coeffs[i] = tmp[2 * i];
    free(tmp);
}

/* Inverse DCT-IV of length n. The DCT-IV is its own inverse up to
 * a scale, and Bink Audio v2 uses it instead of RDFT for newer
 * streams. We implement it via a DCT-IV -> DCT-IV self-inverse:
 *   y[k] = sum_{n=0..N-1} x[n] * cos((pi/N) * (n + 1/2) * (k + 1/2))
 * Naive O(N^2) implementation; audio frames are <= 8192 samples so
 * the cost is acceptable for the rare DCT-mode tracks. */
static void audio_inverse_dct_iv(float* coeffs, int n)
{
    if (n <= 0) return;
    const double PI = 3.14159265358979323846;
    float* tmp = (float*)calloc((size_t)n, sizeof(float));
    if (!tmp) return;
    for (int k = 0; k < n; ++k) {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            sum += coeffs[i] * cos(PI / (double)n
                * ((double)i + 0.5) * ((double)k + 0.5));
        }
        tmp[k] = (float)(sum * 2.0 / (double)n);
    }
    memcpy(coeffs, tmp, (size_t)n * sizeof(float));
    free(tmp);
}

/* Read a 29-bit "Bink float" from the bitstream: 5-bit exponent +
 * 23-bit mantissa + 1-bit sign (read in that order, per the wiki).
 * Used for the two leading audio coefficients per channel per
 * frame. */
static float audio_read_float(BitReader* br)
{
    int power = (int)br_read(br, 5);
    uint32_t mant = br_read(br, 23);
    int sign = (int)br_read(br, 1);
    /* Reconstruct as mantissa * 2^(power - 23) with sign. */
    double v = (double)mant * ldexp(1.0, power - 23);
    if (sign) v = -v;
    return (float)v;
}

/* Bink Audio variable-length coefficient length table (16 entries),
 * straight from the wiki. The selector chooses how many bits the
 * next batch of coefficient values uses. */
static const int kBinkAudioRleLengths[16] = {
    2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64
};

/* Decode one audio frame. Reads coefficients per the documented
 * Bink Audio (v1) layout:
 *   - 2 floats per channel (leading DC-ish samples)
 *   - per critical band: 8-bit quantization step
 *   - per coefficient (band-by-band): variable-length signed magnitude
 *   - inverse RDFT / DCT-IV (selected by track flag bit 12)
 *   - linear-ramp overlap-add against the previous frame's half-window
 *
 * Without the inverse transform implementation the decoded samples
 * are wrong, but the bitstream cursor advances correctly so other
 * codepaths stay in sync. */
static int audio_decode_frame(Bink1AudioState* a, BitReader* br,
    int16_t* out, int out_capacity)
{
    if (!a->initialized) return 0;
    int n = a->frame_len;
    int half = a->overlap_len;
    int channels = a->info.is_stereo ? 2 : 1;

    if (out_capacity < half * channels) return 0;

    for (int c = 0; c < channels; ++c) {
        memset(a->coeffs[c], 0, (size_t)n * sizeof(float));
    }

    /* Per-channel coefficient decode. */
    for (int c = 0; c < channels; ++c) {
        if (br_remaining(br) < 64) break;

        /* Two leading 29-bit floats. */
        a->coeffs[c][0] = audio_read_float(br);
        a->coeffs[c][1] = audio_read_float(br);

        /* Per critical band: 8-bit log-scale quantizer index. */
        float quantizers[26];
        for (int band = 0; band < a->band_count && band < 26; ++band) {
            int qi = (int)br_read(br, 8);
            if (qi > 95) qi = 95;
            /* quant = 10 ^ (qi * 0.0664) per spec. */
            quantizers[band] = (float)pow(10.0, qi * 0.0664);
        }

        /* Per-coefficient decode, band-by-band. Each band processes
         * its range of FFT bins with a shared variable-length scheme:
         *   width_idx = read 4 bits
         *   width_bits = kBinkAudioRleLengths[width_idx]
         *   for each bin in this batch:
         *     magnitude = read width_bits bits
         *     if magnitude: sign = read 1 bit
         *     value = (sign ? -magnitude : magnitude) * quant[band]
         *
         * Batch sizes are not fully documented; the libavcodec
         * implementation re-reads width_idx at band boundaries and
         * after every batch fills. We approximate by reading one
         * value per bin -- correctness depends on the exact wiki-
         * unspecified batch boundary rule. */
        int bin = 2;  /* first two bins were the leading floats */
        for (int band = 0; band < a->band_count && bin < n / 2; ++band) {
            int band_end = a->band_bounds[band + 1];
            if (band_end > n / 2) band_end = n / 2;
            float q = quantizers[band];
            while (bin < band_end) {
                if (br_remaining(br) < 5) goto channel_done;
                int width_idx = (int)br_read(br, 4);
                int width_bits = kBinkAudioRleLengths[width_idx];
                int batch_count = 8;  /* spec-unspecified; libav uses small batches */
                if (bin + batch_count > band_end) batch_count = band_end - bin;
                for (int i = 0; i < batch_count; ++i) {
                    if (br_remaining(br) < width_bits + 1) goto channel_done;
                    uint32_t mag = br_read(br, width_bits);
                    float v = (float)mag * q;
                    if (mag != 0 && br_read(br, 1)) v = -v;
                    a->coeffs[c][bin++] = v;
                }
            }
        }
        channel_done: ;
    }

    /* Inverse transform: RDFT for older streams, DCT-IV for newer
     * streams (selected by audio_flags bit 12). Both convert the
     * decoded coefficient buffer from frequency domain to time
     * domain in place. The RDFT path uses a standard radix-2 DIT
     * iterative FFT with conjugate twiddles. */
    int is_dct = a->info.is_dct;
    for (int c = 0; c < channels; ++c) {
        if (!is_dct) {
            audio_inverse_rdft(a->coeffs[c], n);
        } else {
            audio_inverse_dct_iv(a->coeffs[c], n);
        }
    }

    for (int c = 0; c < channels; ++c) {
        float* prev = a->prev_tail[c];
        float* cur  = a->coeffs[c];
        for (int i = 0; i < half; ++i) {
            /* Linear-ramp overlap-add per spec. */
            float ramp_new = (float)i / (float)half;
            float ramp_old = 1.0f - ramp_new;
            float v = prev[i] * ramp_old + cur[i] * ramp_new;
            int s = (int)v;
            if (s < -32768) s = -32768;
            if (s > 32767) s = 32767;
            out[i * channels + c] = (int16_t)s;
        }
        /* Save current frame's tail for next overlap-add. */
        memcpy(prev, cur + half, (size_t)half * sizeof(float));
    }
    return half;
}

/* ------------------------------------------------------------------ */
/* 6. Bink Video decoder                                              */
/* ------------------------------------------------------------------ */

/* Each plane is decoded in 8x8 blocks, scanned row-major. The bundle
 * data for each plane is read up-front for that plane's pass, then
 * block-by-block decoding consumes bundle entries as needed. */

/* Bundle: alias for the forward-declared BundleData up top. The
 * full doc comment lives next to the typedef above. */
typedef BundleData Bundle;

typedef struct {
    /* Per-bundle state. */
    Bundle bundles[BINK_SRC_COUNT];

    /* Plane geometry. */
    int width, height;          /* pixel dims (may not be multiple of 8) */
    int width_blocks;           /* ceil(width / 8) */
    int height_blocks;          /* ceil(height / 8) */
    int stride;                 /* row stride of the plane in bytes */

    /* Output plane buffer (current frame). Owned by the decoder. */
    uint8_t* dst;

    /* Previous frame's plane for MOTION / RESIDUE reference. */
    uint8_t* prev;
} BinkPlane;

/* Initialise a bundle's buffers. Called once per (plane, bundle) on
 * the first frame; subsequent frames just reset the read/write
 * cursors. */
static bool bundle_alloc(Bundle* b, int width_blocks, int height_blocks, int has_values)
{
    size_t blocks = (size_t)width_blocks * (size_t)height_blocks;
    /* A bundle entry can occupy up to 64 elements per block in the
     * worst case (RAW), so size to that bound. */
    b->capacity = blocks * 64;
    b->entries = (uint8_t*)calloc(b->capacity, 1);
    if (!b->entries) return false;
    if (has_values) {
        b->values = (int16_t*)calloc(b->capacity, sizeof(int16_t));
        if (!b->values) return false;
    }
    b->write_pos = 0;
    b->read_pos = 0;
    return true;
}

static void bundle_reset_cursors(Bundle* b)
{
    b->write_pos = 0;
    b->read_pos = 0;
}

static void bundle_free(Bundle* b)
{
    free(b->entries);
    free(b->values);
    b->entries = NULL;
    b->values = NULL;
    b->capacity = 0;
    b->write_pos = b->read_pos = 0;
}

/* Decode one 8x8 block. The block-type for this block was already
 * extracted from BINK_SRC_BLOCK_TYPES. */
static void decode_block_skip(BinkPlane* plane, int bx, int by)
{
    /* Copy 8x8 from previous frame at the same position. */
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    const uint8_t* src = plane->prev + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        memcpy(dst, src, 8);
        dst += plane->stride;
        src += plane->stride;
    }
}

static void decode_block_fill(BinkPlane* plane, int bx, int by, uint8_t value)
{
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        memset(dst, value, 8);
        dst += plane->stride;
    }
}

/* --- Bink 8x8 integer IDCT ---------------------------------------- *
 *
 * Bink uses its own scaled-integer transform (NOT JPEG/MJPEG DCT).
 * The forward transform is documented on the Multimedia Wiki; the
 * inverse below is the matched reconstruction. Implementation is the
 * butterfly variant: 1-D IDCT on each row, then on each column. The
 * scaling factor is folded into the dequant tables the bundle layer
 * provides, so this function operates on already-dequantised
 * coefficients and writes residue values that the block decoders
 * then add to motion-compensated or all-zero predictions.
 *
 * Coefficient memory layout: 64 ints in natural (row-major) order.
 */

static void bink_idct_row(int* p)
{
    int a0 = p[0] + p[4];
    int a1 = p[0] - p[4];
    int a2 = p[2] + p[6];
    int a3 = (((p[2] - p[6]) * 181 + 0x40) >> 7);

    int b0 = a0 + a2;
    int b1 = a1 + a3;
    int b2 = a1 - a3;
    int b3 = a0 - a2;

    int t0 = p[1] + p[7];
    int t1 = p[5] + p[3];
    int t2 = p[1] - p[7];
    int t3 = p[5] - p[3];

    int c0 = t0 + t1;
    int c1 = (((t0 - t1) * 181 + 0x40) >> 7);
    int c2 = (((t2 - t3) * 181 + 0x40) >> 7);
    int c3 = t2 + t3;

    p[0] = b0 + c0;
    p[1] = b1 + c1;
    p[2] = b2 + c2;
    p[3] = b3 + c3;
    p[4] = b3 - c3;
    p[5] = b2 - c2;
    p[6] = b1 - c1;
    p[7] = b0 - c0;
}

static void bink_idct_block(int* block)
{
    /* Rows. */
    for (int r = 0; r < 8; ++r) bink_idct_row(block + r * 8);
    /* Columns (in-place via scratch). */
    int col[8];
    for (int c = 0; c < 8; ++c) {
        for (int r = 0; r < 8; ++r) col[r] = block[r * 8 + c];
        bink_idct_row(col);
        for (int r = 0; r < 8; ++r) {
            int v = (col[r] + 0x20) >> 6;
            block[r * 8 + c] = v;
        }
    }
}

static uint8_t clip_byte(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* --- Block decoders ---------------------------------------------- *
 *
 * Each block decoder writes 8x8 pixels to plane->dst at (bx*8, by*8).
 * Decoders that need previous-frame reference read from plane->prev
 * at the corresponding position (plus motion offsets where
 * applicable).
 *
 * Bundle data for these is consumed in advance by the plane decoder
 * and passed in here as pre-extracted parameters; this keeps the
 * block decoders free of per-block bitstream interactions.
 */

static void decode_block_motion(BinkPlane* plane, int bx, int by,
    int mx, int my)
{
    /* Integer-pixel motion compensation: copy 8x8 from (bx*8+mx,
     * by*8+my) in the previous frame. Half-pel filtering will need
     * a follow-up pass; for now we round to nearest integer pixel. */
    int sx = bx * 8 + mx;
    int sy = by * 8 + my;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    /* Clamp the source so we don't read off the end of the plane. */
    int max_x = plane->width - 8;
    int max_y = plane->height - 8;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (sx > max_x) sx = max_x;
    if (sy > max_y) sy = max_y;

    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    const uint8_t* src = plane->prev + sy * plane->stride + sx;
    for (int r = 0; r < 8; ++r) {
        memcpy(dst, src, 8);
        dst += plane->stride;
        src += plane->stride;
    }
}

static void decode_block_raw(BinkPlane* plane, int bx, int by,
    const uint8_t* bytes)
{
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        memcpy(dst, bytes + r * 8, 8);
        dst += plane->stride;
    }
}

static void decode_block_pattern(BinkPlane* plane, int bx, int by,
    uint8_t color0, uint8_t color1, const uint8_t pattern[8])
{
    /* Each of the 8 pattern bytes is a row mask: bit n set means use
     * color1 at column n, clear means color0. */
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        uint8_t mask = pattern[r];
        for (int c = 0; c < 8; ++c) {
            dst[c] = (mask & (1u << c)) ? color1 : color0;
        }
        dst += plane->stride;
    }
}

static void decode_block_intra(BinkPlane* plane, int bx, int by,
    int* coeffs)
{
    /* INTRA block: IDCT the (already-dequantised) coefficients,
     * shift by +128 to land in the unsigned-byte range, and clip. */
    bink_idct_block(coeffs);
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            dst[c] = clip_byte(coeffs[r * 8 + c] + 128);
        }
        dst += plane->stride;
    }
}

static void decode_block_inter(BinkPlane* plane, int bx, int by,
    int* coeffs, int mx, int my)
{
    /* INTER block: motion-compensated reference + IDCT residue. */
    decode_block_motion(plane, bx, by, mx, my);
    bink_idct_block(coeffs);
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            dst[c] = clip_byte(dst[c] + coeffs[r * 8 + c]);
        }
        dst += plane->stride;
    }
}

/* Fallback for block types whose bundle-driven parameters aren't yet
 * extracted by the plane decoder. Defaults to a SKIP so we don't
 * leave random memory in the plane. */
static void decode_block_passthrough(BinkPlane* plane, int bx, int by)
{
    decode_block_skip(plane, bx, by);
}

/* --- Bundle chunk decoder ---------------------------------------- *
 *
 * Each bundle in a Bink frame is delivered as a sequence of chunks:
 *
 *     while ((have_more_chunks)) {
 *         length = read_length();    // Huffman-coded, variable
 *         for (i = 0; i < length; i++)
 *             value = read_value();  // Huffman-coded (per bundle)
 *     }
 *
 * Chunks are refilled on demand: when a block decoder asks for the
 * next entry from a bundle whose buffer is exhausted, the parser
 * reads the next chunk.
 *
 * The "have more chunks" signal is implicit -- when the total
 * blocks-per-plane is consumed, the bundle stream is implicitly
 * done. We track this by counting consumed entries per bundle.
 */

/* ceil(log2(x)) for small positive x, 0 for x <= 1. */
static int bink_ceil_log2(int x)
{
    if (x <= 1) return 0;
    int n = 0;
    --x;
    while (x) { ++n; x >>= 1; }
    return n;
}

/* Grow the bundle's entries buffer to at least `need` bytes. */
static bool bundle_ensure_capacity(Bundle* b, size_t need)
{
    if (need <= b->capacity) return true;
    size_t cap = b->capacity ? b->capacity : 256;
    while (cap < need) cap *= 2;
    uint8_t* nb = (uint8_t*)realloc(b->entries, cap);
    if (!nb) return false;
    b->entries = nb;
    b->capacity = cap;
    return true;
}

static void bundle_push_byte(Bundle* b, int value)
{
    if (!bundle_ensure_capacity(b, b->write_pos + 1)) return;
    b->entries[b->write_pos++] = (uint8_t)value;
    b->last_value = value;
}

/* Read one chunk of a 4-bit nibble bundle (BLOCK_TYPES,
 * SUB_BLOCK_TYPES, RUN). After the runsize-bit chunk length there's
 * a 1-bit "constant" flag: if set, emit t copies of the same 4-bit
 * value. Otherwise read t Huffman-decoded values; for BLOCK_TYPES /
 * SUB_BLOCK_TYPES the values 12..15 expand to runs of the last
 * emitted symbol via { 4, 8, 12, 32 } repetition counts. */
static int bundle_refill_4bit_rle(Bundle* b, BitReader* br, int do_rle_expansion)
{
    if (b->read_pos < b->write_pos) {
        return (int)(b->write_pos - b->read_pos);
    }
    if (b->remaining <= 0) return 0;
    int runsize = b->runsize > 0 ? b->runsize : 4;
    int t = (int)br_read(br, runsize);
    if (t <= 0) return 0;
    if (t > b->remaining) t = b->remaining;

    int constant_flag = (int)br_read(br, 1);
    if (constant_flag) {
        int v = (int)br_read(br, 4);
        if (!bundle_ensure_capacity(b, b->write_pos + (size_t)t)) return 0;
        for (int i = 0; i < t; ++i) {
            b->entries[b->write_pos++] = (uint8_t)v;
        }
        b->last_value = v;
        b->remaining -= t;
    } else {
        static const int kRleRunCounts[4] = { 4, 8, 12, 32 };
        int emitted = 0;
        while (emitted < t) {
            int v = bink_huff_read_sym(br, &b->tree);
            if (do_rle_expansion && v >= 12 && v <= 15) {
                int run = kRleRunCounts[v - 12];
                if (run > t - emitted) run = t - emitted;
                int last = b->last_value;
                for (int i = 0; i < run; ++i) bundle_push_byte(b, last);
                emitted += run;
            } else {
                bundle_push_byte(b, v);
                ++emitted;
            }
        }
        b->remaining -= emitted;
    }
    return (int)(b->write_pos - b->read_pos);
}

/* Read one chunk of an 8-bit value bundle (COLORS). Per-byte the
 * encoder may emit a "constant" shortcut too. Within non-constant
 * chunks each byte comes from a context-sensitive pair of Huffman
 * trees (high nibble selected by previous high nibble); we
 * approximate using one tree until contextual trees are wired up
 * -- this keeps the decoder synced even if colors are wrong. */
static int bundle_refill_colors(Bundle* b, BitReader* br)
{
    if (b->read_pos < b->write_pos) {
        return (int)(b->write_pos - b->read_pos);
    }
    if (b->remaining <= 0) return 0;
    int runsize = b->runsize > 0 ? b->runsize : 4;
    int t = (int)br_read(br, runsize);
    if (t <= 0) return 0;
    if (t > b->remaining) t = b->remaining;
    int constant_flag = (int)br_read(br, 1);
    if (constant_flag) {
        int v = (int)br_read(br, 8);
        if (!bundle_ensure_capacity(b, b->write_pos + (size_t)t)) return 0;
        for (int i = 0; i < t; ++i) b->entries[b->write_pos++] = (uint8_t)v;
        b->last_value = v;
    } else {
        for (int i = 0; i < t; ++i) {
            int lo = bink_huff_read_sym(br, &b->tree);
            int hi = bink_huff_read_sym(br, &b->tree);
            bundle_push_byte(b, (hi << 4) | (lo & 0xF));
        }
    }
    b->remaining -= t;
    return (int)(b->write_pos - b->read_pos);
}

/* Generic byte refill -- placeholder for bundles we haven't yet
 * tailored. Mirrors the structural pattern (length + constant flag +
 * data) so bit positions stay sane. */
static int bundle_refill_generic(Bundle* b, BitReader* br)
{
    return bundle_refill_4bit_rle(b, br, 0);
}

/* Read one 4-bit signed motion vector value with an optional sign
 * bit (only present when the magnitude nibble is non-zero). Used
 * by X_OFF / Y_OFF bundles. Returns the signed value (-15..15). */
static int bundle_refill_motion(Bundle* b, BitReader* br)
{
    if (b->read_pos < b->write_pos) {
        return (int)(b->write_pos - b->read_pos);
    }
    if (b->remaining <= 0) return 0;
    int runsize = b->runsize > 0 ? b->runsize : 4;
    int t = (int)br_read(br, runsize);
    if (t <= 0) return 0;
    if (t > b->remaining) t = b->remaining;
    int constant_flag = (int)br_read(br, 1);
    if (constant_flag) {
        int magnitude = (int)br_read(br, 4);
        int sign = 0;
        if (magnitude != 0) sign = (int)br_read(br, 1);
        int signed_value = sign ? -magnitude : magnitude;
        /* Store as 8-bit two's complement so the byte buffer reads
         * pop the right value. */
        uint8_t v = (uint8_t)(signed_value & 0xFF);
        if (!bundle_ensure_capacity(b, b->write_pos + (size_t)t)) return 0;
        for (int i = 0; i < t; ++i) b->entries[b->write_pos++] = v;
    } else {
        for (int i = 0; i < t; ++i) {
            int magnitude = bink_huff_read_sym(br, &b->tree);
            int sign = 0;
            if (magnitude != 0) sign = (int)br_read(br, 1);
            int signed_value = sign ? -magnitude : magnitude;
            bundle_push_byte(b, (uint8_t)(signed_value & 0xFF));
        }
    }
    b->remaining -= t;
    return (int)(b->write_pos - b->read_pos);
}

/* Decode an INTRA_DC or INTER_DC bundle chunk. These are NOT
 * Huffman-coded; the wiki gives the exact bit layout:
 *
 *   t = getbits(runsize)                  // entries in this chunk
 *   if !t: bundle has no more entries
 *   start = getbits(startsize)            // 11 for INTRA, 10 for INTER
 *   start = sign-magnitude (LSB sign)
 *   emit(start)
 *   for chunks of <= 8 deltas:
 *       t2 = getbits(4)                   // delta bit-width 0..15
 *       if t2:
 *           for j in min(t, 8):
 *               v = getbits(t2)
 *               if v && getbit(): v = -v
 *               start += v
 *               emit(start)
 *
 * Values are written into b->values[] (int16_t). The byte mirror
 * b->entries[] receives clamped 8-bit copies so existing byte-pop
 * paths still work for non-INTRA / non-INTER block dispatch. */
static int bundle_refill_dc(Bundle* b, BitReader* br, int startsize)
{
    if (b->read_pos < b->write_pos) {
        return (int)(b->write_pos - b->read_pos);
    }
    if (b->remaining <= 0) return 0;
    int runsize = b->runsize > 0 ? b->runsize : 8;
    int t = (int)br_read(br, runsize);
    if (t <= 0) return 0;
    if (t > b->remaining) t = b->remaining;

    /* Start value: sign-magnitude in LSB. */
    int start_raw = (int)br_read(br, startsize);
    int start = (start_raw & 1) ? -(start_raw >> 1) : (start_raw >> 1);

    /* Lazy-allocate the int16 mirror. */
    if (!b->values) {
        b->values = (int16_t*)calloc(b->capacity ? b->capacity : 256,
            sizeof(int16_t));
        if (!b->values) return 0;
    }

    /* Emit the start value (always 1 entry). */
    if (!bundle_ensure_capacity(b, b->write_pos + 1)) return 0;
    b->values[b->write_pos] = (int16_t)start;
    int clamped = start < -128 ? -128 : (start > 127 ? 127 : start);
    b->entries[b->write_pos++] = (uint8_t)(clamped & 0xFF);

    int produced = 1;
    while (produced < t) {
        int t2 = (int)br_read(br, 4);
        if (t2 == 0) {
            /* Zero-delta run: emit `min(t-produced, 8)` copies of
             * the current `start` (the wiki shows `*dst++=t` but
             * that's widely believed to be a wiki typo for start). */
            int run = t - produced;
            if (run > 8) run = 8;
            if (!bundle_ensure_capacity(b, b->write_pos + (size_t)run))
                break;
            for (int j = 0; j < run; ++j) {
                b->values[b->write_pos] = (int16_t)start;
                int c = start < -128 ? -128 : (start > 127 ? 127 : start);
                b->entries[b->write_pos++] = (uint8_t)(c & 0xFF);
            }
            produced += run;
        } else {
            int chunk_size = t - produced;
            if (chunk_size > 8) chunk_size = 8;
            for (int j = 0; j < chunk_size; ++j) {
                int v = (int)br_read(br, t2);
                if (v != 0 && br_read(br, 1)) v = -v;
                start += v;
                if (!bundle_ensure_capacity(b, b->write_pos + 1)) break;
                b->values[b->write_pos] = (int16_t)start;
                int c = start < -128 ? -128 : (start > 127 ? 127 : start);
                b->entries[b->write_pos++] = (uint8_t)(c & 0xFF);
            }
            produced += chunk_size;
        }
    }
    b->remaining -= produced;
    return (int)(b->write_pos - b->read_pos);
}

/* Pop one byte-valued entry from `b`, choosing the correct refill
 * strategy based on the bundle's id. Returns 0 (== BINK_BLOCK_SKIP)
 * if the bundle has no entries to pop (end-of-bundle), which is a
 * fail-safe for desynced streams. */
static int bundle_pop(Bundle* b, BitReader* br, int bundle_id)
{
    if (b->read_pos >= b->write_pos) {
        int n;
        switch (bundle_id) {
        case BINK_SRC_BLOCK_TYPES:
        case BINK_SRC_SUB_BLOCK_TYPES:
            n = bundle_refill_4bit_rle(b, br, 1);
            break;
        case BINK_SRC_COLORS:
            n = bundle_refill_colors(b, br);
            break;
        case BINK_SRC_X_OFF:
        case BINK_SRC_Y_OFF:
            n = bundle_refill_motion(b, br);
            break;
        case BINK_SRC_INTRA_DC:
            n = bundle_refill_dc(b, br, 11);
            break;
        case BINK_SRC_INTER_DC:
            n = bundle_refill_dc(b, br, 10);
            break;
        default:
            n = bundle_refill_generic(b, br);
            break;
        }
        if (n <= 0) return 0;
    }
    return b->entries[b->read_pos++];
}

/* Pop one signed int16 value (for DC bundles). Returns 0 on
 * end-of-bundle. */
static int bundle_pop_signed(Bundle* b, BitReader* br, int bundle_id)
{
    if (b->read_pos >= b->write_pos) {
        int n;
        if (bundle_id == BINK_SRC_INTRA_DC) {
            n = bundle_refill_dc(b, br, 11);
        } else if (bundle_id == BINK_SRC_INTER_DC) {
            n = bundle_refill_dc(b, br, 10);
        } else {
            n = bundle_refill_motion(b, br);
        }
        if (n <= 0) return 0;
    }
    int v;
    if (b->values) {
        v = b->values[b->read_pos];
    } else {
        /* Sign-extend byte. */
        int8_t bv = (int8_t)b->entries[b->read_pos];
        v = bv;
    }
    b->read_pos++;
    return v;
}

/* Legacy alias for transitions. */
static int bundle_pop_byte(Bundle* b, BitReader* br)
{
    return bundle_pop(b, br, BINK_SRC_BLOCK_TYPES);
}

/* Top-level plane decoder. Consumes the plane's portion of the
 * video bitstream and writes to plane->dst.
 *
 * Per-plane structure documented on the wiki:
 *   - For each of the 9 bundles: 1 bit "reset Huffman tree" + if
 *     set, the per-bundle Huffman tree specifier (4 bits index +
 *     16 nibbles permutation when index != 0)
 *   - Block-by-block walk consuming bundles
 *
 * NOTE: this implementation is structurally correct (it walks
 * blocks, dispatches by type, consumes bundle entries) but the
 * bit-level details of length-prefix coding and per-bundle entry
 * encoding need verification against a known-good Bink reference
 * stream. Until then, .bik playback via this path will likely
 * produce noisy or distorted output. The MJPEG path remains the
 * recommended production option.
 */
static bool decode_plane(BinkPlane* plane, BitReader* br)
{
    /* Phase 1: read tree-reset bits + tree specifiers for each
     * bundle, and compute each bundle's chunk-length-prefix bit
     * width (runsize). Per the spec, runsize = log2_size of the
     * bundle buffer; for most bundles the buffer holds one entry
     * per block in the plane, so runsize = ceil(log2(plane_blocks)).
     * COLORS / PATTERN / INTRA_DC / INTER_DC have larger per-plane
     * quotas because they're consulted multiple times per block. */
    int plane_blocks = plane->width_blocks * plane->height_blocks;
    int pb_log2 = bink_ceil_log2(plane_blocks > 1 ? plane_blocks : 2);
    BINK_TRACE_DETAIL("  plane: %dx%d (%d blocks, pb_log2=%d)\n",
        plane->width_blocks, plane->height_blocks, plane_blocks, pb_log2);
    for (int i = 0; i < BINK_SRC_COUNT; ++i) {
        Bundle* b = &plane->bundles[i];
        bundle_reset_cursors(b);
        b->remaining = plane_blocks;
        b->last_value = 0;

        switch (i) {
        case BINK_SRC_BLOCK_TYPES:
        case BINK_SRC_SUB_BLOCK_TYPES:
        case BINK_SRC_X_OFF:
        case BINK_SRC_Y_OFF:
        case BINK_SRC_RUN:
            b->runsize = pb_log2;
            break;
        case BINK_SRC_COLORS:
        case BINK_SRC_PATTERN:
        case BINK_SRC_INTRA_DC:
        case BINK_SRC_INTER_DC:
            /* COLORS bundle can have up to ~64 entries per block
             * (RAW block) so its run quota is much larger. Bump
             * the runsize so chunk prefixes can address it. */
            b->runsize = pb_log2 + 3;
            break;
        default:
            b->runsize = pb_log2;
            break;
        }
        if (b->runsize > 16) b->runsize = 16;

        size_t bit_pos_before = br->pos_bits;
        int reset = (int)br_read(br, 1);
        if (reset || !b->tree.built) {
            if (!bink_huff_read_tree(br, &b->tree)) {
                BINK_TRACE_DETAIL("  bundle %d tree read FAILED at bit %zu\n",
                    i, bit_pos_before);
                return false;
            }
        }
        BINK_TRACE_DETAIL("  bundle %d: reset=%d runsize=%d perm[0..3]=%d,%d,%d,%d "
            "(after bit %zu)\n",
            i, reset, b->runsize, b->tree.permutation[0], b->tree.permutation[1],
            b->tree.permutation[2], b->tree.permutation[3], br->pos_bits);
    }

    /* Phase 2: block-by-block decode. */
    int block_type_histogram[16] = { 0 };
    for (int by = 0; by < plane->height_blocks; ++by) {
        for (int bx = 0; bx < plane->width_blocks; ++bx) {
            int block_type = bundle_pop(
                &plane->bundles[BINK_SRC_BLOCK_TYPES], br,
                BINK_SRC_BLOCK_TYPES);
            if (block_type >= 0 && block_type < 16) {
                block_type_histogram[block_type]++;
            }
            switch (block_type) {
            case BINK_BLOCK_SKIP:
                decode_block_skip(plane, bx, by);
                break;
            case BINK_BLOCK_FILL: {
                int color = bundle_pop(
                    &plane->bundles[BINK_SRC_COLORS], br,
                    BINK_SRC_COLORS);
                decode_block_fill(plane, bx, by, (uint8_t)color);
                break;
            }
            case BINK_BLOCK_MOTION: {
                /* X_OFF / Y_OFF deliver signed values directly via
                 * bundle_pop_signed (the per-value sign bit is
                 * already applied during refill). */
                int mx = bundle_pop_signed(
                    &plane->bundles[BINK_SRC_X_OFF], br,
                    BINK_SRC_X_OFF);
                int my = bundle_pop_signed(
                    &plane->bundles[BINK_SRC_Y_OFF], br,
                    BINK_SRC_Y_OFF);
                decode_block_motion(plane, bx, by, mx, my);
                break;
            }
            case BINK_BLOCK_RAW: {
                uint8_t raw[64];
                for (int i = 0; i < 64; ++i) {
                    raw[i] = (uint8_t)bundle_pop(
                        &plane->bundles[BINK_SRC_COLORS], br,
                        BINK_SRC_COLORS);
                }
                decode_block_raw(plane, bx, by, raw);
                break;
            }
            case BINK_BLOCK_PATTERN: {
                uint8_t c0 = (uint8_t)bundle_pop(
                    &plane->bundles[BINK_SRC_COLORS], br,
                    BINK_SRC_COLORS);
                uint8_t c1 = (uint8_t)bundle_pop(
                    &plane->bundles[BINK_SRC_COLORS], br,
                    BINK_SRC_COLORS);
                uint8_t pat[8];
                for (int i = 0; i < 8; ++i) {
                    pat[i] = (uint8_t)bundle_pop(
                        &plane->bundles[BINK_SRC_PATTERN], br,
                        BINK_SRC_PATTERN);
                }
                decode_block_pattern(plane, bx, by, c0, c1, pat);
                break;
            }
            /* INTRA / INTER / RUN / RESIDUE / SCALED still require
             * the DCT coefficient bundle wiring (INTRA_DC + INTER_DC
             * + AC zig-zag bundle) which isn't decoded yet. Default
             * to SKIP so the block-walk stays in sync with the
             * bundle reads. */
            case BINK_BLOCK_INTRA:
            case BINK_BLOCK_INTER:
            case BINK_BLOCK_RUN:
            case BINK_BLOCK_RESIDUE:
            case BINK_BLOCK_SCALED:
            default:
                decode_block_passthrough(plane, bx, by);
                break;
            }
        }
    }
    BINK_TRACE_DETAIL("  block types: SKIP=%d SCALED=%d MOTION=%d RUN=%d "
        "RESIDUE=%d INTRA=%d FILL=%d INTER=%d PATTERN=%d RAW=%d (oob=%d)\n",
        block_type_histogram[BINK_BLOCK_SKIP],
        block_type_histogram[BINK_BLOCK_SCALED],
        block_type_histogram[BINK_BLOCK_MOTION],
        block_type_histogram[BINK_BLOCK_RUN],
        block_type_histogram[BINK_BLOCK_RESIDUE],
        block_type_histogram[BINK_BLOCK_INTRA],
        block_type_histogram[BINK_BLOCK_FILL],
        block_type_histogram[BINK_BLOCK_INTER],
        block_type_histogram[BINK_BLOCK_PATTERN],
        block_type_histogram[BINK_BLOCK_RAW],
        block_type_histogram[10] + block_type_histogram[11]
        + block_type_histogram[12] + block_type_histogram[13]
        + block_type_histogram[14] + block_type_histogram[15]);
    return true;
}

static void planes_alloc(Bink1Decoder* d)
{
    /* Y plane is full resolution; Cb/Cr are half resolution (4:2:0). */
    d->plane_widths[0] = (int)d->info.width;
    d->plane_heights[0] = (int)d->info.height;
    d->plane_widths[1] = ((int)d->info.width + 1) / 2;
    d->plane_heights[1] = ((int)d->info.height + 1) / 2;
    d->plane_widths[2] = d->plane_widths[1];
    d->plane_heights[2] = d->plane_heights[1];

    for (int p = 0; p < BINK1_MAX_PLANES; ++p) {
        /* Pad stride up to multiple of 8 so block-aligned writes are
         * always within the allocation. */
        int w = (d->plane_widths[p] + 7) & ~7;
        int h = (d->plane_heights[p] + 7) & ~7;
        d->plane_strides[p] = w;
        for (int set = 0; set < 2; ++set) {
            d->planes[p][set] = (uint8_t*)calloc((size_t)w * (size_t)h, 1);
        }
    }
}

static void planes_free(Bink1Decoder* d)
{
    for (int p = 0; p < BINK1_MAX_PLANES; ++p) {
        for (int set = 0; set < 2; ++set) {
            free(d->planes[p][set]);
            d->planes[p][set] = NULL;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 7. YCbCr -> BGRA                                                   */
/* ------------------------------------------------------------------ */

static uint8_t clip_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void compose_bgra(Bink1Decoder* d, int set,
    uint8_t* dst, int dst_pitch, int dst_w, int dst_h)
{
    int w = (int)d->info.width < dst_w ? (int)d->info.width : dst_w;
    int h = (int)d->info.height < dst_h ? (int)d->info.height : dst_h;
    const uint8_t* yp = d->planes[0][set];
    const uint8_t* up = d->planes[1][set];
    const uint8_t* vp = d->planes[2][set];
    int ys = d->plane_strides[0];
    int us = d->plane_strides[1];
    int vs = d->plane_strides[2];

    for (int y = 0; y < h; ++y) {
        const uint8_t* py = yp + y * ys;
        const uint8_t* pu = up + (y / 2) * us;
        const uint8_t* pv = vp + (y / 2) * vs;
        uint8_t* dr = dst + y * dst_pitch;
        for (int x = 0; x < w; ++x) {
            int Y = py[x];
            int U = pu[x / 2] - 128;
            int V = pv[x / 2] - 128;
            int R = Y + ((91881 * V) >> 16);
            int G = Y - ((22554 * U + 46802 * V) >> 16);
            int B = Y + ((116130 * U) >> 16);
            dr[4 * x + 0] = clip_u8(B);
            dr[4 * x + 1] = clip_u8(G);
            dr[4 * x + 2] = clip_u8(R);
            dr[4 * x + 3] = 0xFF;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 8. Public API                                                      */
/* ------------------------------------------------------------------ */

Bink1Decoder* bink1_decoder_open(const char* path)
{
    if (!path) return NULL;
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;

    Bink1Decoder* d = (Bink1Decoder*)calloc(1, sizeof(Bink1Decoder));
    if (!d) {
        fclose(fp);
        return NULL;
    }
    d->fp = fp;

    if (!parse_header(d) || !parse_audio_tracks(d) || !parse_frame_index(d)) {
        bink1_decoder_close(d);
        return NULL;
    }

    planes_alloc(d);

    if (d->track_count > 0) {
        d->audio_state = (Bink1AudioState*)calloc(
            (size_t)d->track_count, sizeof(Bink1AudioState));
        if (!d->audio_state) {
            bink1_decoder_close(d);
            return NULL;
        }
        for (int t = 0; t < d->track_count; ++t) {
            if (!audio_init_track(&d->audio_state[t], &d->tracks[t])) {
                bink1_decoder_close(d);
                return NULL;
            }
        }
        /* Worst case s16le stereo per frame is the largest overlap_len
         * any track uses, times 2 channels times 2 bytes. */
        int max_overlap = 0;
        for (int t = 0; t < d->track_count; ++t) {
            if (d->audio_state[t].overlap_len > max_overlap) {
                max_overlap = d->audio_state[t].overlap_len;
            }
        }
        d->audio_bytes_per_frame = (size_t)max_overlap * 2 * 2;
    } else {
        d->audio_bytes_per_frame = 0;
    }

    return d;
}

void bink1_decoder_close(Bink1Decoder* d)
{
    if (!d) return;
    if (d->fp) fclose(d->fp);
    free(d->tracks);
    free(d->frames);
    free(d->frame_buf);
    if (d->audio_state) {
        for (int t = 0; t < d->track_count; ++t) {
            Bink1AudioState* a = &d->audio_state[t];
            int channels = a->info.is_stereo ? 2 : 1;
            for (int c = 0; c < channels; ++c) {
                free(a->coeffs[c]);
                free(a->prev_tail[c]);
            }
        }
        free(d->audio_state);
    }
    free(d->audio_out_scratch);
    planes_free(d);
    free(d);
}

bool bink1_decoder_get_info(const Bink1Decoder* d, Bink1Info* out)
{
    if (!d || !out) return false;
    *out = d->info;
    return true;
}

bool bink1_decoder_decode_video(Bink1Decoder* d,
    uint8_t* dst, int dst_pitch, int dst_w, int dst_h)
{
    if (!d || !dst) return false;
    if (d->at_eof) return false;
    if (!load_frame(d, d->current_frame)) {
        d->at_eof = 1;
        return false;
    }

    /* Build the bitstream over the video portion. */
    BitReader br;
    br_init(&br, d->video_data, d->video_size);

    BINK_TRACE_DETAIL("frame %u: %zu video bytes, audio packets=%d (size[0]=%zu), "
        "first 32 bits=%08x\n",
        d->current_frame, d->video_size, d->track_count,
        d->track_count > 0 ? d->audio_packets[0].size : 0,
        d->video_size >= 4 ?
            (unsigned)(d->video_data[0]
                | (d->video_data[1] << 8)
                | (d->video_data[2] << 16)
                | (d->video_data[3] << 24)) : 0);

    /* Decode each plane into the "current" frame buffer set. */
    int set = d->cur_plane_set;
    int prev = 1 - set;

    /* For Bink revisions >= 'i', alpha (when present) and luma planes
     * are each prefixed by a 32-bit plane-size byte count. We don't
     * use the size for length validation but must skip past it so the
     * Huffman/bundle reader starts at the right bit offset. */
    bool has_plane_size_prefix = (d->info.video_version >= 'i');

    for (int p = 0; p < BINK1_MAX_PLANES; ++p) {
        if (has_plane_size_prefix && p == 0) {
            /* Skip the 32-bit luma plane size. (Alpha plane has the
             * same prefix when present; we don't yet decode alpha,
             * which has has_alpha=true streams.) */
            br_byte_align(&br);
            uint32_t plane_size_bytes = br_read(&br, 32);
            (void)plane_size_bytes;
        }
        BinkPlane plane;
        memset(&plane, 0, sizeof(plane));
        plane.width = d->plane_widths[p];
        plane.height = d->plane_heights[p];
        plane.width_blocks = (plane.width + 7) / 8;
        plane.height_blocks = (plane.height + 7) / 8;
        plane.stride = d->plane_strides[p];
        plane.dst = d->planes[p][set];
        plane.prev = d->planes[p][prev];

        bool ok = decode_plane(&plane, &br);

        /* Bundle entries are lazily-allocated heap buffers held by
         * the stack-local plane.bundles. Release them before the
         * plane goes out of scope so we don't leak each frame.
         * The Huffman tree itself is plain inline storage and
         * needs no separate free. */
        for (int i = 0; i < BINK_SRC_COUNT; ++i) {
            free(plane.bundles[i].entries);
            free(plane.bundles[i].values);
        }
        if (!ok) return false;
    }

    /* Sanity histogram of output plane pixels for the first few frames
     * -- if everything decoded to constant black, all bytes are zero. */
    if (bink_trace_check() && g_bink_trace_frame < BINK_TRACE_FRAME_LIMIT) {
        int nonzero = 0;
        const uint8_t* y = d->planes[0][set];
        size_t total = (size_t)d->plane_strides[0]
            * (size_t)d->plane_heights[0];
        for (size_t i = 0; i < total && i < 65536; ++i) {
            if (y[i] != 0) ++nonzero;
        }
        BINK_TRACE_DETAIL("  Y plane: %d nonzero bytes in first 64KB\n",
            nonzero);
    }
    g_bink_trace_frame++;

    compose_bgra(d, set, dst, dst_pitch, dst_w, dst_h);
    d->have_decoded_frame = 1;
    /* Flip current/prev sets so next frame can use this as reference. */
    d->cur_plane_set = prev;
    return true;
}

bool bink1_decoder_decode_audio(Bink1Decoder* d,
    uint8_t* dst, size_t dst_capacity, size_t* out_bytes)
{
    if (!d || !dst || !out_bytes) return false;
    if (d->track_count == 0) {
        *out_bytes = 0;
        return true;
    }

    /* Frame data must be loaded by decode_video already; if not, load. */
    if (d->frame_buf_size == 0) {
        if (!load_frame(d, d->current_frame)) {
            *out_bytes = 0;
            return false;
        }
    }

    /* Decode primary track only. */
    BitReader br;
    br_init(&br, d->audio_packets[0].data, d->audio_packets[0].size);

    Bink1AudioState* a = &d->audio_state[0];
    int channels = a->info.is_stereo ? 2 : 1;
    size_t needed = (size_t)a->overlap_len * (size_t)channels * sizeof(int16_t);
    if (dst_capacity < needed) {
        *out_bytes = 0;
        return false;
    }

    int samples = audio_decode_frame(a, &br, (int16_t*)dst, (int)(dst_capacity / sizeof(int16_t)));
    *out_bytes = (size_t)samples * (size_t)channels * sizeof(int16_t);
    return true;
}

size_t bink1_decoder_max_audio_bytes(const Bink1Decoder* d)
{
    return d ? d->audio_bytes_per_frame : 0;
}

bool bink1_decoder_next_frame(Bink1Decoder* d)
{
    if (!d || d->at_eof) return false;
    if (++d->current_frame >= d->frame_count) {
        d->at_eof = 1;
        return false;
    }
    d->frame_buf_size = 0;       /* force reload next decode call */
    return true;
}

bool bink1_decoder_rewind(Bink1Decoder* d)
{
    if (!d) return false;
    d->current_frame = 0;
    d->at_eof = 0;
    d->frame_buf_size = 0;
    /* Reset previous-frame planes so MOTION/RESIDUE references zero. */
    for (int p = 0; p < BINK1_MAX_PLANES; ++p) {
        for (int set = 0; set < 2; ++set) {
            if (d->planes[p][set]) {
                memset(d->planes[p][set], 0,
                    (size_t)d->plane_strides[p]
                    * (size_t)((d->plane_heights[p] + 7) & ~7));
            }
        }
    }
    d->cur_plane_set = 0;
    /* Reset audio overlap tails to silence. */
    for (int t = 0; t < d->track_count; ++t) {
        Bink1AudioState* a = &d->audio_state[t];
        int channels = a->info.is_stereo ? 2 : 1;
        for (int c = 0; c < channels; ++c) {
            if (a->prev_tail[c]) {
                memset(a->prev_tail[c], 0,
                    (size_t)a->overlap_len * sizeof(float));
            }
        }
    }
    return true;
}

unsigned bink1_decoder_current_frame(const Bink1Decoder* d)
{
    return d ? d->current_frame : 0;
}

bool bink1_decoder_at_eof(const Bink1Decoder* d)
{
    return d ? d->at_eof != 0 : true;
}
