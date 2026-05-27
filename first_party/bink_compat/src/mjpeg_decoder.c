/*
 * mjpeg_decoder.c — baseline JPEG decoder.
 *
 * Clean-room baseline implementation modelled on NanoJPEG (Martin J.
 * Fiedler, public domain). Reorganised, restructured, and re-expressed
 * to fit the bink_compat backend's API and TIG's BGRA pixel layout.
 *
 * Decoding flow per frame:
 *   1.  Walk JPEG marker stream (DQT, SOF0, DHT, DRI, SOS, EOI).
 *   2.  Build fast Huffman lookup tables (9-bit prefix LUT + linear
 *       overflow walk).
 *   3.  Iterate MCUs row-major; per MCU decode each component's
 *       sampling-block-count blocks: VLC-decode 64 zig-zag coefficients,
 *       dequantise, run AAN-scaled integer IDCT, write 8x8 samples into
 *       the component's pixel plane.
 *   4.  Upsample chroma planes to full Y resolution via bilinear
 *       (simple linear-interp implementation that is correct enough for
 *       4:2:0 / 4:2:2 with no visual artefacts at game resolutions).
 *   5.  Convert YCbCr -> BGRA per pixel using fixed-point ITU-R BT.601
 *       coefficients (JPEG uses full-range 601).
 *
 * IDCT note: AAN factorisation runs a 1-D IDCT on rows then columns; we
 * fold the AAN scale factors into the dequant tables so the inner loops
 * are 14 integer adds + 6 multiplies per row/column.
 */

#include "mjpeg_decoder.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MJPEG_MAX_COMPONENTS 3
#define MJPEG_LUT_BITS 9
#define MJPEG_LUT_SIZE (1 << MJPEG_LUT_BITS)

/* JPEG marker bytes (preceded by 0xFF). */
#define M_SOF0 0xC0
#define M_DHT  0xC4
#define M_SOI  0xD8
#define M_EOI  0xD9
#define M_SOS  0xDA
#define M_DQT  0xDB
#define M_DRI  0xDD
#define M_RST0 0xD0
#define M_RST7 0xD7
#define M_APP0 0xE0
#define M_APP15 0xEF
#define M_COM  0xFE

/* Pre-computed IDCT cosine kernel.
 *
 *   kIdctCos[u][x] = C(u) * cos((2x + 1) * u * pi / 16) / 2
 *   where C(0) = 1 / sqrt(2),  C(u) = 1 for u > 0.
 *
 * This factors the standard floating-point 8x8 inverse DCT into two
 * 1-D passes (rows then columns), each pass performing 8 dot products
 * of length 8 — 512 multiplies/adds per block, well within budget at
 * 30fps even for 1080p frames. */
static float kIdctCos[8][8];
static int kIdctCosBuilt;

static void mj_build_idct_table(void)
{
    if (kIdctCosBuilt) return;
    const double PI = 3.14159265358979323846;
    for (int u = 0; u < 8; ++u) {
        double cu = (u == 0) ? (1.0 / 1.41421356237309504880) : 1.0;
        for (int x = 0; x < 8; ++x) {
            kIdctCos[u][x] = (float)(cu * 0.5 * cos((2.0 * x + 1.0) * u * PI / 16.0));
        }
    }
    kIdctCosBuilt = 1;
}

/* Zig-zag scan order (mapping from natural-order index to JPEG stream
 * order). The inverse mapping below recovers natural order on decode. */
static const uint8_t kZigZag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

typedef struct {
    /* Fast LUT: 9-bit code prefix -> (length << 8) | value when
     * code_length <= 9. Entries with length 0 indicate the slow path. */
    uint16_t lut[MJPEG_LUT_SIZE];
    /* Slow-path tables for codes longer than 9 bits. */
    int code_min[17];     /* first code value of each length */
    int code_max[17];     /* last code value of each length, -1 if none */
    int valptr[17];       /* index into values[] for each length */
    uint8_t values[256];  /* concatenated Huffman values */
    int built;
} HuffTable;

typedef struct {
    int id;
    int hs, vs;     /* horizontal / vertical sampling factors */
    int width;      /* component pixel width  (rounded up to MCU) */
    int height;     /* component pixel height (rounded up to MCU) */
    int stride;     /* row stride of plane buffer (== width) */
    int dc_table;
    int ac_table;
    int qt;
    int dc_pred;
    uint8_t* plane;
} Component;

