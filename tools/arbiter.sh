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
#   tools/arbiter.sh <scenario> [--app "<name|path>"] [--headless]
#                    [--check-captures] [--update-baselines]
#                    [--tolerance-px N] [--tolerance-delta D] [--report FILE]
#   tools/arbiter.sh --list
#
# --headless runs on SDL's dummy video driver + software renderer (no window / GL
# context / vsync), so it works with no display and needs no caffeinate -- the
# intended CI mode. (Forces the software render path; GPU-path scenarios should
# run windowed.)
#
# Visual-regression gate: a scenario that writes captures to /tmp/arbiter-captures/
# (e.g. `capturescreen /tmp/arbiter-captures/town.bmp`) can be gated against stored
# baselines under tools/baselines/<scenario>/:
#   --check-captures      diff each capture vs its baseline (tolerance-based; a
#                         missing baseline or an over-tolerance diff fails the run).
#   --update-baselines    copy this run's captures into the baseline dir (then exit).
#   --tolerance-px N      allow up to N differing pixels (default 700 -- above the
#                         ~550px seed+fixeddt ambient-animation floor; see Determinism).
#   --tolerance-delta D   ignore per-channel deltas <= D (default 16).
# --report FILE writes a JSON summary (exit, asserts, bench-ab, capture diffs).
#
# Env overrides:
#   APP   path to the variant .app (or a bare app name under ~/Applications/Arcanum)
#
# Requires a build configured with -DARCANUM_HARNESS=ON (the command channel is
# compiled out of ship builds). Exit codes: 0 = ok / gates passed, 1 = a gate
# failed (assert / capture-diff), 2 = setup error.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SCEN_DIR="$HERE/scenarios"
BASELINE_DIR="$HERE/baselines"
DIFF_TOOL="$HERE/gpu_test/diff_bmp.py"
CAP_DIR=/tmp/arbiter-captures
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
HEADLESS=0
CHECK_CAPTURES=0
UPDATE_BASELINES=0
TOL_PX=700
TOL_DELTA=16
REPORT=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --app) APP="$2"; shift 2 ;;
    --headless) HEADLESS=1; shift ;;
    --check-captures) CHECK_CAPTURES=1; shift ;;
    --update-baselines) UPDATE_BASELINES=1; shift ;;
    --tolerance-px) TOL_PX="$2"; shift 2 ;;
    --tolerance-delta) TOL_DELTA="$2"; shift 2 ;;
    --report) REPORT="$2"; shift 2 ;;
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

# Display sleep wedges the GL vsync swap; keep it awake for windowed runs.
# Headless uses the dummy driver (no swap), so no caffeinate is needed.
if [[ "$HEADLESS" -eq 0 ]] && ! pgrep -f "caffeinate -dis" >/dev/null 2>&1; then
  caffeinate -dis &
  CAFFEINATE_PID=$!
fi

# Back-to-back runs race on the data root + window focus.
pkill -9 -f "$APP" 2>/dev/null || true
sleep 1
rm -f "$DEBUG_LOG"
# Fresh capture dir so we only diff captures this run produced.
rm -rf "$CAP_DIR"; mkdir -p "$CAP_DIR"

echo "==> scenario: $SCENARIO  ($SCEN_FILE)"
echo "==> app:      $APP_PATH"
[[ "$HEADLESS" -eq 1 ]] && echo "==> mode:     headless (dummy video + software renderer)"

HL_ARG=()
[[ "$HEADLESS" -eq 1 ]] && HL_ARG=(-headless)

# Run foreground from the data root so the binary finds ./data, ./modules. The
# scenario ends in `quit`, which exits cleanly (harness pre-confirms the modal),
# so we get the real exit code -- including assert-render-under's exit(1).
( cd "$ARC_DIR" && ARCANUM_GPU_CMD="$SCEN_FILE" \
    "$BIN" -window "${HL_ARG[@]}" -ApplePersistenceIgnoreState YES >/tmp/arcanum-arbiter.out 2>&1 )
RC=$?

echo "==> scenario exit: $RC"
echo "==> harness output:"
grep -E "\[gpu-cmd\]|\[harness\]" "$DEBUG_LOG" 2>/dev/null | sed 's/^/    /' || true

[[ -n "${CAFFEINATE_PID:-}" ]] && kill "$CAFFEINATE_PID" 2>/dev/null || true

SCEN_BASELINE="$BASELINE_DIR/$SCENARIO"

