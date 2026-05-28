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
#include "bink1_tables.h"

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
 * Huffman-coded stream interleaved into the strip-by-strip decode
 * pass. Bink1 uses 9 bundles total; the layout below matches the
 * order used by Helco/bonkdec's PlaneDecoder. */
typedef enum {
    BINK_SRC_BLOCK_TYPES = 0,     /* 4-bit, RLE-encoded */
    BINK_SRC_SUB_BLOCK_TYPES,     /* 4-bit, RLE-encoded */
    BINK_SRC_COLORS,              /* 8-bit, contextual high-nibble */
    BINK_SRC_PATTERN,             /* 4-bit pairs (low + high nibble) */
    BINK_SRC_X_OFF,               /* 4-bit signed motion */
    BINK_SRC_Y_OFF,               /* 4-bit signed motion */
    BINK_SRC_INTRA_DC,            /* 16-bit delta, 11-bit start */
    BINK_SRC_INTER_DC,            /* 16-bit signed delta, 11-bit start */
    BINK_SRC_PATTERN_LENGTHS,     /* 4-bit, simple fill (for RUN block) */
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

/* Advance to the next 32-bit word boundary. Bink frames are written as
 * sequences of 32-bit little-endian words, and many decode-time
 * sub-streams (notably the per-band audio coefficients) are zero-
 * padded up to the next word boundary. */
static void br_word_align(BitReader* br)
{
    size_t r = br->pos_bits & 31;
    if (r) br->pos_bits += 32 - r;
    if (br->pos_bits > br->total_bits) br->pos_bits = br->total_bits;
}

/* Read a 29-bit signed float: 5-bit signed exponent (offset -22),
 * 23-bit unsigned mantissa, 1 sign bit. Used by the Bink Audio
 * decoder for the first two coefficients of every frame. */
static float br_read_float29(BitReader* br)
{
    int exp = (int)br_read(br, 5) - 22;
    uint32_t mant = br_read(br, 23);
    int sign = (int)br_read(br, 1);
    float v = ldexpf((float)mant, exp);
    return sign ? -v : v;
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

/* Internal audio decoder state for one track. Follows bonkdec's
 * AudioDecoder model: the transform operates on a single interleaved
 * buffer covering all channels, so samples_per_frame already includes
 * the channel multiplier. */
typedef struct {
    Bink1AudioTrackInfo info;

    int samples_per_frame;      /* FFT length = channels * (512..2048) */
    int samples_per_window;     /* overlap region = samples_per_frame/16 */
    int samples_per_block;      /* samples emitted per block = spf - spw */
    float dct_scale;            /* 2 / sqrt(samples_per_frame) */

    int   quantizer_bands[27];  /* coefficient index at each band edge */
    int   band_count;           /* number of band edges - 1 */
    float quantizers[26];       /* per-band dequantization multiplier */

    float* coeffs;              /* samples_per_frame floats (interleaved) */
    int16_t* quant_coeffs;      /* samples_per_frame shorts (reused for samples) */
    int16_t* window;            /* overlap tail from previous block */
    int    is_first_decode;
    int    initialized;
} Bink1AudioState;

/* Bink Huffman tree state. Each per-bundle tree binds a 4-bit tree
 * index (selecting one of the 16 prebuilt lookup tables in
 * bink1_tables.c) to a 16-symbol permutation. Decoding is a peek
 * over max_bits bits followed by a table lookup.
 */
typedef struct {
    int      tree_id;            /* 0..15 */
    uint8_t  permutation[16];
    int      built;
} BinkHuffTreeData;

/* Bundle decode state (fully defined below). Forward-declare the
 * minimal shape needed by the decoder struct.
 *
 * Bink bundles are *not* simple streams of values: each chunk is
 * prefixed by a length-in-elements (variable bit width per bundle,
 * computed from the plane geometry), and the per-element encoding
 * depends on the bundle kind:
 *   - 4-bit Huffman with multiple fill modes (RLE, pairs, simple)
 *   - 8-bit contextual: 16 high-nibble trees + 1 low-nibble tree
 *   - 16-bit delta-coded shorts (DC bundles)
 *
 * The buffer is sized 1 << max_length_in_bits bytes/shorts so a
 * single chunk can never overflow it. Each new 8-row strip triggers
 * a refill via the bundle's Fill* function. Once a Fill reads a
 * zero-length chunk the bundle is "done" for the rest of the plane.
 */
typedef struct {
    /* Tree id and permutation for the 4-bit value-Huffman. The DC
     * bundles do not use Huffman at all; their tree slot is unused. */
    BinkHuffTreeData tree;

    /* For Bundle8Bit (COLORS): 16 contextual high-nibble trees plus
     * the low-nibble tree (stored in `tree` above). The last decoded
     * high-nibble selects the next high-tree. */
    BinkHuffTreeData high_trees[16];
    int last_tree_index;

    /* Element buffers: only one is used per bundle, but we keep both
     * pointers so the struct can serve all three element widths. */
    uint8_t* buf8;
    int16_t* buf16;

    int max_length_in_bits;     /* width of the length prefix */
    int offset;                 /* read position into buf[] */
    int length;                 /* chunk length (in elements) */
    int is_signed;              /* sign handling on element decode */
    int is_done;                /* set after a zero-length chunk */
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
        uint32_t sample_count;   /* output sample count from packet header */
    } audio_packets[BINK1_MAX_AUDIO_TRACKS];

    /* Decoded plane buffers: planes[0..2][0..1] = current and previous
     * frame Y/Cb/Cr planes. Each plane is stored row-major with stride
     * == padded width (rounded up to 8). */
    uint8_t* planes[BINK1_MAX_PLANES][2];
    int      plane_widths[BINK1_MAX_PLANES];
    int      plane_heights[BINK1_MAX_PLANES];
    int      plane_strides[BINK1_MAX_PLANES];
    int      cur_plane_set;     /* 0 or 1: which set holds the current frame */

    /* Per-plane bundles (9 each). Allocated once on open with sizes
     * derived from the plane's pixel geometry; reused across frames. */
    BundleData bundles[BINK1_MAX_PLANES][BINK_SRC_COUNT];


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

    /* The audio metadata lives in three consecutive per-track tables,
     * each 4 bytes/track (matching RAD's layout). Getting the sizes
     * wrong here shifts the frame index table that follows, which
     * silently desyncs every frame offset.
     *   (a) header1: { uint16 unknown; uint16 channels }
     *   (b) header2: { uint16 sample_rate; uint16 flags }
     *   (c) track_id: uint32
     */

    /* (a) header1 -- 4 bytes/track. We don't use the unknown field;
     * channel count is re-derived from the stereo flag below. */
    for (int i = 0; i < d->track_count; ++i) {
        uint8_t buf[4];
        if (!read_exact(d->fp, buf, 4)) return false;
    }

    /* (b) header2 -- 4 bytes/track:
     *   2 bytes: sample_rate (little-endian, e.g. 0xAC44 = 44100)
     *   2 bytes: flags
     *       bit 12 (0x1000): DCT codec mode (Bink Audio newer)
     *       bit 13 (0x2000): stereo
     * Channel count is derived from the stereo flag; Bink1 has no
     * support for >2 channels per track. */
    for (int i = 0; i < d->track_count; ++i) {
        uint8_t buf[4];
        if (!read_exact(d->fp, buf, 4)) return false;
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
            d->audio_packets[t].sample_count = 0;
            continue;
        }
        /* stored_len includes the 4-byte sample_count word + the
         * raw audio bytes. The sample_count is the number of decoded
         * output samples (across all channels) this packet yields. */
        if (pos + stored_len > d->frame_buf_size) return false;
        if (stored_len < 4) return false;
        d->audio_packets[t].sample_count = rd_u32le(d->frame_buf + pos);
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

typedef BinkHuffTreeData BinkHuffTree;

/* Read one symbol from the bitstream using one of the prebuilt Bink
 * Huffman trees plus a per-bundle symbol permutation. Each prebuilt
 * tree is a flat lookup of 2^max_bits entries: the high nibble is the
 * code length (number of bits actually consumed), the low nibble is
 * the symbol index into the permutation. */
static int bink_huff_read_sym(BitReader* br, const BinkHuffTree* t)
{
    int max_bits = (int)bink_huff_max_bits[t->tree_id];
    uint32_t bits = br_peek(br, max_bits);
    uint8_t entry = bink_huff_tree[t->tree_id][bits];
    br_skip(br, entry >> 4);
    return t->permutation[entry & 0xF];
}

/* Decode a per-bundle Huffman tree specifier from the bitstream:
 *   - 4 bits: tree_id (0..15)
 *   - If tree_id == 0: identity permutation (raw 4-bit nibbles).
 *   - Otherwise: 1-bit mode selector:
 *       1 = "explicit": read 3-bit count, then count+1 4-bit symbols
 *           in order; the remaining unused symbols fall through in
 *           natural order.
 *       0 = "shuffle": read 2-bit depth N; merge "from"/"to" buffers
 *           N+1 times, doubling the merge size each iteration; each
 *           merge step reads 1 bit per output slot to decide whether
 *           the next value comes from the left or right run.
 * Matches Helco/bonkdec's Huffman.ReadTree exactly. */
static bool bink_huff_read_tree(BitReader* br, BinkHuffTree* t)
{
    int tree_id = (int)br_read(br, 4);
    if (tree_id >= BINK_HUFF_TREE_COUNT) return false;
    t->tree_id = tree_id;
    /* Initial symbol permutation = identity. */
    for (int i = 0; i < 16; ++i) t->permutation[i] = (uint8_t)i;

    if (tree_id == 0) {
        t->built = 1;
        return true;
    }

    if (br_read(br, 1) == 1) {
        /* Explicit branch. */
        int first_count = (int)br_read(br, 3);
        uint8_t perm[16];
        int taken_mask = 0;
        int i = 0;
        for (; i <= first_count; ++i) {
            int v = (int)br_read(br, 4);
            perm[i] = (uint8_t)v;
            taken_mask |= 1 << v;
        }
        for (int j = 0; j < 16; ++j) {
            if (!(taken_mask & (1 << j))) perm[i++] = (uint8_t)j;
        }
        memcpy(t->permutation, perm, 16);
    } else {
        /* Shuffle branch. */
        int shuffle_depth = (int)br_read(br, 2);
        uint8_t buf_a[16], buf_b[16];
        memcpy(buf_a, t->permutation, 16); /* identity */
        uint8_t* to = buf_a;
        uint8_t* from = buf_b;
        for (int i = 0; i <= shuffle_depth; ++i) {
            uint8_t* tmp = from; from = to; to = tmp;
            int merge_size = 1 << i;
            for (int j = 0; j < 16; j += merge_size * 2) {
                /* Merge two adjacent runs of merge_size from `from`
                 * into `to`, choosing per-output-slot via 1 bit. */
                int p = 0, q = 0;
                int out = 0;
                while (p < merge_size && q < merge_size) {
                    if (br_read(br, 1) == 0) {
                        to[j + out++] = from[j + p++];
                    } else {
                        to[j + out++] = from[j + merge_size + q++];
                    }
                }
                while (p < merge_size) to[j + out++] = from[j + p++];
                while (q < merge_size) to[j + out++] = from[j + merge_size + q++];
            }
        }
        memcpy(t->permutation, to, 16);
    }
    t->built = 1;
    return true;
}

/* ------------------------------------------------------------------ */
/* 5. Bink Audio decoder                                              */
/* ------------------------------------------------------------------ */
/*
 * Follows Helco/bonkdec's AudioDecoder. The transform spans all
 * channels in a single interleaved buffer: samples_per_frame already
 * includes the channel count, so a stereo 44.1 kHz track uses a
 * 4096-point inverse real DFT covering L/R interleaved.
 *
 * Per coded block:
 *   - align bit reader to the next 32-bit word
 *   - 2 leading 29-bit floats (the DC and Nyquist-ish coefficients)
 *   - per-band 8-bit quantizer exponents -> 10^(exp * 0.0664)
 *   - run-length-coded quantized coefficients
 *   - dequantize per band
 *   - inverse real DFT, scale by 2/sqrt(N), clamp to int16
 *   - triangular-window overlap-add against the previous block's tail
 *
 * A single audio packet may hold several blocks; we loop until the
 * packet's declared sample count is produced.
 */

/* In-place iterative radix-2 decimation-in-time complex FFT. `data`
 * is interleaved { re0, im0, re1, im1, ... } of length 2*n; `inverse`
 * picks the sign and applies the 1/n normalisation. n must be a power
 * of two. */
static void audio_fft_complex(float* data, int n, int inverse)
{
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

/* Inverse real DFT, in place, length n (n a power of two). On input,
 * coeffs holds the packed half-spectrum in the layout used by Bink /
 * Ooura's rdft: coeffs[0] = R[0] (DC), coeffs[1] = R[n/2] (Nyquist),
 * coeffs[2k], coeffs[2k+1] = (R[k], I[k]) for 1 <= k < n/2. On output
 * coeffs[0..n-1] holds the n real time-domain samples (the IRDFT as
 * defined by Ooura, i.e. *without* the trailing 2/n scale -- the
 * caller folds the audio's 2/sqrt(n) factor in afterward). */
static void audio_inverse_rdft(float* coeffs, int n)
{
    if (n <= 0 || (n & (n - 1)) != 0) return;
    float* tmp = (float*)calloc((size_t)n * 2, sizeof(float));
    if (!tmp) return;
    tmp[0] = coeffs[0];               /* DC, imag 0 */
    tmp[1] = 0.0f;
    tmp[2 * (n / 2)] = coeffs[1];     /* Nyquist, imag 0 */
    tmp[2 * (n / 2) + 1] = 0.0f;
    for (int k = 1; k < n / 2; ++k) {
        float re = coeffs[2 * k];
        float im = coeffs[2 * k + 1];
        tmp[2 * k] = re;
        tmp[2 * k + 1] = im;
        tmp[2 * (n - k)] = re;         /* Hermitian mirror */
        tmp[2 * (n - k) + 1] = -im;
    }
    /* Use the *forward* complex transform (exp(-i...)) on the
     * Hermitian spectrum: expanding the conjugate-symmetric pairs
     * gives a[k] = R0 + R_{n/2}(-1)^k + 2*sum(R[j]cos + I[j]sin),
     * which is exactly 2x Ooura's IRDFT (case 2, excluding scale).
     * So take real parts and halve to match the reference, leaving
     * the audio's own 2/sqrt(n) factor to the caller. */
    audio_fft_complex(tmp, n, 0);
    for (int i = 0; i < n; ++i) coeffs[i] = tmp[2 * i] * 0.5f;
    free(tmp);
}

static int16_t audio_clip16(float v)
{
    int s = (int)lrintf(v);
    if (s < -32768) s = -32768;
    if (s > 32767) s = 32767;
    return (int16_t)s;
}

/* Bink Audio run-length code table: the 4-bit selector maps to a run
 * multiplier (the unpacker advances 8 * RunLengths[idx] coefficients). */
static const int kBinkAudioRunLengths[16] = {
    2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64
};

static bool audio_init_track(Bink1AudioState* a, const Bink1AudioTrackInfo* info)
{
    a->info = *info;
    int channels = info->is_stereo ? 2 : 1;
    int base = (info->sample_rate >= 44100) ? 2048
             : (info->sample_rate >= 22050) ? 1024
             : 512;
    int spf = channels * base;
    a->samples_per_frame = spf;
    a->samples_per_window = spf / 16;
    a->samples_per_block = spf - a->samples_per_window;
    a->dct_scale = (float)(2.0 / sqrt((double)spf));

    /* Quantizer band edges: walk the critical-band table while the
     * band edge is below the (interleaved) half sample rate, mapping
     * each edge to a coefficient index, then close with spf/2. */
    int sample_rate = channels * (int)info->sample_rate;
    int half_rate = (sample_rate + 1) / 2;
    int nb = 0;
    for (int i = 0; i < BINK_AUDIO_BAND_COUNT; ++i) {
        if (bink_audio_critical_freq[i] >= half_rate) break;
        int idx = (spf / 2) * bink_audio_critical_freq[i] / half_rate;
        if (idx < 1) idx = 1;
        a->quantizer_bands[nb++] = idx;
    }
    a->quantizer_bands[nb++] = spf / 2;
    a->band_count = nb - 1;     /* number of bands = edges - 1 */

    a->coeffs = (float*)calloc((size_t)spf, sizeof(float));
    a->quant_coeffs = (int16_t*)calloc((size_t)spf, sizeof(int16_t));
    a->window = (int16_t*)calloc((size_t)a->samples_per_window, sizeof(int16_t));
    if (!a->coeffs || !a->quant_coeffs || !a->window) return false;
    a->is_first_decode = 1;
    a->initialized = 1;
    return true;
}

/* Decode the per-band 8-bit quantizer exponents for one block. */
static void audio_read_quantizers(Bink1AudioState* a, BitReader* br)
{
    for (int i = 0; i < a->band_count; ++i) {
        int exp = (int)br_read(br, 8);
        a->quantizers[i] = (float)pow(10.0, exp * 0.066399999);
    }
}

/* Unpack the run-length-coded quantized coefficients into
 * a->quant_coeffs[2 .. spf). */
static void audio_unpack_coeffs(Bink1AudioState* a, BitReader* br)
{
    int spf = a->samples_per_frame;
    int i = 2;
    while (i < spf) {
        int end;
        if (br_read(br, 1) == 0) {
            end = i + 8;
        } else {
            end = i + 8 * kBinkAudioRunLengths[br_read(br, 4)];
        }
        if (end > spf) end = spf;

        int bits = (int)br_read(br, 4);
        if (bits == 0) {
            for (; i < end; ++i) a->quant_coeffs[i] = 0;
            continue;
        }
        for (; i < end; ++i) {
            int v = (int)br_read(br, bits);
            if (v != 0 && br_read(br, 1) != 0) v = -v;
            a->quant_coeffs[i] = (int16_t)v;
        }
    }
}

/* Multiply the quantized coefficients by their band's quantizer. The
 * two leading float coefficients (indices 0,1) are left untouched. */
static void audio_dequantize(Bink1AudioState* a)
{
    int ci = 2;
    for (int b = 0; b < a->band_count; ++b) {
        int band_size = a->quantizer_bands[b + 1] - a->quantizer_bands[b];
        float q = a->quantizers[b];
        for (int i = 0; i < band_size; ++i) {
            a->coeffs[ci] = q * a->quant_coeffs[ci]; ++ci;
            a->coeffs[ci] = q * a->quant_coeffs[ci]; ++ci;
        }
    }
}

/* Triangular-window overlap-add of the freshly transformed block
 * (now sitting in a->quant_coeffs as int16) against the previous
 * block's saved tail, writing samples_per_block samples to out. */
static void audio_apply_window(Bink1AudioState* a, int16_t* out)
{
    int spw = a->samples_per_window;
    int spb = a->samples_per_block;
    if (a->is_first_decode) {
        a->is_first_decode = 0;
        memcpy(out, a->quant_coeffs, (size_t)spb * sizeof(int16_t));
    } else {
        for (int i = 0; i < spw; ++i) {
            out[i] = (int16_t)((a->quant_coeffs[i] * i
                + a->window[i] * (spw - i)) / spw);
        }
        memcpy(out + spw, a->quant_coeffs + spw,
            (size_t)(spb - spw) * sizeof(int16_t));
    }
    memcpy(a->window, a->quant_coeffs + spb, (size_t)spw * sizeof(int16_t));
}

/* Decode an entire audio packet (which may contain several blocks)
 * into interleaved s16 samples. `sample_count` is the packet's
 * declared output sample count (across all channels). Returns the
 * number of int16 samples written. */
static int audio_decode_packet(Bink1AudioState* a, BitReader* br,
    uint32_t sample_count, int16_t* out, int out_capacity)
{
    if (!a->initialized) return 0;
    /* DCT-mode Bink Audio (binkaudio_dct, newer RAD encodes) is not
     * implemented yet. Emit clean silence rather than running the RDFT
     * transform on DCT-coded data, which would decode to noise. Video
     * is unaffected. */
    if (a->info.is_dct) return 0;
    int spf = a->samples_per_frame;
    int spb = a->samples_per_block;
    int samples_left = (int)sample_count;
    if (samples_left > out_capacity) samples_left = out_capacity;
    int written = 0;

    while (samples_left > 0) {
        br_word_align(br);
        if (br_remaining(br) < 64) break;

        memset(a->coeffs, 0, (size_t)spf * sizeof(float));
        a->coeffs[0] = br_read_float29(br);
        a->coeffs[1] = br_read_float29(br);

        audio_read_quantizers(a, br);
        audio_unpack_coeffs(a, br);
        audio_dequantize(a);

        audio_inverse_rdft(a->coeffs, spf);
        for (int i = 0; i < spf; ++i) {
            a->quant_coeffs[i] = audio_clip16(a->dct_scale * a->coeffs[i]);
        }

        int use = samples_left < spb ? samples_left : spb;
        if (written + spb > out_capacity) break;
        audio_apply_window(a, out + written);
        written += use;
        samples_left -= use;
    }
    return written;
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

/* Plane geometry for a single decode pass. Bundles live in the
 * decoder struct (persistent across frames); the plane struct is
 * purely transient pointer + size carrying. */
typedef struct {
    int width, height;          /* pixel dims (block-aligned ceil) */
    int width_blocks;           /* ceil(width / 8) */
    int height_blocks;          /* ceil(height / 8) */
    int stride;                 /* row stride of the plane in bytes */
    uint8_t* dst;               /* current frame's plane buffer */
    uint8_t* prev;              /* previous frame's plane buffer */
    Bundle* bundles;            /* points at d->bundles[plane_idx] */
} BinkPlane;

/* Compute max_length_in_bits = ceil(log2(min_value_count +
 * add_block_lines * (plane_width_pixels / 8))). The plane width
 * is aligned up to a multiple of 8 before division, matching
 * bonkdec's AdaptSize. The 9 Bink1 bundles use add_block_lines
 * values of 1, 1, 64, 8, 1, 1, 1, 1, 48 (BLOCK_TYPES, SUB,
 * COLORS, PATTERN, X_OFF, Y_OFF, INTRA_DC, INTER_DC, PATLEN). */
static int bundle_max_bits_for(int min_value_count, int add_block_lines,
    int plane_width_aligned)
{
    int size = min_value_count + add_block_lines * (plane_width_aligned / 8);
    int n = 1;
    while ((1 << n) < size) ++n;
    return n;
}

static const int kBundleMinValueCount = 512;
/* Per-bundle add_block_lines table (indexed by BinkSrcBundle). */
static const int kBundleAddLines[BINK_SRC_COUNT] = {
    /* BLOCK_TYPES      */ 1,
    /* SUB_BLOCK_TYPES  */ 1,
    /* COLORS           */ 64,
    /* PATTERN          */ 8,
    /* X_OFF            */ 1,
    /* Y_OFF            */ 1,
    /* INTRA_DC         */ 1,
    /* INTER_DC         */ 1,
    /* PATTERN_LENGTHS  */ 48,
};
/* Whether each bundle reads signed elements via the per-element
 * sign bit. Currently only X_OFF / Y_OFF use it. */
static const int kBundleIsSigned[BINK_SRC_COUNT] = {
    /* BLOCK_TYPES      */ 0,
    /* SUB_BLOCK_TYPES  */ 0,
    /* COLORS           */ 0,
    /* PATTERN          */ 0,
    /* X_OFF            */ 1,
    /* Y_OFF            */ 1,
    /* INTRA_DC         */ 0, /* unsigned 11-bit start */
    /* INTER_DC         */ 1, /* signed 10-bit start (sign-mag) */
    /* PATTERN_LENGTHS  */ 0,
};

static bool bundle_alloc(Bundle* b, int bundle_id, int plane_width_aligned)
{
    int sub = bundle_id == BINK_SRC_SUB_BLOCK_TYPES;
    int width = sub ? plane_width_aligned / 2 : plane_width_aligned;
    b->max_length_in_bits = bundle_max_bits_for(
        kBundleMinValueCount, kBundleAddLines[bundle_id], width);
    b->is_signed = kBundleIsSigned[bundle_id];
    size_t cap = (size_t)1 << b->max_length_in_bits;
    if (bundle_id == BINK_SRC_INTRA_DC || bundle_id == BINK_SRC_INTER_DC) {
        b->buf16 = (int16_t*)calloc(cap, sizeof(int16_t));
        if (!b->buf16) return false;
    } else {
        b->buf8 = (uint8_t*)calloc(cap, 1);
        if (!b->buf8) return false;
    }
    b->offset = 0;
    b->length = 0;
    b->is_done = 0;
    b->last_tree_index = 0;
    b->tree.built = 0;
    for (int i = 0; i < 16; ++i) b->high_trees[i].built = 0;
    return true;
}

static void bundle_free(Bundle* b)
{
    free(b->buf8);
    free(b->buf16);
    b->buf8 = NULL;
    b->buf16 = NULL;
    b->offset = b->length = 0;
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
 * Bonkdec's exact algorithm. Operates column-by-column then row-by-
 * row, with the per-column dequantiser folded inline (`quantizers[]`
 * is a 64-entry slice from bink_intra_quant or bink_inter_quant). The
 * final 16-bit value per pixel is clamped via Clamp() to land in the
 * unsigned-byte range with a +127 bias; INTRA blocks just write the
 * output, INTER blocks add the signed result to a motion-compensated
 * reference.
 */

/* Clamp(i) = (uint16_t)(i + 127) >> 8 -- this is bonkdec's exact
 * final clamp; the +127 rounds up half-step, then the unsigned shift
 * acts as saturation when the value exceeds the byte range. */
static inline int16_t bink_clamp(int v)
{
    return (int16_t)((uint16_t)(v + 127) >> 8);
}

static void bink_idct8x8(const int16_t* coeffs, int16_t* dst,
    const int32_t* quantizers)
{
    int tmp[64];
    /* --- Column pass --- */
    for (int i = 0; i < 8; ++i) {
        const int16_t* c = coeffs + i;
        const int32_t* q = quantizers + i;
        int* w = tmp + i;
        if (c[8] || c[16] || c[24] || c[32] || c[40] || c[48] || c[56]) {
            int c0 = (q[0] * c[0]) >> 11;
            int c1 = (q[8] * c[8]) >> 11;
            int c2 = (q[16] * c[16]) >> 11;
            int c3 = (q[24] * c[24]) >> 11;
            int c4 = (q[32] * c[32]) >> 11;
            int c5 = (q[40] * c[40]) >> 11;
            int c6 = (q[48] * c[48]) >> 11;
            int c7 = (q[56] * c[56]) >> 11;

            int v13 = c4 + c0;
            int v14 = c0 - c4;
            int v16 = c6 + c2;
            int v17 = ((BINK_IDCT_C2 * (c2 - c6)) >> 11) - (c6 + c2);
            int v60 = v14 - v17;
            int v58 = v17 + v14;
            int v56 = v16 + v13;
            int v62 = v13 - v16;
            int v52 = c5 + c3;
            int v21 = c5 - c3;
            int v22 = c1 + c7;
            int v23 = c1 - c7;
            int v66 = v22 + v52;
            int v24 = (BINK_IDCT_C3 * (v23 + v21)) >> 11;
            int v25 = v24 + ((BINK_IDCT_C4 * v21) >> 11) - (v22 + v52);
            int v26 = ((BINK_IDCT_C2 * (v22 - v52)) >> 11) - v25;
            int v64 = v26 + ((BINK_IDCT_C1 * v23) >> 11) - v24;

            w[0]  = v56 + v66;
            w[8]  = v25 + v58;
            w[16] = v26 + v60;
            w[24] = v62 - v64;
            w[32] = v64 + v62;
            w[40] = v60 - v26;
            w[48] = v58 - v25;
            w[56] = v56 - v66;
        } else {
            int dc = (q[0] * c[0]) >> 11;
            w[0] = w[8] = w[16] = w[24] = w[32] = w[40] = w[48] = w[56] = dc;
        }
    }
    /* --- Row pass --- */
    for (int r = 0; r < 8; ++r) {
        const int* w = tmp + r * 8;
        int16_t* d = dst + r * 8;
        int c0 = w[0], c1 = w[1], c2 = w[2], c3 = w[3];
        int c4 = w[4], c5 = w[5], c6 = w[6], c7 = w[7];

        int v32 = c0 + c4;
        int v61 = c0 - c4;
        int v34 = c2 + c6;
        int v35 = v34 + v32;
        int v36 = v32 - v34;
        int v37 = ((BINK_IDCT_C2 * (c2 - c6)) >> 11) - v34;
        int v39 = v37 + v61;
        v61 = v61 - v37;
        int v43 = c3 + c5;
        int v44 = c5 - c3;
        int v45 = c1 + c7;
        int v46 = c1 - c7;
        int v67 = v45 + v43;
        int v47 = (BINK_IDCT_C3 * (v46 + v44)) >> 11;
        int v48 = v47 + ((BINK_IDCT_C4 * v44) >> 11) - (v45 + v43);
        int v49 = ((BINK_IDCT_C2 * (v45 - v43)) >> 11) - v48;
        int v50 = v49 + ((BINK_IDCT_C1 * v46) >> 11) - v47;

        d[0] = bink_clamp(v67 + v35);
        d[1] = bink_clamp(v48 + v39);
        d[2] = bink_clamp(v61 + v49);
        d[3] = bink_clamp(v36 - v50);
        d[4] = bink_clamp(v50 + v36);
        d[5] = bink_clamp(v61 - v49);
        d[6] = bink_clamp(v39 - v48);
        d[7] = bink_clamp(v35 - v67);
    }
}

/* --- DCT coefficient decoder (bonkdec PlaneDecoder.DCT.cs) -------- *
 *
 * The coefficient walker is a bit-plane scan over 32 coefficient
 * pairs (4-coefficient groups). It uses an "operation stack" of
 * (coeffI, mode) entries that grows and shrinks as bits are read:
 * each iteration shrinks the bit-plane mask (`mask = 1 << bitCount`,
 * starting from a 4-bit "max bit count" prefix and decrementing to
 * zero), and each operation either drills further or directly reads
 * a sign-magnitude coefficient at the current mask. */
static void bink_decode_dct_coeffs(BitReader* br, int16_t* quant_coeffs);

/* Helper: read one bit-plane coefficient at the given mask/bitCount. */
static inline int16_t bink_read_coeff(BitReader* br, uint32_t mask, int bit_count)
{
    int16_t v = (int16_t)(bit_count == 0 ? 1 : (mask | br_read(br, bit_count)));
    if (br_read(br, 1) == 1) v = (int16_t)-v;
    return v;
}

static void bink_decode_dct_coeffs(BitReader* br, int16_t* quant_coeffs_out)
{
    int16_t local[64] = { 0 };
    /* Operation stack: 128 slots, growing left and right from the
     * middle. start_op grows downward (decreases), next_op grows
     * upward (increases). */
    uint8_t ops_coeff[128];
    uint8_t ops_mode[128];
    int start_op = 64;
    int next_op = start_op + 6;
    ops_coeff[start_op + 0] = 4;   ops_mode[start_op + 0] = 0;
    ops_coeff[start_op + 1] = 24;  ops_mode[start_op + 1] = 0;
    ops_coeff[start_op + 2] = 44;  ops_mode[start_op + 2] = 0;
    ops_coeff[start_op + 3] = 1;   ops_mode[start_op + 3] = 3;
    ops_coeff[start_op + 4] = 2;   ops_mode[start_op + 4] = 3;
    ops_coeff[start_op + 5] = 3;   ops_mode[start_op + 5] = 3;

    int max_bit_count = (int)br_read(br, 4);
    uint32_t mask = 1u << (max_bit_count - 1);
    for (int bit_count = max_bit_count - 1; bit_count >= 0;
        --bit_count, mask >>= 1) {
        int cur_op = start_op;
        while (cur_op < next_op) {
            if ((ops_coeff[cur_op] == 0 && ops_mode[cur_op] == 0)
                || br_read(br, 1) == 0) {
                ++cur_op;
                continue;
            }
            int cur_i = ops_coeff[cur_op];
            int cur_mode = ops_mode[cur_op];
            switch (cur_mode) {
            case 1:
                ops_mode[cur_op] = 2;
                ops_coeff[next_op] = (uint8_t)(cur_i + 4);  ops_mode[next_op++] = 2;
                ops_coeff[next_op] = (uint8_t)(cur_i + 8);  ops_mode[next_op++] = 2;
                ops_coeff[next_op] = (uint8_t)(cur_i + 12); ops_mode[next_op++] = 2;
                break;
            case 0:
                ops_coeff[cur_op] = (uint8_t)(cur_i + 4);
                ops_mode[cur_op] = 1;
                for (int j = 0; j < 4; ++j, ++cur_i) {
                    if (br_read(br, 1) == 1) {
                        --start_op;
                        ops_coeff[start_op] = (uint8_t)cur_i;
                        ops_mode[start_op] = 3;
                        continue;
                    }
                    local[cur_i] = bink_read_coeff(br, mask, bit_count);
                }
                break;
            case 2:
                ops_coeff[cur_op] = 0; ops_mode[cur_op] = 0; ++cur_op;
                for (int j = 0; j < 4; ++j, ++cur_i) {
                    if (br_read(br, 1) == 1) {
                        --start_op;
                        ops_coeff[start_op] = (uint8_t)cur_i;
                        ops_mode[start_op] = 3;
                        continue;
                    }
                    local[cur_i] = bink_read_coeff(br, mask, bit_count);
                }
                break;
            case 3:
                local[cur_i] = bink_read_coeff(br, mask, bit_count);
                ops_coeff[cur_op] = 0; ops_mode[cur_op] = 0; ++cur_op;
                break;
            default:
                /* unreachable */
                ++cur_op;
                break;
            }
        }
    }

    /* Preserve caller-supplied DC, then permute the locally-decoded
     * AC pairs into the output via the scan order. Each scan entry
     * names a coefficient *pair* (two int16_t); copy them element-
     * wise to avoid the strict-aliasing UB of reinterpreting the
     * int16_t array as uint32_t (which miscompiles under -O2). */
    local[0] = quant_coeffs_out[0];
    for (int i = 0; i < BINK_DCT_SCAN_LEN; ++i) {
        int src = bink_dct_scan_order[i];
        quant_coeffs_out[2 * i]     = local[2 * src];
        quant_coeffs_out[2 * i + 1] = local[2 * src + 1];
    }
}

/* --- Block decoders ---------------------------------------------- *
 *
 * Each block decoder writes 8x8 pixels (or 16x16 for the SCALED
 * variants) to plane->dst at (bx*8, by*8). They consume entries from
 * plane->bundles inline as they decode. Algorithms follow bonkdec
 * exactly.
 */

/* Return one element from a bundle and advance its read cursor.
 * Signed bundles sign-extend the byte; DC bundles return the stored
 * int16_t. The bundle is assumed non-empty (the caller refilled it
 * before this strip's block walk). */
static int bundle_next(Bundle* b)
{
    if (b->offset >= b->length) return 0;
    int i = b->offset++;
    if (b->buf16) return (int)b->buf16[i];
    if (b->is_signed) return (int)(int8_t)b->buf8[i];
    return (int)b->buf8[i];
}

/* Copy N bytes from a Bundle8Bit's buffer. Returns a pointer into
 * the bundle's buffer that's valid until the next Fill on this
 * bundle. */
static const uint8_t* bundle_next_run(Bundle* b, int count)
{
    if (count < 0 || b->offset + count > b->length) return NULL;
    const uint8_t* p = b->buf8 + b->offset;
    b->offset += count;
    return p;
}

static void decode_block_skip_at(BinkPlane* plane, int bx, int by)
{
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    const uint8_t* src = plane->prev + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        memcpy(dst, src, 8);
        dst += plane->stride;
        src += plane->stride;
    }
}

static void decode_block_motion_at(BinkPlane* plane, int bx, int by,
    int mx, int my)
{
    /* Bink's motion vectors are absolute pixel offsets from the
     * source block position into the previous frame. Bonkdec lets
     * unaligned reads happen and relies on the plane padding to
     * keep them in-bounds; we clamp for safety. */
    int sx = bx * 8 + mx;
    int sy = by * 8 + my;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    int max_x = plane->width - 8;
    int max_y = plane->height - 8;
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

static void decode_block_fill_at(BinkPlane* plane, int bx, int by)
{
    uint8_t color = (uint8_t)bundle_next(&plane->bundles[BINK_SRC_COLORS]);
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        memset(dst, color, 8);
        dst += plane->stride;
    }
}

static void decode_block_raw_at(BinkPlane* plane, int bx, int by)
{
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        const uint8_t* row = bundle_next_run(&plane->bundles[BINK_SRC_COLORS], 8);
        if (row) memcpy(dst, row, 8);
        dst += plane->stride;
    }
}

static void decode_block_pattern_at(BinkPlane* plane, int bx, int by)
{
    uint8_t c0 = (uint8_t)bundle_next(&plane->bundles[BINK_SRC_COLORS]);
    uint8_t c1 = (uint8_t)bundle_next(&plane->bundles[BINK_SRC_COLORS]);
    uint32_t c0w = c0; c0w |= c0w << 8; c0w |= c0w << 16;
    uint32_t c1w = c1; c1w |= c1w << 8; c1w |= c1w << 16;
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        int pat = bundle_next(&plane->bundles[BINK_SRC_PATTERN]);
        uint32_t lo_mask = bink_pattern[pat & 0xF];
        uint32_t hi_mask = bink_pattern[(pat >> 4) & 0xF];
        uint32_t* dr = (uint32_t*)(void*)dst;
        dr[0] = (c0w & lo_mask) | (c1w & ~lo_mask);
        dr[1] = (c0w & hi_mask) | (c1w & ~hi_mask);
        dst += plane->stride;
    }
}

/* Decode a RUN-fill block into a temporary 8x8 buffer: read a 4-bit
 * pattern index, walk pixels in that pattern's scan order, alternating
 * between "many-color" and "single-color" fragments separated by 1-bit
 * flags. Shared by the RUN and SCALED-RUN block decoders. */
static void decode_run_fill_to_temp(BinkPlane* plane, BitReader* br, uint8_t* tmp)
{
    int pattern_i = (int)br_read(br, 4);
    const uint8_t* scan = bink_run_pattern[pattern_i & 0xF];
    int i = 0;
    Bundle* colors = &plane->bundles[BINK_SRC_COLORS];
    Bundle* patlens = &plane->bundles[BINK_SRC_PATTERN_LENGTHS];
    while (i < 63) {
        int run = bundle_next(patlens) + 1;
        if (i + run > 64) run = 64 - i;
        if (br_read(br, 1) == 0) {
            const uint8_t* row = bundle_next_run(colors, run);
            if (row) {
                for (int j = 0; j < run; ++j) tmp[scan[i + j]] = row[j];
            }
        } else {
            uint8_t color = (uint8_t)bundle_next(colors);
            for (int j = 0; j < run; ++j) tmp[scan[i + j]] = color;
        }
        i += run;
    }
    if (i < 64) {
        tmp[scan[i]] = (uint8_t)bundle_next(colors);
    }
}

static void decode_block_run_at(BinkPlane* plane, int bx, int by, BitReader* br)
{
    uint8_t tmp[64];
    decode_run_fill_to_temp(plane, br, tmp);
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        memcpy(dst, tmp + r * 8, 8);
        dst += plane->stride;
    }
}

/* RESIDUE block: motion-compensated reference + decoded residue
 * values added at scan-order positions (bonkdec's bit-plane walk). */
static void decode_block_residue_at(BinkPlane* plane, int bx, int by, BitReader* br)
{
    int mx = bundle_next(&plane->bundles[BINK_SRC_X_OFF]);
    int my = bundle_next(&plane->bundles[BINK_SRC_Y_OFF]);
    decode_block_motion_at(plane, bx, by, mx, my);

    int8_t residue[64] = { 0 };
    uint8_t ops_idx[128];
    uint8_t ops_mode[128];
    int start_op = 64;
    int next_op = start_op + 4;
    ops_idx[start_op + 0] = 4;  ops_mode[start_op + 0] = 0;
    ops_idx[start_op + 1] = 24; ops_mode[start_op + 1] = 0;
    ops_idx[start_op + 2] = 44; ops_mode[start_op + 2] = 0;
    ops_idx[start_op + 3] = 0;  ops_mode[start_op + 3] = 2;

    int mask_count = (int)br_read(br, 7);
    int bit_count = (int)br_read(br, 3) + 1;
    int mask = 1 << (bit_count - 1);
    uint8_t residue_indices[64];
    int residue_count = 0;
    int done = 0;

    for (int i = 0; i < bit_count && !done; ++i, mask >>= 1) {
        /* Refine previously-touched residue positions. */
        for (int j = 0; j < residue_count && !done; ++j) {
            if (br_read(br, 1) == 0) continue;
            int8_t* cur = &residue[residue_indices[j]];
            *cur += (int8_t)(*cur < 0 ? -mask : mask);
            if (mask_count-- == 0) { done = 1; break; }
        }
        if (done) break;
        int cur_op = start_op;
        while (cur_op < next_op && !done) {
            if ((ops_idx[cur_op] == 0 && ops_mode[cur_op] == 0)
                || br_read(br, 1) == 0) {
                ++cur_op;
                continue;
            }
            int cur_i = ops_idx[cur_op];
            int cur_mode = ops_mode[cur_op];
            switch (cur_mode) {
            case 1:
                ops_mode[cur_op] = 2;
                ops_idx[next_op] = (uint8_t)(cur_i + 4);  ops_mode[next_op++] = 2;
                ops_idx[next_op] = (uint8_t)(cur_i + 8);  ops_mode[next_op++] = 2;
                ops_idx[next_op] = (uint8_t)(cur_i + 12); ops_mode[next_op++] = 2;
                break;
            case 0:
                ops_idx[cur_op] = (uint8_t)(cur_i + 4);
                ops_mode[cur_op] = 1;
                for (int j = 0; j < 4 && !done; ++j, ++cur_i) {
                    if (br_read(br, 1) == 1) {
                        --start_op;
                        ops_idx[start_op] = (uint8_t)cur_i;
                        ops_mode[start_op] = 3;
                        continue;
                    }
                    residue_indices[residue_count++] = (uint8_t)cur_i;
                    residue[cur_i] = (int8_t)(br_read(br, 1) == 0 ? mask : -mask);
                    if (mask_count-- == 0) { done = 1; break; }
                }
                break;
            case 2:
                ops_idx[cur_op] = 0; ops_mode[cur_op] = 0; ++cur_op;
                for (int j = 0; j < 4 && !done; ++j, ++cur_i) {
                    if (br_read(br, 1) == 1) {
                        --start_op;
                        ops_idx[start_op] = (uint8_t)cur_i;
                        ops_mode[start_op] = 3;
                        continue;
                    }
                    residue_indices[residue_count++] = (uint8_t)cur_i;
                    residue[cur_i] = (int8_t)(br_read(br, 1) == 0 ? mask : -mask);
                    if (mask_count-- == 0) { done = 1; break; }
                }
                break;
            case 3:
                ops_idx[cur_op] = 0; ops_mode[cur_op] = 0; ++cur_op;
                residue_indices[residue_count++] = (uint8_t)cur_i;
                residue[cur_i] = (int8_t)(br_read(br, 1) == 0 ? mask : -mask);
                if (mask_count-- == 0) { done = 1; break; }
                break;
            default:
                ++cur_op;
                break;
            }
        }
    }

    /* Apply residue values to the motion-compensated reference.
     * Residue is laid out in the residue scan order. */
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            int8_t res = residue[bink_residue_scan_order[r * 8 + c]];
            dst[c] = (uint8_t)((int8_t)dst[c] + res);
        }
        dst += plane->stride;
    }
}

