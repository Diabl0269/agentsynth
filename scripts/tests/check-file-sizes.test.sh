#!/usr/bin/env bash
#
# Unit tests for scripts/check-file-sizes.sh.
#
# The guard exists to stop a file from ever again growing the way GraphEditor.cpp and a few test
# files did (9,000+ and 6,000+ lines) -- if the guard itself has a hole (a mis-scanned exclusion,
# a ratchet direction that doesn't actually ratchet, a baseline comparison that silently passes
# everything), the cap is decorative. Runs in the Lint job -- no compiler, no network, ~1-2 s
# (each case builds and commits a tiny throwaway git repo).
#
# Usage: bash scripts/tests/check-file-sizes.test.sh

set -uo pipefail

# Git hooks export GIT_DIR (often absolute) -- and sometimes GIT_WORK_TREE/GIT_INDEX_FILE -- into
# every process they run. This harness builds throwaway git repos with `git init`/`git add`; with
# an inherited GIT_DIR those commands would target the REAL repository with the fixture dir as
# its work tree, staging every real file as deleted and adding fixture files to the real index
# (FRO82 -- that is exactly what happened when a pre-push hook ran ci-local.sh). Strip the
# inherited git env first so the fixtures are genuinely isolated wherever this runs.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/check-file-sizes.sh"

TMPROOT="$(mktemp -d)"
trap 'rm -rf "$TMPROOT"' EXIT
REPO="$TMPROOT/repo"
BASELINE="$TMPROOT/baseline.txt"

export FILE_SIZE_CAP=20
export FILE_SIZE_BASELINE="$BASELINE"

pass=0
fail=0

# --- fixture helpers ----------------------------------------------------------------------------

reset_repo() {
    rm -rf "$REPO"
    mkdir -p "$REPO"
    (cd "$REPO" && git init -q && git -c user.email=t@t -c user.name=t commit -qm init --allow-empty)
    rm -f "$BASELINE"
}

# make_file <relative-path> <line-count> -- writes an N-line file (dirs created as needed).
make_file() {
    local path="$REPO/$1" n="$2" i
    mkdir -p "$(dirname "$path")"
    : >"$path"
    i=1
    while [ "$i" -le "$n" ]; do
        printf 'line %d\n' "$i" >>"$path"
        i=$((i + 1))
    done
}

commit_all() {
    (cd "$REPO" && git add -A && git -c user.email=t@t -c user.name=t commit -qm fixture)
}

# set_baseline <line> [<line> ...] -- writes $BASELINE with one line per argument.
set_baseline() {
    : >"$BASELINE"
    for line in "$@"; do
        printf '%s\n' "$line" >>"$BASELINE"
    done
}

run_check() {
    bash "$CHECK" --root "$REPO"
}

assert_pass() {
    local desc="$1" output
    if output="$(run_check 2>&1)"; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc (expected exit 0, got failure)"
        echo "$output"
        fail=$((fail + 1))
    fi
}

assert_fail() {
    local desc="$1" expect_grep="$2" output
    if output="$(run_check 2>&1)"; then
        echo "FAIL: $desc (expected non-zero exit, got success)"
        echo "$output"
        fail=$((fail + 1))
    elif echo "$output" | grep -qF -- "$expect_grep"; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc (exited non-zero but output didn't mention '$expect_grep')"
        echo "$output"
        fail=$((fail + 1))
    fi
}

# --- cases ----------------------------------------------------------------------------------

reset_repo
make_file "Small.cpp" 10
commit_all
assert_pass "a file under the cap passes with no baseline"

reset_repo
make_file "Big.cpp" 30
commit_all
assert_fail "a file over the cap with no baseline entry fails, naming the file" "Big.cpp is 30 lines"

reset_repo
make_file "Big.cpp" 30
commit_all
set_baseline "30 Big.cpp"
assert_pass "a baseline entry matching the exact current count passes"

reset_repo
make_file "Grown.cpp" 30
commit_all
set_baseline "25 Grown.cpp"
assert_fail "a file that grew past its baseline entry fails" "grew from 25 to 30"

reset_repo
make_file "Shrunk.cpp" 30
commit_all
set_baseline "35 Shrunk.cpp"
assert_fail "a file that shrank below its baseline entry fails and mentions --update" "--update"

reset_repo
make_file "NowSmall.cpp" 15
commit_all
set_baseline "25 NowSmall.cpp"
assert_fail "a baseline entry for a file now under the cap fails as stale" "NowSmall.cpp is a stale baseline entry"

reset_repo
set_baseline "30 Ghost.cpp"
assert_fail "a baseline entry for a missing file fails as stale" "Ghost.cpp is a stale baseline entry"

reset_repo
make_file "assets/Huge.cpp" 500
commit_all
assert_pass "an excluded-prefix file is ignored even when huge"

reset_repo
make_file "Untracked.cpp" 30
# deliberately not committed -- git ls-files must never see it
assert_pass "an untracked over-cap file is ignored"

reset_repo
make_file "Big.bin" 30
commit_all
assert_pass "a non-matching extension is ignored"

reset_repo
set_baseline "# a comment line, tolerated" "" "30 Big.cpp"
make_file "Big.cpp" 30
commit_all
assert_pass "comment and blank lines in the baseline are tolerated"

reset_repo
make_file "Zeta.cpp" 25
make_file "Alpha.cpp" 30
commit_all
update_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
expected="$(
    cat <<'EOF'
# Legacy files over the cap. STRICT ratchet: an entry is the exact current line count.
# A file may never grow past its entry; when it shrinks, run `bash scripts/check-file-sizes.sh --update`
# so the entry tightens; once under the cap the entry must be removed (--update does that).
# Never add a NEW file here -- split it instead.
30 Alpha.cpp
25 Zeta.cpp
EOF
)"
actual="$(cat "$BASELINE")"
if [ "$actual" = "$expected" ]; then
    echo "PASS: --update writes the expected sorted content with the header"
    pass=$((pass + 1))
