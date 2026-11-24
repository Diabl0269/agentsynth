# scripts/lib/build-slots.sh -- sourced by scripts/ci-local.sh, not executed directly.
#
# Shares the machine fairly between gates that compile at the same time. Two limits:
#
#   1. One build per build folder. A non-blocking advisory lock on <build dir>/.ci-local-build.lock is
#      held for the whole build and test run; a second run in the same folder exits at once and names
#      the holder's pid (kept in <build dir>/.ci-local-build.pid).
#   2. A fixed number of build slots on the machine, shared by every worktree of the repository (the
#      slot mechanism of scripts/lib/test-shards.sh, in a sibling folder). Only the compile step holds
#      one, and only a heavy build (more than CI_LOCAL_SLOT_MIN_EDGES steps left); a gate with no free slot says so once and polls every few seconds.
#
# A priority build (CI_LOCAL_PRIORITY=1, or ci-local.sh --priority) may also take one extra reserved slot
# beyond CI_LOCAL_BUILD_SLOTS, so an urgent session starts at once when every normal slot is busy. The
# reserved slot is one more slot file in the same folder that ordinary builds never try, so at most one
# priority build holds it at a time (a second priority build waits for it or for a normal slot).
#
# Each build appends a line to <git common dir>/ci-local-build-times.log (time, worktree, steps, seconds
# waited, build seconds, slots in use, "priority" or empty, CPU seconds) for tuning the slot count. Build
# seconds is wall time while the slot is held; CPU seconds is the user plus system time of the build
# command and its children (two decimals), so a slot held by a build that mostly waits shows up.
#
# A build that holds a slot but writes no .o or .a file for BUILD_SLOT_STALL_SECS (default 1800) in the
# build folder (BUILD_SLOT_BUILD_DIR, set by ci-local.sh) is presumed stuck: a warning is printed and
# its slot is RELEASED, so the gates queued behind it can start. The build itself is NOT killed; it
# keeps running without a slot, and the folder lock still applies.
#
# Both are advisory locks on a held file, so a crashed run frees them. Tools: flock (Linux) or lockf
# (macOS). Neither present: a warning, and no limit.
#
# Environment:
#   CI_LOCAL_BUILD_SLOTS         builds allowed at once on the machine (default: cores / jobs, at least 1)
#   CI_LOCAL_PRIORITY            1: this build may also take the one reserved extra slot
#   CI_LOCAL_BUILD_SLOT_TIMEOUT  seconds a build may wait for a slot (default: 7200)
#   CI_LOCAL_SLOT_MIN_EDGES      a build with this many steps left or fewer takes no slot (default: 60)
#   CI_LOCAL_BUILD_POLL          seconds between looks at the slots (default: 3)
#   BUILD_SLOT_BUILD_DIR         the build folder the stall watchdog watches for new .o/.a files
#   BUILD_SLOT_STALL_SECS        seconds without a new .o/.a file before the slot is released (default: 1800; 0: off)
#   BUILD_SLOT_STALL_POLL        seconds between watchdog looks (default: a third of the above, 1 to 30)
#   TEST_SLOT_TOOL               flock|lockf|none, forces the tool (for the unit test)

# shellcheck source=scripts/lib/test-shards.sh
source "$(dirname "${BASH_SOURCE[0]}")/test-shards.sh"

BUILD_DIR_LOCK_HOLDER=""

# build_slots_total <jobs>
build_slots_total() {
    case "${CI_LOCAL_BUILD_SLOTS:-}" in
        '' | *[!0-9]* | 0) ;;
        *)
            echo "$CI_LOCAL_BUILD_SLOTS"
            return 0
            ;;
    esac
    local jobs="${1:-1}" slots
    [ "$jobs" -lt 1 ] && jobs=1
    slots=$(($(test_shards_cores) / jobs))
    [ "$slots" -lt 1 ] && slots=1
    echo "$slots"
}

