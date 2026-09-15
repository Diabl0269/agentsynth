#!/usr/bin/env bash
#
# Unit tests for scripts/check-function-sizes.sh (and the scanner it drives,
# scripts/function-size-scan.awk).
#
# Modelled directly on scripts/tests/check-file-sizes.test.sh -- same fixture-repo approach, same
# assert helpers, same hook-environment and GIT_DIR-safety cases. What's different here is the
# scanner itself: these fixtures exist to prove the brace-depth C++ parsing is right (comments,
# strings, nested scopes, lambdas), not just the ratchet-baseline bookkeeping the file-size guard
# already covers thoroughly. Fixtures live under Source/ because that's this guard's real scan
# scope (Source/, Tests/, Tools/ -- not check-file-sizes.sh's whole-tree scope). Runs in the Lint
# job -- no compiler, no network.
#
# Usage: bash scripts/tests/check-function-sizes.test.sh

set -uo pipefail

# See check-file-sizes.test.sh's identical comment: this harness's own `git init`/`git add` calls
# must never resolve against a real repository's GIT_DIR inherited from a hook.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/check-function-sizes.sh"

TMPROOT="$(mktemp -d)"
trap 'rm -rf "$TMPROOT"' EXIT
REPO="$TMPROOT/repo"
BASELINE="$TMPROOT/baseline.txt"

export FUNCTION_SIZE_CAP=5
export FUNCTION_SIZE_BASELINE="$BASELINE"

pass=0
fail=0

# --- fixture helpers ----------------------------------------------------------------------------

reset_repo() {
    rm -rf "$REPO"
    mkdir -p "$REPO"
    (cd "$REPO" && git init -q && git -c user.email=t@t -c user.name=t commit -qm init --allow-empty)
    rm -f "$BASELINE"
}

# write_file <relative-path-under-Source> -- writes stdin to Source/<path> (dirs created as
# needed). Every fixture lives under Source/ because that's this guard's real scan scope.
write_file() {
    local path="$REPO/Source/$1"
    mkdir -p "$(dirname "$path")"
    cat >"$path"
}