/* Helper for INTRA / INTER: decode 64 coefficients and apply IDCT. */
static void decode_dct_block(BitReader* br, int16_t* values_out,
    Bundle* dc_bundle, const int32_t (*all_quants)[64])
{
    int16_t quant_coeffs[64] = { 0 };
    quant_coeffs[0] = (int16_t)bundle_next(dc_bundle);
    bink_decode_dct_coeffs(br, quant_coeffs);
    uint32_t qi = br_read(br, 4) & 0xF;
    const int32_t* quants = all_quants[qi];
    bink_idct8x8(quant_coeffs, values_out, quants);
}

static void decode_block_intra_at(BinkPlane* plane, int bx, int by, BitReader* br)
{
    int16_t values[64];
    decode_dct_block(br, values, &plane->bundles[BINK_SRC_INTRA_DC],
        bink_intra_quant);
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) dst[c] = (uint8_t)values[r * 8 + c];
        dst += plane->stride;
    }
}

static void decode_block_inter_at(BinkPlane* plane, int bx, int by, BitReader* br)
{
    int mx = bundle_next(&plane->bundles[BINK_SRC_X_OFF]);
    int my = bundle_next(&plane->bundles[BINK_SRC_Y_OFF]);
    int16_t values[64];
    decode_dct_block(br, values, &plane->bundles[BINK_SRC_INTER_DC],
        bink_inter_quant);
    /* Motion compensation source position. */
    int sx = bx * 8 + mx;
    int sy = by * 8 + my;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    int max_x = plane->width - 8;
    int max_y = plane->height - 8;
    if (sx > max_x) sx = max_x;
    if (sy > max_y) sy = max_y;
    const uint8_t* src = plane->prev + sy * plane->stride + sx;
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            int v = (int)src[c] + (int)(int8_t)values[r * 8 + c];
            dst[c] = (uint8_t)v;
        }
        src += plane->stride;
        dst += plane->stride;
    }
}