# build_slots_dir <repo_root>
build_slots_dir() {
    local dir
    dir="$(test_slots_dir "$1")"
    printf '%s/agentsynth-build-slots\n' "$(dirname "$dir")"
}

# build_dir_lock <build_dir>
#
# Takes the build folder's lock for the rest of this process (a helper process holds the locked file
# and ends when this process does). Returns 1, after printing who holds it, when another run already has it.
build_dir_lock() {
    local dir="$1" tool lock pidfile token holder i
    tool="$(test_slot_pick_tool)"
    if [ "$tool" = none ]; then
        echo "ci-local: WARNING neither flock nor lockf found -- no one-build-per-folder lock."
        return 0
    fi
    mkdir -p "$dir"
    lock="$dir/.ci-local-build.lock"
    pidfile="$dir/.ci-local-build.pid"
    token="$$-$(date +%s)-$RANDOM"
    local body="echo '$$ $token' >'$pidfile.$token' && mv '$pidfile.$token' '$pidfile'; while kill -0 $$ 2>/dev/null; do sleep 1; done"
    case "$tool" in
        flock) flock -n -E 75 "$lock" bash -c "$body" >/dev/null 2>&1 & ;;
        *) lockf -s -t 0 "$lock" bash -c "$body" >/dev/null 2>&1 & ;;
    esac
    holder=$!
    i=0
    while [ "$i" -lt 150 ]; do
        if [ "$(cut -d' ' -f2 "$pidfile" 2>/dev/null)" = "$token" ]; then
            BUILD_DIR_LOCK_HOLDER="$holder"
            trap 'build_dir_unlock' EXIT
            return 0
        fi
        kill -0 "$holder" 2>/dev/null || break
        sleep 0.2
        i=$((i + 1))
    done
    wait "$holder" 2>/dev/null || true
    echo "ci-local: another build is already running in $dir (pid $(cut -d' ' -f1 "$pidfile" 2>/dev/null || echo unknown))" >&2
    return 1
}

build_dir_unlock() {
    [ -n "$BUILD_DIR_LOCK_HOLDER" ] && kill "$BUILD_DIR_LOCK_HOLDER" 2>/dev/null || true
    return 0
}

# build_edges_left <build_dir> [targets...] -- how many build steps ninja still has to run, or "unknown".
# CI_LOCAL_BUILD_EDGES forces the number (for the unit test).
build_edges_left() {
    local dir="$1" n
    shift
    if [ -n "${CI_LOCAL_BUILD_EDGES:-}" ]; then
        echo "$CI_LOCAL_BUILD_EDGES"
        return 0
    fi
    if [ ! -f "$dir/build.ninja" ] || ! command -v ninja >/dev/null 2>&1; then
        echo unknown
        return 0
    fi
    local plan
    plan="$(ninja -C "$dir" -n "$@" 2>/dev/null || true)"
    # A stale build.ninja makes the dry run stop after the regenerate step, so its count says nothing about
    # the real build: treat it as unknown (heavy).
    if printf '%s\n' "$plan" | grep -q 'Re-running CMake\|Re-checking globbed directories'; then
        echo unknown
        return 0
    fi
    n="$(printf '%s\n' "$plan" | grep -c '^\[' || true)"
    echo "${n:-unknown}"
}

# build_slots_busy <slots_dir> <slots> -- how many slots another run holds right now.
build_slots_busy() {
    local dir="$1" slots="$2" tool i=0 busy=0 rc
    tool="$(test_slot_pick_tool)"
    [ "$tool" = none ] && { echo 0; return 0; }
    mkdir -p "$dir"
    while [ "$i" -lt "$slots" ]; do
        rc=0
        case "$tool" in
            flock) flock -n -E "$TEST_SLOT_TIMEOUT_STATUS" "$dir/slot-$i" true || rc=$? ;;
            *) lockf -s -t 0 "$dir/slot-$i" true || rc=$? ;;
        esac
        [ "$rc" -eq "$TEST_SLOT_TIMEOUT_STATUS" ] && busy=$((busy + 1))
        i=$((i + 1))
    done
    echo "$busy"
}