# --update-baselines: adopt this run's captures as the new reference, then stop.
if [[ "$UPDATE_BASELINES" -eq 1 ]]; then
  shopt -s nullglob
  caps=("$CAP_DIR"/*.bmp)
  shopt -u nullglob
  if [[ ${#caps[@]} -eq 0 ]]; then
    echo "==> update-baselines: no captures in $CAP_DIR (scenario must write there)" >&2
    exit 2
  fi
  mkdir -p "$SCEN_BASELINE"
  cp -f "${caps[@]}" "$SCEN_BASELINE/"
  echo "==> updated ${#caps[@]} baseline(s) in $SCEN_BASELINE"
  exit 0
fi

# --check-captures: tolerance-diff each capture against its baseline.
CAP_RESULTS=()   # "name|status" for the report
if [[ "$CHECK_CAPTURES" -eq 1 ]]; then
  echo "==> capture check (tol: <=$TOL_PX px, delta>$TOL_DELTA):"
  shopt -s nullglob
  caps=("$CAP_DIR"/*.bmp)
  shopt -u nullglob
  if [[ ${#caps[@]} -eq 0 ]]; then
    echo "    (no captures produced -- did the scenario write to $CAP_DIR ?)"
  fi
  for cap in "${caps[@]}"; do
    name="$(basename "$cap")"
    base="$SCEN_BASELINE/$name"
    if [[ ! -f "$base" ]]; then
      echo "    $name: NO BASELINE (run --update-baselines once to create)"
      CAP_RESULTS+=("$name|no-baseline"); RC=1; continue
    fi
    if python3 "$DIFF_TOOL" "$base" "$cap" \
         --tolerance-px "$TOL_PX" --tolerance-delta "$TOL_DELTA" \
         --out-dir "$CAP_DIR" >/dev/null 2>&1; then
      echo "    $name: PASS"; CAP_RESULTS+=("$name|pass")
    else
      echo "    $name: FAIL (over tolerance -- see $CAP_DIR/diff_*.png)"
      CAP_RESULTS+=("$name|fail"); RC=1
    fi
  done
fi

# --report: machine-readable summary parsed from the debug log + capture checks.
if [[ -n "$REPORT" ]]; then
  # grep -c prints "0" AND exits non-zero on no match, so don't add `|| echo 0`
  # (that would double it); just default an empty result (missing file) to 0.
  apass=$(grep -cE "\[gpu-cmd\] assert.* -> PASS" "$DEBUG_LOG" 2>/dev/null); apass=${apass:-0}
  afail=$(grep -cE "\[gpu-cmd\] assert.* -> FAIL" "$DEBUG_LOG" 2>/dev/null); afail=${afail:-0}
  {
    echo "{"
    echo "  \"scenario\": \"$SCENARIO\","
    echo "  \"headless\": $([[ $HEADLESS -eq 1 ]] && echo true || echo false),"
    echo "  \"exit_code\": $RC,"
    echo "  \"asserts\": { \"pass\": $apass, \"fail\": $afail },"
    printf "  \"bench_ab\": ["
    sep=""
    while IFS= read -r l; do
      tname=$(sed -E 's/.*bench-ab ([^:]+):.*/\1/' <<<"$l")
      off=$(sed -E 's/.*off ([0-9.]+)ms.*/\1/' <<<"$l")
      on=$(sed -E 's/.*on ([0-9.]+)ms.*/\1/' <<<"$l")
      pct=$(sed -E 's/.*\(([+-][0-9.]+)%\).*/\1/' <<<"$l")
      printf '%s{"toggle":"%s","off_ms":%s,"on_ms":%s,"delta_pct":%s}' "$sep" "$tname" "$off" "$on" "$pct"
      sep=", "
    done < <(grep -E "\[gpu-cmd\] bench-ab .*: off" "$DEBUG_LOG" 2>/dev/null)
    echo "],"
    printf "  \"captures\": ["
    sep=""
    for r in "${CAP_RESULTS[@]:-}"; do
      [[ -z "$r" ]] && continue
      printf '%s{"name":"%s","status":"%s"}' "$sep" "${r%%|*}" "${r##*|}"; sep=", "
    done
    echo "]"
    echo "}"
  } > "$REPORT"
  echo "==> report: $REPORT"
fi

echo "==> result: $([[ $RC -eq 0 ]] && echo PASS || echo FAIL) (exit $RC)"
exit $RC
