#!/usr/bin/env bash
# Tests scripts/ci-linux-run-tests.sh with a fake Tests binary: every shard runs with its own settings
# folder and profile, and a failed, crashed or empty shard fails the step.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
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
echo "$GTEST_TOTAL_SHARDS $AGENTSYNTH_SETTINGS_DIR $LLVM_PROFILE_FILE" >"$FAKE_OUT/shard-$GTEST_SHARD_INDEX"
[ "$GTEST_SHARD_INDEX" = "${FAKE_FAIL:-}" ] && exit 3
[ "$GTEST_SHARD_INDEX" = "${FAKE_EMPTY:-}" ] && exit 0
[ "$GTEST_SHARD_INDEX" = "${FAKE_HANG:-}" ] && sleep 30
echo "[==========] 5 tests from 2 test suites ran. (1 ms total)"
FAKE
chmod +x "$WORK/Tests"

run() { # run [VAR=value...]
    rm -rf "$WORK/out" "$WORK/tmp"
    mkdir -p "$WORK/out" "$WORK/tmp"
    (cd "$WORK" && env -u GITHUB_ACTIONS FAKE_OUT="$WORK/out" RUNNER_TEMP="$WORK/tmp" TESTS_BINARY="$WORK/Tests" \
        LINUX_TEST_SHARDS=3 "$@" bash "$ROOT/scripts/ci-linux-run-tests.sh" 2>&1)
}

run >/dev/null
check "all shards passing passes" [ $? -eq 0 ]
check "every shard ran" [ "$(ls "$WORK/out" | wc -l | tr -d ' ')" = 3 ]
check "shards have their own profile" grep -q 'default-1.profraw' "$WORK/out/shard-1"
check "shards have their own settings folder" grep -q 'agentsynth-settings-2' "$WORK/out/shard-2"

out="$(run FAKE_FAIL=1)"
check "a failed shard fails the step" [ $? -ne 0 ]
check "the failure names the shard" grep -q 'Shard 1 failed (exit 3)' <<<"$out"

run FAKE_EMPTY=2 >/dev/null
check "a shard with no summary line fails the step" [ $? -ne 0 ]

out="$(run FAKE_HANG=0 LINUX_TEST_SHARD_SECONDS=1)"
check "a hung shard is killed and fails the step" [ $? -ne 0 ]
check "the hung shard is named" grep -q 'Shard 0 failed' <<<"$out"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
