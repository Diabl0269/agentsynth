#!/usr/bin/env bash
#
# Unit tests for scripts/check-comment-provenance.sh.
#
# The guard keeps ticket ids and dates out of code comments. If the guard itself has a hole -- a
# scanner that counts a string literal or misses a block-comment line, an exemption that leaks
# into Source/, a ratchet that doesn't actually ratchet, or a scan that silently reads nothing
# under a hook's inherited GIT_DIR -- the rule is decorative. Runs in the Lint job -- no compiler,
# no network, a few seconds (each case builds and commits a tiny throwaway git repo).
#
# Usage: bash scripts/tests/check-comment-provenance.test.sh

set -uo pipefail

# A git hook (or a nested harness run) inherits GIT_DIR (and sometimes GIT_WORK_TREE/
# GIT_INDEX_FILE); without stripping it, this harness's own `git init`/`git add` calls against
# throwaway fixture repos could target the REAL repository instead. The hook-env case below
# re-exports it on purpose, for the checker only.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/check-comment-provenance.sh"

TMPROOT="$(mktemp -d)"
trap 'rm -rf "$TMPROOT"' EXIT
REPO="$TMPROOT/repo"
BASELINE="$TMPROOT/baseline.txt"

export COMMENT_PROVENANCE_BASELINE="$BASELINE"

pass=0
fail=0

# --- fixture helpers ----------------------------------------------------------------------------

reset_repo() {
    rm -rf "$REPO"
    mkdir -p "$REPO"
    (cd "$REPO" && git init -q && git -c user.email=t@t -c user.name=t commit -qm init --allow-empty)
    rm -f "$BASELINE"
}

# write_file <relative-path> -- writes stdin verbatim to <relative-path> (dirs created as needed).
write_file() {
    local path="$REPO/$1"
    mkdir -p "$(dirname "$path")"
    cat >"$path"
}

# make_clean <relative-path> -- a tracked file with a comment and no provenance.
make_clean() {
    write_file "$1" <<'EOF'
// Behaviour only: the buffer is swapped on the message thread.
int clean = 1;
EOF
}

