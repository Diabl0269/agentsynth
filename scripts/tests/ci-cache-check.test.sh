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
               -u BUILD_NINJA BUILD_NINJA=/nonexistent/build.ninja \
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

# --- runs that legitimately start cold: a miss is expected, not a failure ------------------
# Two cases share this path, both driven by CACHE_WARM_EXPECTED=false from ci.yml: the
# push-to-main seeding run, and a pull request from a fork (GitHub gives forks an isolated cache
# scope with no read access to the base repository's entries, so a miss is guaranteed and is not
# something the contributor can fix).
run "seeding run: cold cache is not a failure" 0 \
    "::notice::Cache miss on a run not expected to have a warm cache" \
    CACHE_MATCHED_KEY= \
    DEPS_MATCHED_KEY= \
    CACHE_WARM_EXPECTED=false \
    CCACHE_STATS_FILE="$WORK/stats-cold.txt"

run "fork PR: cold cache is not a failure even with enforcement on" 0 \
    "::notice::Cache miss on a run not expected to have a warm cache" \
    CACHE_MATCHED_KEY= \
    DEPS_MATCHED_KEY= \
    CACHE_WARM_EXPECTED=false \
    CACHE_CHECK_ENFORCE=true \
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

# --- fail-safe cross-validation ------------------------------------------------------------
# Regression cases for the mis-wired-inputs bug: run 31301691346 restored every cache and hit
# 100%, yet the check reported MISS on all three platforms, because the workflow read
# cache-matched-key off the combined actions/cache action (which only declares cache-hit) instead
# of actions/cache/restore. Enforcing on that would have failed every build, so an empty matched
# key contradicted by a high hit rate must warn about the check — never fail the build.
cat >"$WORK/stats-perfect.txt" <<'EOF'
Cacheable calls:   268 / 268 (100.0%)
  Hits:            268 / 268 (100.0%)
    Direct:        268 / 268 (100.0%)
    Preprocessed:    0 / 268 (0.00%)
  Misses:            0 / 268 (0.00%)
EOF

run "fail-safe: empty matched keys + 100% hit rate does not fail" 0 \
    "::warning::Cache reported as not restored, yet ccache hit 100%" \
    CACHE_MATCHED_KEY= \
    DEPS_MATCHED_KEY= \
    CACHE_WARM_EXPECTED=true \
    CACHE_CHECK_ENFORCE=true \
    CCACHE_STATS_FILE="$WORK/stats-perfect.txt"

# Negative assertion: the contradiction must not be relabelled as the (benign) seeding-run case,
# which would hide a mis-wired check behind a reassuring notice.
contradiction_out="$(env -u CACHE_MATCHED_KEY -u DEPS_MATCHED_KEY -u CACHE_WARM_EXPECTED \
    -u CACHE_MIN_HIT_RATE -u CACHE_CHECK_ENFORCE -u CCACHE_STATS_FILE \
    CACHE_MATCHED_KEY= DEPS_MATCHED_KEY= CACHE_WARM_EXPECTED=true CACHE_CHECK_ENFORCE=true \
    CCACHE_STATS_FILE="$WORK/stats-perfect.txt" \
    GITHUB_STEP_SUMMARY="$WORK/summary.md" bash "$CHECK" 2>&1)"
# Must match the live notice text in ci-cache-check.sh — if this string drifts out of sync the
# assertion silently passes for the wrong reason, which is exactly the failure mode being guarded.
if printf '%s' "$contradiction_out" | grep -qF "not expected to have a warm cache"; then
    printf 'FAIL  fail-safe: does not misreport the contradiction as a seeding run\n%s\n' \
        "$(defang "$contradiction_out")"
    fail=$((fail + 1))
else
    printf 'ok    fail-safe: does not misreport the contradiction as a seeding run\n'
    pass=$((pass + 1))
fi

run "fail-safe: a genuinely cold cache (15%) still fails" 1 "::error::" \
    CACHE_MATCHED_KEY= \
    DEPS_MATCHED_KEY= \
    CACHE_WARM_EXPECTED=true \
    CACHE_CHECK_ENFORCE=true \
    CCACHE_STATS_FILE="$WORK/stats-cold.txt"

# --- compiler-launcher audit ---------------------------------------------------------------
# Regression cases for the Aug 2026 fault: CMake's compiler launcher is per language, so setting
# only C/CXX left all 103 JUCE Objective-C++ units bypassing ccache — rebuilt cold every macOS run,
# and invisible to the hit rate, because a compile that never reaches ccache counts as neither hit
# nor miss. Only build.ninja can see it, so the check reads the generator's own output.
cat >"$WORK/ninja-good.ninja" <<'EOF'
rule CXX_COMPILER__Core_unscanned_Release
  depfile = $DEP_FILE
  command = /opt/homebrew/bin/ccache /usr/bin/c++ $DEFINES $INCLUDES -o $out -c $in
  description = Building CXX object $out
rule OBJCXX_COMPILER__Core_unscanned_Release
  command = /opt/homebrew/bin/ccache /usr/bin/c++ -x objective-c++ $DEFINES -o $out -c $in
rule CXX_STATIC_LIBRARY_LINKER__Core_Release
  command = /usr/bin/ar qc $out $in
rule RC_COMPILER__AgentSynth_Release
  command = rc.exe /fo$out $in
EOF

