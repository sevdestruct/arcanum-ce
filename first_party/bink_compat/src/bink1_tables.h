/*
 * bink1_tables.h -- precomputed tables for Bink1 video + audio decode.
 *
 * The Bink1 bitstream uses a number of fixed lookup tables that the
 * encoder and decoder must agree on: 16 prebuilt Huffman trees (used
 * by every bundle's symbol-permutation tree), 16 intra and 16 inter
 * quantization matrices for the 8x8 IDCT, the nonstandard 8x8
 * coefficient scan order, the 16 fixed two-color pattern templates,
 * the 16 64-byte run-fill scan patterns, the scaled-block pattern
 * variants, the critical-band edges for the audio decoder, and the
 * 16 run-length codes used by the audio coefficient packer.
 *
 * None of these are documented in any public RAD specification. They
 * were independently reverse-engineered and published by Helco in
 * https://github.com/Helco/bonkdec under the MIT license, which we
 * port here verbatim into C with that license preserved in the
 * accompanying tables source file. This is the table-data path; the
 * algorithmic side (bundle walker, DCT decoder, block dispatch) is
 * in bink1_decoder.c.
 */

#ifndef BINK1_TABLES_H_
#define BINK1_TABLES_H_

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* 16 prebuilt Huffman trees                                          */
/* ------------------------------------------------------------------ */
/* Each entry in a tree is one byte: high nibble = code length in
 * bits, low nibble = symbol index into the per-bundle permutation
 * table. The tree is indexed by peeking maxBitSize bits from the
 * stream; the high nibble tells you how many of those bits were
 * actually consumed. Symbol counts are always 16; the lookup table
 * size is 1 << maxBitSize. */

#define BINK_HUFF_TREE_COUNT 16
#define BINK_HUFF_SYMBOL_COUNT 16

/* Per-tree maximum code length in bits. The lookup table for tree i
 * has 1 << bink_huff_max_bits[i] entries. */
extern const uint8_t bink_huff_max_bits[BINK_HUFF_TREE_COUNT];

/* Lookup tables (16 trees of variable size). bink_huff_tree[i] points
 * at the lookup table for tree i; bink_huff_tree_size[i] gives its
 * length in bytes. */
extern const uint8_t* const bink_huff_tree[BINK_HUFF_TREE_COUNT];
extern const uint32_t bink_huff_tree_size[BINK_HUFF_TREE_COUNT];

/* ------------------------------------------------------------------ */
/* Quantizer matrices                                                 */
/* ------------------------------------------------------------------ */
/* 16 intra and 16 inter quantization matrices, each 64 entries
 * (8x8). Used by the IDCT to dequantize coefficients before the
 * inverse transform. */

extern const int32_t bink_intra_quant[16][64];
extern const int32_t bink_inter_quant[16][64];

/* ------------------------------------------------------------------ */
/* DCT coefficient scan order                                         */
/* ------------------------------------------------------------------ */
/* The decoder walks coefficient *pairs* in this order (32 entries),
 * not the standard JPEG zigzag. Each entry is the pair index (0..31)
 * into the unrolled 64-coefficient block, used as (2i, 2i+1). */

#define BINK_DCT_SCAN_LEN 32
extern const uint8_t bink_dct_scan_order[BINK_DCT_SCAN_LEN];

/* ResidueScanOrder derived from DCT scan order: expand each pair
 * index i into the byte indices 2i and 2i+1. 64 entries. */
extern const uint8_t bink_residue_scan_order[64];

/* ------------------------------------------------------------------ */
/* Pattern templates                                                  */
/* ------------------------------------------------------------------ */
/* Bink's PATTERN block emits one of 16 fixed two-color masks per
 * half-row. Each pattern is a 32-bit mask: 1-bits select color1,
 * 0-bits select color2. */

extern const uint32_t bink_pattern[16];

/* For the SCALED variant, each row is doubled to 64 bits. */
extern const uint64_t bink_scaled_pattern[16];

/* ------------------------------------------------------------------ */
/* Run-fill scan patterns                                             */
/* ------------------------------------------------------------------ */
/* The RUN block decoder chooses one of 16 scan orders, each 64 bytes
 * long, that determines the order pixels are filled in. */

extern const uint8_t bink_run_pattern[16][64];

/* ------------------------------------------------------------------ */
/* Audio decoder constants                                            */
/* ------------------------------------------------------------------ */

/* Critical-band edges in Hz; the inverse RDFT operates on bands
 * defined by these frequencies. 25 entries. */
#define BINK_AUDIO_BAND_COUNT 25
extern const int bink_audio_critical_freq[BINK_AUDIO_BAND_COUNT];

/* 16 run-length codes used by the audio coefficient unpacker. */
#define BINK_AUDIO_RUN_LENGTH_COUNT 16
extern const int bink_audio_run_lengths[BINK_AUDIO_RUN_LENGTH_COUNT];

/* ------------------------------------------------------------------ */
/* IDCT constants                                                     */
/* ------------------------------------------------------------------ */
/* The 8x8 IDCT uses four scaled rotation constants. The IDCT
 * algorithm itself is implemented in bink1_decoder.c. */

#define BINK_IDCT_C1 ( 2217)
#define BINK_IDCT_C2 ( 2896)
#define BINK_IDCT_C3 ( 3784)
#define BINK_IDCT_C4 (-5352)

#endif /* BINK1_TABLES_H_ */