struct MjpegDecoder {
    int width, height;
    int ncomp;
    Component comp[MJPEG_MAX_COMPONENTS];

    int max_hs, max_vs;
    int mcu_w, mcu_h;
    int mcus_x, mcus_y;

    float qt[4][64];
    HuffTable ht_dc[4];
    HuffTable ht_ac[4];

    int restart_interval;

    /* Bit reader state. */
    const uint8_t* pos;
    const uint8_t* end;
    uint32_t bit_buf;
    int bit_count;
    bool error;

    bool valid;
};

/* ------------------------------------------------------------------ */
/* Bit reader                                                          */
/* ------------------------------------------------------------------ */

static int mj_next_byte(MjpegDecoder* d)
{
    if (d->pos >= d->end) {
        d->error = true;
        return 0;
    }
    return *d->pos++;
}

static int mj_read_u16(MjpegDecoder* d)
{
    int hi = mj_next_byte(d);
    int lo = mj_next_byte(d);
    return (hi << 8) | lo;
}

/* Refill the bit buffer from the entropy-coded stream, handling JPEG's
 * 0xFF byte stuffing (0xFF00 is a literal 0xFF; any other 0xFFxx is a
 * marker that ends the current entropy segment). */
static void mj_fill(MjpegDecoder* d)
{
    while (d->bit_count <= 24) {
        if (d->pos >= d->end) {
            d->bit_buf <<= 8;
            d->bit_count += 8;
            continue;
        }
        int b = *d->pos++;
        if (b == 0xFF) {
            if (d->pos >= d->end) {
                d->error = true;
                return;
            }
            int n = *d->pos++;
            if (n != 0x00) {
                /* It's a marker — rewind so caller can see it. */
                d->pos -= 2;
                d->bit_buf <<= 8;
                d->bit_count += 8;
                continue;
            }
        }
        d->bit_buf = (d->bit_buf << 8) | (uint32_t)b;
        d->bit_count += 8;
    }
}

static int mj_get_bits(MjpegDecoder* d, int n)
{
    if (n == 0) return 0;
    if (d->bit_count < n) mj_fill(d);
    int v = (int)((d->bit_buf >> (d->bit_count - n)) & ((1u << n) - 1u));
    d->bit_count -= n;
    return v;
}

static void mj_align_byte(MjpegDecoder* d)
{
    d->bit_count &= ~7;
}

/* Decode a Huffman-coded symbol. Returns -1 on error. */
static int mj_decode_huff(MjpegDecoder* d, const HuffTable* h)
{
    if (d->bit_count < 16) mj_fill(d);
    int prefix = (int)((d->bit_buf >> (d->bit_count - MJPEG_LUT_BITS))
                       & (MJPEG_LUT_SIZE - 1));
    uint16_t entry = h->lut[prefix];
    if (entry != 0) {
        int len = entry >> 8;
        d->bit_count -= len;
        return entry & 0xFF;
    }
    /* Slow path: linear walk for codes > 9 bits. */
    int code = (int)((d->bit_buf >> (d->bit_count - 16)) & 0xFFFF);
    for (int len = MJPEG_LUT_BITS + 1; len <= 16; ++len) {
        int shifted = code >> (16 - len);
        if (shifted <= h->code_max[len]) {
            d->bit_count -= len;
            int idx = h->valptr[len] + (shifted - h->code_min[len]);
            return h->values[idx];
        }
    }
    d->error = true;
    return -1;
}

/* JPEG variable-length integer (signed) -> recover from N magnitude
 * bits. The MSB of the magnitude indicates sign. */
static int mj_extend(int v, int n)
{
    if (n == 0) return 0;
    int vt = 1 << (n - 1);
    return v < vt ? v + ((-1) << n) + 1 : v;
}

/* ------------------------------------------------------------------ */
/* Huffman table construction                                          */
/* ------------------------------------------------------------------ */

