# scripts/lib/test-lock.sh -- sourced by scripts/ci-local.sh, not executed directly.
#
# WHY THIS EXISTS: two Tests binaries running at the same time on one machine collide through the
# shared on-disk "Agent Synth" ApplicationProperties file, even from separate worktrees with
# separate build directories (docs/development/local-ci.md "Running suites in parallel"). FRO192:
# two sessions in two worktrees ran their suites together and one reported
# ProjectLoadStripGainTest.CheckpointBC_RealAppOpenAndMixerPanelPreserveEachStripsSavedGain as
# FAILED; it passed alone and passed again under a lock. The other session was wrapping its run in
# a hand-rolled `lockf -t 3600 <repo>/.claude/worktrees/.tests.lock`, but scripts/ci-local.sh --
# the documented gate, and the pre-push hook -- ran the binary with no lock at all, so the
# documented path was exactly the one that raced. A false FAILED sends whoever sees it hunting a
# regression in unrelated code, so the gate itself now serialises the test step.
#
# The lock file lives under the repository's git COMMON dir (`git rev-parse --git-common-dir`),
# which every linked worktree of the same repository shares, so two worktrees resolve the same
# path without any of them knowing about the others. Locking uses whichever advisory-lock tool the
# platform ships: `flock` (util-linux, Linux) or `lockf` (BSD, macOS). Neither present: the run
# proceeds UNLOCKED with a warning rather than failing, since a missing tool must never turn a
# green suite red -- but that is a degraded mode, not a supported one.
#
# Usage (from scripts/ci-local.sh, around its test-run step):
#   source scripts/lib/test-lock.sh
#   lock="$(test_lock_path "$REPO_ROOT")"
#   test_lock_run "$lock" 3600 "$TESTS_BIN"
#   # exit status: the command's own, or TEST_LOCK_TIMEOUT_STATUS (75, EX_TEMPFAIL, the value
#   # lockf itself uses) when the lock could not be taken within the timeout.
#
# TEST_LOCK_TOOL=flock|lockf|none forces the tool (the unit test uses it; never needed otherwise).

TEST_LOCK_TIMEOUT_STATUS=75

# test_lock_path <repo_root>
#
# Prints the lock path shared by every worktree of the repository at <repo_root>. Falls back to a
# file under <repo_root> itself when it is not a git checkout at all (a tarball export), so callers
# never special-case that.
test_lock_path() {
    local repo_root="$1"
    local common_dir
    common_dir="$(git -C "$repo_root" rev-parse --git-common-dir 2>/dev/null || true)"
    if [ -z "$common_dir" ]; then
        printf '%s/.tests.lock\n' "$repo_root"
        return 0
    fi
    case "$common_dir" in
        /*) : ;;
        *) common_dir="$repo_root/$common_dir" ;;
    esac
    # Normalise (`..` segments, symlinks) so the SAME lock file is named from every worktree.
    common_dir="$(cd "$common_dir" && pwd -P)"
    printf '%s/agentsynth-tests.lock\n' "$common_dir"
}

# test_lock_pick_tool -- prints flock, lockf or none.
test_lock_pick_tool() {
    if [ -n "${TEST_LOCK_TOOL:-}" ]; then
        printf '%s\n' "$TEST_LOCK_TOOL"
    elif command -v flock >/dev/null 2>&1; then
        echo flock
    elif command -v lockf >/dev/null 2>&1; then
        echo lockf
    else
        echo none
    fi
}

# test_lock_run <lock_path> <timeout_seconds> <command> [args...]
#
# Runs <command> while holding <lock_path>. Prints a "waiting" line when the lock is already held
# so a session watching the output does not look hung. Returns the command's exit status, or
# TEST_LOCK_TIMEOUT_STATUS when the lock was not acquired within <timeout_seconds>.
test_lock_run() {
    local lock="$1" timeout="$2"
    shift 2
    local tool rc
    tool="$(test_lock_pick_tool)"
    mkdir -p "$(dirname "$lock")"

    case "$tool" in
        flock)
            if ! flock -n "$lock" true 2>/dev/null; then
                echo "ci-local: waiting for another test run to finish (lock: $lock, up to ${timeout}s)..."
            fi
            # -E: flock's own timeout status, so a timeout and a failing command stay telling apart.
            flock -w "$timeout" -E "$TEST_LOCK_TIMEOUT_STATUS" "$lock" "$@"
            rc=$?
            ;;
        lockf)
            if ! lockf -s -t 0 "$lock" true 2>/dev/null; then
                echo "ci-local: waiting for another test run to finish (lock: $lock, up to ${timeout}s)..."
            fi
            # lockf exits EX_TEMPFAIL (75) itself on timeout; -s keeps its own message quiet.
            lockf -s -t "$timeout" "$lock" "$@"
            rc=$?
            ;;
        *)
            echo "ci-local: WARNING neither flock nor lockf found -- running the suite UNLOCKED; a concurrent suite on this machine can cause false failures (see docs/development/local-ci.md)."
            "$@"
            rc=$?
            ;;
    esac

    if [ "$rc" -eq "$TEST_LOCK_TIMEOUT_STATUS" ] && [ "$tool" != none ]; then
        echo "ci-local: timed out after ${timeout}s waiting for the test lock ($lock)." >&2
    fi
    return "$rc"
}