/* --- SCALED block variants (16x16 effective coverage) ------------- *
 *
 * Bonkdec dispatches a sub-block-type via SUB_BLOCK_TYPES bundle and
 * then decodes a 16x16 region from a single 8x8 source, scaling each
 * pixel up 2x via duplication. Only sub-types 3 (RUN), 5 (INTRA),
 * 6 (FILL), 8 (PATTERN), 9 (RAW) are permitted by the format.
 */
static void decode_block_scaled_fill_at(BinkPlane* plane, int bx, int by)
{
    uint8_t color = (uint8_t)bundle_next(&plane->bundles[BINK_SRC_COLORS]);
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 16; ++r) {
        memset(dst, color, 16);
        dst += plane->stride;
    }
}

static void decode_block_scaled_raw_at(BinkPlane* plane, int bx, int by)
{
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        const uint8_t* row = bundle_next_run(&plane->bundles[BINK_SRC_COLORS], 8);
        if (!row) row = (const uint8_t*)"\0\0\0\0\0\0\0\0";
        for (int x = 0; x < 8; ++x) {
            dst[2 * x] = dst[2 * x + 1] = row[x];
            (dst + plane->stride)[2 * x] = (dst + plane->stride)[2 * x + 1] = row[x];
        }
        dst += 2 * plane->stride;
    }
}

