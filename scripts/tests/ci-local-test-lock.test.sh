#!/usr/bin/env bash
#
# Unit tests for scripts/lib/test-lock.sh -- the shared test lock scripts/ci-local.sh takes around
# its test-run step (FRO192). Exercises test_lock_path against a real throwaway repo + worktree
# (so the "every worktree names the same file" property is tested for real, not by inspection) and
# test_lock_run with tiny shell commands standing in for the Tests binary: serialisation, the
# timeout status, exit-code passthrough, and the no-tool fallback. Runs in the Lint job -- no
# compiler, no network, a few seconds (the serialisation cases sleep briefly on purpose).
#
# Usage: bash scripts/tests/ci-local-test-lock.test.sh

set -uo pipefail

# When run from a git hook, git exports GIT_DIR and friends; the throwaway repos below must not
# act on the real one (same trap as scripts/ci-local.sh, FRO82).
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LIB="$SCRIPT_DIR/scripts/lib/test-lock.sh"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# shellcheck source=scripts/lib/test-lock.sh
source "$LIB"

pass=0
fail=0

ok() {
    echo "PASS: $1"
    pass=$((pass + 1))
}

ko() {
    echo "FAIL: $1"
    fail=$((fail + 1))
}

assert_eq() { # <desc> <expected> <actual>
    if [ "$2" = "$3" ]; then
        ok "$1"
    else
        ko "$1 -- expected '$2', got '$3'"
    fi
}

# --- test_lock_path ------------------------------------------------------------------------------

# A plain directory that is not a git checkout falls back to a file under the directory itself.
mkdir -p "$WORK/plain"
assert_eq "non-git directory falls back to <root>/.tests.lock" \
    "$WORK/plain/.tests.lock" "$(test_lock_path "$WORK/plain")"

# A repo and a linked worktree of it name the SAME lock file (the whole point).
git init -q "$WORK/repo"
git -C "$WORK/repo" -c user.name=t -c user.email=t@t commit -q --allow-empty -m init
git -C "$WORK/repo" worktree add -q "$WORK/wt" -b wt-branch
main_lock="$(test_lock_path "$WORK/repo")"
wt_lock="$(test_lock_path "$WORK/wt")"
assert_eq "worktree resolves the same lock file as its main checkout" "$main_lock" "$wt_lock"
case "$main_lock" in
    "$(cd "$WORK/repo/.git" && pwd -P)"/agentsynth-tests.lock) ok "lock file lives under the git common dir" ;;
    *) ko "lock file should live under the git common dir, got '$main_lock'" ;;
esac

# --- test_lock_run: which tools can this machine test? ----------------------------------------------

tools=()
command -v flock >/dev/null 2>&1 && tools+=(flock)
command -v lockf >/dev/null 2>&1 && tools+=(lockf)
if [ "${#tools[@]}" -eq 0 ]; then
    echo "SKIP: neither flock nor lockf on PATH -- only the no-tool fallback is testable here."
fi

for tool in ${tools[@]+"${tools[@]}"}; do
    export TEST_LOCK_TOOL="$tool"
    lock="$WORK/$tool.lock"
    log="$WORK/$tool.log"
    : >"$log"

    # Serialisation: two runs started together must not interleave. Each writes start/end around a
    # short sleep; an unlocked pair would produce "A-start B-start ...".
    test_lock_run "$lock" 30 sh -c "echo A-start >>'$log'; sleep 1; echo A-end >>'$log'" &
    pid_a=$!
    sleep 0.2
    test_lock_run "$lock" 30 sh -c "echo B-start >>'$log'; sleep 1; echo B-end >>'$log'" >"$WORK/$tool.b.out" &
    pid_b=$!
    wait "$pid_a" "$pid_b"
    assert_eq "[$tool] concurrent runs are serialised" \
        "A-start A-end B-start B-end" "$(tr '\n' ' ' <"$log" | sed 's/ $//')"
    if grep -q "waiting for another test run" "$WORK/$tool.b.out"; then
        ok "[$tool] the second run prints the waiting line"
    else
        ko "[$tool] the second run should print the waiting line, got: $(cat "$WORK/$tool.b.out")"
    fi

    # Timeout: with the lock held for longer than the timeout, the caller gets EX_TEMPFAIL and the
    # command never runs.
    test_lock_run "$lock" 30 sleep 3 &
    pid_hold=$!
    sleep 0.2
    marker="$WORK/$tool.ran"
    test_lock_run "$lock" 1 sh -c ": >'$marker'" >/dev/null 2>"$WORK/$tool.err"
    rc=$?
    assert_eq "[$tool] timeout returns TEST_LOCK_TIMEOUT_STATUS" "$TEST_LOCK_TIMEOUT_STATUS" "$rc"
    if [ -e "$marker" ]; then
        ko "[$tool] the command must not run when the lock timed out"
    else
        ok "[$tool] the command did not run on timeout"
    fi
    if grep -q "timed out" "$WORK/$tool.err"; then
        ok "[$tool] timeout is reported on stderr"
    else
        ko "[$tool] timeout should be reported on stderr"
    fi
    wait "$pid_hold"

    # Exit-code passthrough: the command's own status comes back untouched (ci-local's `fail`
    # relies on this to tell a red suite from a lock problem).
    test_lock_run "$lock" 5 sh -c 'exit 3' >/dev/null
    assert_eq "[$tool] command exit status passes through" 3 "$?"
    test_lock_run "$lock" 5 true >/dev/null
    assert_eq "[$tool] success passes through" 0 "$?"
done

# --- no-tool fallback ----------------------------------------------------------------------------
export TEST_LOCK_TOOL=none
out="$(test_lock_run "$WORK/none.lock" 5 sh -c 'echo ran; exit 4' 2>&1)"
rc=$?
assert_eq "[none] runs the command unlocked and passes its status through" 4 "$rc"
case "$out" in
    *"running the suite UNLOCKED"*ran*) ok "[none] warns that the run is unlocked" ;;
    *) ko "[none] should warn and still run, got: $out" ;;
esac
unset TEST_LOCK_TOOL

echo
echo "ci-local-test-lock.test.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
