#!/usr/bin/env bash
#
# ai-eval-ratchet.sh — P1-11: guard against the nightly/manual AIPatchHarness run getting quietly
# worse, without ever gating a merge (the harness needs a live model, so it never runs in ci.yml).
#
# WHY a ratchet and not a fixed threshold: the applied-after-retry rate is noisy by nature (model
# sampling, prompt phrasing, network hiccups). A fixed floor either nags on normal variance or is
# set so low it never catches a real regression. Comparing against the last-committed baseline,
# and only failing on a drop bigger than normal noise, is the only version of this that stays
# useful.
#
# Reads its inputs from the environment (mirrors scripts/ci-cache-check.sh's convention, and keeps
# this independently testable — see scripts/tests/ai-eval-ratchet.test.sh):
#   CURRENT_JSON          path to this run's AIPatchHarness --json output (required)
#   BASELINE_JSON         path to the committed baseline --json output. If unset or the file
#                         doesn't exist, there is nothing to ratchet against yet — this is
#                         expected before the first baseline is committed, so it's a notice, not
#                         a failure. The first `--json` run a human is happy with becomes the
#                         baseline by committing it at this path.
#   RATCHET_MIN_ATTEMPTS  both runs need at least this many attempts before the rate is trusted
#                         (default 50 — see P1-11's task text)
#   RATCHET_MAX_DROP      fail only if the applied rate drops more than this many percentage
#                         points versus the baseline (default 8)
#   GITHUB_STEP_SUMMARY   optional: append a summary block here, same convention as
#                         ci-cache-check.sh
#
# Exit 0: no baseline yet, not enough attempts to trust the rate, or the rate held/improved.
# Exit 1: applied rate dropped more than RATCHET_MAX_DROP points with both sides at n >= RATCHET_MIN_ATTEMPTS.

set -uo pipefail

CURRENT_JSON="${CURRENT_JSON:-}"
BASELINE_JSON="${BASELINE_JSON:-}"
RATCHET_MIN_ATTEMPTS="${RATCHET_MIN_ATTEMPTS:-50}"
RATCHET_MAX_DROP="${RATCHET_MAX_DROP:-8}"

if [ -z "$CURRENT_JSON" ] || [ ! -f "$CURRENT_JSON" ]; then
    echo "::error::CURRENT_JSON is required and must point at an existing AIPatchHarness --json file"
    exit 1
fi

# rate_and_n <file> — prints "<applied-rate-x100> <attempted>" (rate scaled by 100 so bash's
# integer arithmetic below stays exact; e.g. "8234 40" means 82.34% over 40 attempts).
rate_and_n() {
    python3 - "$1" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    data = json.load(f)
attempted = data.get("attempted", 0)
applied = data.get("appliedCount")
if applied is None:
    # Older recordings (pre-P1-11) have no root-level appliedCount; sum the records instead.
    applied = sum(1 for r in data.get("records", []) if r.get("appliedAfterRetry"))
rate_x100 = (10000 * applied) // attempted if attempted > 0 else 0
print(f"{rate_x100} {attempted}")
PY
}

if [ -z "$BASELINE_JSON" ] || [ ! -f "$BASELINE_JSON" ]; then
    echo "::notice::No committed baseline yet (BASELINE_JSON='$BASELINE_JSON') — skipping the ratchet." \
         "This run can become the baseline once a human is happy with it."
    exit 0
fi

read -r current_rate_x100 current_n < <(rate_and_n "$CURRENT_JSON")
read -r baseline_rate_x100 baseline_n < <(rate_and_n "$BASELINE_JSON")

current_rate=$(awk "BEGIN { printf \"%.1f\", $current_rate_x100 / 100 }")
baseline_rate=$(awk "BEGIN { printf \"%.1f\", $baseline_rate_x100 / 100 }")

summary=""
if [ "$current_n" -lt "$RATCHET_MIN_ATTEMPTS" ] || [ "$baseline_n" -lt "$RATCHET_MIN_ATTEMPTS" ]; then
    echo "::notice::Not enough attempts to trust the rate (current n=$current_n, baseline n=$baseline_n," \
         "need >= $RATCHET_MIN_ATTEMPTS on both) — skipping the ratchet."
    exit 0
fi

drop_x100=$((baseline_rate_x100 - current_rate_x100))
drop=$(awk "BEGIN { printf \"%.1f\", $drop_x100 / 100 }")
max_drop_x100=$((RATCHET_MAX_DROP * 100))

summary="applied rate: baseline ${baseline_rate}% (n=$baseline_n) -> current ${current_rate}% (n=$current_n), drop ${drop} pts (ceiling ${RATCHET_MAX_DROP} pts)"

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### AI Patch Eval ratchet"
        echo "$summary"
    } >>"$GITHUB_STEP_SUMMARY"
fi

if [ "$drop_x100" -gt "$max_drop_x100" ]; then
    echo "::error::$summary — exceeds the ${RATCHET_MAX_DROP}-point ceiling"
    exit 1
fi

echo "::notice::$summary — within the ${RATCHET_MAX_DROP}-point ceiling"
exit 0
