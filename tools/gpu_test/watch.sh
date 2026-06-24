#!/usr/bin/env bash
#
# VISIBLE smooth-scroll + zoom demo. Launches the (Perf GPU Accel) build in the
# FOREGROUND and drives a watchable scene: it SMOOTHLY scrolls (many small steps,
# not a teleport) at z=1.0, z=0.5, and z=2.0, with zoom in/out transitions between.
#
# (run.sh = headless measurement harness, `open -g` background. This one foregrounds
#  it and dismisses the menu so you can actually watch.)
#
# Usage:
#   tools/gpu_test/watch.sh [SAVE] [VOIDFADE]
#     SAVE      save slot (default Slot0015 = Town-Outdoor-Nighttime)
#     VOIDFADE  1 = ARCANUM_OPT_VOIDFADE (near-zero void-fade cost)
#
# Towns: Slot0015 (night, many lights), Slot0013 (Town), Slot0014 (indoor day).
set -uo pipefail

SAVE="${1:-Slot0015}"
VF="${2:-}"
APP="${APP:-$HOME/Applications/Arcanum/Arcanum Community Edition (Perf GPU Accel).app}"
BIN="$APP/Contents/MacOS/arcanum-ce"
CMD=/tmp/arcanum-watch-cmd.txt
DBG=/tmp/arcanum-debug.log
[[ -x "$BIN" ]] || { echo "ERROR: not built/deployed: $BIN" >&2; exit 2; }

# smooth DX DY STEPS : emit STEPS small scrollby's (DX/DY px each, ~1 frame apart)
# so the camera glides instead of teleporting. ~20px/frame ≈ real edge-scroll speed.
smooth() { local n; for ((n = 0; n < $3; n++)); do echo "scrollby $1 $2"; echo "wait 1"; done; }

gen_cmd() {
  echo "loadsave $SAVE"; echo "wait 150"
  # --- smooth scroll at z=1.0 ---
  smooth 20 0 36; smooth 0 20 30; smooth -20 -12 36
  echo "setzoom 0.5"; echo "wait 120"
  # --- smooth scroll at z=0.5 (the dense zoomed-out case) ---
  smooth 22 0 40; smooth 0 22 34; smooth -22 12 40; smooth 14 -14 30
  echo "setzoom 1.0"; echo "wait 80"
  echo "setzoom 2.0"; echo "wait 90"
  # --- smooth scroll at z=2.0 (zoomed in) ---
  smooth 18 0 30; smooth 0 -18 26; smooth -18 14 30
  echo "setzoom 1.0"; echo "wait 90"
  echo "quit"
}

pkill -9 -f "Perf GPU Accel" 2>/dev/null || true
sleep 1
rm -f "$DBG"
gen_cmd > "$CMD"

echo "==> watch (smooth scroll + zoom): $SAVE   voidfade=${VF:-off}"
echo "    window comes to the front; scrolls smoothly at z=1.0 / 0.5 / 2.0, then quits"
[[ -n "$VF" ]] && export ARCANUM_OPT_VOIDFADE="$VF"
ARCANUM_GPU_CMD="$CMD" open "$APP" --args -window -gpucmd:"$CMD"

for i in $(seq 1 120); do
  pgrep -f "Perf GPU Accel" >/dev/null || { echo "    done after ${i}s"; break; }
  sleep 1
done
pkill -9 -f "Perf GPU Accel" 2>/dev/null || true
