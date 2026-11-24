#!/usr/bin/env bash
# Tests scripts/ci-asan-run-tests.sh (the shared sanitizer shard runner, FRO188) with a fake Tests
# binary: the default label is ASAN, SANITIZER_LABEL=TSAN relabels it and gives each shard its own
# TSAN settings folder, and a failed shard fails the run without stopping the later shards.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ci-sanitizer-run-tests.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
check() { # check <description> <command...>
    local desc="$1"
    shift
    if "$@"; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc"
        fail=$((fail + 1))
    fi
}

cat >"$WORK/Tests" <<'FAKE'
#!/usr/bin/env bash
echo "$GTEST_TOTAL_SHARDS $AGENTSYNTH_SETTINGS_DIR" >>"$FAKE_OUT"
[ "$GTEST_SHARD_INDEX" = "${FAKE_FAIL:-}" ] && exit 66
exit 0
FAKE
chmod +x "$WORK/Tests"

run() { # run <label-or-empty> [env...]; prints the runner's output, sets RC
    local label="$1"
    shift
    : >"$WORK/out"
    OUT="$(env "$@" ${label:+SANITIZER_LABEL=$label} FAKE_OUT="$WORK/out" TESTS_BINARY="$WORK/Tests" \
        RUNNER_TEMP="$WORK" bash "$ROOT/scripts/ci-asan-run-tests.sh" 2>&1)"
    RC=$?
}

run "" ASAN_TEST_SHARDS=2
check "default run is labelled ASAN and exits 0" bash -c '[ "$1" = 0 ] && grep -q "ASAN shard 1 of 2" <<<"$2"' _ "$RC" "$OUT"
check "default run uses the asan settings folders" grep -q "agentsynth-asan-settings-1" "$WORK/out"

run TSAN SANITIZER_TEST_SHARDS=3
check "TSAN run is labelled TSAN with its own shard count" bash -c '[ "$1" = 0 ] && grep -q "TSAN shard 2 of 3" <<<"$2"' _ "$RC" "$OUT"
check "TSAN run uses tsan settings folders" grep -q "agentsynth-tsan-settings-2" "$WORK/out"

run TSAN SANITIZER_TEST_SHARDS=3 FAKE_FAIL=1
check "a failed TSAN shard fails the run, names itself, and later shards still ran" \
    bash -c '[ "$1" != 0 ] && grep -q "TSAN shard 1 failed (exit 66)" <<<"$2" && grep -q "TSAN shard 2 of 3" <<<"$2"' _ "$RC" "$OUT"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
