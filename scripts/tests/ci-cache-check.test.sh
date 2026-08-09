#!/usr/bin/env bash
#
# Unit tests for scripts/ci-cache-check.sh.
#
# The cache check is the thing that tells us the build cache broke. If IT breaks silently we are
# back to where we started (nine days of 100%-cold builds nobody noticed), so it gets tests of
# its own. Runs in the Lint job — no compiler, no runner-specific state, ~1 s.
#
# Usage: bash scripts/tests/ci-cache-check.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/ci-cache-check.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

# ccache >= 4.7 output, as emitted by the ubuntu-24.04 / macos-26 / windows-2025 runner images.
cat >"$WORK/stats-modern.txt" <<'EOF'
Cacheable calls:   263 / 268 (98.13%)
  Hits:            210 / 263 (79.84%)
    Direct:        180 / 210 (85.71%)
    Preprocessed:   30 / 210 (14.29%)
  Misses:           53 / 263 (20.16%)
Local storage:
  Hits:            210 / 263 (79.84%)
  Misses:           53 / 263 (20.16%)
EOF

# The cold-build stats actually observed in run 31297134084 (15.59% hits).
cat >"$WORK/stats-cold.txt" <<'EOF'
Cacheable calls:   263 / 268 (98.13%)
  Hits:             41 / 263 (15.59%)
    Direct:         18 /  41 (43.90%)
    Preprocessed:   23 /  41 (56.10%)
  Misses:          222 / 263 (84.41%)
EOF

# ccache <= 4.6 legacy output — still shipped by some Homebrew/apt builds.
cat >"$WORK/stats-legacy.txt" <<'EOF'
cache hit (direct)                   180
cache hit (preprocessed)              30
cache miss                            53
cache hit rate                     79.84 %
EOF

: >"$WORK/stats-empty.txt"

# run <name> <expected_exit> <expected_substring|-> [env assignments...]
#
# Every input the script reads is explicitly unset before the test's own assignments are applied.
# Without that the tests are not hermetic: ci.yml sets CACHE_CHECK_ENFORCE and CACHE_WARM_EXPECTED
# as workflow-level env, the Lint job inherits them, and `env VAR=...` ADDS to the ambient
# environment rather than replacing it — so the three "must exit 1" cases below inherited
# enforce=false from CI and passed locally while failing in CI. Keep the -u list in sync with the
# variables documented at the top of ci-cache-check.sh.
run() {
    local name="$1" want_exit="$2" want_text="$3"
    shift 3
    local out status
    out="$(env -u CACHE_MATCHED_KEY -u DEPS_MATCHED_KEY -u CACHE_WARM_EXPECTED \
               -u CACHE_MIN_HIT_RATE -u CACHE_CHECK_ENFORCE -u CCACHE_STATS_FILE \
               "$@" GITHUB_STEP_SUMMARY="$WORK/summary.md" bash "$CHECK" 2>&1)"
    status=$?

    if [ "$status" -ne "$want_exit" ]; then
        printf 'FAIL  %s\n      expected exit %s, got %s\n%s\n' \
            "$name" "$want_exit" "$status" "$(defang "$out")"
        fail=$((fail + 1))
        return
    fi
    if [ "$want_text" != "-" ] && ! printf '%s' "$out" | grep -qF "$want_text"; then
        printf 'FAIL  %s\n      expected output to contain: %s\n%s\n' \
            "$name" "$want_text" "$(defang "$out")"
        fail=$((fail + 1))
        return
    fi
    printf 'ok    %s\n' "$name"
    pass=$((pass + 1))
}

# The script under test emits GitHub workflow commands (::error::, ::warning::). Echoing them
# verbatim when a test fails would make Actions render a *fixture* as a real job annotation, so
# neuter the leading colons before printing.
defang() { printf '%s' "$1" | sed 's/^::/__/'; }

# --- healthy path -------------------------------------------------------------------------
run "healthy: both caches restored, high hit rate" 0 "ccache hit rate   : 79%" \
    CACHE_MATCHED_KEY=Linux-ccache-main-abc \
    DEPS_MATCHED_KEY=Linux-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-modern.txt"

# --- the regression this whole change exists to catch -------------------------------------
run "broken: ccache did not restore on a PR run" 1 "::error::ccache cache did not restore" \
    CACHE_MATCHED_KEY= \
    DEPS_MATCHED_KEY=Linux-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-cold.txt"

run "broken: build/_deps did not restore on a PR run" 1 "::error::build/_deps cache did not restore" \
    CACHE_MATCHED_KEY=Linux-ccache-main-abc \
    DEPS_MATCHED_KEY= \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-modern.txt"

run "broken: neither cache restored — the Aug 2026 state" 1 "::error::" \
    CACHE_MATCHED_KEY= \
    DEPS_MATCHED_KEY= \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-cold.txt"

# --- seeding run: a miss here is expected, not a failure ----------------------------------
run "seeding run: cold cache is not a failure" 0 "::notice::Cache miss on a cache-seeding run" \
    CACHE_MATCHED_KEY= \
    DEPS_MATCHED_KEY= \
    CACHE_WARM_EXPECTED=false \
    CCACHE_STATS_FILE="$WORK/stats-cold.txt"

# --- enforcement toggle -------------------------------------------------------------------
run "enforce=false: reports the failure but exits 0" 0 "::notice::CACHE_CHECK_ENFORCE is not" \
    CACHE_MATCHED_KEY= \
    DEPS_MATCHED_KEY= \
    CACHE_WARM_EXPECTED=true \
    CACHE_CHECK_ENFORCE=false \
    CCACHE_STATS_FILE="$WORK/stats-cold.txt"

# --- stat parsing -------------------------------------------------------------------------
run "parses legacy ccache <=4.6 stat format" 0 "ccache hit rate   : 79%" \
    CACHE_MATCHED_KEY=macOS-ccache-main-abc \
    DEPS_MATCHED_KEY=macOS-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-legacy.txt"

run "low hit rate warns but does not fail" 0 "::warning::ccache hit rate 15%" \
    CACHE_MATCHED_KEY=Linux-ccache-main-abc \
    DEPS_MATCHED_KEY=Linux-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-cold.txt"

run "hit-rate floor is configurable" 0 "-" \
    CACHE_MATCHED_KEY=Linux-ccache-main-abc \
    DEPS_MATCHED_KEY=Linux-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CACHE_MIN_HIT_RATE=10 \
    CCACHE_STATS_FILE="$WORK/stats-cold.txt"

run "no ccache statistics at all warns" 0 "::warning::No ccache statistics available" \
    CACHE_MATCHED_KEY=Linux-ccache-main-abc \
    DEPS_MATCHED_KEY=Linux-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-empty.txt"

# --- job summary is written ---------------------------------------------------------------
if grep -q "Build cache health" "$WORK/summary.md"; then
    printf 'ok    writes a GitHub job summary\n'
    pass=$((pass + 1))
else
    printf 'FAIL  writes a GitHub job summary\n'
    fail=$((fail + 1))
fi

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