# The exact shape of the bug: CXX wired, OBJCXX not.
cat >"$WORK/ninja-no-objcxx.ninja" <<'EOF'
rule CXX_COMPILER__Core_unscanned_Release
  command = /opt/homebrew/bin/ccache /usr/bin/c++ $DEFINES -o $out -c $in
rule OBJCXX_COMPILER__Core_unscanned_Release
  command = /usr/bin/c++ -x objective-c++ $DEFINES -o $out -c $in
  description = Building OBJCXX object $out
EOF

run "launcher audit: every compile rule uses ccache" 0 "OBJCXX compile rules  : 1/1 via ccache" \
    CACHE_MATCHED_KEY=macOS-ccache-main-abc \
    DEPS_MATCHED_KEY=macOS-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-modern.txt" \
    BUILD_NINJA="$WORK/ninja-good.ninja"

run "launcher audit: OBJCXX bypassing ccache fails the build" 1 \
    "::error::1 of 1 OBJCXX compile rules do not go through ccache" \
    CACHE_MATCHED_KEY=macOS-ccache-main-abc \
    DEPS_MATCHED_KEY=macOS-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-modern.txt" \
    BUILD_NINJA="$WORK/ninja-no-objcxx.ninja"

# The link rule and the Windows resource compiler never go through ccache and must not be flagged.
run "launcher audit: link and RC rules are out of scope" 0 "-" \
    CACHE_MATCHED_KEY=macOS-ccache-main-abc \
    DEPS_MATCHED_KEY=macOS-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-modern.txt" \
    BUILD_NINJA="$WORK/ninja-good.ninja"

# The layout CMake actually generates: build.ninja holds the build statements and `include`s
# CMakeFiles/rules.ninja, which is where every rule definition lives. The first version of this
# audit read build.ninja alone, matched nothing, and was caught in CI by the warning below rather
# than by a test — so the real layout is now a fixture.
mkdir -p "$WORK/realdir/CMakeFiles"
cat >"$WORK/realdir/build.ninja" <<'EOF'
include CMakeFiles/rules.ninja
build CMakeFiles/Core.dir/juce_core.mm.o: OBJCXX_COMPILER__Core_unscanned_Release ../juce_core.mm
EOF
cat >"$WORK/realdir/CMakeFiles/rules.ninja" <<'EOF'
rule OBJCXX_COMPILER__Core_unscanned_Release
  depfile = $DEP_FILE
  command = /opt/homebrew/bin/ccache /usr/bin/c++ -x objective-c++ $DEFINES -o $out -c $in
  description = Building OBJCXX object $out
rule CXX_COMPILER__Core_unscanned_Release
  command = /opt/homebrew/bin/ccache /usr/bin/c++ $DEFINES -o $out -c $in
EOF

run "launcher audit: reads rules from CMakeFiles/rules.ninja, as CMake emits them" 0 \
    "OBJCXX compile rules  : 1/1 via ccache" \
    CACHE_MATCHED_KEY=macOS-ccache-main-abc \
    DEPS_MATCHED_KEY=macOS-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-modern.txt" \
    BUILD_NINJA="$WORK/realdir/build.ninja"

# Same layout, launcher missing on OBJCXX only — the actual Aug 2026 bug, seen through the real
# file layout rather than a flattened fixture.
mkdir -p "$WORK/realdir-bad/CMakeFiles"
cp "$WORK/realdir/build.ninja" "$WORK/realdir-bad/build.ninja"
cat >"$WORK/realdir-bad/CMakeFiles/rules.ninja" <<'EOF'
rule OBJCXX_COMPILER__Core_unscanned_Release
  command = /usr/bin/c++ -x objective-c++ $DEFINES -o $out -c $in
rule CXX_COMPILER__Core_unscanned_Release
  command = /opt/homebrew/bin/ccache /usr/bin/c++ $DEFINES -o $out -c $in
EOF

run "launcher audit: real layout, OBJCXX unwired, fails" 1 \
    "::error::1 of 1 OBJCXX compile rules do not go through ccache" \
    CACHE_MATCHED_KEY=macOS-ccache-main-abc \
    DEPS_MATCHED_KEY=macOS-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-modern.txt" \
    BUILD_NINJA="$WORK/realdir-bad/build.ninja"

run "launcher audit: absent build.ninja is skipped, not a failure" 0 \
    "launcher audit    : skipped" \
    CACHE_MATCHED_KEY=macOS-ccache-main-abc \
    DEPS_MATCHED_KEY=macOS-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-modern.txt"

# A build.ninja whose compile rules the audit cannot recognise (CMake renamed them, say) must warn
# that the audit checked nothing — not print the reassuring "skipped" line, which would restore the
# exact silent-success blind spot the audit was added to close.
cat >"$WORK/ninja-unparseable.ninja" <<'EOF'
rule CXX_BUILD_STEP_Core_Release
  command = /usr/bin/c++ -o $out -c $in
EOF

run "launcher audit: unrecognised rule names warn instead of passing quietly" 0 \
    "::warning::Launcher audit recognised no C/CXX/OBJC/OBJCXX compile rules" \
    CACHE_MATCHED_KEY=macOS-ccache-main-abc \
    DEPS_MATCHED_KEY=macOS-deps3-def \
    CACHE_WARM_EXPECTED=true \
    CCACHE_STATS_FILE="$WORK/stats-modern.txt" \
    BUILD_NINJA="$WORK/ninja-unparseable.ninja"

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