# make_dirty <relative-path> <count> -- a file with <count> `//` comment lines carrying a ticket id.
make_dirty() {
    local path="$REPO/$1" n="$2" i=1
    mkdir -p "$(dirname "$path")"
    : >"$path"
    while [ "$i" -le "$n" ]; do
        printf '// note %d: see FRO%d for context\n' "$i" "$i" >>"$path"
        i=$((i + 1))
    done
    printf 'int code = 1;\n' >>"$path"
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

record() {
    # record <ok:0|1> <description>
    if [ "$1" -eq 0 ]; then
        echo "PASS: $2"
        pass=$((pass + 1))
    else
        echo "FAIL: $2"
        fail=$((fail + 1))
    fi
}

assert_pass() {
    local desc="$1" output
    if output="$(run_check 2>&1)"; then
        record 0 "$desc"
    else
        record 1 "$desc (expected exit 0, got failure)"
        echo "$output"
    fi
}

assert_fail() {
    local desc="$1" expect_grep="$2" output
    if output="$(run_check 2>&1)"; then
        record 1 "$desc (expected non-zero exit, got success)"
        echo "$output"
    elif echo "$output" | grep -qF -- "$expect_grep"; then
        record 0 "$desc"
    else
        record 1 "$desc (exited non-zero but output didn't mention '$expect_grep')"
        echo "$output"
    fi
}

# assert_baseline_is <description> <line> [<line> ...] -- the baseline's DATA lines equal the args.
assert_baseline_is() {
    local desc="$1" actual expected
    shift
    actual="$(grep -v '^#' "$BASELINE" | grep -v '^[[:space:]]*$')"
    expected="$(printf '%s\n' "$@")"
    [ "$#" -eq 0 ] && expected=""
    if [ "$actual" = "$expected" ]; then
        record 0 "$desc"
    else
        record 1 "$desc"
        echo "expected: $expected"
        echo "actual:   $actual"
    fi
}

# --- check mode ---------------------------------------------------------------------------------

reset_repo
make_clean "Source/A.cpp"
make_clean "Tests/ATests.cpp"
commit_all
assert_pass "a clean tree passes with no baseline file"

reset_repo
make_dirty "Source/Foo.cpp" 2
make_clean "Source/Bar.cpp"
commit_all
assert_fail "a new file with a ticket id and no baseline entry fails, naming the file and count" "Source/Foo.cpp has 2 comment line(s) carrying provenance"
output="$(run_check 2>&1)"
if echo "$output" | grep -qF "Source/Foo.cpp:1: // note 1: see FRO1 for context" && echo "$output" | grep -qF "comment-provenance-guard.md"; then
    record 0 "the failure prints the offending lines and points at the fix doc"
else
    record 1 "the failure prints the offending lines and points at the fix doc"
    echo "$output"
fi

reset_repo
make_dirty "Source/Foo.cpp" 3
commit_all
set_baseline "3 Source/Foo.cpp"
assert_pass "a baselined file exactly at its entry passes"

reset_repo
make_dirty "Source/Foo.cpp" 4
commit_all
set_baseline "3 Source/Foo.cpp"
assert_fail "a baselined file past its entry fails as growth" "Source/Foo.cpp grew from 3 to 4"

reset_repo
make_dirty "Source/Foo.cpp" 2
commit_all
set_baseline "3 Source/Foo.cpp"
assert_fail "a baselined file below its entry fails until --update tightens it" "Source/Foo.cpp shrank from 3 to 2"

reset_repo
make_clean "Source/Foo.cpp"
commit_all
set_baseline "3 Source/Foo.cpp"
assert_fail "a baselined file that reached zero is a stale entry" "Source/Foo.cpp is a stale baseline entry"

reset_repo
make_clean "Source/Foo.cpp"
commit_all
set_baseline "1 Source/Gone.cpp"
assert_fail "a baseline entry for a missing file is a stale entry" "Source/Gone.cpp is a stale baseline entry"

# --- what counts as a comment line ----------------------------------------------------------------

reset_repo
write_file "Source/Kinds.cpp" <<'EOF'
// ISO date 2026-09-28 in a line comment
int a = 1; // trailing T142 ticket
/* block one-liner P3-2 */
/* block start
   middle FRO9 line
   plain middle
   end 2026-01-02 */
int b = 2;
EOF
commit_all
set_baseline "5 Source/Kinds.cpp"
assert_pass "line, trailing, one-line block and multi-line block comment lines are all counted (5)"
set_baseline "4 Source/Kinds.cpp"
assert_fail "...and not one fewer" "Source/Kinds.cpp grew from 4 to 5"

reset_repo
write_file "Source/Strings.cpp" <<'EOF'
const char* kDate = "2026-09-28";
const char* kTicket = "FRO123 // still a string";
const char* kBlock = "/* FRO7 */";
char q = '"'; const char* kAfterQuote = "2026-01-01";
int big = 1'000; const char* kSep = "FRO5";
const char* kEscaped = "a \" 2026-01-01 \" b";
// real comment without provenance
EOF
commit_all
assert_pass "dates and ticket ids inside string/char literals on code lines are not counted"

reset_repo
write_file "Source/Words.cpp" <<'EOF'
// MANAGE12 and PROFRO5x and FRO and FRO-12 and 2026-9-28 and T99 and T1000 are not ids
// WEBP2 PRO12abc P1- are not ids either
int a = 1;
EOF
commit_all
assert_pass "look-alike words (no word boundary, wrong shape) are not counted"

reset_repo
write_file "Source/Ids.cpp" <<'EOF'
// (FRO42) and BAC7, WEB8; MAR9. AGE10 PRO11 P4-2 T101 2026-09-28!
EOF
commit_all
set_baseline "1 Source/Ids.cpp"
assert_pass "every id family and the date match, several on one line count once"

# --- the Regression-test exemption (Tests/ only) --------------------------------------------------

reset_repo
write_file "Tests/RegTests.cpp" <<'EOF'
// Regression test for FRO97: the mixer strip was lost on rebuild
TEST(Reg, Case) {}
// Regression test for PRO5: another pinned bug
// See FRO12 for the original report
EOF
commit_all
set_baseline "1 Tests/RegTests.cpp"
assert_pass "under Tests/, 'Regression test for <ID>:' lines are exempt but other ids still count"

reset_repo
write_file "Tests/RegTests.cpp" <<'EOF'
// Regression test for FRO97 without the colon
// Regression test for FRO97: fine
EOF
commit_all
set_baseline "1 Tests/RegTests.cpp"
assert_pass "the exemption needs the colon: the colon-less form is counted"

reset_repo
write_file "Source/Reg.cpp" <<'EOF'
// Regression test for FRO97: the same text under Source/ is provenance
EOF
commit_all
assert_fail "the same 'Regression test for' line under Source/ is NOT exempt" "Source/Reg.cpp has 1 comment line(s)"

# --- scope -----------------------------------------------------------------------------------------

reset_repo
write_file "Source/notes.txt" <<'EOF'
// FRO1 2026-01-01
EOF
write_file "Docs/Thing.cpp" <<'EOF'
// FRO1 2026-01-01
EOF
write_file "Tests/Fix.h" <<'EOF'
int x; // fine
EOF
make_clean "Source/A.cpp"
commit_all
assert_pass "non-C-family files and paths outside Source/ and Tests/ are not scanned"

reset_repo
write_file "Tests/Deep/Nest/Thing.mm" <<'EOF'
// FRO1 in an ObjC++ file under Tests/
EOF
commit_all
assert_fail "Tests/**.mm files are scanned" "Tests/Deep/Nest/Thing.mm has 1 comment line(s)"

# --- --update ---------------------------------------------------------------------------------------

reset_repo
make_dirty "Source/A.cpp" 2
make_dirty "Tests/B.cpp" 1
make_clean "Source/C.cpp"
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
assert_baseline_is "the first --update bootstraps the baseline from the tree, sorted" "2 Source/A.cpp" "1 Tests/B.cpp"
assert_pass "the tree passes the baseline --update just wrote"

reset_repo
make_dirty "Source/A.cpp" 2
make_dirty "Source/B.cpp" 1
commit_all
set_baseline "5 Source/A.cpp" "1 Source/B.cpp"
make_clean "Source/B.cpp"
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
assert_baseline_is "--update tightens a shrunk entry and removes a zeroed one" "2 Source/A.cpp"

reset_repo
make_dirty "Source/A.cpp" 3
commit_all
set_baseline "2 Source/A.cpp"
if output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"; then
    record 1 "--update refuses to raise an entry (expected failure)"
else
    if echo "$output" | grep -qF "would raise the baseline from 2 to 3"; then
        record 0 "--update refuses to raise an entry"
    else
        record 1 "--update refuses to raise an entry (wrong message)"
        echo "$output"
    fi
fi
assert_baseline_is "...and leaves the baseline untouched" "2 Source/A.cpp"

reset_repo
make_dirty "Source/A.cpp" 1
make_dirty "Source/New.cpp" 2
commit_all
set_baseline "1 Source/A.cpp"
if output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"; then
    record 1 "--update refuses to add a new file (expected failure)"
else
    if echo "$output" | grep -qF "Source/New.cpp is a new file with provenance comments (2 lines)"; then
        record 0 "--update refuses to add a new file"
    else
        record 1 "--update refuses to add a new file (wrong message)"
        echo "$output"
    fi
fi
assert_baseline_is "...and leaves the baseline untouched when adding is refused" "1 Source/A.cpp"

reset_repo
make_dirty "Source/A.cpp" 3
make_dirty "Source/New.cpp" 2
commit_all
set_baseline "2 Source/A.cpp"
if output="$(bash "$CHECK" --root "$REPO" --update --allow-growth 2>&1)" && echo "$output" | grep -qF "::warning::Source/A.cpp raised from 2 to 3" && echo "$output" | grep -qF "::warning::Source/New.cpp is a new file"; then
    record 0 "--update --allow-growth lets a raise and a new file through, with a ::warning:: each"
else
    record 1 "--update --allow-growth lets a raise and a new file through, with a ::warning:: each"
    echo "$output"
fi
assert_baseline_is "...and writes both" "3 Source/A.cpp" "2 Source/New.cpp"

reset_repo
make_dirty "Source/A.cpp" 1
commit_all
rm -f "$BASELINE"
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
if [ -f "$BASELINE" ] && head -1 "$BASELINE" | grep -q '^#'; then
    record 0 "--update writes a commented header and creates the baseline file"
else
    record 1 "--update writes a commented header and creates the baseline file"
fi

# --- hook environment ---------------------------------------------------------------------------------

reset_repo
make_dirty "Source/Foo.cpp" 2
commit_all
hook_output="$(GIT_DIR="$TMPROOT/does-not-exist" GIT_WORK_TREE="$TMPROOT" bash "$CHECK" --root "$REPO" 2>&1)"
hook_status=$?
if [ "$hook_status" -ne 0 ] && echo "$hook_output" | grep -qF "Source/Foo.cpp has 2 comment line(s)"; then
    record 0 "an exported GIT_DIR/GIT_WORK_TREE (git hook env) does not blind the scan"
else
    record 1 "an exported GIT_DIR/GIT_WORK_TREE (git hook env) does not blind the scan"
    echo "exit=$hook_status"
    echo "$hook_output"
fi

# --- vacuous-pass fail-safe: every scanned file reads as empty -------------------------------------------

reset_repo
make_dirty "Source/A.cpp" 1
make_dirty "Source/B.cpp" 1
commit_all
clone_dir="$TMPROOT/no-checkout-clone"
rm -rf "$clone_dir"
git clone -q --no-checkout "$REPO" "$clone_dir"
(cd "$clone_dir" && git read-tree HEAD)
failsafe_output="$(bash "$CHECK" --root "$clone_dir" 2>&1)"
failsafe_status=$?
if [ "$failsafe_status" -ne 0 ] && echo "$failsafe_output" | grep -qF -- "::error::check-comment-provenance read every one of 2 scanned files as empty"; then
    record 0 "the vacuous-pass fail-safe catches every scanned file reading as empty"
else
    record 1 "the vacuous-pass fail-safe catches every scanned file reading as empty"
    echo "exit=$failsafe_status"
    echo "$failsafe_output"
fi
rm -rf "$clone_dir"

# --- --list ------------------------------------------------------------------------------------------------

reset_repo
make_dirty "Source/A.cpp" 1
make_dirty "Source/B.cpp" 3
commit_all
if [ "$(bash "$CHECK" --root "$REPO" --list 1 2>&1)" = "3 Source/B.cpp" ]; then
    record 0 "--list N prints the N heaviest files"
else
    record 1 "--list N prints the N heaviest files"
fi

# --- the real thing: the repo's own tree + its own committed baseline must pass ----------------------------------
if output="$( (unset COMMENT_PROVENANCE_BASELINE && bash "$CHECK" --root "$SCRIPT_DIR") 2>&1)"; then
    record 0 "the real repo tree passes its own committed baseline"
else
    record 1 "the real repo tree passes its own committed baseline"
    echo "$output"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
