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
    int16_t* values;
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

    /* Demux the frame: each audio track contributes a 4-byte size
     * followed by `size` bytes of audio bitstream. After all audio
     * packets, the remainder is the video bitstream. */
    size_t pos = 0;
    for (int t = 0; t < d->track_count; ++t) {
        if (pos + 4 > d->frame_buf_size) return false;
        uint32_t aud_size = rd_u32le(d->frame_buf + pos);
        pos += 4;
        if (pos + aud_size > d->frame_buf_size) return false;
        d->audio_packets[t].data = d->frame_buf + pos;
        d->audio_packets[t].size = aud_size;
        pos += aud_size;
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

/* Decode a per-bundle Huffman tree specifier from the bitstream:
 *   - 4 bits: tree index (0..15) selecting one of the prebuilt
 *     code-length tables in kBinkHuffLengths.
 *   - If index > 0 and the bitstream has more data, read the symbol
 *     permutation (16 entries × 4 bits each).
 *   - If index == 0, no permutation: identity 0..15.
 * Returns true on success. */
static bool bink_huff_read_tree(BitReader* br, BinkHuffTree* t)
{
    int idx = (int)br_read(br, 4);
    if (idx >= 16) return false;
    if (!bink_huff_build(t, kBinkHuffLengths[idx])) return false;
    if (idx == 0) {
        for (int i = 0; i < 16; ++i) t->permutation[i] = (uint8_t)i;
    } else {
        /* The permutation is delivered via a small "swap" sequence
         * encoded in the stream. The simple variant (used in the
         * Bink1 video bundle headers) is a per-position 4-bit
         * value: 16 nibbles giving the value mapped to each
         * canonical-order slot. */
        for (int i = 0; i < 16; ++i) {
            t->permutation[i] = (uint8_t)br_read(br, 4);
        }
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

/* Decode one audio frame. Reads `coeffs` from the bitstream, applies
 * the inverse transform, performs overlap-add against prev_tail, and
 * emits interleaved s16 samples into `out`. Returns the number of
 * samples written per channel. */
static int audio_decode_frame(Bink1AudioState* a, BitReader* br,
    int16_t* out, int out_capacity)
{
    if (!a->initialized) return 0;
    int n = a->frame_len;
    int half = a->overlap_len;
    int channels = a->info.is_stereo ? 2 : 1;

    if (out_capacity < half * channels) return 0;

    /* TODO: full Bink Audio coefficient decoder. The per-frame
     * structure is:
     *   - 4 bytes: 32-bit frame ID (sync)
     *   - per channel:
     *       - per band: quantization step (8 bits)
     *       - per coefficient: sign + magnitude (variable-length)
     *   - terminator
     * For now we silently drop coefficients (decode silence) so the
     * audio path doesn't fault, while the video decoder can be
     * developed and tested. Replace with the real coefficient
     * decoder once verified against a known-good sample. */
    (void)br;
    for (int c = 0; c < channels; ++c) {
        memset(a->coeffs[c], 0, (size_t)n * sizeof(float));
    }

    /* Overlap-add against the previous tail and emit s16 samples. */
    for (int c = 0; c < channels; ++c) {
        float* prev = a->prev_tail[c];
        float* cur  = a->coeffs[c];
        for (int i = 0; i < half; ++i) {
            float v = prev[i] + cur[i];
            int s = (int)v;
            if (s < -32768) s = -32768;
            if (s > 32767) s = 32767;
            out[i * channels + c] = (int16_t)s;
        }
        /* Save current frame's tail for the next overlap-add. */
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

/* Read a Bink bundle chunk length. Length-prefix size is determined
 * by the bundle's remaining-block count: the encoder uses
 * ceil(log2(remaining + 1)) bits so a chunk can address any subset
 * of the remaining blocks. This matches the standard pattern in
 * Bink Video's bundle layer where each bundle is a run-encoded
 * stream stopped exactly at the plane's block count.
 *
 * `remaining` is the number of un-read entries left in this
 * bundle's plane allotment. Returns 0 (no more chunks) when
 * remaining hits 0. */
static int bink_read_length(BitReader* br, int remaining)
{
    if (remaining <= 0) return 0;
    int bits = bink_ceil_log2(remaining + 1);
    if (bits <= 0) return remaining;
    if (bits > 16) bits = 16;
    int len = (int)br_read(br, bits);
    if (len <= 0) return 0;
    if (len > remaining) len = remaining;
    return len;
}

/* Refill `b` with at least one chunk worth of entries. `remaining`
 * is the bundle's run quota for this plane. Returns the number of
 * entries currently available for popping (may be zero at end-of-
 * bundle). Allocates b->entries lazily on first chunk if needed.  */
static int bundle_refill_bytes(Bundle* b, BitReader* br, int remaining)
{
    if (b->read_pos < b->write_pos) {
        return (int)(b->write_pos - b->read_pos);
    }
    int len = bink_read_length(br, remaining);
    if (len <= 0) return 0;

    /* Lazy allocation: bundles attached to stack-local BinkPlane
     * objects never see bundle_alloc -- size the entries buffer to
     * one chunk's worth here on first use. Growable on subsequent
     * chunks if a single chunk exceeds the initial capacity. */
    size_t need = b->write_pos + (size_t)len;
    if (need > b->capacity) {
        size_t cap = b->capacity ? b->capacity : 256;
        while (cap < need) cap *= 2;
        uint8_t* nb = (uint8_t*)realloc(b->entries, cap);
        if (!nb) return 0;
        b->entries = nb;
        b->capacity = cap;
    }
    for (int i = 0; i < len; ++i) {
        int sym = bink_huff_read_sym(br, &b->tree);
        b->entries[b->write_pos++] = (uint8_t)sym;
    }
    return (int)(b->write_pos - b->read_pos);
}

/* Pop one byte-valued entry from the bundle, refilling from the
 * bitstream as needed. `remaining` is updated to reflect entries
 * consumed (write side) so the next chunk size is computed against
 * the live quota. Returns 0 if the bundle has no entries to pop
 * (end-of-bundle), which is a fail-safe for desynced streams. */
static int bundle_pop_byte(Bundle* b, BitReader* br)
{
    if (b->read_pos >= b->write_pos) {
        int n = bundle_refill_bytes(b, br, b->remaining);
        if (n <= 0) return 0;
        b->remaining -= n;
    }
    return b->entries[b->read_pos++];
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
     * bundle. Bink stores them as a packed prefix. */
    int plane_blocks = plane->width_blocks * plane->height_blocks;
    for (int i = 0; i < BINK_SRC_COUNT; ++i) {
        Bundle* b = &plane->bundles[i];
        bundle_reset_cursors(b);
        /* BLOCK_TYPES is always one entry per block. Other bundles
         * have variable demand depending on block-type mix; we
         * conservatively start them at plane_blocks too so the
         * first chunk's length-prefix has enough bits. */
        b->remaining = plane_blocks;
        int reset = (int)br_read(br, 1);
        if (reset || !b->tree.built) {
            if (!bink_huff_read_tree(br, &b->tree)) return false;
        }
    }

    /* Phase 2: block-by-block decode. */
    for (int by = 0; by < plane->height_blocks; ++by) {
        for (int bx = 0; bx < plane->width_blocks; ++bx) {
            int block_type = bundle_pop_byte(
                &plane->bundles[BINK_SRC_BLOCK_TYPES], br);
            switch (block_type) {
            case BINK_BLOCK_SKIP:
                decode_block_skip(plane, bx, by);
                break;
            case BINK_BLOCK_FILL: {
                int color = bundle_pop_byte(
                    &plane->bundles[BINK_SRC_COLORS], br);
                decode_block_fill(plane, bx, by, (uint8_t)color);
                break;
            }
            case BINK_BLOCK_MOTION: {
                int mx = bundle_pop_byte(
                    &plane->bundles[BINK_SRC_X_OFF], br);
                int my = bundle_pop_byte(
                    &plane->bundles[BINK_SRC_Y_OFF], br);
                /* Bundle values are unsigned bytes; interpret as
                 * signed (range -128..127). */
                if (mx >= 128) mx -= 256;
                if (my >= 128) my -= 256;
                decode_block_motion(plane, bx, by, mx, my);
                break;
            }
            case BINK_BLOCK_RAW: {
                uint8_t raw[64];
                for (int i = 0; i < 64; ++i) {
                    raw[i] = (uint8_t)bundle_pop_byte(
                        &plane->bundles[BINK_SRC_COLORS], br);
                }
                decode_block_raw(plane, bx, by, raw);
                break;
            }
            case BINK_BLOCK_PATTERN: {
                uint8_t c0 = (uint8_t)bundle_pop_byte(
                    &plane->bundles[BINK_SRC_COLORS], br);
                uint8_t c1 = (uint8_t)bundle_pop_byte(
                    &plane->bundles[BINK_SRC_COLORS], br);
                uint8_t pat[8];
                for (int i = 0; i < 8; ++i) {
                    pat[i] = (uint8_t)bundle_pop_byte(
                        &plane->bundles[BINK_SRC_PATTERN], br);
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

    /* Decode each plane into the "current" frame buffer set. */
    int set = d->cur_plane_set;
    int prev = 1 - set;
    for (int p = 0; p < BINK1_MAX_PLANES; ++p) {
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