static void decode_block_scaled_pattern_at(BinkPlane* plane, int bx, int by)
{
    uint8_t c0 = (uint8_t)bundle_next(&plane->bundles[BINK_SRC_COLORS]);
    uint8_t c1 = (uint8_t)bundle_next(&plane->bundles[BINK_SRC_COLORS]);
    uint64_t c0w = c0; c0w |= c0w << 8; c0w |= c0w << 16; c0w |= c0w << 32;
    uint64_t c1w = c1; c1w |= c1w << 8; c1w |= c1w << 16; c1w |= c1w << 32;
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    for (int r = 0; r < 8; ++r) {
        int pat = bundle_next(&plane->bundles[BINK_SRC_PATTERN]);
        uint64_t lo = bink_scaled_pattern[pat & 0xF];
        uint64_t hi = bink_scaled_pattern[(pat >> 4) & 0xF];
        uint64_t* row = (uint64_t*)(void*)dst;
        row[0] = (c0w & lo) | (c1w & ~lo);
        row[1] = (c0w & hi) | (c1w & ~hi);
        row += plane->stride / 8;
        row[0] = (c0w & lo) | (c1w & ~lo);
        row[1] = (c0w & hi) | (c1w & ~hi);
        dst += 2 * plane->stride;
    }
}

/* SCALED INTRA: decode an 8x8 DCT block, render each value as a 2x2
 * pixel quad (16x16 total). */
