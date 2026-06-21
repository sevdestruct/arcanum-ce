#!/usr/bin/env bash
#
# Autonomous GPU vs software regression harness for arcanum-ce.
#
# Launches the deployed (Perf GPU Accel) build foreground, auto-loads a save,
# drives the engine via the ARCANUM_GPU_CMD command channel to flip tile
# render paths + capture BMPs at the same scene, then diffs the two captures.
# No UI interaction.
#
# Usage:
#   tools/gpu_test/run.sh [SAVE]
# Env overrides:
#   APP    path to the variant .app
#   OUT    output directory for the diff (default /tmp)
#   SETTLE settle frames between flip and capture (default 90)
#   POLL   max seconds to wait for run to finish (default 60)
#
set -uo pipefail

SAVE="${1:-SlotAuto}"
APP="${APP:-$HOME/Applications/Arcanum/Arcanum Community Edition (Perf GPU Accel).app}"
OUT="${OUT:-/tmp}"
SETTLE="${SETTLE:-30}"
POLL="${POLL:-120}"

CMD_FILE=/tmp/arcanum-gpu-cmd.txt
SW_BMP=/tmp/world_sw.bmp
GPU_BMP=/tmp/world_gpu.bmp
DONE_MARK=/tmp/arcanum-gpu-done.bmp
DEBUG_LOG=/tmp/arcanum-debug.log

BIN="$APP/Contents/MacOS/arcanum-ce"
if [[ ! -x "$BIN" ]]; then
  echo "ERROR: binary not found: $BIN" >&2
  echo "Build + deploy the (Perf GPU Accel) variant first, or set APP=..." >&2
  exit 2
fi

# Kill any lingering instance — back-to-back runs otherwise race on the data
# root + window focus.
pkill -9 -f "Perf GPU Accel" 2>/dev/null || true
sleep 1

# Fresh capture targets so we can detect new vs stale.
rm -f "$SW_BMP" "$GPU_BMP" "$DONE_MARK" "$DEBUG_LOG" "$CMD_FILE"

# Build the command script. The engine's gamelib_ping reads this every frame
# (in menu AND in-game), so a `loadsave` dismisses the main menu naturally.
{
  echo "# dismiss menu by loading the save"
  echo "loadsave $SAVE"
  echo "wait $SETTLE"
  echo "setpath software"
  echo "wait $SETTLE"
  echo "capture $SW_BMP"
  echo "wait 5"
  echo "setpath gpu"
  echo "wait $SETTLE"
  echo "trace"          # arm the GPU dispatch trace for the next world pass
  echo "wait 5"
  echo "capture $GPU_BMP"
  echo "wait 5"
  echo "capture $DONE_MARK"  # marker that the run finished
  echo "quit"
} > "$CMD_FILE"

echo "==> launch: $APP"
echo "    save:   $SAVE"
echo "    cmd:    $CMD_FILE"
# With ARCANUM_GPU_CMD set, the engine skips the window present (iso_redraw
# still fills the iso buffer, which is what we capture). That decouples the
# render loop from window focus -- no foreground pump / app-nap workarounds
# needed. The MP harness runs the same way.
ARCANUM_GPU_CMD="$CMD_FILE" \
  open -g "$APP" --args -window -gpucmd:"$CMD_FILE"

echo "==> polling for $DONE_MARK (up to ${POLL}s)..."
for i in $(seq 1 "$POLL"); do
  if [[ -f "$DONE_MARK" ]]; then
    echo "    captures ready after ${i}s"
    break
  fi
  sleep 1
done

# Give the app a beat to finish quitting, then make sure it's down.
sleep 2
pkill -9 -f "Perf GPU Accel" 2>/dev/null || true

if [[ ! -f "$SW_BMP" || ! -f "$GPU_BMP" ]]; then
  echo "==> FAIL: missing capture(s)"
  echo "    sw=$([[ -f $SW_BMP ]] && echo OK || echo MISSING)"
  echo "    gpu=$([[ -f $GPU_BMP ]] && echo OK || echo MISSING)"
  echo "==> last 30 lines of debug log:"
  tail -30 "$DEBUG_LOG" 2>/dev/null || echo "(no log)"
  exit 1
fi

echo "==> diff: $SW_BMP vs $GPU_BMP"
python3 "$(dirname "$0")/diff_bmp.py" "$SW_BMP" "$GPU_BMP" --out-dir "$OUT"
DIFF_RC=$?

echo "==> done (exit=$DIFF_RC)"
exit $DIFF_RC
