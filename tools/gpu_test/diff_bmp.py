#!/usr/bin/env python3
"""
GPU vs software render diff for arcanum-ce.

Usage:
    diff_bmp.py <reference.bmp> <candidate.bmp> [--out-dir DIR]

Reads both BMPs, reports per-pixel difference stats, and writes a heatmap
PNG (`diff_heatmap.png`) and an "obvious bug regions" PNG (`diff_regions.png`)
to OUT_DIR (default /tmp). Exit code 0 if frames are pixel-identical, 1 if
any difference (always print stats; the bug-region image is only written
when meaningful differences exist).

Stats reported:
  - total pixels
  - differing pixels (count + %)
  - max absolute channel delta
  - mean delta over differing pixels
  - largest connected differing region (bbox + pixel count) -- the
    "biggest visible bug" locator
"""
import sys
import os
from pathlib import Path

try:
    from PIL import Image, ImageChops
    import numpy as np
except ImportError as e:
    print(f"diff_bmp.py: missing dependency: {e}", file=sys.stderr)
    print("  pip3 install pillow numpy", file=sys.stderr)
    sys.exit(2)


def parse_args():
    args = sys.argv[1:]
    out_dir = "/tmp"
    paths = []
    i = 0
    while i < len(args):
        if args[i] == "--out-dir":
            out_dir = args[i + 1]
            i += 2
        else:
            paths.append(args[i])
            i += 1
    if len(paths) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    return Path(paths[0]), Path(paths[1]), Path(out_dir)


def find_largest_region(mask, min_area=64):
    """Find the largest connected differing region. Returns (bbox, area)
    or (None, 0). Uses scipy if available, falls back to a flood-fill
    over the differing pixels."""
    try:
        from scipy.ndimage import label, find_objects
        lbl, n = label(mask)
        if n == 0:
            return None, 0
        sizes = np.bincount(lbl.ravel())[1:]  # skip background
        biggest = int(np.argmax(sizes)) + 1
        if sizes[biggest - 1] < min_area:
            return None, 0
        slices = find_objects(lbl == biggest)
        if not slices or slices[0] is None:
            return None, 0
        y_sl, x_sl = slices[0]
        return (x_sl.start, y_sl.start, x_sl.stop, y_sl.stop), int(sizes[biggest - 1])
    except ImportError:
        # No scipy. Approximate with the bbox of all differing pixels >= min_area.
        ys, xs = np.where(mask)
        if len(xs) < min_area:
            return None, 0
        return (int(xs.min()), int(ys.min()), int(xs.max()) + 1,
                int(ys.max()) + 1), int(len(xs))


def main():
    ref_path, cand_path, out_dir = parse_args()
    out_dir.mkdir(parents=True, exist_ok=True)

    if not ref_path.exists():
        print(f"FAIL: reference not found: {ref_path}", file=sys.stderr)
        return 2
    if not cand_path.exists():
        print(f"FAIL: candidate not found: {cand_path}", file=sys.stderr)
        return 2

    ref = Image.open(ref_path).convert("RGB")
    cand = Image.open(cand_path).convert("RGB")
    if ref.size != cand.size:
        print(f"FAIL: size mismatch ref={ref.size} cand={cand.size}", file=sys.stderr)
        return 2

    r = np.asarray(ref, dtype=np.int16)
    c = np.asarray(cand, dtype=np.int16)
    delta = np.abs(r - c)            # H x W x 3
    per_pixel = delta.max(axis=2)    # max channel delta per pixel
    differ = per_pixel > 0
    n_diff = int(differ.sum())
    n_total = int(per_pixel.size)
    pct = 100.0 * n_diff / n_total

    print(f"reference : {ref_path}")
    print(f"candidate : {cand_path}")
    print(f"size      : {ref.size[0]} x {ref.size[1]} = {n_total} px")
    print(f"differing : {n_diff} px ({pct:.2f}%)")
    if n_diff == 0:
        print("PASS: pixel-identical")
        return 0

    print(f"max delta : {int(per_pixel.max())}")
    print(f"mean delta (over differing): {float(delta.sum()) / max(1, n_diff * 3):.2f}")

    # Heatmap: visualize delta intensity. White = identical, red = big.
    heat = np.zeros((*per_pixel.shape, 3), dtype=np.uint8)
    norm = (per_pixel * 255 // max(1, int(per_pixel.max()))).astype(np.uint8)
    heat[..., 0] = norm
    heat[..., 1] = 255 - norm
    heat[..., 2] = 255 - norm
    heat_path = out_dir / "diff_heatmap.png"
    Image.fromarray(heat).save(heat_path)
    print(f"heatmap   : {heat_path}")

    # Side-by-side overlay: candidate with differing pixels marked red.
    overlay = np.asarray(cand, dtype=np.uint8).copy()
    overlay[differ] = [255, 0, 0]
    over_path = out_dir / "diff_overlay.png"
    Image.fromarray(overlay).save(over_path)
    print(f"overlay   : {over_path}")

    # Locate the biggest connected diff region (the obvious bug).
    bbox, area = find_largest_region(differ)
    if bbox is not None:
        x0, y0, x1, y1 = bbox
        print(f"biggest region: ({x0},{y0})-({x1},{y1}) "
              f"= {x1 - x0}x{y1 - y0} ({area} px)")
        # Crop both for direct comparison.
        ref.crop(bbox).save(out_dir / "diff_region_ref.png")
        cand.crop(bbox).save(out_dir / "diff_region_cand.png")
        print(f"region crops: {out_dir}/diff_region_{{ref,cand}}.png")

    print("FAIL: differences detected")
    return 1


if __name__ == "__main__":
    sys.exit(main())
