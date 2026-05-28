# Bink1 native decoder — implementation status

## What works now

- **Container parser** (`parse_header`, `parse_audio_tracks`,
  `parse_frame_index`): reads the BIK[a-i] magic, video header
  (width/height/frames/fps/flags), per-track audio metadata
  (sample_rate, channels, stereo+DCT flags), frame offset/keyframe
  index. Validated against the real `SierraLogo.bik` shipped with
  Arcanum: reports `BIKi, 800x400, 201 frames, 27.8fps, 1 audio
  track @ 44100Hz stereo RDFT` — exactly matching what ffmpeg reads
  from the same file.

- **Per-frame demuxer** (`load_frame`): seeks to a frame, reads its
  bytes, splits the N audio packets + remaining video bitstream.

- **Bitstream reader** (`BitReader`): little-endian read / peek /
  skip / byte-align.

- **Plane management**: Y/U/V allocation at the right resolutions
  (4:2:0 chroma subsampling) with current+previous-frame
  double-buffering for motion-reference blocks.

- **YCbCr → BGRA composer**: standard BT.601 fixed-point conversion
  matching the MJPEG path so colors are consistent.

- **HBINK dispatcher integration** (`bink_compat.c`): both backends
  carry a `BinkCompatKind` tag as their second struct member so the
  public Bink* dispatch functions route correctly to either path.
  `BinkOpen` checks `ARCANUM_BINK_DIRECT=1` and tries the Bink1
  path before falling through to AVI sidecar resolution. Confirmed
  the dispatch routes correctly and the decoder doesn't crash on
  open or frame iteration.

- **BINKSND wiring** (`bink1_push_audio`): mirrors the AVI backend so
  decoded PCM samples flow through the same SDL3_mixer plumbing.

- **Bink Huffman tree builder** (`bink_huff_build`,
  `bink_huff_read_tree`): canonical-Huffman lookup from per-symbol
  code lengths, with the 16 prebuilt code-length tables and the
  per-bundle permutation parser. Tree storage is allocated per
  bundle so it survives across frames (the per-bundle "reset" bit
  decides whether to re-read or reuse).

- **Bink 8x8 integer IDCT** (`bink_idct_block`): row + column
  butterfly with Bink's scaled rotation constant (181, >>7) and
  6-bit final scaling. Standard Bink reconstruction transform;
  operates on already-dequantised coefficients.

- **Block decoders** for 7 of 10 types:
  - `SKIP`: copy from previous frame
  - `FILL`: solid-color 8x8
  - `MOTION`: integer-pixel motion comp from previous frame
    (half-pel filtering deferred)
  - `RAW`: 64 literal bytes
  - `PATTERN`: 2-color + 8-byte per-row mask
  - `INTRA`: full IDCT + level shift + clip
  - `INTER`: motion comp + IDCT residue overlay

- **Bundle framework** (`Bundle`, `bundle_alloc`,
  `bundle_refill_bytes`, `bundle_pop_byte`): per-bundle Huffman
  tree + lazily-filled entry buffer. The `decode_plane` walk
  consumes BLOCK_TYPES first, then dispatches to specific block
  decoders that pull additional bundle entries as needed.

- **Per-plane bundle persistence**: bundle state (including
  Huffman trees) is forward-declared at the top of the file and
  attached to `Bink1Decoder` via the `BundleData` / `BinkHuffTreeData`
  typedefs, so the Bink1Decoder struct stays well-typed without
  the cross-section void-pointer hack.

## What is still stubbed (will produce incorrect video output)

- **Length-prefix decoding** (`bink_read_length`): currently reads
  7 raw bits. The real scheme is logspan / variable-length per
  bundle. Until this matches the reference, every bundle chunk
  size is wrong and the bit stream desyncs after the first chunk.

- **Per-bundle entry encoding**: each bundle has its own value
  encoding (signed/unsigned, byte vs nibble pair, etc.). The
  current code treats every bundle as plain byte-sequence Huffman.

- **INTRA / INTER DC bundles**: the `INTRA_DC` and `INTER_DC`
  bundles carry the DC coefficient for each block, separately
  from the AC coefficients. Not yet wired into the INTRA / INTER
  decoders, so those blocks decode with DC=0 (very dark output).

- **AC coefficient decoder**: INTRA and INTER blocks need 63 AC
  coefficients per block, decoded via run-length zig-zag (similar
  to JPEG end-of-block coding but with Bink's specific Huffman
  tables). Not implemented; INTRA/INTER blocks currently call
  `decode_block_intra` / `decode_block_inter` with a zeroed
  coefficient array.

