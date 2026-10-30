#!/usr/bin/env bash
#
# Tests scripts/lib/test-shards.sh with a fake test binary: the slot limit holds across gates, a slot
# frees when its holder dies, each shard gets its own state, and a red or crashed shard fails the run.

set -uo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/test-shards.sh
source "$REPO_ROOT/scripts/lib/test-shards.sh"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
failures=0
check() { # check <name> <condition...>
    local name="$1"
    shift
    if "$@"; then echo "ok: $name"; else echo "FAIL: $name"; failures=$((failures + 1)); fi
}

mkdir -p "$TMP/repo"
git -C "$TMP/repo" init -q

fake() { # fake <path> <body>
    { printf '#!/usr/bin/env bash\n'; printf '%s\n' "$2"; } >"$1"
    chmod +x "$1"
}

# --- every shard runs, with its own folders -----------------------------------------------------
fake "$TMP/ok" "echo \"\$GTEST_SHARD_INDEX/\$GTEST_TOTAL_SHARDS \$AGENTSYNTH_SETTINGS_DIR \$TMPDIR \$0\" >>\"$TMP/seen\"
echo '[==========] 5 tests from 1 test suite ran. (1 ms total)'"
out="$(CI_LOCAL_TEST_SHARDS=4 test_shards_run "$TMP/repo" "$TMP/work" "$TMP/ok" 2>&1)"
check "a green run succeeds" test $? -eq 0
check "the tests of all shards are counted" grep -q '20 tests ran in 4 shards' <<<"$out"
check "the work folder goes after a green run" test ! -e "$TMP/work"
check "each shard has its own index" test "$(cut -d' ' -f1 "$TMP/seen" | sort -u | wc -l | tr -d ' ')" = 4
check "each shard has its own settings" test "$(cut -d' ' -f2 "$TMP/seen" | sort -u | wc -l | tr -d ' ')" = 4
check "each shard has its own temp folder" test "$(cut -d' ' -f3 "$TMP/seen" | sort -u | wc -l | tr -d ' ')" = 4
check "each shard has its own executable name" test "$(cut -d' ' -f4 "$TMP/seen" | xargs -n1 basename | sort -u | wc -l | tr -d ' ')" = 4

# --- failures -----------------------------------------------------------------------------------
fake "$TMP/red" "[ \"\$GTEST_SHARD_INDEX\" = 1 ] && { echo '[  FAILED  ] Suite.Name'; exit 1; }
echo '[==========] 1 test from 1 test suite ran.'"
out="$(CI_LOCAL_TEST_SHARDS=3 test_shards_run "$TMP/repo" "$TMP/work" "$TMP/red" 2>&1)"
check "a shard red twice fails the run" test $? -ne 0
check "the failing test is printed" grep -q 'FAILED  \] Suite.Name' <<<"$out"
check "a red run keeps its logs" test -f "$TMP/work/shard-1/log"

fake "$TMP/flaky" "if [ \"\$GTEST_SHARD_INDEX\" = 1 ] && [ ! -e \"$TMP/flaked\" ]; then touch \"$TMP/flaked\"; echo '[  FAILED  ] Suite.Flaky'; exit 1; fi
echo '[==========] 1 test from 1 test suite ran.'"
out="$(CI_LOCAL_TEST_SHARDS=3 test_shards_run "$TMP/repo" "$TMP/work" "$TMP/flaky" 2>&1)"
check "a shard green the second time passes the run" test $? -eq 0
check "the flaky test is named" grep -q 'FAILED  \] Suite.Flaky' <<<"$out"
check "the flaky shard is called out" grep -q 'FLAKY shard 1 passed' <<<"$out"
check "the second run's tests are counted once" grep -q '3 tests ran' <<<"$out"

fake "$TMP/silent" "exit 0"
CI_LOCAL_TEST_SHARDS=2 test_shards_run "$TMP/repo" "$TMP/work" "$TMP/silent" >/dev/null 2>&1
check "a shard that exits 0 with no summary fails the run" test $? -ne 0

# --- the slot limit holds across overlapping gates ----------------------------------------------
fake "$TMP/count" "n=\$(mktemp \"$TMP/running.XXXXXX\")
ls \"$TMP\"/running.* | wc -l >>\"$TMP/peak\"
sleep 0.3
rm -f \"\$n\"
echo '[==========] 1 test from 1 test suite ran.'"
(CI_LOCAL_TEST_SLOTS=2 CI_LOCAL_TEST_SHARDS=3 test_shards_run "$TMP/repo" "$TMP/w1" "$TMP/count" >/dev/null 2>&1) &
one=$!
(CI_LOCAL_TEST_SLOTS=2 CI_LOCAL_TEST_SHARDS=3 test_shards_run "$TMP/repo" "$TMP/w2" "$TMP/count" >/dev/null 2>&1) &
two=$!
wait "$one"; rc1=$?
wait "$two"; rc2=$?
check "both overlapping gates finish" test "$rc1$rc2" = 00
check "never more test processes than slots" test "$(sort -n "$TMP/peak" | tail -1 | tr -d ' ')" -le 2

# --- a slot waits, times out, and frees when its holder dies ------------------------------------
dir="$(test_slots_dir "$TMP/repo")"
# The holder is the test process itself, so killing it is what a crashed test does.
test_slot_run "$dir" 1 60 bash -c "echo \$\$ >\"$TMP/holder\"; exec sleep 30" &
holder=$!
sleep 1
test_slot_run "$dir" 1 1 true
check "a full machine times out with the distinct status" test $? -eq "$TEST_SLOT_TIMEOUT_STATUS"
kill "$(cat "$TMP/holder")" 2>/dev/null
wait "$holder" 2>/dev/null
test_slot_run "$dir" 1 5 true
check "a dead holder frees its slot" test $? -eq 0

TEST_SLOT_TOOL=none test_slot_run "$dir" 1 1 true
check "no lock tool still runs the command" test $? -eq 0

# --- the slot folder is shared between worktrees ------------------------------------------------
git -C "$TMP/repo" -c user.name=t -c user.email=t@t commit -q --allow-empty -m x
git -C "$TMP/repo" worktree add -q "$TMP/wt"
check "worktrees share one slot folder" test "$(test_slots_dir "$TMP/repo")" = "$(test_slots_dir "$TMP/wt")"

[ "$failures" -eq 0 ] && echo "ci-local-test-shards: all passed" || exit 1