static void decode_block_scaled_intra_at(BinkPlane* plane, int bx, int by, BitReader* br)
{
    int16_t values[64];
    decode_dct_block(br, values, &plane->bundles[BINK_SRC_INTRA_DC],
        bink_intra_quant);
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    const int16_t* v = values;
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 16; j += 2) {
            uint8_t val = (uint8_t)*v++;
            dst[j] = dst[j + 1] = val;
            dst[plane->stride + j] = dst[plane->stride + j + 1] = val;
        }
        dst += 2 * plane->stride;
    }
}

/* SCALED RUN: run-fill an 8x8 temp block, render each value as a 2x2
 * pixel quad (16x16 total). */
static void decode_block_scaled_run_at(BinkPlane* plane, int bx, int by, BitReader* br)
{
    uint8_t tmp[64];
    decode_run_fill_to_temp(plane, br, tmp);
    uint8_t* dst = plane->dst + by * 8 * plane->stride + bx * 8;
    const uint8_t* t = tmp;
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 16; j += 2) {
            uint8_t val = *t++;
            dst[j] = dst[j + 1] = val;
            dst[plane->stride + j] = dst[plane->stride + j + 1] = val;
        }
        dst += 2 * plane->stride;
    }
}

/* --- Bundle reset/fill ------------------------------------------- *
 *
 * Reset reads each bundle's Huffman tree(s) once per plane. Fill
 * reads a length prefix + per-element data once per 8-row strip.
 * After a zero-length fill the bundle is "done" for the rest of
 * the plane.
 */