else
    echo "FAIL: --update writes the expected sorted content with the header"
    echo "--- expected ---"
    echo "$expected"
    echo "--- actual ---"
    echo "$actual"
    fail=$((fail + 1))
fi
assert_pass "a subsequent check passes against the just-written baseline"

reset_repo
make_file "Small.cpp" 5
make_file "Mid.cpp" 15
make_file "Big1.cpp" 40
make_file "Big2.cpp" 30
make_file "Big3.cpp" 20
commit_all
list_output="$(bash "$CHECK" --root "$REPO" --list 3 2>&1)"
list_lines="$(printf '%s\n' "$list_output" | wc -l | tr -d ' ')"
first_count="$(printf '%s\n' "$list_output" | sed -n '1p' | awk '{print $1}')"
second_count="$(printf '%s\n' "$list_output" | sed -n '2p' | awk '{print $1}')"
third_count="$(printf '%s\n' "$list_output" | sed -n '3p' | awk '{print $1}')"
if [ "$list_lines" -eq 3 ] && [ "$first_count" -eq 40 ] && [ "$second_count" -eq 30 ] && [ "$third_count" -eq 20 ]; then
    echo "PASS: --list 3 prints the three largest scanned files, largest first"
    pass=$((pass + 1))
else
    echo "FAIL: --list 3 prints the three largest scanned files, largest first"
    echo "$list_output"
    fail=$((fail + 1))
fi

# --- FRO81: git-hook environment (GIT_DIR/etc. inherited) must never cause a vacuous pass -------
#
# Git exports GIT_DIR (and sometimes GIT_WORK_TREE/GIT_INDEX_FILE) into hook processes. With no
# --root override, the script resolves ROOT via `cd "$SCRIPT_DIR" && git rev-parse --show-toplevel`
# -- a relative GIT_DIR (the common case: hooks run with cwd at the repo root and GIT_DIR=.git)
# resolves against SCRIPT_DIR instead of the real repo root once inside that cd, so the git call
# fails, ROOT was empty, and every file read back as "missing" (0 lines) -- a silent, vacuous
# pass. These cases run the script from a real fixture repo (not via --root, which would sidestep
# ROOT resolution entirely and prove nothing) to exercise that path directly.

# (a) GIT_DIR=.git exported from the fixture repo root, script physically inside that repo (the
# real pre-commit/pre-push hook shape) -- must resolve ROOT correctly and report the real numbers.
reset_repo
make_file "Big.cpp" 30
commit_all
mkdir -p "$REPO/scripts"
cp "$CHECK" "$REPO/scripts/check-file-sizes.sh"   # deliberately NOT `git add`ed -- must not self-scan
set_baseline "30 Big.cpp"
hook_output="$(cd "$REPO" && FILE_SIZE_CAP="$FILE_SIZE_CAP" FILE_SIZE_BASELINE="$BASELINE" GIT_DIR=.git bash scripts/check-file-sizes.sh 2>&1)"
hook_status=$?
if [ "$hook_status" -eq 0 ] && echo "$hook_output" | grep -qF -- "1 files scanned, 1 over the ${FILE_SIZE_CAP}-line cap, largest: Big.cpp (30 lines)"; then
    echo "PASS: an inherited relative GIT_DIR from the fixture repo root resolves ROOT correctly and reports real numbers"
    pass=$((pass + 1))
else
    echo "FAIL: an inherited relative GIT_DIR from the fixture repo root resolves ROOT correctly and reports real numbers"
    echo "exit=$hook_status"
    echo "$hook_output"
    fail=$((fail + 1))
fi

