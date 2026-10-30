# scripts/lib/test-shards.sh -- sourced by scripts/ci-local.sh, not executed directly.
#
# Runs the Tests binary as GoogleTest shards, in parallel, without one gate waiting for another's
# whole suite. Two rules make overlapping suites safe:
#
#   1. Each shard owns its on-disk state. It gets its own AGENTSYNTH_SETTINGS_DIR and TMPDIR, and
#      runs through a uniquely named hard link of the binary. The link is what isolates JUCE's temp
#      folder on macOS: JUCE ignores TMPDIR there and uses ~/Library/Caches/<executable name>, where the suite's stretched-loop cache and about 40
#      fixed-name temp files live. Linux and Windows follow TMPDIR.
#   2. The machine has a fixed number of test slots, shared by every worktree of the repository.
#      A shard holds one slot while it runs, so six gates at once overlap instead of queueing whole
#      suites, and the machine never runs more than the slot count of test processes.
#
# A slot is an advisory lock on a file under the git common dir, so a crashed run frees its slots.
# Tools: flock (Linux) or lockf (macOS). Neither present: shards run with no slot limit and a warning.
#
# Environment:
#   CI_LOCAL_TEST_SHARDS        shards per suite (default: 8)
#   CI_LOCAL_TEST_SLOTS         test processes allowed on the machine (default: three quarters of the cores)
#   CI_LOCAL_TEST_SLOT_TIMEOUT  seconds one shard may wait for a slot (default: 3600)
#   TEST_SLOT_TOOL              flock|lockf|none, forces the tool (for the unit test)

TEST_SLOT_TIMEOUT_STATUS=75

