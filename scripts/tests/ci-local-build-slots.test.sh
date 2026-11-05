#!/usr/bin/env bash
#
# Tests scripts/lib/build-slots.sh: one build per folder (the second run fails at once and names the
# holder), the slot count, and a build that waits for a held machine-wide slot and then runs.

set -uo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

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

# Runs build_dir_lock in a fresh shell, then the given shell snippet while the lock is held.
run_locked() { # run_locked <build dir> <snippet>
    bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_dir_lock '$1' || exit 1; $2"
}

# --- one build per folder -------------------------------------------------------------------------
run_locked "$TMP/build" "touch '$TMP/first-in'; while [ ! -e '$TMP/release' ]; do sleep 0.2; done" &
first=$!
for _ in $(seq 1 50); do [ -e "$TMP/first-in" ] && break; sleep 0.2; done
start=$(date +%s)
out="$(run_locked "$TMP/build" 'echo second ran' 2>&1)"
rc=$?
check "a second run in the same folder fails" test "$rc" -ne 0
check "it fails fast" test $(($(date +%s) - start)) -lt 10
check "it names the folder and the holder" grep -qE "another build is already running in $TMP/build \(pid [0-9]+\)" <<<"$out"
check "the second run did not build" test "$out" = "${out/second ran/}"
out2="$(run_locked "$TMP/other-build" 'echo other ran' 2>&1)"
check "another folder is not blocked" grep -q 'other ran' <<<"$out2"
touch "$TMP/release"
wait "$first"
check "the first run finished" test $? -eq 0
out="$(run_locked "$TMP/build" 'echo again ran' 2>&1)"
check "the lock is free after the holder ends" grep -q 'again ran' <<<"$out"

# --- the slot count -------------------------------------------------------------------------------
slots() { bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slots_total $1"; }
check "slots are at least 1" test "$(slots 100000)" = 1
check "CI_LOCAL_BUILD_SLOTS overrides" test "$(CI_LOCAL_BUILD_SLOTS=5 slots 8)" = 5
check "a bad override is ignored" test "$(CI_LOCAL_BUILD_SLOTS=x slots 100000)" = 1

build_slots_dir_for() { bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slots_dir '$1'"; }

# --- a held slot makes the build wait, then it runs -----------------------------------------------
slot_script="source '$REPO_ROOT/scripts/lib/build-slots.sh'; dir=\"\$(build_slots_dir '$TMP/repo')\"; mkdir -p \"\$dir\""
if [ "$(bash -c "source '$REPO_ROOT/scripts/lib/test-shards.sh'; test_slot_pick_tool")" = none ]; then
    echo "skip: neither flock nor lockf"
else
    bash -c "$slot_script; test_slot_run \"\$dir\" 1 60 bash -c 'touch \"$TMP/held\"; while [ ! -e \"$TMP/free\" ]; do sleep 0.2; done'" &
    holder=$!
    for _ in $(seq 1 50); do [ -e "$TMP/held" ] && break; sleep 0.2; done
    (
        export CI_LOCAL_BUILD_SLOTS=1 CI_LOCAL_BUILD_POLL=0.2
        bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slot_run '$TMP/repo' 8 500 bash -c 'echo built'" >"$TMP/wait.out" 2>&1
    ) &
    waiter=$!
    sleep 2
    check "the second build waits while the slot is held" kill -0 "$waiter"
    check "it says so once" test "$(grep -c 'waiting for a build slot (1 of 1 in use by other gates)' "$TMP/wait.out")" = 1
    check "it has not built yet" test "$(grep -c '^built$' "$TMP/wait.out")" = 0
    touch "$TMP/free"
    wait "$waiter"
    check "it builds once the slot is released" grep -q '^built$' "$TMP/wait.out"
    wait "$holder"
    # A crashed holder frees its slot: nothing waits when two slots exist and one is held.
    # A small build takes no slot, so it runs at once even with every slot held.
    bash -c "$slot_script; test_slot_run \"\$dir\" 1 60 bash -c 'touch \"$TMP/held2\"; while [ ! -e \"$TMP/free2\" ]; do sleep 0.2; done'" &
    holder=$!
    for _ in $(seq 1 50); do [ -e "$TMP/held2" ] && break; sleep 0.2; done
    out="$(CI_LOCAL_BUILD_SLOTS=1 CI_LOCAL_BUILD_POLL=0.2 bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slot_run '$TMP/repo' 8 10 echo small-built" 2>&1)"
    check "a small build runs without a slot" grep -q '^small-built$' <<<"$out"
    check "it does not wait" test "$(grep -c 'waiting for a build slot' <<<"$out")" = 0
    (CI_LOCAL_SLOT_MIN_EDGES=5 CI_LOCAL_BUILD_SLOTS=1 CI_LOCAL_BUILD_POLL=0.2 bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slot_run '$TMP/repo' 8 10 echo over-built" >"$TMP/over.out" 2>&1) &
    over=$!
    sleep 2
    check "a build over the threshold waits for the slot" grep -q 'waiting for a build slot' "$TMP/over.out"
    touch "$TMP/free2"
    wait "$over"
    check "it builds when the slot is released" grep -q '^over-built$' "$TMP/over.out"
    wait "$holder"
    log="$(dirname "$(build_slots_dir_for "$TMP/repo")")/ci-local-build-times.log"
    check "each build is logged with six fields" test "$(head -1 "$log" | awk -F'\t' '{print NF}')" = 6
    check "the small build is logged with its edge count" awk -F'\t' '$3==10 {f=1} END {exit !f}' "$log"
    out="$(CI_LOCAL_BUILD_SLOTS=2 bash -c "$slot_script; test_slot_run \"\$dir\" 2 5 true; build_slot_run '$TMP/repo' 8 500 echo free-slot-built" 2>&1)"
    check "a free slot builds without waiting" test "$out" = "free-slot-built"
fi

# --- the step count -------------------------------------------------------------------------------
mkdir -p "$TMP/bin" "$TMP/count-build"
touch "$TMP/count-build/build.ninja"
printf '#!/bin/sh\nprintf "%%s\\n" "$FAKE_NINJA_OUT"\n' >"$TMP/bin/ninja"
chmod +x "$TMP/bin/ninja"
count() { PATH="$TMP/bin:$PATH" FAKE_NINJA_OUT="$1" bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_edges_left '$TMP/count-build' Tests"; }
check "a dry run's steps are counted" test "$(count $'[1/3] CXX a.o\n[2/3] CXX b.o\n[3/3] LINK Tests')" = 3
check "a stale build.ninja (dry run stops at the regenerate step) counts as unknown, so the build takes a slot" \
    test "$(count $'[1/2] Re-running CMake...\n[2/2] Re-checking globbed directories...')" = unknown

[ "$failures" -eq 0 ] || { echo "$failures check(s) failed"; exit 1; }
echo "all checks passed"