static bool bundle_read_length(Bundle* b, BitReader* br)
{
    if (b->is_done) return false;
    if (b->offset != b->length) return false;
    b->length = (int)br_read(br, b->max_length_in_bits);
    if (b->length == 0) { b->is_done = 1; return false; }
    b->offset = 0;
    return true;
}

static bool bundle_reset_4bit(Bundle* b, BitReader* br)
{
    b->offset = b->length = 0;
    b->is_done = 0;
    return bink_huff_read_tree(br, &b->tree);
}

static bool bundle_reset_8bit(Bundle* b, BitReader* br)
{
    b->offset = b->length = 0;
    b->is_done = 0;
    b->last_tree_index = 0;
    for (int i = 0; i < 16; ++i) {
        if (!bink_huff_read_tree(br, &b->high_trees[i])) return false;
    }
    return bink_huff_read_tree(br, &b->tree);
}

static void bundle_reset_16bit(Bundle* b)
{
    b->offset = b->length = 0;
    b->is_done = 0;
}

/* BLOCK_TYPES / SUB_BLOCK_TYPES: 4-bit values with RLE expansion. */
static void bundle_fill_4bit_rle(Bundle* b, BitReader* br)
{
    if (!bundle_read_length(b, br)) return;
    if (br_read(br, 1) == 1) {
        uint8_t v = (uint8_t)br_read(br, 4);
        for (int i = 0; i < b->length; ++i) b->buf8[i] = v;
        return;
    }
    static const int kRle[4] = { 4, 8, 12, 32 };
    uint8_t last = 0;
    int i = 0;
    while (i < b->length) {
        int v = bink_huff_read_sym(br, &b->tree);
        if (v < 12) {
            b->buf8[i++] = last = (uint8_t)v;
        } else {
            int run = kRle[v - 12];
            int end = i + run;
            if (end > b->length) end = b->length;
            for (int j = i; j < end; ++j) b->buf8[j] = last;
            i = end;
        }
    }
}