# (b) GIT_DIR=/nonexistent with the script run from OUTSIDE any git repo and no --root -- ROOT
# resolution must fail loudly (::error:: + non-zero exit), never silently report a vacuous pass.
nogit_dir="$TMPROOT/no-git-here"
rm -rf "$nogit_dir"
mkdir -p "$nogit_dir"
cp "$CHECK" "$nogit_dir/check-file-sizes.sh"
nogit_output="$(cd "$nogit_dir" && GIT_DIR=/nonexistent bash ./check-file-sizes.sh 2>&1)"
nogit_status=$?
if [ "$nogit_status" -ne 0 ] && echo "$nogit_output" | grep -qF -- "::error::check-file-sizes: 'git rev-parse --show-toplevel' failed"; then
    echo "PASS: ROOT resolution failing outside a git repo fails loudly instead of passing vacuously"
    pass=$((pass + 1))
else
    echo "FAIL: ROOT resolution failing outside a git repo fails loudly instead of passing vacuously"
    echo "exit=$nogit_status"
    echo "$nogit_output"
    fail=$((fail + 1))
fi
rm -rf "$nogit_dir"

# (c) awk fail-safe backstop: ROOT resolves to a real git repo (so (b)'s guard doesn't fire), but
# every tracked file is missing on disk -- reproduced honestly with a `git clone --no-checkout`
# (a valid index without a checked-out working tree; `git read-tree HEAD` populates the index the
# same way a bare checkout step would, without materializing the files). This is the same shape
# as the original bug (git thinks the files exist; the filesystem doesn't have them at ROOT) but
# without relying on GIT_DIR at all, so it isolates the awk-level backstop from the ROOT-resolution
# guard in (b).
reset_repo
make_file "f1.txt" 5
make_file "f2.txt" 5
commit_all
clone_dir="$TMPROOT/no-checkout-clone"
rm -rf "$clone_dir"
git clone -q --no-checkout "$REPO" "$clone_dir"
(cd "$clone_dir" && git read-tree HEAD)
failsafe_output="$(bash "$CHECK" --root "$clone_dir" 2>&1)"
failsafe_status=$?
if [ "$failsafe_status" -ne 0 ] && echo "$failsafe_output" | grep -qF -- "::error::check-file-sizes read every one of 2 scanned files as 0 lines"; then
    echo "PASS: the awk fail-safe catches every scanned file reading as 0 lines and fails loudly"
    pass=$((pass + 1))
else
    echo "FAIL: the awk fail-safe catches every scanned file reading as 0 lines and fails loudly"
    echo "exit=$failsafe_status"
    echo "$failsafe_output"
    fail=$((fail + 1))
fi
rm -rf "$clone_dir"

# --- FRO82: running this harness from a git hook must never touch the hook's repository --------
#
# Hooks export GIT_DIR (absolute, in the pre-push case). Before the `unset` at the top of this file,
# the fixture repos' `git init`/`git add` calls resolved to the hook's repository with the fixture
# directory as its work tree -- staging every real file as deleted and adding fixture files to the
# real index. Reproduce with a sentinel repo: run this very harness with GIT_DIR pointing at it and
# require its index and work tree to come back untouched. The nested run is told to skip this case
# (otherwise it would recurse forever).
if [ -z "${CHECK_FILE_SIZES_TEST_NESTED:-}" ]; then
    sentinel="$TMPROOT/sentinel"
    mkdir -p "$sentinel"
    (cd "$sentinel" && git init -q && echo keep >keep.txt && git add keep.txt \
        && git -c user.email=t@t -c user.name=t commit -qm sentinel)
    (cd "$sentinel" && GIT_DIR="$sentinel/.git" CHECK_FILE_SIZES_TEST_NESTED=1 \
        bash "$SCRIPT_DIR/scripts/tests/check-file-sizes.test.sh" >/dev/null 2>&1) || true
    sentinel_status="$(git -C "$sentinel" status --porcelain 2>&1)"
    sentinel_files="$(git -C "$sentinel" ls-files 2>&1)"
    if [ -z "$sentinel_status" ] && [ "$sentinel_files" = "keep.txt" ]; then
        echo "PASS: running the harness under a hook-style GIT_DIR leaves the hook's repository untouched"
        pass=$((pass + 1))
    else
        echo "FAIL: running the harness under a hook-style GIT_DIR leaves the hook's repository untouched"
        echo "  sentinel status: $sentinel_status"
        echo "  sentinel files:  $sentinel_files"
        fail=$((fail + 1))
    fi
fi

# --- the real thing: the repo's own tree + its own committed baseline must pass -----------------
if output="$( (unset FILE_SIZE_CAP FILE_SIZE_BASELINE && bash "$CHECK" --root "$SCRIPT_DIR") 2>&1)"; then
    echo "PASS: the real repo tree passes its own committed baseline"
    pass=$((pass + 1))
else
    echo "FAIL: the real repo tree passes its own committed baseline"
    echo "$output"
    fail=$((fail + 1))
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