# build_slot_wrapper -- the shell snippet that runs inside the slot: <stamp> <slots dir> <build dir>
# <stall secs> <poll secs> <lock tool> <command> [args...]. Writes the start time to <stamp>, runs the command (kept
# in the foreground, so Ctrl-C reaches it), writes the CPU seconds of the command to <stamp>.cpu, and
# exits with the command's status. A background watchdog releases the slot when no .o/.a file in the
# build folder changed for <stall secs>: with flock, by unlocking the slot file the tool left open in
# this process; with lockf, which keeps the lock in its own process, by ending that process.
BUILD_SLOT_WRAPPER='
stamp=$0 sdir=$1 bdir=$2 stall=$3 poll=$4 tool=$5
shift 5
date +%s >"$stamp"
echo $$ >"$stamp.wrap"
lockfds=""
lockpid=$PPID
canrelease=""
if [ -n "$sdir" ] && [ "$stall" -gt 0 ] && [ -d "$bdir" ]; then
    [ "$tool" = lockf ] && canrelease=1
    for fd in 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        [ -e "/dev/fd/$fd" ] || continue
        for f in "$sdir"/slot-*; do
            if [ "/dev/fd/$fd" -ef "$f" ]; then lockfds="$lockfds $fd"; break; fi
        done
    done
fi
watchdog=""
[ "$tool" = flock ] && [ -n "$lockfds" ] && canrelease=1
if [ -n "$canrelease" ]; then
    closes=""
    for fd in $lockfds; do closes="$closes $fd>&-"; done
    (
        marker="$stamp.touch"
        touch "$marker"
        last=$(date +%s)
        while :; do
            eval "sleep $poll $closes"
            now=$(date +%s)
            if [ -n "$(find "$bdir" -type f \( -name "*.o" -o -name "*.a" \) -newer "$marker" -print -quit 2>/dev/null)" ]; then
                touch "$marker"
                last=$now
            elif [ $((now - last)) -ge "$stall" ]; then
                echo "ci-local: WARNING no .o/.a file changed in $bdir for ${stall}s: releasing this build slot so queued gates can start (the build is not killed and keeps running)." >&2
                if [ "$tool" = lockf ]; then
                    # lockf holds the lock in its own process and does not pass the file on: ending it
                    # (this command carries on, and build_slot_run waits for it) frees the slot.
                    kill "$lockpid" 2>/dev/null
                else
                    for fd in $lockfds; do
                        python3 -c "import fcntl,sys; fcntl.flock(int(sys.argv[1]), fcntl.LOCK_UN)" "$fd" 2>/dev/null
                    done
                fi
                exit 0
            fi
        done
    ) &
    watchdog=$!
fi
rc=0
"$@" || rc=$?
if [ -n "$watchdog" ]; then
    kill "$watchdog" 2>/dev/null
    wait "$watchdog" 2>/dev/null
fi
rm -f "$stamp.touch"
# `times` must run in this shell, not in a pipeline subshell, or the children it reports are not ours.
times >"$stamp.times"
tail -1 "$stamp.times" | awk '"'"'{ t = 0; for (i = 1; i <= NF; i++) { split($i, a, "m"); t += a[1] * 60 + a[2] } printf "%.2f\n", t }'"'"' >"$stamp.cpu"
rm -f "$stamp.times"
echo "$rc" >"$stamp.rc"
exit "$rc"
'