/* PATTERN: 4-bit pairs (lo nibble + hi nibble) packed into one byte. */
static void bundle_fill_4bit_pairs(Bundle* b, BitReader* br)
{
    if (!bundle_read_length(b, br)) return;
    for (int i = 0; i < b->length; ++i) {
        int lo = bink_huff_read_sym(br, &b->tree);
        int hi = bink_huff_read_sym(br, &b->tree);
        b->buf8[i] = (uint8_t)((hi << 4) | (lo & 0xF));
    }
}

/* PATTERN_LENGTHS: unsigned simple. */
static void bundle_fill_4bit_unsigned(Bundle* b, BitReader* br)
{
    if (!bundle_read_length(b, br)) return;
    if (br_read(br, 1) == 1) {
        uint8_t v = (uint8_t)br_read(br, 4);
        for (int i = 0; i < b->length; ++i) b->buf8[i] = v;
        return;
    }
    for (int i = 0; i < b->length; ++i) {
        b->buf8[i] = (uint8_t)bink_huff_read_sym(br, &b->tree);
    }
}

/* X_OFF / Y_OFF: signed simple. */
static void bundle_fill_4bit_signed(Bundle* b, BitReader* br)
{
    if (!bundle_read_length(b, br)) return;
    if (br_read(br, 1) == 1) {
        int v = (int)br_read(br, 4);
        if (v != 0 && br_read(br, 1)) v = -v;
        uint8_t bv = (uint8_t)v;
        for (int i = 0; i < b->length; ++i) b->buf8[i] = bv;
        return;
    }
    for (int i = 0; i < b->length; ++i) {
        int v = bink_huff_read_sym(br, &b->tree);
        if (v != 0 && br_read(br, 1)) v = -v;
        b->buf8[i] = (uint8_t)v;
    }
}

/* COLORS: 8-bit contextual. */
static void bundle_fill_8bit(Bundle* b, BitReader* br)
{
    if (!bundle_read_length(b, br)) return;
    int is_memset = (int)br_read(br, 1);
    int n = is_memset ? 1 : b->length;
    for (int i = 0; i < n; ++i) {
        int hi = bink_huff_read_sym(br, &b->high_trees[b->last_tree_index]);
        b->last_tree_index = hi & 0xF;
        int lo = bink_huff_read_sym(br, &b->tree);
        b->buf8[i] = (uint8_t)((hi << 4) | (lo & 0xF));
    }
    if (is_memset) {
        for (int i = 1; i < b->length; ++i) b->buf8[i] = b->buf8[0];
    }
}

/* INTRA_DC / INTER_DC: delta-coded shorts. start_bits = 11. */
static void bundle_fill_16bit(Bundle* b, BitReader* br)
{
    if (!bundle_read_length(b, br)) return;
    const int start_bits = 11;
    int last;
    if (b->is_signed) {
        last = (int)br_read(br, start_bits - 1);
        if (last != 0 && br_read(br, 1)) last = -last;
    } else {
        last = (int)br_read(br, start_bits);
    }
    int i = 0;
    b->buf16[i++] = (int16_t)last;
    while (i < b->length) {
        int run_end = (i + 8 > b->length) ? b->length : i + 8;
        int rb = (int)br_read(br, 4);
        if (rb == 0) {
            while (i < run_end) b->buf16[i++] = (int16_t)last;
        } else {
            while (i < run_end) {
                int v = (int)br_read(br, rb);
                if (v != 0 && br_read(br, 1)) v = -v;
                last += v;
                b->buf16[i++] = (int16_t)last;
            }
        }
    }
}

/* --- decode_plane ------------------------------------------------- */

