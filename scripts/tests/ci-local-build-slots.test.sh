#!/usr/bin/env bash
#
# Tests scripts/lib/build-slots.sh: one build per folder (the second run fails at once and names the
# holder), the slot count, and a build that waits for a held machine-wide slot and then runs.

set -uo pipefail
# `ci-local.sh --priority` exports this into every step; the tests below set it themselves where they mean it, so a
# priority gate must not turn the waiting tests into instant builds.
unset CI_LOCAL_PRIORITY
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/ci-local-build-slots.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
failures=0
# The waiting notices appear once a waiter has started; on a loaded machine that takes longer than a fixed sleep, so the
# tests below poll for the notice (up to 20 s) before checking.
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
    for _ in $(seq 1 100); do grep -q "waiting for a build slot" "$TMP/wait.out" 2>/dev/null && break; sleep 0.2; done
    sleep 1
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
    for _ in $(seq 1 100); do grep -q "waiting for a build slot" "$TMP/over.out" 2>/dev/null && break; sleep 0.2; done
    check "a build over the threshold waits for the slot" grep -q 'waiting for a build slot' "$TMP/over.out"
    touch "$TMP/free2"
    wait "$over"
    check "it builds when the slot is released" grep -q '^over-built$' "$TMP/over.out"
    wait "$holder"
    log="$(dirname "$(build_slots_dir_for "$TMP/repo")")/ci-local-build-times.log"
    check "each build is logged with eight fields (CPU seconds last)" test "$(tail -1 "$log" | awk -F'\t' '{print NF}')" = 8
    check "the small build is logged with its edge count" awk -F'\t' '$3==10 {f=1} END {exit !f}' "$log"
    out="$(CI_LOCAL_BUILD_SLOTS=2 bash -c "$slot_script; test_slot_run \"\$dir\" 2 5 true; build_slot_run '$TMP/repo' 8 500 echo free-slot-built" 2>&1)"
    check "a free slot builds without waiting" test "$out" = "free-slot-built"
fi

# --- CPU seconds and the stall watchdog --------------------------------------------------------------
if [ "$(bash -c "source '$REPO_ROOT/scripts/lib/test-shards.sh'; test_slot_pick_tool")" != none ]; then
    log="$(dirname "$(build_slots_dir_for "$TMP/repo")")/ci-local-build-times.log"
    CI_LOCAL_BUILD_SLOTS=1 bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slot_run '$TMP/repo' 8 500 bash -c 'i=0; while [ \$i -lt 60000 ]; do i=\$((i+1)); done'" >/dev/null 2>&1
    check "the CPU seconds of a build are logged as the last field" \
        awk -F'\t' 'END { exit !(NF == 8 && $8 + 0 > 0) }' "$log"

    run_stall() { # run_stall <build dir> <command> -- a build holding the only slot, stall limit 2 s
        CI_LOCAL_BUILD_SLOTS=1 BUILD_SLOT_BUILD_DIR="$1" BUILD_SLOT_STALL_SECS=2 BUILD_SLOT_STALL_POLL=1 \
            bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slot_run '$TMP/repo' 8 500 bash -c \"\$0\"" "$2"
    }
    slots_busy_now() { bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slots_busy \"\$(build_slots_dir '$TMP/repo')\" 1"; }

    mkdir -p "$TMP/stalled-build"
    run_stall "$TMP/stalled-build" "touch '$TMP/stalled-up'; sleep 9; echo stalled-done" >"$TMP/stall.out" 2>&1 &
    stalled=$!
    for _ in $(seq 1 50); do [ -e "$TMP/stalled-up" ] && break; sleep 0.2; done
    check "while it still writes nothing the build holds the slot" test "$(slots_busy_now)" = 1
    for _ in $(seq 1 40); do grep -q 'releasing this build slot' "$TMP/stall.out" 2>/dev/null && break; sleep 0.2; done
    check "a build with no new .o/.a file past the limit gets a warning" grep -q 'releasing this build slot' "$TMP/stall.out"
    check "its slot is free while it keeps running" test "$(slots_busy_now)" = 0
    check "the build is still running (it is not killed)" kill -0 "$stalled"
    wait "$stalled"
    check "the stalled build finishes on its own" grep -q '^stalled-done$' "$TMP/stall.out"

    mkdir -p "$TMP/rc-build"
    run_stall "$TMP/rc-build" "sleep 4; exit 7" >/dev/null 2>&1
    check "a build whose slot was released still returns its own exit status" test $? -eq 7

    mkdir -p "$TMP/active-build"
    run_stall "$TMP/active-build" "touch '$TMP/active-up'; for n in 1 2 3 4 5 6 7 8 9 10 11 12; do touch '$TMP/active-build/f.o'; sleep 0.5; done; echo active-done" >"$TMP/active.out" 2>&1 &
    active=$!
    for _ in $(seq 1 50); do [ -e "$TMP/active-up" ] && break; sleep 0.2; done
    sleep 4
    check "a build that keeps writing .o files keeps its slot" test "$(slots_busy_now)" = 1
    wait "$active"
    check "and gets no warning" test "$(grep -c 'releasing this build slot' "$TMP/active.out")" = 0
    check "the active build ran to the end" grep -q '^active-done$' "$TMP/active.out"
