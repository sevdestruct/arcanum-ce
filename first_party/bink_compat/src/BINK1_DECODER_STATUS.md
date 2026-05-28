# Bink1 native decoder — implementation status

## Summary

The Bink1 decoder is the **default** video path: a stock Arcanum
install plays its `.bik` cutscenes (and custom `.bik` replacements)
directly, with no FFmpeg dependency and no pre-conversion step. Video
and RDFT-mode audio are complete and verified against reference
decoders; DCT-mode audio (newer RAD encodes) is the one remaining
piece, in progress.

Set `ARCANUM_BINK_DIRECT=0` to force the legacy MJPEG/AVI sidecar
path instead (A/B comparison, or to dodge a decoder bug on a
specific file).

## Verification

The decoder was validated against
[Helco/bonkdec](https://github.com/Helco/bonkdec) (an MIT-licensed
clean-room Bink1 decoder) and FFmpeg, both run locally on Arcanum's
own cutscene files plus fresh RAD Video Tools encodes (1080p):

- **Video luma plane**: byte-identical to bonkdec.
- **Video full color (RGB)**: matches FFmpeg within ±1 LSB across the
  frame (mean abs diff ~0.04), including correct true-black letterbox
  bars — see the colour-range note below.
- **RDFT audio**: 100% of decoded samples match bonkdec within ±1 LSB
  (float rounding between our radix-2 FFT and bonkdec's Ooura
  split-radix FFT), and every frame stays sample-aligned.
- **DCT audio**: in progress; being verified against FFmpeg's
  decoded PCM as the oracle.

### Colour range

Bink stores luma/chroma in limited (studio) range — black is Y=16,
white Y=235. The composer applies the standard integer BT.601
limited→full-range conversion (298/256 luma gain with the -16 luma
and -128 chroma offsets), so blacks land at RGB 0 and the output
matches FFmpeg. (An earlier full-range matrix lifted blacks to a
grey 16.)

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

- **DCT-mode audio** (`is_dct` track flag, `binkaudio_dct`): in
  progress. Arcanum's original cutscenes are all RDFT, but the modern
  RAD Video Tools encoder emits DCT-mode audio, so custom RAD-encoded
  cutscenes need this. Video plays regardless; only that audio track
  is affected until the DCT path lands.
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
# Build the game (native Bink is the default video path):
cmake --build --preset macos-release
~/Applications/Arcanum/<branch>.app/Contents/MacOS/arcanum-ce
# (set ARCANUM_BINK_DIRECT=0 to force the legacy MJPEG/AVI path)

# Standalone decode verification tool (EXCLUDE_FROM_ALL CMake target):
cmake --build --preset macos-release --target bink1_verify
BV=out/build/macos/first_party/bink_compat/Release/bink1_verify
"$BV" path/to/movie.bik 60                       # PASS/PARTIAL verdict
BINK_VERIFY_PPM=/tmp/f.ppm "$BV" movie.bik 60     # dump last frame (PPM)
BINK_VERIFY_PCM=/tmp/a.pcm "$BV" movie.bik 60     # dump audio (s16 PCM)
# It also detects Bink2 (KB2) files and tells you to re-encode as Bink 1.
```