test_shards_cores() {
    (command -v nproc >/dev/null 2>&1 && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4
}

# test_slots_dir <repo_root> -- the slot folder every worktree of <repo_root> shares.
test_slots_dir() {
    local repo_root="$1" common_dir
    common_dir="$(git -C "$repo_root" rev-parse --git-common-dir 2>/dev/null || true)"
    if [ -z "$common_dir" ]; then
        printf '%s/.test-slots\n' "$repo_root"
        return 0
    fi
    case "$common_dir" in
        /*) : ;;
        *) common_dir="$repo_root/$common_dir" ;;
    esac
    common_dir="$(cd "$common_dir" && pwd -P)"
    printf '%s/agentsynth-test-slots\n' "$common_dir"
}

test_slots_total() {
    case "${CI_LOCAL_TEST_SLOTS:-}" in
        '' | *[!0-9]* | 0) ;;
        *)
            echo "$CI_LOCAL_TEST_SLOTS"
            return 0
            ;;
    esac
    local slots=$(($(test_shards_cores) * 3 / 4))
    [ "$slots" -lt 1 ] && slots=1
    echo "$slots"
}

test_slot_pick_tool() {
    if [ -n "${TEST_SLOT_TOOL:-}" ]; then
        printf '%s\n' "$TEST_SLOT_TOOL"
    elif command -v flock >/dev/null 2>&1; then
        echo flock
    elif command -v lockf >/dev/null 2>&1; then
        echo lockf
    else
        echo none
    fi
}

# test_slot_run <slots_dir> <slots> <timeout_seconds> <command> [args...]
#
# Runs <command> while holding one free slot. Returns its exit status, or TEST_SLOT_TIMEOUT_STATUS
# when no slot came free in time. A command that itself exits 75 is simply run again in another
# slot; nothing the suite does exits 75.
test_slot_run() {
    local dir="$1" slots="$2" timeout="$3"
    shift 3
    local tool rc i start
    tool="$(test_slot_pick_tool)"
    if [ "$tool" = none ]; then
        "$@"
        return $?
    fi
    mkdir -p "$dir"
    start="$(date +%s)"
    while :; do
        i=0
        while [ "$i" -lt "$slots" ]; do
            case "$tool" in
                flock) flock -n -E "$TEST_SLOT_TIMEOUT_STATUS" "$dir/slot-$i" "$@" ;;
                *) lockf -s -t 0 "$dir/slot-$i" "$@" ;;
            esac
            rc=$?
            [ "$rc" -ne "$TEST_SLOT_TIMEOUT_STATUS" ] && return "$rc"
            i=$((i + 1))
        done
        [ $(($(date +%s) - start)) -ge "$timeout" ] && return "$TEST_SLOT_TIMEOUT_STATUS"
        sleep 1
    done
}

# test_shard_start <shard_dir> <link_name> <binary> <index> <total> <slots_dir> <slots> <timeout> [gtest args...]
test_shard_start() {
    local shard_dir="$1" name="$2" binary="$3" index="$4" total="$5" dir="$6" slots="$7" timeout="$8"
    shift 8
    mkdir -p "$shard_dir/settings" "$shard_dir/tmp"
    [ -e "$shard_dir/$name" ] || ln "$binary" "$shard_dir/$name" 2>/dev/null || cp "$binary" "$shard_dir/$name"
    (
        export GTEST_TOTAL_SHARDS="$total" GTEST_SHARD_INDEX="$index"
        export AGENTSYNTH_SETTINGS_DIR="$shard_dir/settings" TMPDIR="$shard_dir/tmp"
        test_slot_run "$dir" "$slots" "$timeout" "$shard_dir/$name" "$@" >"$shard_dir/log" 2>&1
    )
}

# test_shard_green <exit status> <log>
test_shard_green() {
    [ "$1" -eq 0 ] && grep -aqE '^\[==========\] [0-9]+ tests? from' "$2"
}

# test_shards_run <repo_root> <work_dir> <tests_binary> [gtest args...]
#
# Runs every shard. A red shard (a failed test, or a process that died) runs once more on its own
# after the others finish, so a test that only fails on a fully loaded machine doesn't cost a whole
# gate. It is reported by name as FLAKY either way. Returns 0 only when each shard ended up exiting 0
# and printing gtest's summary, whose absence means the process died before finishing.
test_shards_run() {
    local repo_root="$1" work="$2" binary="$3"
    shift 3
    local shards="${CI_LOCAL_TEST_SHARDS:-8}"
    case "$shards" in '' | *[!0-9]* | 0) shards=8 ;; esac
    local slots dir timeout tool run_id i started
    slots="$(test_slots_total)"
    dir="$(test_slots_dir "$repo_root")"
    timeout="${CI_LOCAL_TEST_SLOT_TIMEOUT:-3600}"
    tool="$(test_slot_pick_tool)"
    run_id="$$-$(date +%s)"
    started="$(date +%s)"
    [ "$tool" = none ] && echo "ci-local: WARNING neither flock nor lockf found -- shards run with no machine-wide slot limit."

    rm -rf "$work"
    mkdir -p "$work"
    echo "ci-local: $shards shards, up to $slots test processes on this machine (slots: $dir)."

    local pids=() retried=""
    i=0
    while [ "$i" -lt "$shards" ]; do
        test_shard_start "$work/shard-$i" "Tests-gate-$run_id-$i" "$binary" "$i" "$shards" "$dir" "$slots" "$timeout" "$@" &
        pids+=("$!")
        i=$((i + 1))
    done

    local overall=0 ran total=0 rc log
    i=0
    while [ "$i" -lt "$shards" ]; do
        rc=0
        wait "${pids[$i]}" || rc=$?
        log="$work/shard-$i/log"
        if ! test_shard_green "$rc" "$log"; then
            # A slot timeout is the machine, not the tests, and a second wait would not help.
            if [ "$rc" -eq "$TEST_SLOT_TIMEOUT_STATUS" ]; then
                echo "ci-local: shard $i gave up waiting for a test slot after ${timeout}s; re-run, or raise CI_LOCAL_TEST_SLOT_TIMEOUT." >&2
                overall=1
            else
                retried="$retried $i"
            fi
        fi
        i=$((i + 1))
    done

    for i in $retried; do
        log="$work/shard-$i/log"
        echo "ci-local: FLAKY? shard $i went red (load, or a real failure), running it again alone:"
        grep -aE 'FAILED  \] [A-Z]|^libc\+\+abi|Segmentation' "$log" | sort -u | head -20 || true
        mv "$log" "$log.first"
        rc=0
        test_shard_start "$work/shard-$i" "Tests-gate-$run_id-$i" "$binary" "$i" "$shards" "$dir" "$slots" "$timeout" "$@" || rc=$?
        if test_shard_green "$rc" "$log"; then
            echo "ci-local: FLAKY shard $i passed the second time; the tests above need fixing."
        else
            echo "=== shard $i failed twice (exit $rc), $log ==="
            grep -aE 'FAILED|Failure|^\[==========\]' "$log" | tail -40 || tail -n 20 "$log"
            overall=1
        fi
    done

    i=0
    while [ "$i" -lt "$shards" ]; do
        ran="$(grep -aoE '^\[==========\] [0-9]+ tests? from' "$work/shard-$i/log" | grep -oE '[0-9]+' | head -1 || true)"
        total=$((total + ${ran:-0}))
        # On macOS JUCE's temp folder is named after the link; nothing else ever removes it.
        rm -rf "${HOME:-/nonexistent}/Library/Caches/Tests-gate-$run_id-$i"
        i=$((i + 1))
    done

    echo "ci-local: $total tests ran in $shards shards, $(($(date +%s) - started)) s."
    [ "$overall" -eq 0 ] && rm -rf "$work"
    return "$overall"
}