# make_function <relative-path-under-Source> <name> <total-lines> -- an N-line free function named
# <name> at Source/<path>: for N==1 a one-liner `void name() {}`; otherwise an opening signature
# line, N-2 filler statements, and a closing `}` line, so the function occupies EXACTLY N lines.
make_function() {
    local path="$REPO/Source/$1" name="$2" n="$3" i
    mkdir -p "$(dirname "$path")"
    if [ "$n" -le 1 ]; then
        printf 'void %s() {}\n' "$name" >"$path"
        return
    fi
    {
        printf 'void %s() {\n' "$name"
        i=1
        while [ "$i" -le $((n - 2)) ]; do
            printf '    int filler%d = %d;\n' "$i" "$i"
            i=$((i + 1))
        done
        printf '}\n'
    } >"$path"
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

# --- basic cap / baseline behaviour, mirroring check-file-sizes.test.sh ------------------------

reset_repo
make_function "Small.cpp" foo 3
commit_all
assert_pass "a function under the cap passes with no baseline"

reset_repo
make_function "Big.cpp" bigFunc 8
commit_all
assert_fail "a function over the cap with no baseline entry fails, naming it and its path:line" \
    "bigFunc (Source/Big.cpp:1) is 8 lines (cap 5)"

reset_repo
make_function "Big.cpp" bigFunc 8
commit_all
set_baseline "8 Source/Big.cpp::bigFunc"
assert_pass "a baseline entry matching the exact current function size passes"

reset_repo
make_function "Grown.cpp" grownFunc 8
commit_all
set_baseline "6 Source/Grown.cpp::grownFunc"
assert_fail "a function that grew past its baseline entry fails" "grew from 6 to 8"

reset_repo
make_function "Shrunk.cpp" shrunkFunc 8
commit_all
set_baseline "12 Source/Shrunk.cpp::shrunkFunc"
assert_fail "a function that shrank below its baseline entry fails and mentions --update" "--update"

reset_repo
make_function "NowSmall.cpp" nowSmall 3
commit_all
set_baseline "9 Source/NowSmall.cpp::nowSmall"
assert_fail "a baseline entry for a function now under the cap fails as stale" "is a stale baseline entry"

reset_repo
set_baseline "9 Source/Ghost.cpp::ghostFunc"
assert_fail "a baseline entry for a missing function fails as stale" \
    "Source/Ghost.cpp::ghostFunc is a stale baseline entry"

# --- the scanner itself: comments, strings, nesting, lambdas, TEST macros -----------------------

reset_repo
write_file "Nested.cpp" <<'EOF'
void outer() {
    if (true) {
        doThing();
    }
    auto lam = [](){
        return 1;
    };
}
EOF
commit_all
assert_fail "a lambda and an if-block nested in a function count toward that one enclosing function" \
    "outer (Source/Nested.cpp:1) is 8 lines"

reset_repo
write_file "Scoped.cpp" <<'EOF'
namespace synth {
class Widget {
public:
    void bar() {
        step1();
        step2();
        step3();
        step4();
    }
};
}
EOF
commit_all
assert_fail "a member function nested inside namespace+class is still detected (both are transparent)" \
    "bar (Source/Scoped.cpp:4) is 6 lines"

reset_repo
write_file "Test.cpp" <<'EOF'
TEST_F(WidgetTests, DoesTheThing) {
    step1();
    step2();
    step3();
    step4();
    step5();
}
EOF
commit_all
assert_fail "a TEST_F body over the cap is detected, named with its full macro argument list" \
    "TEST_F(WidgetTests, DoesTheThing) (Source/Test.cpp:1) is 7 lines"

reset_repo
write_file "Comments.cpp" <<'EOF'
void withComments() {
    // a comment can hide a brace: {
    int x = 1; // and "close" it too: }
    const char* s = "{ not real code }";
    char c1 = '{';
    char c2 = '}';
    doThing();
}
EOF
commit_all
assert_fail "braces inside comments, string literals and char literals are ignored" \
    "withComments (Source/Comments.cpp:1) is 8 lines"

reset_repo
write_file "Table.cpp" <<'EOF'
static const int table[] = {
    1,
    2,
    3,
};

void afterTable() {
    step1();
    step2();
    step3();
    step4();
}
EOF
commit_all
assert_fail "a brace initializer is not treated as a function; parsing continues correctly after it" \
    "afterTable (Source/Table.cpp:7) is 6 lines"

reset_repo
write_file "MultiLine.cpp" <<'EOF'
void MainComponent::performLocateMaster(
    int x,
    int y) {
    step1();
    step2();
    step3();
}
EOF
commit_all
assert_fail "a multi-line function signature's start line is the signature's first line, not the brace's" \
    "MainComponent::performLocateMaster (Source/MultiLine.cpp:1) is 7 lines"

# --- --update: ratchet, growth refusal, --allow-growth, renames ---------------------------------

reset_repo
make_function "Zeta.cpp" zetaFunc 6
make_function "Alpha.cpp" alphaFunc 8
commit_all
update_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
expected="$(
    cat <<'EOF'
# Legacy functions over the cap. STRICT ratchet: an entry is the exact current line count.
# A function may never grow past its entry; when it shrinks, run
# `bash scripts/check-function-sizes.sh --update` so the entry tightens; once under the cap
# the entry must be removed (--update does that).
# Never add a NEW function here -- extract a named step / collaborator instead.
8 Source/Alpha.cpp::alphaFunc
6 Source/Zeta.cpp::zetaFunc
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
make_function "Legacy.cpp" legacyFunc 6
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
baseline_before_raise="$(cat "$BASELINE")"
make_function "Legacy.cpp" legacyFunc 9
commit_all
raise_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
raise_status=$?
baseline_after_raise="$(cat "$BASELINE")"
if [ "$raise_status" -ne 0 ] && echo "$raise_output" | grep -qF -- "Source/Legacy.cpp::legacyFunc would raise the baseline from 6 to 9" \
    && [ "$baseline_before_raise" = "$baseline_after_raise" ]; then
    echo "PASS: --update refuses to raise a baseline entry (exit 1, baseline unchanged)"
    pass=$((pass + 1))
else
    echo "FAIL: --update refuses to raise a baseline entry (exit 1, baseline unchanged)"
    echo "exit=$raise_status"
    echo "$raise_output"
    fail=$((fail + 1))
fi

allow_output="$(bash "$CHECK" --root "$REPO" --update --allow-growth 2>&1)"
allow_status=$?
baseline_after_allow="$(cat "$BASELINE")"
if [ "$allow_status" -eq 0 ] && echo "$allow_output" | grep -qF -- "::warning::Source/Legacy.cpp::legacyFunc raised from 6 to 9" \
    && echo "$baseline_after_allow" | grep -qF -- "9 Source/Legacy.cpp::legacyFunc"; then
    echo "PASS: --update --allow-growth lets a raised entry through with a warning"
    pass=$((pass + 1))
else
    echo "FAIL: --update --allow-growth lets a raised entry through with a warning"
    echo "exit=$allow_status"
    echo "$allow_output"
    fail=$((fail + 1))
fi

reset_repo
make_function "Shrinking.cpp" shrinkFunc 10
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
make_function "Shrinking.cpp" shrinkFunc 7
commit_all
shrink_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
shrink_status=$?
shrink_baseline="$(cat "$BASELINE")"
if [ "$shrink_status" -eq 0 ] && echo "$shrink_baseline" | grep -qF -- "7 Source/Shrinking.cpp::shrinkFunc" \
    && ! echo "$shrink_output" | grep -q "::error::" && ! echo "$shrink_output" | grep -q "::warning::"; then
    echo "PASS: --update tightens a shrunk entry silently (no growth error or warning)"
    pass=$((pass + 1))
else
    echo "FAIL: --update tightens a shrunk entry silently (no growth error or warning)"
    echo "exit=$shrink_status"
    echo "$shrink_output"
    fail=$((fail + 1))
fi

reset_repo
make_function "Old.cpp" movedFunc 8
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
(cd "$REPO" && git mv Source/Old.cpp Source/New.cpp)
mv_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
mv_status=$?
mv_baseline="$(cat "$BASELINE")"
if [ "$mv_status" -eq 0 ] && echo "$mv_baseline" | grep -qF -- "8 Source/New.cpp::movedFunc" \
    && ! echo "$mv_baseline" | grep -q "Old.cpp" && ! echo "$mv_output" | grep -q "::error::"; then
    echo "PASS: a pure git-mv of a file (same function, same size, new path) is accepted without --allow-growth"
    pass=$((pass + 1))
else
    echo "FAIL: a pure git-mv of a file (same function, same size, new path) is accepted without --allow-growth"
    echo "exit=$mv_status"
    echo "$mv_output"
    fail=$((fail + 1))
fi


# FRO78 re-sync (2026-09): a function extracted into a NEW file while its OLD file keeps other
# content -- exactly the shape of FRO77's drag-drop extraction (GraphEditorDragDrop.cpp kept most
# of its content; the extracted bodies landed in a brand-new GraphDragDropController.cpp) -- must
# be accepted by --update WITHOUT --allow-growth, even though git never reports that pair as a
# rename (the old file wasn't deleted, just shrunk, so git's own -M rename detection never fires).
reset_repo
write_file "Combined.cpp" <<'EOF'
void movedFunc() {
    int f1 = 1;
    int f2 = 2;
    int f3 = 3;
    int f4 = 4;
}
void stayingFunc() {
    int f1 = 1;
    int f2 = 2;
    int f3 = 3;
    int f4 = 4;
}
EOF
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
write_file "Combined.cpp" <<'EOF'
void stayingFunc() {
    int f1 = 1;
    int f2 = 2;
    int f3 = 3;
    int f4 = 4;
}
EOF
write_file "Extracted.cpp" <<'EOF'
void movedFunc() {
    int f1 = 1;
    int f2 = 2;
    int f3 = 3;
    int f4 = 4;
}
EOF
commit_all
split_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
split_status=$?
split_baseline="$(cat "$BASELINE")"
if [ "$split_status" -eq 0 ] && echo "$split_baseline" | grep -qF -- "6 Source/Extracted.cpp::movedFunc" \
    && echo "$split_baseline" | grep -qF -- "6 Source/Combined.cpp::stayingFunc" \
    && ! echo "$split_baseline" | grep -q "Combined.cpp::movedFunc" \
    && ! echo "$split_output" | grep -q "::error::"; then
    echo "PASS: a function split into a brand-new file (old file survives, git confirms no rename) is accepted without --allow-growth"
    pass=$((pass + 1))
else
    echo "FAIL: a function split into a brand-new file (old file survives, git confirms no rename) is accepted without --allow-growth"
    echo "exit=$split_status"
    echo "$split_output"
    fail=$((fail + 1))
fi

# Same split shape, but the moved function also GREW -- still needs --allow-growth like any raise.
reset_repo
write_file "Combined2.cpp" <<'EOF'
void movedGrowFunc() {
    int f1 = 1;
    int f2 = 2;
    int f3 = 3;
    int f4 = 4;
}
void stayingFunc2() {
    int f1 = 1;
    int f2 = 2;
    int f3 = 3;
    int f4 = 4;
}
EOF
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
baseline_before_split_grow="$(cat "$BASELINE")"
write_file "Combined2.cpp" <<'EOF'
void stayingFunc2() {
    int f1 = 1;
    int f2 = 2;
    int f3 = 3;
    int f4 = 4;
}
EOF
write_file "Extracted2.cpp" <<'EOF'
void movedGrowFunc() {
    int f1 = 1;
    int f2 = 2;
    int f3 = 3;
    int f4 = 4;
    int f5 = 5;
    int f6 = 6;
    int f7 = 7;
}
EOF
commit_all
splitgrow_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
splitgrow_status=$?
baseline_after_split_grow="$(cat "$BASELINE")"
if [ "$splitgrow_status" -ne 0 ] \
    && echo "$splitgrow_output" | grep -qF -- "Source/Extracted2.cpp::movedGrowFunc would raise the baseline from 6 to 9 lines while moving from Source/Combined2.cpp::movedGrowFunc" \
    && [ "$baseline_before_split_grow" = "$baseline_after_split_grow" ]; then
    echo "PASS: a function that moved AND grew still needs --allow-growth (baseline left untouched)"
    pass=$((pass + 1))
else
    echo "FAIL: a function that moved AND grew still needs --allow-growth (baseline left untouched)"
    echo "exit=$splitgrow_status"
    echo "$splitgrow_output"
    fail=$((fail + 1))
fi

reset_repo
make_function "Existing.cpp" existingFunc 6
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
make_function "BrandNew.cpp" brandNewFunc 9
commit_all
new_output="$(bash "$CHECK" --root "$REPO" --update 2>&1)"
new_status=$?
new_baseline="$(cat "$BASELINE")"
if [ "$new_status" -ne 0 ] && echo "$new_output" | grep -qF -- "Source/BrandNew.cpp::brandNewFunc is a new over-cap function (9 lines" \
    && ! echo "$new_baseline" | grep -q "BrandNew.cpp"; then
    echo "PASS: a genuinely new over-cap function is refused by --update without --allow-growth"
    pass=$((pass + 1))
else
    echo "FAIL: a genuinely new over-cap function is refused by --update without --allow-growth"
    echo "exit=$new_status"
    echo "$new_output"
    fail=$((fail + 1))
fi

# --- --list --------------------------------------------------------------------------------------

reset_repo
make_function "Small.cpp" small 2
make_function "Mid.cpp" mid 4
make_function "Big1.cpp" big1 9
make_function "Big2.cpp" big2 7
make_function "Big3.cpp" big3 6
commit_all
list_output="$(bash "$CHECK" --root "$REPO" --list 3 2>&1)"
list_lines="$(printf '%s\n' "$list_output" | wc -l | tr -d ' ')"
first_count="$(printf '%s\n' "$list_output" | sed -n '1p' | awk '{print $1}')"
second_count="$(printf '%s\n' "$list_output" | sed -n '2p' | awk '{print $1}')"
third_count="$(printf '%s\n' "$list_output" | sed -n '3p' | awk '{print $1}')"
if [ "$list_lines" -eq 3 ] && [ "$first_count" -eq 9 ] && [ "$second_count" -eq 7 ] && [ "$third_count" -eq 6 ]; then
    echo "PASS: --list 3 prints the three largest scanned functions, largest first"
    pass=$((pass + 1))
else
    echo "FAIL: --list 3 prints the three largest scanned functions, largest first"
    echo "$list_output"
    fail=$((fail + 1))
fi

# --- ROOT resolution / hook-environment safety, mirroring check-file-sizes.test.sh's FRO81/FRO82 -

reset_repo
make_function "Big.cpp" bigFunc 8
commit_all
mkdir -p "$REPO/scripts"
cp "$CHECK" "$REPO/scripts/check-function-sizes.sh"
cp "$SCRIPT_DIR/scripts/function-size-scan.awk" "$REPO/scripts/function-size-scan.awk"
set_baseline "8 Source/Big.cpp::bigFunc"
hook_output="$(cd "$REPO" && FUNCTION_SIZE_CAP="$FUNCTION_SIZE_CAP" FUNCTION_SIZE_BASELINE="$BASELINE" GIT_DIR=.git bash scripts/check-function-sizes.sh 2>&1)"
hook_status=$?
if [ "$hook_status" -eq 0 ] && echo "$hook_output" | grep -qF -- "1 functions scanned, 1 over the ${FUNCTION_SIZE_CAP}-line cap, largest: Source/Big.cpp::bigFunc (8 lines)"; then
    echo "PASS: an inherited relative GIT_DIR from the fixture repo root resolves ROOT correctly and reports real numbers"
    pass=$((pass + 1))
else
    echo "FAIL: an inherited relative GIT_DIR from the fixture repo root resolves ROOT correctly and reports real numbers"
    echo "exit=$hook_status"
    echo "$hook_output"
    fail=$((fail + 1))
fi

nogit_dir="$TMPROOT/no-git-here"
rm -rf "$nogit_dir"
mkdir -p "$nogit_dir"
cp "$CHECK" "$nogit_dir/check-function-sizes.sh"
cp "$SCRIPT_DIR/scripts/function-size-scan.awk" "$nogit_dir/function-size-scan.awk"
nogit_output="$(cd "$nogit_dir" && GIT_DIR=/nonexistent bash ./check-function-sizes.sh 2>&1)"
nogit_status=$?
if [ "$nogit_status" -ne 0 ] && echo "$nogit_output" | grep -qF -- "::error::check-function-sizes: 'git rev-parse --show-toplevel' failed"; then
    echo "PASS: ROOT resolution failing outside a git repo fails loudly instead of passing vacuously"
    pass=$((pass + 1))
else
    echo "FAIL: ROOT resolution failing outside a git repo fails loudly instead of passing vacuously"
    echo "exit=$nogit_status"
    echo "$nogit_output"
    fail=$((fail + 1))
fi
rm -rf "$nogit_dir"

# awk fail-safe backstop: ROOT resolves to a real repo, but every tracked file is missing on disk
# (a `git clone --no-checkout` + `git read-tree HEAD`: a valid index, no working tree) -- the same
# shape as the original file-size-guard bug, reproduced honestly rather than via GIT_DIR.
reset_repo
make_function "f1.cpp" f1Func 8
make_function "f2.cpp" f2Func 8
commit_all
clone_dir="$TMPROOT/no-checkout-clone"
rm -rf "$clone_dir"
git clone -q --no-checkout "$REPO" "$clone_dir"
(cd "$clone_dir" && git read-tree HEAD)
failsafe_output="$(bash "$CHECK" --root "$clone_dir" 2>&1)"
failsafe_status=$?
if [ "$failsafe_status" -ne 0 ] && echo "$failsafe_output" | grep -qF -- "check-function-sizes scanned 2 files and found zero functions in any of them"; then
    echo "PASS: the vacuous-pass fail-safe catches every scanned file missing on disk and fails loudly"
    pass=$((pass + 1))
else
    echo "FAIL: the vacuous-pass fail-safe catches every scanned file missing on disk and fails loudly"
    echo "exit=$failsafe_status"
    echo "$failsafe_output"
    fail=$((fail + 1))
fi
rm -rf "$clone_dir"

# Running this harness itself under a hook-style GIT_DIR must never touch the hook's repository --
# see check-file-sizes.test.sh's identical FRO82 case for the history (a pre-push hook run once
# staged 683 real files as deleted before the `unset` at the top of these harnesses existed).
if [ -z "${CHECK_FUNCTION_SIZES_TEST_NESTED:-}" ]; then
    sentinel="$TMPROOT/sentinel"
    mkdir -p "$sentinel"
    (cd "$sentinel" && git init -q && echo keep >keep.txt && git add keep.txt \
        && git -c user.email=t@t -c user.name=t commit -qm sentinel)
    (cd "$sentinel" && GIT_DIR="$sentinel/.git" CHECK_FUNCTION_SIZES_TEST_NESTED=1 \
        bash "$SCRIPT_DIR/scripts/tests/check-function-sizes.test.sh" >/dev/null 2>&1) || true
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
if output="$( (unset FUNCTION_SIZE_CAP FUNCTION_SIZE_BASELINE && bash "$CHECK" --root "$SCRIPT_DIR") 2>&1)"; then
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
