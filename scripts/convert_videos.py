#!/usr/bin/env python3
"""
convert_videos.py — extract Arcanum .bik videos (from DAT archives or
loose files) and convert them to MJPEG-in-AVI sidecars that the
cross-platform video backend in src/game/video_convert.c can play.

Why this script exists alongside the in-game "Convert Now" modal:

  * The in-game modal uses tig_file_list_create which only enumerates
    loose .bik files (about 2 in the stock install — the startup
    logos). Most cutscenes live inside arcanum1.dat / arcanum2.dat /
    arcanum3.dat / arcanum4.dat / tig.dat and aren't visible without a
    DAT parser.
  * This script bundles a small DAT-format reader (lifted from the
    project's existing arcanum-scripts/export_videos.py) so it can
    enumerate AND extract every cutscene the game ships with, then
    feed each one to ffmpeg in a single batch.

Output layout: <game_dir>/data/videos/<basename>.avi
  * This is the override-search location that gmovie.c probes BEFORE
    falling back to the original .bik path, so the conversion result
    is found at runtime on any platform.

Usage:
    # Auto-detect a typical install location:
    python3 scripts/convert_videos.py

    # Or point at an explicit game directory:
    python3 scripts/convert_videos.py --game-dir "/Users/.../Arcanum"

Requirements:
    ffmpeg in PATH. Install with one of:
        macOS:   brew install ffmpeg
        Linux:   sudo apt install ffmpeg   (or dnf install / pacman -S)
        Windows: download from https://ffmpeg.org/download.html

The encoder settings exactly match what the in-game flow uses, so
files produced by either path are interchangeable.
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from dataclasses import dataclass
from pathlib import Path


FOURCC_DAT = 0x44415420
FOURCC_DAT1 = 0x44415431

DEFAULT_GAME_DIR_CANDIDATES = (
    # Plain "Arcanum" comes first — that's the typical active install
    # path. Vanilla / pristine copies are checked as fallbacks so the
    # script doesn't accidentally write into a backup directory while
    # the real install is somewhere else.
    Path.home() / "Applications" / "Arcanum",
    Path.home() / "Applications" / "Arcanum (Vanilla)",
    Path.home() / "Games" / "Arcanum",
    Path("/Applications/Arcanum"),
)

# Subdirectories that cache already-extracted files — skip them.
SKIP_SUBDIRS = {"data/tigcache", "data\\tigcache"}

# ffmpeg arguments matching src/game/video_convert.c convert_one_file().
# Keep these in lockstep — files produced by either path must be
# bit-identical so users can mix CLI + in-game conversion.
FFMPEG_ENCODE_ARGS = [
    "-c:v", "mjpeg",
    "-q:v", "4",
    "-pix_fmt", "yuvj420p",
    "-c:a", "pcm_s16le",
    # Force the container explicitly. We write to a "*.avi.tmp" output
    # and atomically rename on success; ffmpeg can't deduce the muxer
    # from the `.tmp` suffix and would otherwise fail with "Unable to
    # choose an output format" / "Invalid argument".
    "-f", "avi",
]


# ---------------------------------------------------------------------------
# DAT archive reader (lifted from arcanum-scripts/export_videos.py).
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class DatEntry:
    path: str
    flags: int
    size: int
    compressed_size: int
    offset: int


class DatArchive:
    """Read-only Arcanum DAT/DAT1 archive parser.

    The format: trailing footer encodes the position of an entry table
    near the end of the file. Each entry has a name, flags (raw vs zlib),
    sizes, and an absolute byte offset into the archive body.
    """

    def __init__(self, path: Path) -> None:
        self.path = path
        self.entries = self._read_entries()

    def _read_entries(self) -> dict[str, DatEntry]:
        with self.path.open("rb") as stream:
            stream.seek(-12, os.SEEK_END)
            fourcc, _name_table_size, entry_table_offset = struct.unpack(
                "<III", stream.read(12)
            )
            if fourcc == FOURCC_DAT1:
                stream.seek(-24, os.SEEK_END)
                stream.read(16)
                _fourcc, entry_table_offset = struct.unpack("<II", stream.read(8))
            elif fourcc != FOURCC_DAT:
                raise ValueError(f"{self.path} is not a DAT archive")

            stream.seek(-4 - entry_table_offset, os.SEEK_END)
            entry_table_size = struct.unpack("<I", stream.read(4))[0]

            entry_table_pos = stream.tell()
            stream.seek(0, os.SEEK_END)
            archive_size = stream.tell()
            stream.seek(entry_table_pos)

            base_offset = archive_size - entry_table_size - entry_table_offset
            entries_count = struct.unpack("<I", stream.read(4))[0]

            entries: dict[str, DatEntry] = {}
            for _ in range(entries_count):
                name_size = struct.unpack("<I", stream.read(4))[0]
                raw_name = stream.read(name_size)
                # DAT paths are CP-1252 with backslashes. Preserve case
                # in the stored path (so the output .avi inherits the
                # original casing like "SierraLogo") but use a lower-
                # cased key for the lookup map (the engine is case-
                # insensitive at the call sites).
                name = (
                    raw_name.split(b"\0", 1)[0]
                    .decode("cp1252")
                    .replace("\\", "/")
                )
                stream.seek(4, os.SEEK_CUR)
                flags, size, compressed_size, offset = struct.unpack(
                    "<IIII", stream.read(16)
                )
                entries[name.lower()] = DatEntry(
                    path=name,
                    flags=flags,
                    size=size,
                    compressed_size=compressed_size,
                    offset=offset + base_offset,
                )

        return entries

    def read_entry(self, path: str) -> bytes:
        entry = self.entries[path.lower()]
        with self.path.open("rb") as stream:
            stream.seek(entry.offset)
            if entry.flags & 0x01:
                return stream.read(entry.size)
            if entry.flags & 0x02:
                return zlib.decompress(stream.read(entry.compressed_size))
        raise ValueError(
            f"Unsupported DAT entry flags {entry.flags:#x} for {path}"
        )

    def video_entries(self) -> list[DatEntry]:
        return [
            e for e in self.entries.values()
            if any(e.path.lower().endswith(ext) for ext in CONVERTIBLE_EXTS)
        ]


# Video container extensions the engine cannot play directly. .avi is
# excluded because it IS the target format. Mirrors the in-game scan
# (src/game/gmovie.c::is_convertible_video_filename).
CONVERTIBLE_EXTS = (
    ".bik", ".mp4", ".m4v", ".mov", ".mkv", ".webm", ".flv", ".wmv",
)


# ---------------------------------------------------------------------------
# Discovery + collection
# ---------------------------------------------------------------------------


def discover_game_dir(explicit: str | None) -> Path:
    if explicit is not None:
        game_dir = Path(explicit).expanduser().resolve()
        if not (game_dir / "arcanum1.dat").exists():
            raise FileNotFoundError(
                f"Could not find arcanum1.dat in {game_dir}"
            )
        return game_dir

    for candidate in DEFAULT_GAME_DIR_CANDIDATES:
        if (candidate / "arcanum1.dat").exists():
            return candidate

    raise FileNotFoundError(
        "Could not automatically locate an Arcanum install. "
        "Pass --game-dir explicitly."
    )


def discover_archives(game_dir: Path) -> list[DatArchive]:
    archive_paths: list[Path] = []

    modules_dir = game_dir / "modules"
    if modules_dir.exists():
        # Patches first (higher priority overlays), then base modules.
        archive_paths.extend(sorted(modules_dir.glob("*.PATCH*"), reverse=True))
        archive_paths.extend(sorted(modules_dir.glob("*.dat")))

    for name in (
        "arcanum4.dat",
        "arcanum3.dat",
        "arcanum2.dat",
        "arcanum1.dat",
        "tig.dat",
    ):
        candidate = game_dir / name
        if candidate.exists():
            archive_paths.append(candidate)

    seen: set[Path] = set()
    unique_paths: list[Path] = []
    for path in archive_paths:
        resolved = path.resolve()
        if resolved not in seen:
            seen.add(resolved)
            unique_paths.append(resolved)

    return [DatArchive(path) for path in unique_paths]


def is_cache_path(game_dir: Path, path: Path) -> bool:
    try:
        rel = path.relative_to(game_dir)
    except ValueError:
        return False
    parts_lower = "/".join(rel.parts).lower()
    return any(skip in parts_lower for skip in SKIP_SUBDIRS)


def collect_videos(
    archives: list[DatArchive], game_dir: Path
) -> list[tuple[str, bytes]]:
    """De-duplicated list of (relative_path, raw_bytes). Earlier DAT
    archives win over later ones on collision (matches engine load
    order); loose files come last.

    Includes every video file the engine can't play directly --
    .bik cutscenes from the DAT archives, plus any .mp4 / .mov /
    .mkv / .webm replacement files the user has dropped anywhere
    under the game tree."""
    seen_keys: set[str] = set()
    videos: list[tuple[str, bytes]] = []

    for archive in archives:
        for entry in archive.video_entries():
            key = entry.path.lower()
            if key not in seen_keys:
                seen_keys.add(key)
                data = archive.read_entry(entry.path)
                videos.append((entry.path, data))

    # Recursive loose-file scan across the whole game directory.
    for ext in CONVERTIBLE_EXTS:
        for path in sorted(game_dir.rglob(f"*{ext}")):
            if is_cache_path(game_dir, path):
                continue
            rel = path.relative_to(game_dir).as_posix()
            key = rel.lower()
            if key not in seen_keys:
                seen_keys.add(key)
                videos.append((rel, path.read_bytes()))

    return videos


# ---------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------


def find_ffmpeg() -> str | None:
    return shutil.which("ffmpeg")


def basename_no_ext(path: str) -> str:
    return Path(path).stem


def convert_bytes_to_avi(
    ffmpeg: str, bik_bytes: bytes, dst_avi: Path
) -> tuple[bool, str]:
    """Pipe bik_bytes through ffmpeg, write a sibling .tmp, rename to
    dst_avi on success."""
    dst_avi.parent.mkdir(parents=True, exist_ok=True)
    dst_tmp = dst_avi.with_suffix(dst_avi.suffix + ".tmp")

    # Use a temp .bik so ffmpeg can seek (-i - via stdin is fine for
    # most formats but Bink demands seeks for the index).
    with tempfile.NamedTemporaryFile(suffix=".bik", delete=False) as tmp:
        tmp.write(bik_bytes)
        tmp_path = tmp.name

    try:
        cmd = [
            ffmpeg,
            "-nostdin",
            "-loglevel", "error",
            "-stats",
            "-y",
            "-i", tmp_path,
            *FFMPEG_ENCODE_ARGS,
            str(dst_tmp),
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            try:
                dst_tmp.unlink()
            except OSError:
                pass
            tail = (proc.stderr or proc.stdout or "").strip().splitlines()
            return False, (tail[-1] if tail else "ffmpeg failed")
        if dst_avi.exists():
            dst_avi.unlink()
        dst_tmp.rename(dst_avi)
        return True, ""
    finally:
        os.unlink(tmp_path)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--game-dir",
        help="Path to the Arcanum install directory (auto-detected if "
             "omitted).",
    )
    parser.add_argument(
        "--output-subdir",
        default="data/videos",
        help="Subdirectory under --game-dir where .avi sidecars are "
             "written (default: data/videos — the path gmovie.c probes "
             "first at runtime).",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Re-convert files even if their .avi already exists.",
    )
    args = parser.parse_args(argv)

    ffmpeg = find_ffmpeg()
    if ffmpeg is None:
        print(
            "error: ffmpeg not found in PATH.\n"
            "  macOS:   brew install ffmpeg\n"
            "  Linux:   sudo apt install ffmpeg\n"
            "  Windows: https://ffmpeg.org/download.html",
            file=sys.stderr,
        )
        return 2

    try:
        game_dir = discover_game_dir(args.game_dir)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    output_dir = (game_dir / args.output_subdir).resolve()

    print(f"Game dir   : {game_dir}")
    print(f"ffmpeg     : {ffmpeg}")
    print(f"Output dir : {output_dir}")
    print()

    archives = discover_archives(game_dir)
    print(f"Archives   : {len(archives)} DAT file(s)")
    for a in archives:
        print(f"  - {a.path.name}: {len(a.video_entries())} .bik entries")
    print()

    videos = collect_videos(archives, game_dir)
    if not videos:
        print("No .bik video files found.", file=sys.stderr)
        return 1

    todo: list[tuple[str, bytes, Path]] = []
    skipped = 0
    for rel, data in videos:
        stem = basename_no_ext(rel)
        dst = output_dir / f"{stem}.avi"
        if not args.force and dst.exists():
            skipped += 1
            continue
        todo.append((rel, data, dst))

    print(f"Found {len(videos)} video(s); {len(todo)} need conversion "
          f"({skipped} already up-to-date).")
    print()

    if not todo:
        return 0

    converted = 0
    failed = 0
    start = time.time()
    for i, (rel, data, dst) in enumerate(todo, 1):
        size_mb = len(data) / (1024 * 1024)
        print(f"[{i}/{len(todo)}] {rel}  ({size_mb:.1f} MiB)  ->  "
              f"{dst.relative_to(game_dir)}",
              end=" ... ", flush=True)
        ok, err = convert_bytes_to_avi(ffmpeg, data, dst)
        if ok:
            print("ok")
            converted += 1
        else:
            print(f"FAILED: {err}")
            failed += 1

    elapsed = time.time() - start
    print()
    print(f"done in {elapsed:.1f}s: {converted} converted, "
          f"{failed} failed, {skipped} already up-to-date")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