static bool decode_plane(BinkPlane* plane, BitReader* br)
{
    /* Phase 1: reset all 9 bundles + read their Huffman trees. The
     * two DC bundles carry no tree (raw delta-coded) so their reset
     * is a no-op state clear. */
    if (!bundle_reset_4bit(&plane->bundles[BINK_SRC_BLOCK_TYPES], br))
        return false;
    if (!bundle_reset_4bit(&plane->bundles[BINK_SRC_SUB_BLOCK_TYPES], br))
        return false;
    if (!bundle_reset_8bit(&plane->bundles[BINK_SRC_COLORS], br))
        return false;
    if (!bundle_reset_4bit(&plane->bundles[BINK_SRC_PATTERN], br))
        return false;
    if (!bundle_reset_4bit(&plane->bundles[BINK_SRC_X_OFF], br))
        return false;
    if (!bundle_reset_4bit(&plane->bundles[BINK_SRC_Y_OFF], br))
        return false;
    bundle_reset_16bit(&plane->bundles[BINK_SRC_INTRA_DC]);
    bundle_reset_16bit(&plane->bundles[BINK_SRC_INTER_DC]);
    if (!bundle_reset_4bit(&plane->bundles[BINK_SRC_PATTERN_LENGTHS], br))
        return false;

    /* Pre-clear the target plane (matches bonkdec's behaviour and gives
     * a known background for blocks that don't fully cover their 8x8). */
    int W = (plane->width + 7) & ~7;
    int H = (plane->height + 7) & ~7;
    for (int y = 0; y < H; ++y) {
        memset(plane->dst + y * plane->stride, 0xFF, W);
    }

    int block_hist[16] = { 0 };

    /* Phase 2: per 8-row strip, fill all 9 bundles then walk blocks.
     * The fill order is fixed by the format and differs subtly from
     * the reset order (PATTERN_LENGTHS is filled last but reset
     * before the DC bundles). */
    for (int y = 0; y < H; y += 8) {
        bundle_fill_4bit_rle(&plane->bundles[BINK_SRC_BLOCK_TYPES], br);
        bundle_fill_4bit_rle(&plane->bundles[BINK_SRC_SUB_BLOCK_TYPES], br);
        bundle_fill_8bit(&plane->bundles[BINK_SRC_COLORS], br);
        bundle_fill_4bit_pairs(&plane->bundles[BINK_SRC_PATTERN], br);
        bundle_fill_4bit_signed(&plane->bundles[BINK_SRC_X_OFF], br);
        bundle_fill_4bit_signed(&plane->bundles[BINK_SRC_Y_OFF], br);
        bundle_fill_16bit(&plane->bundles[BINK_SRC_INTRA_DC], br);
        bundle_fill_16bit(&plane->bundles[BINK_SRC_INTER_DC], br);
        bundle_fill_4bit_unsigned(&plane->bundles[BINK_SRC_PATTERN_LENGTHS], br);

        int by = y / 8;
        for (int x = 0; x < W; x += 8) {
            int bx = x / 8;
            int btype = bundle_next(&plane->bundles[BINK_SRC_BLOCK_TYPES]);
            if (btype >= 0 && btype < 16) block_hist[btype]++;
            switch (btype) {
            case BINK_BLOCK_SKIP:
                decode_block_skip_at(plane, bx, by);
                break;
            case BINK_BLOCK_SCALED:
                if ((y & 8) == 0) {
                    /* On the even row of an 8-row strip pair, a scaled
                     * block consumes one sub-block-type plus its own
                     * decode. Odd rows skip (the block above already
                     * filled this region). */
                    int sub = bundle_next(&plane->bundles[BINK_SRC_SUB_BLOCK_TYPES]);
                    switch (sub) {
                    case BINK_BLOCK_RUN:
                        decode_block_scaled_run_at(plane, bx, by, br);
                        break;
                    case BINK_BLOCK_INTRA:
                        decode_block_scaled_intra_at(plane, bx, by, br);
                        break;
                    case BINK_BLOCK_FILL:
                        decode_block_scaled_fill_at(plane, bx, by);
                        break;
                    case BINK_BLOCK_PATTERN:
                        decode_block_scaled_pattern_at(plane, bx, by);
                        break;
                    case BINK_BLOCK_RAW:
                        decode_block_scaled_raw_at(plane, bx, by);
                        break;
                    default:
                        decode_block_skip_at(plane, bx, by);
                        break;
                    }
                }
                /* Either way, advance one block in X. */
                x += 8;
                break;
            case BINK_BLOCK_MOTION: {
                int mx = bundle_next(&plane->bundles[BINK_SRC_X_OFF]);
                int my = bundle_next(&plane->bundles[BINK_SRC_Y_OFF]);
                decode_block_motion_at(plane, bx, by, mx, my);
                break;
            }
            case BINK_BLOCK_RUN:
                decode_block_run_at(plane, bx, by, br);
                break;
            case BINK_BLOCK_RESIDUE:
                decode_block_residue_at(plane, bx, by, br);
                break;
            case BINK_BLOCK_INTRA:
                decode_block_intra_at(plane, bx, by, br);
                break;
            case BINK_BLOCK_FILL:
                decode_block_fill_at(plane, bx, by);
                break;
            case BINK_BLOCK_INTER:
                decode_block_inter_at(plane, bx, by, br);
                break;
            case BINK_BLOCK_PATTERN:
                decode_block_pattern_at(plane, bx, by);
                break;
            case BINK_BLOCK_RAW:
                decode_block_raw_at(plane, bx, by);
                break;
            default:
                decode_block_skip_at(plane, bx, by);
                break;
            }
        }
    }

    br_word_align(br);

    if (bink_trace_check() && g_bink_trace_frame < BINK_TRACE_FRAME_LIMIT) {
        BINK_TRACE_DETAIL("  plane %dx%d blocks: SKIP=%d SCALED=%d MOTION=%d "
            "RUN=%d RESIDUE=%d INTRA=%d FILL=%d INTER=%d PATTERN=%d RAW=%d\n",
            W, H,
            block_hist[BINK_BLOCK_SKIP], block_hist[BINK_BLOCK_SCALED],
            block_hist[BINK_BLOCK_MOTION], block_hist[BINK_BLOCK_RUN],
            block_hist[BINK_BLOCK_RESIDUE], block_hist[BINK_BLOCK_INTRA],
            block_hist[BINK_BLOCK_FILL], block_hist[BINK_BLOCK_INTER],
            block_hist[BINK_BLOCK_PATTERN], block_hist[BINK_BLOCK_RAW]);
    }
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
        for (int i = 0; i < BINK_SRC_COUNT; ++i) {
            bundle_alloc(&d->bundles[p][i], i, w);
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
        for (int i = 0; i < BINK_SRC_COUNT; ++i) {
            bundle_free(&d->bundles[p][i]);
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
    /* Bink stores the chroma planes in the order Cr (V) then Cb (U):
     * the first chroma plane we decode (planes[1]) is V, the second
     * (planes[2]) is U. */
    const uint8_t* yp = d->planes[0][set];
    const uint8_t* vp = d->planes[1][set];
    const uint8_t* up = d->planes[2][set];
    int ys = d->plane_strides[0];
    int vs = d->plane_strides[1];
    int us = d->plane_strides[2];

    /* Bink stores luma/chroma in limited (studio) range: black is
     * Y=16, white Y=235, chroma centred on 128 spanning 16..240. Use
     * the standard BT.601 limited-range -> full-range RGB conversion
     * (integer form, 298/256 = 1.164 luma gain) so Y=16 maps to true
     * black and Y=235 to white -- matching what ffmpeg produces. The
     * earlier full-range matrix passed Y straight through, lifting
     * blacks to a grey 16. */
    for (int y = 0; y < h; ++y) {
        const uint8_t* py = yp + y * ys;
        const uint8_t* pu = up + (y / 2) * us;
        const uint8_t* pv = vp + (y / 2) * vs;
        uint8_t* dr = dst + y * dst_pitch;
        for (int x = 0; x < w; ++x) {
            int C = py[x] - 16;
            int D = pu[x / 2] - 128;   /* Cb */
            int E = pv[x / 2] - 128;   /* Cr */
            int R = (298 * C + 409 * E + 128) >> 8;
            int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
            int B = (298 * C + 516 * D + 128) >> 8;
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
        /* An audio packet can hold several blocks; size the worst-case
         * output generously at a handful of blocks' worth of samples
         * per channel-frame so callers can pass a fixed buffer. */
        int max_bytes = 0;
        for (int t = 0; t < d->track_count; ++t) {
            int per = d->audio_state[t].samples_per_frame * 16 * 2;
            if (per > max_bytes) max_bytes = per;
        }
        d->audio_bytes_per_frame = (size_t)max_bytes;
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
            free(a->coeffs);
            free(a->quant_coeffs);
            free(a->window);
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

    size_t luma_end_bit = 0;
    for (int p = 0; p < BINK1_MAX_PLANES; ++p) {
        if (has_plane_size_prefix && p == 0) {
            /* Read the 32-bit luma plane size and use it to bound the
             * Y plane decode -- the chroma planes start exactly that
             * many bytes after the prefix, regardless of how many bits
             * our block walk consumes. */
            br_byte_align(&br);
            uint32_t plane_size_bytes = br_read(&br, 32);
            /* yPlaneSize counts from the start of this 4-byte field,
             * so the luma plane data ends -- and the chroma planes
             * begin -- exactly plane_size_bytes from the prefix start
             * (which is bit 0 of the video bitstream here). */
            luma_end_bit = (size_t)plane_size_bytes * 8;
            BINK_TRACE_DETAIL("  luma plane size prefix: %u bytes, "
                "ends at bit %zu\n", plane_size_bytes, luma_end_bit);
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
        plane.bundles = d->bundles[p];

        bool ok = decode_plane(&plane, &br);
        if (!ok) return false;
        /* After the luma plane, force-seek to the byte-aligned end of
         * the luma plane as given by the prefix. This guarantees the
         * chroma planes start at the right bit offset even if the
         * luma walker over- or under-read. */
        if (has_plane_size_prefix && p == 0 && luma_end_bit > 0) {
            if (br.pos_bits != luma_end_bit) {
                BINK_TRACE_DETAIL("  luma over/under-read by %ld bits, "
                    "seeking to %zu\n",
                    (long)br.pos_bits - (long)luma_end_bit, luma_end_bit);
            }
            br.pos_bits = luma_end_bit;
            if (br.pos_bits > br.total_bits) br.pos_bits = br.total_bits;
        }
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

    /* Decode primary track only. A silent packet yields no samples. */
    if (!d->audio_packets[0].data || d->audio_packets[0].size == 0) {
        *out_bytes = 0;
        return true;
    }

    BitReader br;
    br_init(&br, d->audio_packets[0].data, d->audio_packets[0].size);

    Bink1AudioState* a = &d->audio_state[0];
    int cap_samples = (int)(dst_capacity / sizeof(int16_t));
    int samples = audio_decode_packet(a, &br,
        d->audio_packets[0].sample_count, (int16_t*)dst, cap_samples);
    /* `samples` is already the interleaved (all-channel) count. */
    *out_bytes = (size_t)samples * sizeof(int16_t);
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
    /* Reset audio overlap state to silence. */
    for (int t = 0; t < d->track_count; ++t) {
        Bink1AudioState* a = &d->audio_state[t];
        a->is_first_decode = 1;
        if (a->window) {
            memset(a->window, 0,
                (size_t)a->samples_per_window * sizeof(int16_t));
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