fi

# --- a priority build takes the one reserved slot -------------------------------------------------
if [ "$(bash -c "source '$REPO_ROOT/scripts/lib/test-shards.sh'; test_slot_pick_tool")" != none ]; then
    rm -f "$TMP/pheld" "$TMP/pfree"
    bash -c "$slot_script; test_slot_run \"\$dir\" 1 60 bash -c 'touch \"$TMP/pheld\"; while [ ! -e \"$TMP/pfree\" ]; do sleep 0.2; done'" &
    holder=$!
    for _ in $(seq 1 50); do [ -e "$TMP/pheld" ] && break; sleep 0.2; done
    start=$(date +%s)
    out="$(CI_LOCAL_PRIORITY=1 CI_LOCAL_BUILD_SLOTS=1 CI_LOCAL_BUILD_POLL=0.2 bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slot_run '$TMP/repo' 8 500 echo prio-built" 2>&1)"
    check "a priority build runs at once with every normal slot held" grep -q '^prio-built$' <<<"$out"
    check "it does not wait" test "$(grep -c 'waiting for a build slot' <<<"$out")" = 0
    check "it takes no more than a few seconds" test $(($(date +%s) - start)) -lt 10
    check "it says it is a priority build" grep -q 'priority build' <<<"$out"
    log="$(dirname "$(build_slots_dir_for "$TMP/repo")")/ci-local-build-times.log"
    check "the log line is marked priority" awk -F'\t' '$7=="priority" {f=1} END {exit !f}' "$log"
    # A second priority build waits while the first holds the reserved slot; a normal build still waits too.
    bash -c "$slot_script; test_slot_run \"\$dir\" 2 60 bash -c 'touch \"$TMP/rheld\"; while [ ! -e \"$TMP/rfree\" ]; do sleep 0.2; done'" &
    holder2=$!
    for _ in $(seq 1 50); do [ -e "$TMP/rheld" ] && break; sleep 0.2; done
    (CI_LOCAL_PRIORITY=1 CI_LOCAL_BUILD_SLOTS=1 CI_LOCAL_BUILD_POLL=0.2 bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slot_run '$TMP/repo' 8 500 echo prio2-built" >"$TMP/prio2.out" 2>&1) &
    prio2=$!
    (CI_LOCAL_BUILD_SLOTS=1 CI_LOCAL_BUILD_POLL=0.2 bash -c "source '$REPO_ROOT/scripts/lib/build-slots.sh'; build_slot_run '$TMP/repo' 8 500 echo normal-built" >"$TMP/normal.out" 2>&1) &
    normal=$!
    for _ in $(seq 1 100); do grep -q "waiting for a build slot" "$TMP/prio2.out" 2>/dev/null && break; sleep 0.2; done
    sleep 1
    check "a second priority build waits while the reserved slot is held" grep -q 'waiting for a build slot' "$TMP/prio2.out"
    check "a normal build never takes the reserved slot" test "$(grep -c '^normal-built$' "$TMP/normal.out")" = 0
    touch "$TMP/rfree"
    wait "$holder2"
    wait "$prio2"
    check "the second priority build runs once the reserved slot is free" grep -q '^prio2-built$' "$TMP/prio2.out"
    touch "$TMP/pfree"
    wait "$holder"
    wait "$normal"
    check "the normal build runs once its slot is free" grep -q '^normal-built$' "$TMP/normal.out"
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
