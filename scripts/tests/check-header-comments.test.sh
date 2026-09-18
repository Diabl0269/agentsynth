#!/usr/bin/env bash
#
# Unit tests for scripts/check-header-comments.sh.
#
# The guard exists to relocate maintainer-facing rationale out of shared headers and beside the
# out-of-line definition it describes -- if the guard itself has a hole (a wrong floor, a
# sibling-.cpp check that always/never matches, a ratchet direction that doesn't actually ratchet,
# a block-comment counter that miscounts, or -- the whole reason the tracked quantity is EXCESS
# rather than a raw comment count -- a ratchet that rejects the exact edit the rule mandates), the
# placement rule is decorative or actively counterproductive. Runs in the Lint job -- no compiler,
# no network, ~1-2 s (each case builds and commits a tiny throwaway git repo).
#
# Usage: bash scripts/tests/check-header-comments.test.sh

set -uo pipefail

# See check-file-sizes.test.sh for why this matters: a git hook (or a nested harness run) inherits
# GIT_DIR (and sometimes GIT_WORK_TREE/GIT_INDEX_FILE); without stripping it, this harness's own
# `git init`/`git add` calls against throwaway fixture repos could instead target the REAL
# repository (FRO82).
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/check-header-comments.sh"

TMPROOT="$(mktemp -d)"
trap 'rm -rf "$TMPROOT"' EXIT
REPO="$TMPROOT/repo"
BASELINE="$TMPROOT/baseline.txt"

# Small floor for fast, readable fixtures -- semantics are identical to the default (60), just
# scaled down so a "flagged" fixture doesn't need dozens of lines.
export HEADER_COMMENT_FLOOR=10
export HEADER_COMMENT_BASELINE="$BASELINE"

pass=0
fail=0

# --- fixture helpers ----------------------------------------------------------------------------

reset_repo() {
    rm -rf "$REPO"
    mkdir -p "$REPO"
    (cd "$REPO" && git init -q && git -c user.email=t@t -c user.name=t commit -qm init --allow-empty)
    rm -f "$BASELINE"
}

# make_header <relative-path> <comment-lines> <code-lines> -- writes a header with exactly
# <comment-lines> `//`-style comment lines followed by <code-lines> plain code lines.
make_header() {
    local path="$REPO/$1" c="$2" k="$3" i
    mkdir -p "$(dirname "$path")"
    : >"$path"
    i=1
    while [ "$i" -le "$c" ]; do
        printf '// comment line %d\n' "$i" >>"$path"
        i=$((i + 1))
    done
    i=1
    while [ "$i" -le "$k" ]; do
        printf 'int code_%d = %d;\n' "$i" "$i" >>"$path"
        i=$((i + 1))
    done
}

# make_cpp <relative-path> -- a trivial tracked .cpp file, just to exist as a sibling.
make_cpp() {
    local path="$REPO/$1"
    mkdir -p "$(dirname "$path")"
    printf '// unit\nvoid f() {}\n' >"$path"
}

