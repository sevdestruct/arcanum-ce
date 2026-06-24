#!/usr/bin/env bash
#
# VISIBLE PC-walking test. Launches the (Perf GPU Accel) build FOREGROUND and makes
# the player character actually WALK around (anim_goal_run_to_tile via the `walkby`
# gpucmd). With camera-follow on, the camera scrolls to track the PC -- real movement
# + scrolling -- and we change zoom mid-walk so you see walking at z=1.0 / 0.5 / 2.0.
#
# Usage:
#   tools/gpu_test/walk.sh [SAVE] [VOIDFADE]
#     SAVE      save slot (default Slot0015 = Town-Outdoor-Nighttime)
#     VOIDFADE  1 = ARCANUM_OPT_VOIDFADE (near-zero void-fade cost)
#
# Towns: Slot0015 (night, many lights), Slot0013 (Town), Slot0014 (indoor day).
set -uo pipefail

SAVE="${1:-Slot0015}"
VF="${2:-}"
APP="${APP:-$HOME/Applications/Arcanum/Arcanum Community Edition (Perf GPU Accel).app}"
BIN="$APP/Contents/MacOS/arcanum-ce"
CMD=/tmp/arcanum-walk-cmd.txt
DBG=/tmp/arcanum-debug.log
[[ -x "$BIN" ]] || { echo "ERROR: not built/deployed: $BIN" >&2; exit 2; }

gen_cmd() {
  echo "loadsave $SAVE"; echo "wait 150"
  echo "walkby 16 12";  echo "wait 60"     # start a long walk; camera follows
  echo "setzoom 0.5";   echo "wait 90"     # zoom OUT while walking
  echo "wait 70"                            # keep walking, zoomed out
  echo "setzoom 1.0";   echo "wait 80"     # zoom back IN while walking
  echo "walkby -14 14"; echo "wait 60"
  echo "setzoom 0.6";   echo "wait 140"    # walk a stretch zoomed out
  echo "walkby 16 -12"; echo "wait 120"
  echo "setzoom 2.0";   echo "wait 60"     # zoom in
  echo "walkby -9 -11"; echo "wait 130"    # walk zoomed in
  echo "setzoom 1.0";   echo "wait 100"
  echo "walkby 10 10";  echo "wait 130"
  echo "quit"
}

pkill -9 -f "Perf GPU Accel" 2>/dev/null || true
sleep 1
rm -f "$DBG"
gen_cmd > "$CMD"

echo "==> walk test: $SAVE   voidfade=${VF:-off}"
echo "    PC walks around (camera follows -> scrolls) while zooming 1.0/0.5/2.0, then quits"
[[ -n "$VF" ]] && export ARCANUM_OPT_VOIDFADE="$VF"
ARCANUM_GPU_CMD="$CMD" open "$APP" --args -window -gpucmd:"$CMD"

for i in $(seq 1 120); do
  pgrep -f "Perf GPU Accel" >/dev/null || { echo "    done after ${i}s"; break; }
  sleep 1
done
pkill -9 -f "Perf GPU Accel" 2>/dev/null || true
