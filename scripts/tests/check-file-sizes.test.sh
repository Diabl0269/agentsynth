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