# write_file <relative-path> -- writes stdin verbatim to <relative-path> (dirs created as needed).
write_file() {
    local path="$REPO/$1"
    mkdir -p "$(dirname "$path")"
    cat >"$path"
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
make_header "Source/UI/Foo/Foo.h" 5 20
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
assert_pass "a clean tree (negative excess) passes with no baseline"

reset_repo
make_header "Source/UI/Foo/Foo.h" 15 5
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
assert_fail "a flagged header (excess +10) with no baseline entry fails, naming the file" "Foo.h has 15 comment lines vs 5 code lines (+10 excess"

reset_repo
make_header "Source/UI/Foo/Foo.h" 9 1
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
assert_pass "a header below the comment floor is not flagged even with large positive excess"

reset_repo
make_header "Source/UI/Foo/Foo.h" 100 150
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
assert_pass "a header with negative excess (comments don't outnumber code) is not flagged even when large"

reset_repo
make_header "Source/Timeline/Solo/Solo.h" 50 5
commit_all
assert_pass "a header-only header (no sibling .cpp) is not flagged"

# A genuinely header-only class must STAY exempt even when unrelated classes' .cpp files share its
# directory -- the sibling check is scoped to same-directory + same-basename-PREFIX, never "any
# .cpp in the directory" (that broader test would wrongly un-exempt a header-only class like
# Source/Timeline/EpochExchange.h just because other classes' .cpp files happen to sit alongside it).
reset_repo
make_header "Source/Timeline/Solo/Solo.h" 50 5
make_cpp "Source/Timeline/Solo/OtherClassImpl.cpp"
commit_all
assert_pass "a header-only class stays exempt even when unrelated classes' .cpp files share its directory"

# A class split into per-concern units (no exact <Class>.cpp) must still be caught -- the sibling
# check is a directory+prefix match, not a stem-exact ".cpp with the identical basename" test.
reset_repo
make_header "Source/AudioEngine/AudioEngine.h" 30 5
make_cpp "Source/AudioEngine/AudioEngineTransport.cpp"
make_cpp "Source/AudioEngine/AudioEngineSolo.cpp"
commit_all
assert_fail "a class split into per-concern *.cpp units (no exact <Class>.cpp) is still flagged" "AudioEngine.h has 30 comment lines vs 5 code lines"

reset_repo
make_header "Source/UI/Foo/Foo.h" 20 5
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
make_header "Source/UI/Foo/Foo.h" 25 5
commit_all
assert_fail "a baselined header whose excess grew fails" "grew from +15 to +20"

reset_repo
make_header "Source/UI/Foo/Foo.h" 20 5
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
set_baseline "20 Source/UI/Foo/Foo.h"
assert_fail "a baselined header whose excess shrank fails and mentions --update" "shrank from +20 to +15"

reset_repo
make_header "Source/UI/Foo/Foo.h" 5 20
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
set_baseline "30 Source/UI/Foo/Foo.h"
assert_fail "a stale entry for a now-compliant file fails" "Foo.h is a stale baseline entry (no longer over the placement threshold)"

reset_repo
set_baseline "30 Source/UI/Ghost/Ghost.h"
assert_fail "a stale entry for a deleted file fails" "Ghost.h is a stale baseline entry (file missing"

# --- the whole point of tracking EXCESS instead of a raw comment count --------------------------

reset_repo
make_header "Source/UI/Foo/Foo.h" 20 5
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
# The mandated fix pattern: declare a new member, give it its own short one-line caller-facing
# contract right there in the header. +1 comment, +1 code -- excess is UNCHANGED, so this must pass
# against the very entry --update just wrote, with no --allow-growth needed.
make_header "Source/UI/Foo/Foo.h" 21 6
commit_all
assert_pass "adding a member with its own one-line caller contract (+1 comment, +1 code) does not trip the ratchet"

reset_repo
make_header "Source/UI/Foo/Foo.h" 20 5
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
# By contrast, piling MORE prose in without a matching code addition -- +5 comment, +1 code, excess
# +15 -> +19 -- is exactly the failure mode the guard exists to catch, and must still fail.
make_header "Source/UI/Foo/Foo.h" 25 6
commit_all
assert_fail "piling on more comment than code (+5 comment, +1 code) still fails as a genuine excess grow" "grew from +15 to +19"

# --- --update: ratchet only tightens -------------------------------------------------------------

reset_repo
make_header "Source/UI/Foo/Foo.h" 20 5
make_cpp "Source/UI/Foo/FooImpl.cpp"
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
baseline_before_raise="$(cat "$BASELINE")"
make_header "Source/UI/Foo/Foo.h" 28 5
commit_all
raise_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
raise_status=$?
baseline_after_raise="$(cat "$BASELINE")"
if [ "$raise_status" -ne 0 ] && echo "$raise_output" | grep -qF -- "Foo.h would raise the baseline from +15 to +23" \
    && [ "$baseline_before_raise" = "$baseline_after_raise" ]; then
    echo "PASS: --update refuses to raise a baseline entry without --allow-growth (exit 1, baseline unchanged)"
    pass=$((pass + 1))
else
    echo "FAIL: --update refuses to raise a baseline entry without --allow-growth (exit 1, baseline unchanged)"
    echo "exit=$raise_status"
    echo "$raise_output"
    fail=$((fail + 1))
fi

allow_output="$(bash "$CHECK" --root "$REPO" --update --allow-growth 2>&1)"
allow_status=$?
baseline_after_allow="$(cat "$BASELINE")"
if [ "$allow_status" -eq 0 ] && echo "$allow_output" | grep -qF -- "::warning::Source/UI/Foo/Foo.h raised from +15 to +23" \
    && echo "$baseline_after_allow" | grep -qF -- "23 Source/UI/Foo/Foo.h"; then
    echo "PASS: --update --allow-growth lets a raised entry through with a warning"
    pass=$((pass + 1))
else
    echo "FAIL: --update --allow-growth lets a raised entry through with a warning"
    echo "exit=$allow_status"
    echo "$allow_output"
    fail=$((fail + 1))
fi

reset_repo
make_header "Source/UI/Bar/Bar.h" 40 5
make_cpp "Source/UI/Bar/BarImpl.cpp"
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
make_header "Source/UI/Bar/Bar.h" 32 5
commit_all
shrink_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
shrink_status=$?
shrink_baseline="$(cat "$BASELINE")"
if [ "$shrink_status" -eq 0 ] && echo "$shrink_baseline" | grep -qF -- "27 Source/UI/Bar/Bar.h" \
    && ! echo "$shrink_output" | grep -q "::error::" && ! echo "$shrink_output" | grep -q "::warning::"; then
    echo "PASS: --update tightens a shrunk entry silently (no growth error or warning)"
    pass=$((pass + 1))
else
    echo "FAIL: --update tightens a shrunk entry silently (no growth error or warning)"
    echo "exit=$shrink_status"
    echo "$shrink_output"
    fail=$((fail + 1))
fi

# The same shrink, taken further until the header reads as compliant: --update must drop the entry
# entirely rather than leave a stale one behind.
make_header "Source/UI/Bar/Bar.h" 4 30
commit_all
drop_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
drop_status=$?
drop_baseline="$(cat "$BASELINE")"
if [ "$drop_status" -eq 0 ] && ! echo "$drop_baseline" | grep -q "Bar.h" \
    && ! echo "$drop_output" | grep -q "::error::"; then
    echo "PASS: --update removes an entry that is no longer over the placement threshold"
    pass=$((pass + 1))
else
    echo "FAIL: --update removes an entry that is no longer over the placement threshold"
    echo "exit=$drop_status"
    echo "$drop_output"
    echo "baseline: $drop_baseline"
    fail=$((fail + 1))
fi

# --- block-comment counter -------------------------------------------------------------------

reset_repo
write_file "Source/UI/Baz/Baz.h" <<'EOF'
/* block start
still block
end block */
/* one line */
int x = 1; // trailing comment counts as code
// full line comment
int y = 2;
EOF
make_cpp "Source/UI/Baz/BazImpl.cpp"
commit_all
list_line="$(bash "$CHECK" --root "$REPO" --list 2>&1 | grep -F "Baz.h")"
list_comment="$(printf '%s' "$list_line" | awk '{print $1}')"
list_code="$(printf '%s' "$list_line" | awk '{print $2}')"
list_excess="$(printf '%s' "$list_line" | awk '{print $3}')"
if [ "$list_comment" = "5" ] && [ "$list_code" = "2" ] && [ "$list_excess" = "3" ]; then
    echo "PASS: block comments, one-line comments and trailing code comments are counted correctly"
    pass=$((pass + 1))
else
    echo "FAIL: block comments, one-line comments and trailing code comments are counted correctly"
    echo "expected 5 comment / 2 code / excess 3, got: $list_line"
    fail=$((fail + 1))
fi

# --- vacuous-pass fail-safe (same shape as check-file-sizes.sh: every scanned file reads 0/0) ----

reset_repo
make_header "Source/UI/A/A.h" 5 5
make_header "Source/UI/B/B.h" 5 5
commit_all
clone_dir="$TMPROOT/no-checkout-clone"
rm -rf "$clone_dir"
git clone -q --no-checkout "$REPO" "$clone_dir"
(cd "$clone_dir" && git read-tree HEAD)
failsafe_output="$(bash "$CHECK" --root "$clone_dir" 2>&1)"
failsafe_status=$?
if [ "$failsafe_status" -ne 0 ] && echo "$failsafe_output" | grep -qF -- "::error::check-header-comments read every one of 2 scanned headers as 0 comment / 0 code lines"; then
    echo "PASS: the vacuous-pass fail-safe catches every scanned header reading as 0/0 and fails loudly"
    pass=$((pass + 1))
else
    echo "FAIL: the vacuous-pass fail-safe catches every scanned header reading as 0/0 and fails loudly"
    echo "exit=$failsafe_status"
    echo "$failsafe_output"
    fail=$((fail + 1))
fi
rm -rf "$clone_dir"

# --- the real thing: the repo's own tree + its own committed baseline must pass -----------------
if output="$( (unset HEADER_COMMENT_FLOOR HEADER_COMMENT_BASELINE && bash "$CHECK" --root "$SCRIPT_DIR") 2>&1)"; then
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
