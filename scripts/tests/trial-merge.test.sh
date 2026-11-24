#!/usr/bin/env bash
#
# Tests scripts/lib/trial-merge.sh: the tree CI would test (the branch merged into the newest main) is
# built without touching the working tree, and each reason not to build it has its own status.
# Regression test for FRO1792: a ratchet guard passed on the branch alone and failed in CI on the merge.

set -uo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/lib/trial-merge.sh
source "$REPO_ROOT/scripts/lib/trial-merge.sh"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/trial-merge.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
failures=0
check() { # check <name> <condition...>
    local name="$1"
    shift
    if "$@"; then echo "ok: $name"; else echo "FAIL: $name"; failures=$((failures + 1)); fi
}
g() { git -C "$TMP/repo" -c user.name=t -c user.email=t@t "$@"; }

mkdir -p "$TMP/repo"
git init -q -b main "$TMP/repo"
echo base >"$TMP/repo/shared.txt"
g add -A
g commit -q -m base

# Branch off, then let main move on.
g checkout -q -b feature
echo mine >"$TMP/repo/mine.txt"
g add -A
g commit -q -m mine
g checkout -q main
echo theirs >"$TMP/repo/theirs.txt"
g add -A
g commit -q -m theirs
g update-ref refs/remotes/origin/main main
g checkout -q feature

trial_merge_tree "$TMP/repo" "$TMP/out"
check "a branch behind main gets a merged tree" test $? -eq 0
check "the merged tree holds the branch's file" test -f "$TMP/out/mine.txt"
check "the merged tree holds main's newer file" test -f "$TMP/out/theirs.txt"
check "the merged tree is a git repo the guards can list" test "$(git -C "$TMP/out" ls-files | wc -l | tr -d ' ')" = 3
check "the working tree is untouched" test ! -e "$TMP/repo/theirs.txt"
check "no scratch objects are left" test ! -e "$TMP/out.objects"
check "the real object store gained nothing loose from the merge" test "$(git -C "$TMP/repo" count-objects | cut -d' ' -f1)" -le 12

g merge -q --no-edit main
rm -rf "$TMP/out2"
trial_merge_tree "$TMP/repo" "$TMP/out2"
check "a branch that already holds main needs no trial" test $? -eq 10

trial_merge_tree "$TMP/repo" "$TMP/out3" origin/nothing
check "a missing base ref is skipped" test $? -eq 11

# A conflict: both sides change the same line.
g checkout -q -b clash main
echo ours >"$TMP/repo/shared.txt"
g commit -q -am ours
g checkout -q main
echo main-side >"$TMP/repo/shared.txt"
g commit -q -am main-side
g update-ref refs/remotes/origin/main main
g checkout -q clash
trial_merge_tree "$TMP/repo" "$TMP/out4"
check "a branch that conflicts with main says so" test $? -eq 12

[ "$failures" -eq 0 ] && echo "trial-merge: all passed" || exit 1