# build_slot_run <repo_root> <jobs> <edges> <command> [args...]
#
# Runs the command (the compile step). With more than CI_LOCAL_SLOT_MIN_EDGES steps left (default 60, or
# an unknown count) it holds a build slot; a small incremental build never queues behind a full one.
# Appends one line to <git common dir>/ci-local-build-times.log: timestamp, worktree, edges, seconds
# waited for a slot, build seconds, slots in use at the start, "priority" or empty, CPU seconds. Runs the
# stall watchdog described at the top while the slot is held.
build_slot_run() {
    local repo_root="$1" jobs="$2" edges="$3" slots dir timeout rc=0 min busy=0 t0 t1 t2 stamp prio=""
    shift 3
    slots="$(build_slots_total "$jobs")"
    [ "${CI_LOCAL_PRIORITY:-0}" = 1 ] && prio=priority
    dir="$(build_slots_dir "$repo_root")"
    timeout="${CI_LOCAL_BUILD_SLOT_TIMEOUT:-7200}"
    min="${CI_LOCAL_SLOT_MIN_EDGES:-60}"
    case "$min" in '' | *[!0-9]*) min=60 ;; esac
    stamp="$(mktemp "${TMPDIR:-/tmp}/build-slots.XXXXXX")"
    local stall="${BUILD_SLOT_STALL_SECS:-1800}" poll="${BUILD_SLOT_STALL_POLL:-}" cpu
    case "$stall" in '' | *[!0-9]*) stall=1800 ;; esac
    case "$poll" in '' | *[!0-9]*) poll=$((stall / 3)); [ "$poll" -lt 1 ] && poll=1; [ "$poll" -gt 30 ] && poll=30 ;; esac
    t0="$(date +%s)"
    if [ "$edges" != unknown ] && [ "$edges" -le "$min" ]; then
        echo "ci-local: $edges build steps left (at most $min): building without a build slot."
        bash -c "$BUILD_SLOT_WRAPPER" "$stamp" "" "" 0 1 none "$@" || rc=$?
    else
        if [ "$(test_slot_pick_tool)" = none ]; then
            echo "ci-local: WARNING neither flock nor lockf found -- the build runs with no machine-wide limit."
        fi
        busy="$(build_slots_busy "$dir" "$slots")"
        local take="$slots"
        if [ -n "$prio" ]; then
            take=$((slots + 1))
            echo "ci-local: priority build: may use the reserved slot beyond the $slots normal slot(s)."
        fi
        SLOT_WAIT_NOTICE="ci-local: waiting for a build slot ($busy of $slots in use by other gates)" \
            SLOT_POLL_SECONDS="${CI_LOCAL_BUILD_POLL:-3}" \
            test_slot_run "$dir" "$take" "$timeout" bash -c "$BUILD_SLOT_WRAPPER" "$stamp" "$dir" "${BUILD_SLOT_BUILD_DIR:-}" "$stall" "$poll" "$(test_slot_pick_tool)" "$@" || rc=$?
        if [ "$rc" -eq "$TEST_SLOT_TIMEOUT_STATUS" ]; then
            echo "ci-local: gave up waiting for a build slot after ${timeout}s; re-run, or raise CI_LOCAL_BUILD_SLOT_TIMEOUT." >&2
        fi
    fi
    # A released slot ended the lock tool while the build went on: wait for it, and take its own exit status.
    local wrap
    wrap="$(cat "$stamp.wrap" 2>/dev/null || true)"
    while [ -n "$wrap" ] && kill -0 "$wrap" 2>/dev/null; do sleep 0.5; done
    [ -s "$stamp.rc" ] && rc="$(cat "$stamp.rc")"
    t2="$(date +%s)"
    t1="$(cat "$stamp" 2>/dev/null || true)"
    [ -n "$t1" ] || t1="$t2"
    cpu="$(cat "$stamp.cpu" 2>/dev/null || true)"
    [ -n "$cpu" ] || cpu=0.00
    rm -f "$stamp" "$stamp.cpu" "$stamp.touch" "$stamp.wrap" "$stamp.rc"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$(date '+%Y-%m-%dT%H:%M:%S')" "$(basename "$repo_root")" "$edges" \
        $((t1 - t0)) $((t2 - t1)) "$busy" "$prio" "$cpu" >>"$(dirname "$dir")/ci-local-build-times.log" 2>/dev/null || true
    return "$rc"
}
