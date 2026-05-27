# Bink1 native decoder — implementation status

## What works now

- **Container parser** (`parse_header`, `parse_audio_tracks`,
  `parse_frame_index`): reads the BIK[a-i] magic, video header,
  per-track audio metadata, and the per-frame offset/keyframe index.
  Validated against a real `SierraLogo.bik` from the shipping game
  data (800×400, 201 frames, 27.8fps, 1 audio track @ 44100Hz stereo
  RDFT — matches what `ffprobe` reports for the same file).

- **Per-frame demuxer** (`load_frame`): seeks to a frame, reads its
  bytes, splits the N audio packets + remaining video bitstream.

- **Bitstream reader** (`BitReader`): little-endian read / peek /
  skip / byte-align. Tested by exercise during header parsing.

- **Plane management**: Y/U/V allocation at the right resolutions
  with current+previous-frame double-buffering for motion reference.

- **YCbCr → BGRA composer**: standard BT.601 fixed-point conversion
  identical to the MJPEG path so colors match.

- **HBINK dispatcher integration** (`bink_compat.c`): both backends
  carry a `BinkCompatKind` tag as their second struct member so the
  public Bink* dispatch functions route correctly to either path.
  `BinkOpen` checks `ARCANUM_BINK_DIRECT=1` and tries the Bink1
  path before falling through to AVI sidecar resolution.

- **BINKSND wiring** (`bink1_push_audio`): mirrors the AVI backend so
  decoded PCM samples flow through the same SDL3_mixer plumbing.

## What is stubbed (and will play black-frame video / silent audio)

- **`bink_huff_read_id` / `bink_huff_init`** — returns a raw 4-bit
  value (identity tree only). Real Bink uses 16 prebuilt symbol
  permutations selected by a 4-bit tree index in the stream; the
  per-frame trees are also rebuilt from a small specifier. Replace
  this with the prebuilt-tree table + selector parser.

- **`decode_plane`** — currently defaults every 8×8 block to SKIP
  (copy previous frame). Real implementation needs:
  1. For each of the 9 bundles (BLOCK_TYPES, SUB_BLOCK_TYPES,
     COLORS, PATTERN, X_OFF, Y_OFF, INTRA_DC, INTER_DC, RUN), read
     a length-prefix and Huffman-decode that many entries.
  2. Walk blocks row-major, popping the right bundle entries based
     on the per-block BLOCK_TYPE.
  3. Reconstruct pixels per block type.

- **Block decoders** — only `decode_block_skip` and
  `decode_block_fill` exist. Missing:
  - `MOTION`: copy block from previous frame at (x+mvx, y+mvy)
  - `RUN`: 64-entry run-length + 2 palette colors
  - `RESIDUE`: MOTION + DCT residue overlay
  - `INTRA`: full DCT-coded block
  - `INTER`: DCT residue against previous frame
  - `PATTERN`: 2-color 8×8 dither pattern
  - `RAW`: 64 raw bytes
  - `SCALED`: half-res intra, doubled
  - `SUB_BLOCK_TYPES`: 4×4 sub-block expansion for some types

- **Bink-specific 8×8 IDCT** — not implemented. Bink uses its own
  scaled integer transform (NOT standard JPEG DCT). Constants are
  documented on the Multimedia Wiki.

- **Half-pel motion compensation** — not implemented. Motion
  vectors are 1/2-pel; sub-pixel positions blend four reference
  pixels.

- **Bink Audio coefficient decoder** — `audio_decode_frame` zeros
  the coefficient buffer, so output is silence. Per-frame layout:
  1. 4 bytes sync magic
  2. Per channel:
     - Per critical band: 1 byte log-scale quantization step
     - Variable-length signed magnitude per coefficient
  3. Apply inverse RDFT (older streams) or DCT-IV (newer streams)
  4. Overlap-add against the previous frame's tail

- **`bink1_decoder_decode_audio` integration with `decode_video`** —
  audio packets are demuxed by `load_frame` correctly, but
  `decode_audio` re-reads frame 0 unless `decode_video` has run
  first. Refactor so both helpers share one `ensure_frame_loaded`
  step.

## Estimated remaining effort

Roughly:
- Huffman tree builder + bundle parser: ~1 day
- Block decoders (10 types): ~3-4 days
- Bink IDCT + motion compensation: ~1-2 days
- Bink Audio coefficient + transform: ~2-3 days
- Integration testing against the 8 shipping Arcanum cutscenes: 2-3 days
- Edge cases and stream-version quirks: a week or two

Realistic total: **3-4 weeks of focused codec work** with the
ability to compare output frame-by-frame against FFmpeg's reference
decode of the same files.

## How to test this branch right now

```bash
# Build:
cmake --build --preset macos-release

# Run with the direct-Bink path enabled:
ARCANUM_BINK_DIRECT=1 ~/Applications/Arcanum/affectionate-kirch.app/Contents/MacOS/arcanum-ce

# In another terminal, tail stderr to see container parses:
log stream --predicate 'process == "arcanum-ce"' | grep bink_compat
```

You should see a line like:

```
bink_compat: bink1_open data/TIGCache/movies/SierraLogo.bik:
    BIKi, 800x400, 201 frames, 35970 us/frame, 1 audio tracks
    (rate=44100 ch=2 RDFT)
```

The cutscene window will paint as a black rectangle for the duration
of the video (since `decode_plane` defaults to SKIP and the initial
previous-frame plane is zeroed). With the env var unset, playback
falls through to the AVI/MJPEG path as before.
