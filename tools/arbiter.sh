#!/usr/bin/env bash
#
# Run a named arbiter-harness scenario against a deployed (harness) build and
# propagate its exit code -- so an `assert-render-under` failure inside the
# scenario fails this script too (a real headless CI perf gate).
#
# Scenarios are versioned .txt command scripts under tools/scenarios/. See
# docs/arbiter-harness.md for the command reference.
#
# Usage:
#   tools/arbiter.sh <scenario> [--app "<app name or path>"]
#   tools/arbiter.sh --list
#
# Env overrides:
#   APP   path to the variant .app (or a bare app name under ~/Applications/Arcanum)
#
# Requires a build configured with -DARCANUM_HARNESS=ON (the command channel is
# compiled out of ship builds). Exit codes: the scenario's (0 = ok / gate passed,
# 1 = gate failed via assert-render-under, 2 = setup error).
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SCEN_DIR="$HERE/scenarios"
ARC_DIR="$HOME/Applications/Arcanum"
DEBUG_LOG=/tmp/arcanum-debug.log

if [[ "${1:-}" == "--list" || -z "${1:-}" ]]; then
  echo "scenarios in $SCEN_DIR:"
  for f in "$SCEN_DIR"/*.txt; do
    [[ -e "$f" ]] && echo "  $(basename "$f" .txt)"
  done
  [[ -z "${1:-}" ]] && exit 2 || exit 0
fi

SCENARIO="$1"; shift
APP="${APP:-Arcanum Community Edition (Harness Settle)}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --app) APP="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

SCEN_FILE="$SCEN_DIR/$SCENARIO.txt"
if [[ ! -f "$SCEN_FILE" ]]; then
  echo "ERROR: scenario not found: $SCEN_FILE" >&2
  "$0" --list >&2
  exit 2
fi

# Resolve the app: accept an absolute .app path or a bare name under ~/Applications/Arcanum.
if [[ "$APP" == /* ]]; then
  APP_PATH="$APP"
else
  APP_PATH="$ARC_DIR/$APP.app"
fi
BIN="$APP_PATH/Contents/MacOS/arcanum-ce"
if [[ ! -x "$BIN" ]]; then
  echo "ERROR: binary not found: $BIN" >&2
  echo "  Build a -DARCANUM_HARNESS=ON variant and deploy it, or pass --app." >&2
  exit 2
fi

# Display sleep wedges the GL vsync swap; keep it awake for the run.
if ! pgrep -f "caffeinate -dis" >/dev/null 2>&1; then
  caffeinate -dis &
  CAFFEINATE_PID=$!
fi

# Back-to-back runs race on the data root + window focus.
pkill -9 -f "$APP" 2>/dev/null || true
sleep 1
rm -f "$DEBUG_LOG"

echo "==> scenario: $SCENARIO  ($SCEN_FILE)"
echo "==> app:      $APP_PATH"

# Run foreground from the data root so the binary finds ./data, ./modules. The
# scenario ends in `quit`, which exits cleanly (harness pre-confirms the modal),
# so we get the real exit code -- including assert-render-under's exit(1).
( cd "$ARC_DIR" && ARCANUM_GPU_CMD="$SCEN_FILE" \
    "$BIN" -window -ApplePersistenceIgnoreState YES >/tmp/arcanum-arbiter.out 2>&1 )
RC=$?

echo "==> scenario exit: $RC"
echo "==> harness output:"
grep -E "\[gpu-cmd\]|\[harness\]" "$DEBUG_LOG" 2>/dev/null | sed 's/^/    /' || true

[[ -n "${CAFFEINATE_PID:-}" ]] && kill "$CAFFEINATE_PID" 2>/dev/null || true
exit $RC