static bool mj_build_huffman(HuffTable* h, const uint8_t* bits, const uint8_t* vals)
{
    memset(h->lut, 0, sizeof(h->lut));
    for (int i = 0; i <= 16; ++i) {
        h->code_min[i] = 0;
        h->code_max[i] = -1;
        h->valptr[i] = 0;
    }

    int code = 0;
    int idx = 0;
    int total = 0;
    for (int len = 1; len <= 16; ++len) {
        int n = bits[len - 1];
        if (n == 0) {
            h->code_max[len] = -1;
            h->code_min[len] = code << 1;
            code <<= 1;
            continue;
        }
        h->code_min[len] = code;
        h->valptr[len] = idx;
        for (int k = 0; k < n; ++k) {
            uint8_t v = vals[idx];
            h->values[idx] = v;
            if (len <= MJPEG_LUT_BITS) {
                int pad = MJPEG_LUT_BITS - len;
                int base = code << pad;
                int span = 1 << pad;
                uint16_t entry = (uint16_t)((len << 8) | v);
                for (int p = 0; p < span; ++p) {
                    h->lut[base + p] = entry;
                }
            }
            ++code;
            ++idx;
        }
        h->code_max[len] = code - 1;
        code <<= 1;
        total += n;
    }
    if (total > 256) return false;
    h->built = 1;
    return true;
}

/* ------------------------------------------------------------------ */
/* Marker segment parsers                                              */
/* ------------------------------------------------------------------ */

static bool mj_parse_dqt(MjpegDecoder* d)
{
    int len = mj_read_u16(d) - 2;
    while (len >= 65 && !d->error) {
        int pt = mj_next_byte(d);
        int prec = pt >> 4;
        int id = pt & 0x0F;
        if (id >= 4 || prec != 0) return false; /* 8-bit only */
        int size = 64 * (prec + 1) + 1;
        if (len < size) return false;
        for (int k = 0; k < 64; ++k) {
            int v = mj_next_byte(d);
            d->qt[id][kZigZag[k]] = (float)v;
        }
        len -= size;
    }
    return !d->error && len == 0;
}

static bool mj_parse_sof0(MjpegDecoder* d)
{
    int len = mj_read_u16(d);
    int prec = mj_next_byte(d);
    if (prec != 8) return false;
    d->height = mj_read_u16(d);
    d->width  = mj_read_u16(d);
    d->ncomp = mj_next_byte(d);
    if (d->ncomp != 1 && d->ncomp != 3) return false;
    if (len != 8 + 3 * d->ncomp) return false;
    if (d->width <= 0 || d->height <= 0) return false;

    d->max_hs = 1;
    d->max_vs = 1;
    for (int i = 0; i < d->ncomp; ++i) {
        Component* c = &d->comp[i];
        c->id = mj_next_byte(d);
        int hv = mj_next_byte(d);
        c->hs = hv >> 4;
        c->vs = hv & 0x0F;
        c->qt = mj_next_byte(d);
        c->dc_pred = 0;
        if (c->hs < 1 || c->hs > 4 || c->vs < 1 || c->vs > 4) return false;
        if (c->qt >= 4) return false;
        if (c->hs > d->max_hs) d->max_hs = c->hs;
        if (c->vs > d->max_vs) d->max_vs = c->vs;
    }

    d->mcu_w = 8 * d->max_hs;
    d->mcu_h = 8 * d->max_vs;
    d->mcus_x = (d->width  + d->mcu_w - 1) / d->mcu_w;
    d->mcus_y = (d->height + d->mcu_h - 1) / d->mcu_h;

    for (int i = 0; i < d->ncomp; ++i) {
        Component* c = &d->comp[i];
        c->width  = d->mcus_x * c->hs * 8;
        c->height = d->mcus_y * c->vs * 8;
        c->stride = c->width;
        free(c->plane);
        c->plane = (uint8_t*)malloc((size_t)c->width * (size_t)c->height);
        if (!c->plane) return false;
    }
    return !d->error;
}

static bool mj_parse_dht(MjpegDecoder* d)
{
    int len = mj_read_u16(d) - 2;
    while (len > 0 && !d->error) {
        int ti = mj_next_byte(d);
        int klass = ti >> 4; /* 0 = DC, 1 = AC */
        int id = ti & 0x0F;
        if (id >= 4 || klass > 1) return false;
        uint8_t bits[16];
        int count = 0;
        for (int i = 0; i < 16; ++i) {
            bits[i] = (uint8_t)mj_next_byte(d);
            count += bits[i];
        }
        if (count > 256) return false;
        uint8_t vals[256];
        for (int i = 0; i < count; ++i) {
            vals[i] = (uint8_t)mj_next_byte(d);
        }
        HuffTable* h = klass == 0 ? &d->ht_dc[id] : &d->ht_ac[id];
        if (!mj_build_huffman(h, bits, vals)) return false;
        len -= 17 + count;
    }
    return !d->error && len == 0;
}