- **`RUN`, `RESIDUE`, `SCALED` block types**: fall through to
  SKIP. Each needs its own bundle reads and pixel writes.

- **Half-pel motion compensation**: `decode_block_motion` rounds
  to integer pixels. Half-pel mode (motion vector bit 0 set
  selects 1/2-pixel offsets via a 4-tap blend) not implemented.

- **Bink Audio coefficient decoder**: `audio_decode_frame` zeros
  the coefficient buffer, producing silence. Per-frame layout:
  4-byte sync, per-channel { per-band quantization step (8 bits),
  per-coefficient signed magnitude (variable-length) }, then
  inverse RDFT (older streams) or DCT-IV (newer streams), then
  overlap-add against the previous frame's tail.

- **Sub-block expansion**: the SUB_BLOCK_TYPES bundle splits an
  8x8 macroblock into 4x4 sub-blocks for some types. Not yet
  parsed; affects SCALED block accuracy.

## Why this is still not "usable" for playback

Even though the structural code compiles and runs without crashing,
the chain of TODO items above means the output is visually broken
in multiple ways at once:

1. The length-prefix scheme is a placeholder, so the *very first*
   chunk read from any bundle uses a value that has no relationship
   to what the encoder wrote. Every subsequent bit read is then
   misaligned.

2. With misaligned bits, block types come out as nonsense values,
   so the dispatch picks wrong decoders.

3. Even when the dispatch is right by accident, the DC coefficient
   isn't decoded, so DCT blocks render as constant-zero residue.

To break this chain, the next critical piece is the length-prefix
decoder and per-bundle entry encoding — those alone unlock real
testing because they get the bit stream sync correct. Once the
parser is in sync, individual block decoders can be debugged
against the reference output.

## Estimated remaining effort to "usable"

Realistic, with the ability to diff frame-by-frame against a
known-good FFmpeg decode of the same file:

- Length-prefix + per-bundle entry encoding: **~2-3 days**
- INTRA/INTER DC bundle + AC coefficient decoder: **~2-3 days**
- Remaining block types (RUN, RESIDUE, SCALED, half-pel): **~3-4 days**
- Bink Audio coefficient + transform + overlap-add: **~3-5 days**
- Edge cases (per-Bink-version quirks, large keyframe gaps,
  off-by-one in block walks at right/bottom edges): **~1-2 weeks**
- Integration testing against all 8 Arcanum cutscenes: **~3-5 days**

Total: **3-5 weeks of focused codec engineering** with a reference
decoder available for side-by-side comparison.

## How to test this branch right now

```bash
# Build:
cmake --build --preset macos-release

# Run with the direct-Bink path enabled:
ARCANUM_BINK_DIRECT=1 ~/Applications/Arcanum/affectionate-kirch.app/Contents/MacOS/arcanum-ce

# Or capture stderr to watch the container parse:
ARCANUM_BINK_DIRECT=1 ~/Applications/Arcanum/affectionate-kirch.app/Contents/MacOS/arcanum-ce \
    2>&1 | grep bink_compat
```

You should see a line like:

```
bink_compat: bink1_open data/TIGCache/movies/SierraLogo.bik:
    BIKi, 800x400, 201 frames, 35970 us/frame, 1 audio tracks
    (rate=44100 ch=2 RDFT)
```

The cutscene plays through to natural EOF without crashing, but the
visible output is corrupt (mostly noise / blocky garbage) because
the bit-level parser is still in placeholder mode. With the env var
unset, playback falls through to the AVI/MJPEG path as before.

## Recommended next steps for whoever picks this up

1. Stand up a small offline test harness: open the same .bik file
   with this decoder AND with FFmpeg's `binkdec.c`, dump both
   decoded frames as PNGs, diff them.
2. Fix `bink_read_length` first — that single function unblocks
   real testing of every bundle downstream.
3. Then per-bundle entry encoding (each bundle is a separate,
   bounded piece of work).
4. DC bundle, AC coefficient walk, and the remaining block types
   follow in dependency order.
5. Audio decoder is mostly independent of video, and can be
   tackled in parallel by a different contributor.

The structure of the decoder (per-plane bundles, lazy refill, block
dispatch table, persistent Huffman trees) is set up to support all
of this cleanly — the work is filling in the bit-level details, not
restructuring the framework.
