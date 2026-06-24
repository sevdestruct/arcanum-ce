#!/usr/bin/env bash
#
# VISIBLE harness — launches the (Perf GPU Accel) build in the FOREGROUND and
# drives it through the gpucmd channel so you can WATCH it work: the loadsave
# dismisses the main menu, then it zooms out and scrolls around on its own.
#
# (The measurement harness, run.sh, launches with `open -g` = background/no-focus,
#  which leaves the menu on top from the viewer's side. This one foregrounds it.)
#
# Usage:
#   tools/gpu_test/watch.sh [SAVE] [VOIDFADE]
#     SAVE      save slot to load (default Slot0015 = Town-Outdoor-Nighttime)
#     VOIDFADE  1 = enable the void-edge-fade dedup opt (ARCANUM_OPT_VOIDFADE)
#
# Town saves available: Slot0013 (Town), Slot0015 (Town night, many lights),
#   Slot0014 (Town indoor day), Slot0012 (Blimp).
set -uo pipefail

SAVE="${1:-Slot0015}"
VF="${2:-}"
APP="${APP:-$HOME/Applications/Arcanum/Arcanum Community Edition (Perf GPU Accel).app}"
BIN="$APP/Contents/MacOS/arcanum-ce"
CMD=/tmp/arcanum-watch-cmd.txt
DBG=/tmp/arcanum-debug.log

[[ -x "$BIN" ]] || { echo "ERROR: not built/deployed: $BIN" >&2; exit 2; }

pkill -9 -f "Perf GPU Accel" 2>/dev/null || true
sleep 1
rm -f "$DBG"

# Slow, watchable pacing (waits are in frames; ~60fps). Each step is visible.
{
  echo "loadsave $SAVE"; echo "wait 150"        # menu dismisses, town loads (~2.5s)
  echo "setzoom 0.5";    echo "wait 150"        # zoom all the way out
  echo "scrollby 450 0"; echo "wait 80"         # scroll right into fresh area
  echo "scrollby 0 450"; echo "wait 80"         # scroll down
  echo "scrollby 650 650"; echo "wait 80"       # scroll diagonally
  echo "scrollby -700 -200"; echo "wait 80"
  echo "setzoom 1.0";    echo "wait 150"        # zoom back in
  echo "quit"
} > "$CMD"

echo "==> watching: $SAVE   voidfade=${VF:-off}"
echo "    (the Arcanum window will come to the front; it drives itself, then quits)"
if [[ -n "$VF" ]]; then export ARCANUM_OPT_VOIDFADE="$VF"; fi

# Foreground launch (no -g) so the window is visible + focused (not app-napped).
ARCANUM_GPU_CMD="$CMD" open "$APP" --args -window -gpucmd:"$CMD"

# Wait for it to finish on its own (the script ends with `quit`).
for i in $(seq 1 90); do
  pgrep -f "Perf GPU Accel" >/dev/null || { echo "    done after ${i}s"; break; }
  sleep 1
done
pkill -9 -f "Perf GPU Accel" 2>/dev/null || true