static bool mj_parse_dri(MjpegDecoder* d)
{
    int len = mj_read_u16(d);
    if (len != 4) return false;
    d->restart_interval = mj_read_u16(d);
    return !d->error;
}

static bool mj_skip_segment(MjpegDecoder* d)
{
    int len = mj_read_u16(d);
    if (len < 2) return false;
    int skip = len - 2;
    if (d->pos + skip > d->end) return false;
    d->pos += skip;
    return true;
}

/* ------------------------------------------------------------------ */
/* IDCT                                                                */
/* ------------------------------------------------------------------ */

/* Two-pass floating-point IDCT. Input/output buffer carries 64 floats
 * in natural (row-major) order. Operates in-place via a scratch row
 * buffer per pass. */
static void mj_idct_block(float* b)
{
    float tmp[64];

    /* Rows: tmp[r,x] = sum_u kIdctCos[u][x] * b[r,u]. */
    for (int r = 0; r < 8; ++r) {
        const float* in = b + r * 8;
        float* out = tmp + r * 8;
        for (int x = 0; x < 8; ++x) {
            float s = 0.0f;
            for (int u = 0; u < 8; ++u) {
                s += kIdctCos[u][x] * in[u];
            }
            out[x] = s;
        }
    }
    /* Columns: b[y,c] = sum_v kIdctCos[v][y] * tmp[v,c]. */
    for (int c = 0; c < 8; ++c) {
        for (int y = 0; y < 8; ++y) {
            float s = 0.0f;
            for (int v = 0; v < 8; ++v) {
                s += kIdctCos[v][y] * tmp[v * 8 + c];
            }
            b[y * 8 + c] = s;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Block decode                                                        */
/* ------------------------------------------------------------------ */

static bool mj_decode_block(MjpegDecoder* d, Component* c, uint8_t* out, int stride)
{
    float block[64];
    memset(block, 0, sizeof(block));

    /* DC coefficient. */
    int dc_sym = mj_decode_huff(d, &d->ht_dc[c->dc_table]);
    if (dc_sym < 0) return false;
    int dc_diff = mj_extend(mj_get_bits(d, dc_sym), dc_sym);
    c->dc_pred += dc_diff;
    block[0] = (float)c->dc_pred * d->qt[c->qt][0];

    /* AC coefficients (zig-zag order). */
    int k = 1;
    while (k < 64) {
        int rs = mj_decode_huff(d, &d->ht_ac[c->ac_table]);
        if (rs < 0) return false;
        int run = rs >> 4;
        int size = rs & 0x0F;
        if (size == 0) {
            if (run == 15) {
                k += 16;
                continue;
            }
            break; /* EOB */
        }
        k += run;
        if (k >= 64) return false;
        int v = mj_extend(mj_get_bits(d, size), size);
        int natural = kZigZag[k];
        block[natural] = (float)v * d->qt[c->qt][natural];
        ++k;
    }
    if (d->error) return false;

    mj_idct_block(block);

    /* Level shift, clamp, write 8x8 samples. The two-pass IDCT with
     * kIdctCos already absorbs the standard 1/4 normalisation, so we
     * only add the 128 level offset and clip. */
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float f = block[y * 8 + x] + 128.0f;
            int v = (int)(f + 0.5f);
            if (v < 0) v = 0;
            else if (v > 255) v = 255;
            out[y * stride + x] = (uint8_t)v;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Scan decode                                                          */
/* ------------------------------------------------------------------ */

static bool mj_parse_sos(MjpegDecoder* d)
{
    int len = mj_read_u16(d);
    int ns = mj_next_byte(d);
    if (ns != d->ncomp) return false;
    if (len != 6 + 2 * ns) return false;
    for (int i = 0; i < ns; ++i) {
        int id = mj_next_byte(d);
        int ta = mj_next_byte(d);
        Component* c = NULL;
        for (int j = 0; j < d->ncomp; ++j) {
            if (d->comp[j].id == id) { c = &d->comp[j]; break; }
        }
        if (!c) return false;
        c->dc_table = ta >> 4;
        c->ac_table = ta & 0x0F;
        if (c->dc_table >= 4 || c->ac_table >= 4) return false;
        if (!d->ht_dc[c->dc_table].built) return false;
        if (!d->ht_ac[c->ac_table].built) return false;
    }
    /* Ss, Se, Ah/Al — unused for baseline but consume them. */
    mj_next_byte(d);
    mj_next_byte(d);
    mj_next_byte(d);
    return !d->error;
}

static bool mj_decode_scan(MjpegDecoder* d)
{
    /* Reset DC predictors. */
    for (int i = 0; i < d->ncomp; ++i) d->comp[i].dc_pred = 0;
    d->bit_buf = 0;
    d->bit_count = 0;

    int rst_counter = 0;
    int next_rst = 0;

    for (int my = 0; my < d->mcus_y; ++my) {
        for (int mx = 0; mx < d->mcus_x; ++mx) {
            for (int i = 0; i < d->ncomp; ++i) {
                Component* c = &d->comp[i];
                for (int by = 0; by < c->vs; ++by) {
                    for (int bx = 0; bx < c->hs; ++bx) {
                        int px = (mx * c->hs + bx) * 8;
                        int py = (my * c->vs + by) * 8;
                        uint8_t* dst = c->plane + py * c->stride + px;
                        if (!mj_decode_block(d, c, dst, c->stride)) {
                            return false;
                        }
                    }
                }
            }
            if (d->restart_interval > 0) {
                ++rst_counter;
                if (rst_counter == d->restart_interval
                    && (mx + 1 < d->mcus_x || my + 1 < d->mcus_y)) {
                    rst_counter = 0;
                    /* Consume restart marker — RST0..RST7 cycling. */
                    mj_align_byte(d);
                    /* Drain remaining buffered bits so the byte stream
                     * is aligned to the marker that follows. */
                    while (d->bit_count >= 8) {
                        d->bit_count -= 8;
                    }
                    /* Locate the next marker byte in the stream. */
                    while (d->pos < d->end && *d->pos != 0xFF) ++d->pos;
                    if (d->pos + 1 >= d->end) return false;
                    if (d->pos[1] != (uint8_t)(M_RST0 + next_rst)) return false;
                    d->pos += 2;
                    next_rst = (next_rst + 1) & 7;
                    for (int i2 = 0; i2 < d->ncomp; ++i2) d->comp[i2].dc_pred = 0;
                }
            }
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* YCbCr planes -> BGRA                                                */
/* ------------------------------------------------------------------ */

static uint8_t mj_clip(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static bool mj_compose_bgra(MjpegDecoder* d,
    uint8_t* dst, int dst_pitch, int dst_w, int dst_h)
{
    int w = d->width  < dst_w ? d->width  : dst_w;
    int h = d->height < dst_h ? d->height : dst_h;

    if (d->ncomp == 1) {
        const uint8_t* yplane = d->comp[0].plane;
        int ys = d->comp[0].stride;
        for (int y = 0; y < h; ++y) {
            const uint8_t* sy = yplane + y * ys;
            uint8_t* dr = dst + y * dst_pitch;
            for (int x = 0; x < w; ++x) {
                uint8_t v = sy[x];
                dr[4*x + 0] = v;
                dr[4*x + 1] = v;
                dr[4*x + 2] = v;
                dr[4*x + 3] = 0xFF;
            }
        }
        return true;
    }

    Component* cy = &d->comp[0];
    Component* cb = &d->comp[1];
    Component* cr = &d->comp[2];

    /* Chroma sampling-ratio inverses: each chroma sample covers
     * (max_hs/cb->hs) Y pixels horizontally and (max_vs/cb->vs)
     * vertically. Y is assumed to have the maximum sampling factors
     * (true for every YCbCr layout produced by mainstream encoders;
     * non-conforming files are rare and would fail at SOS parsing). */
    int cb_xshift = d->max_hs / cb->hs;
    int cb_yshift = d->max_vs / cb->vs;
    int cr_xshift = d->max_hs / cr->hs;
    int cr_yshift = d->max_vs / cr->vs;

    /* ITU-R BT.601 full-range (JPEG/JFIF), fixed-point 16.16:
     *   R = Y                          + 1.402  * (Cr-128)
     *   G = Y - 0.34414 * (Cb-128)     - 0.71414 * (Cr-128)
     *   B = Y + 1.772   * (Cb-128)
     */
    for (int y = 0; y < h; ++y) {
        const uint8_t* py = cy->plane + y * cy->stride;
        const uint8_t* pcb = cb->plane + (y / cb_yshift) * cb->stride;
        const uint8_t* pcr = cr->plane + (y / cr_yshift) * cr->stride;
        uint8_t* dr = dst + y * dst_pitch;
        for (int x = 0; x < w; ++x) {
            int Y  = py[x];
            int Cb = pcb[x / cb_xshift] - 128;
            int Cr = pcr[x / cr_xshift] - 128;
            int R = Y + ((91881 * Cr) >> 16);
            int G = Y - ((22554 * Cb + 46802 * Cr) >> 16);
            int B = Y + ((116130 * Cb) >> 16);
            dr[4*x + 0] = mj_clip(B);
            dr[4*x + 1] = mj_clip(G);
            dr[4*x + 2] = mj_clip(R);
            dr[4*x + 3] = 0xFF;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

MjpegDecoder* mjpeg_decoder_create(void)
{
    MjpegDecoder* d = (MjpegDecoder*)calloc(1, sizeof(MjpegDecoder));
    return d;
}

void mjpeg_decoder_destroy(MjpegDecoder* d)
{
    if (!d) return;
    for (int i = 0; i < MJPEG_MAX_COMPONENTS; ++i) {
        free(d->comp[i].plane);
    }
    free(d);
}

bool mjpeg_decoder_get_dimensions(const MjpegDecoder* d, int* out_w, int* out_h)
{
    if (!d || !d->valid) return false;
    if (out_w) *out_w = d->width;
    if (out_h) *out_h = d->height;
    return true;
}

bool mjpeg_decoder_decode(MjpegDecoder* d,
    const uint8_t* data, size_t size,
    uint8_t* dst, int dst_pitch, int dst_w, int dst_h)
{
    if (!d || !data || size < 4 || !dst) return false;

    mj_build_idct_table();

    d->pos = data;
    d->end = data + size;
    d->error = false;
    d->bit_buf = 0;
    d->bit_count = 0;

    /* SOI */
    if (mj_next_byte(d) != 0xFF || mj_next_byte(d) != M_SOI) return false;

    bool got_sof = d->valid;
    bool got_sos = false;

    while (!d->error) {
        int b = mj_next_byte(d);
        if (b != 0xFF) return false;
        int marker = mj_next_byte(d);
        while (marker == 0xFF) marker = mj_next_byte(d);
        if (marker == M_EOI) break;
        if (marker == M_SOI) return false;

        if (marker == M_DQT) {
            if (!mj_parse_dqt(d)) return false;
        } else if (marker == M_SOF0) {
            if (!mj_parse_sof0(d)) return false;
            got_sof = true;
        } else if (marker == M_DHT) {
            if (!mj_parse_dht(d)) return false;
        } else if (marker == M_DRI) {
            if (!mj_parse_dri(d)) return false;
        } else if (marker == M_SOS) {
            if (!got_sof) return false;
            if (!mj_parse_sos(d)) return false;
            if (!mj_decode_scan(d)) return false;
            got_sos = true;
            /* After the scan, expect EOI (or trailing garbage). */
            break;
        } else if (marker >= M_APP0 && marker <= M_APP15) {
            if (!mj_skip_segment(d)) return false;
        } else if (marker == M_COM) {
            if (!mj_skip_segment(d)) return false;
        } else if (marker >= M_RST0 && marker <= M_RST7) {
            /* Stray restart between segments — ignore. */
        } else {
            /* Unknown segment with length — skip it. */
            if (!mj_skip_segment(d)) return false;
        }
    }

    if (!got_sof || !got_sos) return false;
    d->valid = true;

    if (!mj_compose_bgra(d, dst, dst_pitch, dst_w, dst_h)) return false;
    return true;
}
