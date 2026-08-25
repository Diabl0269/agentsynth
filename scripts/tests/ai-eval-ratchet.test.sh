#!/usr/bin/env bash
#
# Unit tests for scripts/ai-eval-ratchet.sh (P1-11). Runs in the Lint job — no compiler, no
# model, no network, ~1s. Fixtures are minimal AIPatchHarness --json shapes, not real recordings.
#
# Usage: bash scripts/tests/ai-eval-ratchet.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RATCHET="$SCRIPT_DIR/scripts/ai-eval-ratchet.sh"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ai-eval-ratchet.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

run() {
    local name="$1" want_exit="$2" want_text="$3"
    shift 3
    local out status
    out="$(env -u CURRENT_JSON -u BASELINE_JSON -u RATCHET_MIN_ATTEMPTS -u RATCHET_MAX_DROP \
               -u GITHUB_STEP_SUMMARY "$@" bash "$RATCHET" 2>&1)"
    status=$?

    if [ "$status" -ne "$want_exit" ]; then
        printf 'FAIL  %s\n      expected exit %s, got %s\n%s\n' "$name" "$want_exit" "$status" "$out"
        fail=$((fail + 1))
        return
    fi
    if [ "$want_text" != "-" ] && ! printf '%s' "$out" | grep -qF "$want_text"; then
        printf 'FAIL  %s\n      expected output to contain: %s\n%s\n' "$name" "$want_text" "$out"
        fail=$((fail + 1))
        return
    fi
    printf 'ok    %s\n' "$name"
    pass=$((pass + 1))
}

# make_run <path> <attempted> <appliedCount>
make_run() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, sys
path, attempted, applied = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(path, "w") as f:
    json.dump({"attempted": attempted, "appliedCount": applied, "records": []}, f)
PY
}

# make_run_records_only <path> <attempted> <appliedCount> — no root-level appliedCount, forcing
# the script's fallback that sums per-record appliedAfterRetry (pre-P1-11 recordings).
make_run_records_only() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, sys
path, attempted, applied = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
records = [{"appliedAfterRetry": True}] * applied + [{"appliedAfterRetry": False}] * (attempted - applied)
with open(path, "w") as f:
    json.dump({"attempted": attempted, "records": records}, f)
PY
}

make_run "$WORK/current-good.json" 60 57   # 95.0%
make_run "$WORK/current-small-drop.json" 60 54  # 90.0%, 5pt drop
make_run "$WORK/current-big-drop.json" 60 42    # 70.0%, 25pt drop
make_run "$WORK/baseline.json" 60 57       # 95.0%
make_run "$WORK/current-few-attempts.json" 10 9
make_run "$WORK/baseline-few-attempts.json" 10 9
make_run_records_only "$WORK/current-legacy.json" 60 57

# --- missing/invalid CURRENT_JSON -----------------------------------------------------------
run "fails without CURRENT_JSON" 1 "::error::CURRENT_JSON is required" \
    CURRENT_JSON= BASELINE_JSON="$WORK/baseline.json"

run "fails when CURRENT_JSON does not exist" 1 "::error::CURRENT_JSON is required" \
    CURRENT_JSON="$WORK/nope.json" BASELINE_JSON="$WORK/baseline.json"

# --- no baseline yet: inert, never fails ----------------------------------------------------
run "no baseline: skips the ratchet" 0 "No committed baseline yet" \
    CURRENT_JSON="$WORK/current-good.json" BASELINE_JSON=

run "baseline path set but file absent: skips the ratchet" 0 "No committed baseline yet" \
    CURRENT_JSON="$WORK/current-good.json" BASELINE_JSON="$WORK/does-not-exist.json"

# --- not enough attempts on either side: inert ----------------------------------------------
run "too few attempts: skips the ratchet" 0 "Not enough attempts to trust the rate" \
    CURRENT_JSON="$WORK/current-few-attempts.json" BASELINE_JSON="$WORK/baseline-few-attempts.json"

# --- the actual ratchet ----------------------------------------------------------------------
run "rate held: passes" 0 "within the 8-point ceiling" \
    CURRENT_JSON="$WORK/current-good.json" BASELINE_JSON="$WORK/baseline.json"

run "small drop within ceiling: passes" 0 "within the 8-point ceiling" \
    CURRENT_JSON="$WORK/current-small-drop.json" BASELINE_JSON="$WORK/baseline.json"

run "big drop beyond ceiling: fails" 1 "exceeds the 8-point ceiling" \
    CURRENT_JSON="$WORK/current-big-drop.json" BASELINE_JSON="$WORK/baseline.json"

run "ceiling is configurable" 0 "within the 30-point ceiling" \
    CURRENT_JSON="$WORK/current-big-drop.json" BASELINE_JSON="$WORK/baseline.json" \
    RATCHET_MAX_DROP=30

run "minimum attempts is configurable" 0 "within the 8-point ceiling" \
    CURRENT_JSON="$WORK/current-few-attempts.json" BASELINE_JSON="$WORK/baseline-few-attempts.json" \
    RATCHET_MIN_ATTEMPTS=5

# --- legacy recordings with no root-level appliedCount ---------------------------------------
run "falls back to summing records when appliedCount is absent" 0 "within the 8-point ceiling" \
    CURRENT_JSON="$WORK/current-legacy.json" BASELINE_JSON="$WORK/baseline.json"

# --- job summary ------------------------------------------------------------------------------
env -u CURRENT_JSON -u BASELINE_JSON -u RATCHET_MIN_ATTEMPTS -u RATCHET_MAX_DROP \
    CURRENT_JSON="$WORK/current-good.json" BASELINE_JSON="$WORK/baseline.json" \
    GITHUB_STEP_SUMMARY="$WORK/summary.md" bash "$RATCHET" >/dev/null 2>&1
if grep -q "AI Patch Eval ratchet" "$WORK/summary.md" 2>/dev/null; then
    printf 'ok    writes a GitHub job summary\n'
    pass=$((pass + 1))
else
    printf 'FAIL  writes a GitHub job summary\n'
    fail=$((fail + 1))
fi

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
