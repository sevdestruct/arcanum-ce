# Bink1 native decoder — implementation status

## Summary

The Bink1 decoder is **functionally complete**: video and audio both
decode correctly, verified against a reference decoder. A stock
Arcanum install plays its `.bik` cutscenes directly through this path
with no FFmpeg dependency and no pre-conversion step.

## Verification

The decoder was validated bit-for-bit against
[Helco/bonkdec](https://github.com/Helco/bonkdec) (an MIT-licensed
clean-room Bink1 decoder) built locally and run on Arcanum's own
cutscene files:

- **Video luma plane**: byte-identical to bonkdec on frame 0 of the
  test cutscenes.
- **Video full color**: visually identical to FFmpeg's `binkdec` on
  both keyframes and motion-compensated INTER frames (the engine and
  movie content render the same).
- **Audio**: 100% of decoded samples match bonkdec within ±1 LSB
  (the only difference is floating-point rounding between our
  radix-2 FFT and bonkdec's Ooura split-radix FFT), and every frame
  stays sample-aligned.

## What works

### Container
- Header parse (`BIK[a-i]` magic, dimensions, fps, flags).
- Audio track metadata: the per-track tables are 4 bytes each
  (header1 = unknown + channels, header2 = sample_rate + flags,
  track_id), totalling 12 bytes/track. Getting this wrong shifts the
  frame-offset index — a bug that cost a full debugging pass.
- Per-frame index with keyframe bit.
- Per-frame demux: N audio packets (each a 4-byte size + 4-byte
  sample_count + payload) followed by the video bitstream.

### Video
- Bitstream reader: LSB-first reads, 32-bit word alignment, 29-bit
  float reads (audio).
- 16 prebuilt Huffman trees via flat peek+lookup tables; per-bundle
  tree specifiers (identity / explicit / shuffle-merge).
- 9-bundle layer (BLOCK_TYPES, SUB_BLOCK_TYPES, COLORS, PATTERN,
  X_OFF, Y_OFF, INTRA_DC, INTER_DC, PATTERN_LENGTHS) with per-bundle
  buffer sizing, strip-by-strip refill, and the correct
  reset-vs-fill ordering quirk.
- All 10 block types: SKIP, SCALED (FILL/RAW/PATTERN/INTRA/RUN
  sub-variants), MOTION, RUN, RESIDUE, INTRA, FILL, INTER, PATTERN,
  RAW.
- DCT coefficient decoder (the bit-plane op-stack walker) + the Bink
  8x8 integer IDCT with the real rotation constants and inline
  per-column dequantization.
- Luma-plane size prefix handling for BIK rev 'h'+; chroma planes
  decode from the remainder.
- YCbCr→BGRA composition (chroma stored Cr-then-Cb).

### Audio
- Interleaved multi-channel inverse real DFT (one transform across
  all channels per block).
- Per-band log-scale quantizers, run-length coefficient unpacking,
  per-band dequantize.
- Triangular-window overlap-add across blocks; multi-block packets.

## Licensing

- Algorithmic code (`bink1_decoder.c`) is original, written from the
  Multimedia Wiki format description.
- Fixed numeric tables (`bink1_tables.c`) are ported from
  Helco/bonkdec (MIT); the upstream MIT notice is preserved in that
  file and applies to those values.
- No code is derived from FFmpeg (LGPL) or ScummVM (GPL).

## Known limitations / follow-ups

- **DCT-mode audio** (`is_dct` track flag) falls back to the RDFT
  path. Arcanum's cutscenes are all RDFT, so this is untested; a
  DCT-IV transform would be needed for newer Bink Audio streams.
- **Alpha plane** (BIK streams with `has_alpha`) is not decoded.
  Arcanum's cutscenes have no alpha.
- **Half-pel motion compensation** is integer-pel only. The
  reference output matched on the tested files, so Arcanum's encoder
  apparently doesn't use half-pel here, but other Bink1 content
  might.
- The inverse RDFT uses a simple radix-2 FFT (O(N log N)) rather
  than a split-radix; fine for 4096-point audio frames but not the
  fastest possible.

## How to test

```bash
# Build:
cmake --build --preset macos-release

# Run with the direct-Bink path enabled:
ARCANUM_BINK_DIRECT=1 ~/Applications/Arcanum/<branch>.app/Contents/MacOS/arcanum-ce

# Standalone decode test (dumps a PPM frame + optional PCM audio):
clang -O2 -I include -I src test/test_bink1.c src/bink1_decoder.c \
    src/bink1_tables.c -o /tmp/test_bink1 -lm
/tmp/test_bink1 path/to/movie.bik 60         # decodes 60 frames, dumps frame 59
DUMP_AUDIO=1 /tmp/test_bink1 path/to/movie.bik 60   # also dumps /tmp/my_audio.pcm
```

With `ARCANUM_BINK_DIRECT` unset, playback falls through to the
AVI/MJPEG path as before.
