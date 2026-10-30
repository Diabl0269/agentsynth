#!/usr/bin/env bash
#
# Unit tests for scripts/check-fixed-waits.sh.
#
# The guard flags tests that pump the message loop for a fixed time and then assert. If the scanner
# has a hole -- a poll loop it flags, a wrapper it misses, a block boundary it ignores, a ratchet
# that doesn't ratchet -- the rule is decorative. Runs in the Lint job -- no compiler, no network, a
# few seconds (each case builds and commits a tiny throwaway git repo).
#
# Usage: bash scripts/tests/check-fixed-waits.test.sh

set -uo pipefail

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/check-fixed-waits.sh"

TMPROOT="$(mktemp -d)"
trap 'rm -rf "$TMPROOT"' EXIT
REPO="$TMPROOT/repo"
BASELINE="$TMPROOT/baseline.txt"
export FIXED_WAITS_BASELINE="$BASELINE"

pass=0
fail=0

PUMP='juce::MessageManager::getInstance()->runDispatchLoopUntil'

reset_repo() {
    rm -rf "$REPO"
    mkdir -p "$REPO"
    (cd "$REPO" && git init -q && git -c user.email=t@t -c user.name=t commit -qm init --allow-empty)
    rm -f "$BASELINE"
}

# write_file <relative-path> -- writes stdin verbatim.
write_file() {
    local path="$REPO/$1"
    mkdir -p "$(dirname "$path")"
    cat >"$path"
}

commit_all() {
    (cd "$REPO" && git add -A && git -c user.email=t@t -c user.name=t commit -qm fixture)
}

record() {
    if [ "$1" -eq 0 ]; then
        echo "PASS: $2"
        pass=$((pass + 1))
    else
        echo "FAIL: $2"
        fail=$((fail + 1))
    fi
}

run_check() { bash "$CHECK" --root "$REPO"; }

assert_pass() {
    local desc="$1" output
    commit_all
    if output="$(run_check 2>&1)"; then
        record 0 "$desc"
    else
        record 1 "$desc (expected exit 0, got failure)"
        echo "$output"
    fi
}

assert_fail() {
    local desc="$1" expect="$2" output
    commit_all
    if output="$(run_check 2>&1)"; then
        record 1 "$desc (expected non-zero exit, got success)"
        echo "$output"
    elif grep -qF -- "$expect" <<<"$output"; then
        record 0 "$desc"
    else
        record 1 "$desc (exited non-zero but output didn't mention '$expect')"
        echo "$output"
    fi
}

# --- the flagged shape ------------------------------------------------------------------------

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    $PUMP(50);
    EXPECT_TRUE(done());
}
EOF
assert_fail "a fixed pump followed by EXPECT_ is flagged with file:line" "Tests/ATests.cpp:3:"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    $PUMP(kSettleMs + 20);
    ASSERT_EQ(count, 1);
}
EOF
assert_fail "a k-constant expression argument is flagged too" "Tests/ATests.cpp:3:"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    $PUMP(50); EXPECT_TRUE(done());
}
EOF
assert_fail "an assertion later on the same line is flagged" "Tests/ATests.cpp:3:"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    $PUMP(20);
    int unused = 0;

    // a comment between the wait and the check
    EXPECT_TRUE(done());
}
EOF
assert_fail "blank lines, comments and a statement between the pump and the assertion still count" "Tests/ATests.cpp:3:"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    $PUMP(50);
    cleanup();
}
TEST(A, C) {
    EXPECT_TRUE(done());
}
EOF
assert_pass "a pump whose block closes before the next test's assertion is not flagged"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    $PUMP(50);
    int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8, i = 9;
    use(a, b, c, d, e, f, g, h, i);
    use(a, b, c, d, e, f, g, h, i);
    use(a, b, c, d, e, f, g, h, i);
    use(a, b, c, d, e, f, g, h, i);
    use(a, b, c, d, e, f, g, h, i);
    use(a, b, c, d, e, f, g, h, i);
    use(a, b, c, d, e, f, g, h, i);
    use(a, b, c, d, e, f, g, h, i);
    use(a, b, c, d, e, f, g, h, i);
    EXPECT_TRUE(done());
}
EOF
assert_pass "an assertion more than 8 code lines after the pump is out of range"

# --- not flagged ------------------------------------------------------------------------------------

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    synth::test::waitUntil([&] { return done(); });
    EXPECT_TRUE(done());
}
EOF
assert_pass "waitUntil is not flagged"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    for (int i = 0; i < 50 && !done(); ++i)
        $PUMP(20);
    EXPECT_TRUE(done());
}
TEST(A, C) {
    start();
    for (int i = 0; i < 50 && !done(); ++i) {
        $PUMP(20);
    }
    EXPECT_TRUE(done());
    while (!ready())
        $PUMP(10);
    EXPECT_TRUE(ready());
    do {
        $PUMP(10);
    } while (!ready());
    EXPECT_TRUE(ready());
    while (juce::Time::getMillisecondCounter() < deadline) {
        $PUMP(10);
    }
    EXPECT_TRUE(ready());
}
EOF
assert_pass "a pump inside a loop whose condition checks state (the poll pattern) is not flagged"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    for (int i = 0; i < 5; ++i)
        $PUMP(20);
    EXPECT_TRUE(done());
}
EOF
assert_fail "a counted loop of fixed pumps is still a fixed wait" "Tests/ATests.cpp:4:"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    start();
    $PUMP(ms);
    EXPECT_TRUE(done());
}
EOF
assert_pass "a computed (non-literal, non-constant) argument is not flagged"

reset_repo
write_file Tests/ATests.cpp <<EOF
// $PUMP(50); EXPECT_TRUE(x) -- prose
TEST(A, B) {
    const char* s = "$PUMP(50);";
    /* $PUMP(50);
       EXPECT_TRUE(x); */
    EXPECT_TRUE(s != nullptr);
}
EOF
assert_pass "comments and string literals are ignored"

reset_repo
write_file Source/A.cpp <<EOF
void f() {
    $PUMP(50);
    EXPECT_TRUE(done());
}
EOF
assert_pass "files outside Tests/ are not scanned"

# --- wrappers -----------------------------------------------------------------------------------------

reset_repo
write_file Tests/ATests.cpp <<EOF
namespace {
void pump() { $PUMP(50); }
const auto settle = [] { $PUMP(30); };
void multi() {
    $PUMP(20);
}
}
TEST(A, B) {
    start();
    pump();
    EXPECT_TRUE(done());
}
TEST(A, C) {
    start();
    settle();
    EXPECT_TRUE(done());
}
TEST(A, D) {
    start();
    multi();
    EXPECT_TRUE(done());
}
EOF
assert_fail "a one-line wrapper, a lambda wrapper and a three-line wrapper are all flagged at their calls" "Tests/ATests.cpp:10:"
output="$(run_check 2>&1)"
if grep -qF "Tests/ATests.cpp:15:" <<<"$output" && grep -qF "Tests/ATests.cpp:20:" <<<"$output" && ! grep -qF "Tests/ATests.cpp:2:" <<<"$output"; then
    record 0 "...each call is named and the wrapper definitions are not"
else
    record 1 "...each call is named and the wrapper definitions are not"
    echo "$output"
fi

reset_repo
write_file Tests/Rig.h <<EOF
#pragma once
struct Rig { void flush() { $PUMP(20); } };
EOF
write_file Tests/WithRigTests.cpp <<EOF
#include "Rig.h"
TEST(A, B) {
    Rig s;
    s.flush();
    EXPECT_TRUE(done());
}
EOF
write_file Tests/WithoutRigTests.cpp <<EOF
TEST(A, B) {
    Recorder recorder;
    recorder.flush();
    EXPECT_TRUE(done());
}
EOF
commit_all
output="$(run_check 2>&1)"
if grep -qF "Tests/WithRigTests.cpp:4:" <<<"$output" && ! grep -qF "WithoutRigTests" <<<"$output"; then
    record 0 "a header wrapper applies to the files that include it and to no others"
else
    record 1 "a header wrapper applies to the files that include it and to no others"
    echo "$output"
fi

# --- baseline ratchet ------------------------------------------------------------------------------------

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    $PUMP(50);
    EXPECT_FALSE(fired());
}
EOF
printf '%s\n' "Tests/ATests.cpp | $PUMP(50); | nothing may happen" >"$BASELINE"
assert_pass "a baselined site passes"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    $PUMP(50);
    EXPECT_FALSE(fired());
}
TEST(A, C) {
    $PUMP(50);
    EXPECT_FALSE(fired());
}
EOF
printf '%s\n' "Tests/ATests.cpp | $PUMP(50); | nothing may happen" >"$BASELINE"
assert_fail "N identical sites need N baseline entries" "Tests/ATests.cpp:6:"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    synth::test::waitUntil([&] { return done(); });
}
EOF
printf '%s\n' "Tests/ATests.cpp | $PUMP(50); | converted since" >"$BASELINE"
assert_fail "a baseline entry that matches nothing is stale" "stale baseline entry"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    $PUMP(50);
    EXPECT_FALSE(fired());
}
EOF
printf '%s\n' "# header" "Tests/ATests.cpp | $PUMP(50); | keep this reason" "Tests/Gone.cpp | $PUMP(50); | stale" >"$BASELINE"
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
if grep -qF "keep this reason" "$BASELINE" && ! grep -qF "Gone.cpp" "$BASELINE" && grep -qF "# header" "$BASELINE"; then
    record 0 "--update drops stale entries and keeps comments and reasons"
else
    record 1 "--update drops stale entries and keeps comments and reasons"
    cat "$BASELINE"
fi

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    $PUMP(50);
    EXPECT_FALSE(fired());
}
EOF
: >"$BASELINE"
commit_all
bash "$CHECK" --root "$REPO" --update >/dev/null 2>&1
if [ ! -s "$BASELINE" ] && ! run_check >/dev/null 2>&1; then
    record 0 "--update never adds an entry"
else
    record 1 "--update never adds an entry"
fi

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    $PUMP(50);
    EXPECT_FALSE(fired());
}
EOF
commit_all
if [ "$(bash "$CHECK" --root "$REPO" --list 2>&1)" = "Tests/ATests.cpp:2: $PUMP(50);" ]; then
    record 0 "--list prints every site as path:line: code"
else
    record 1 "--list prints every site as path:line: code"
fi

# --- an async dispatch helper that ends in a fixed pump ------------------------------------------------------

reset_repo
write_file Tests/ATests.cpp <<EOF
void clickNow(juce::Button& button) {
    button.triggerClick();
    $PUMP(30);
}
EOF
assert_fail "a three-line helper that calls triggerClick and then pumps for a fixed time is flagged" "Tests/ATests.cpp:3:"

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    auto press = [&](juce::Component& c) { c.keyPressed(key); $PUMP(30); };
    press(a);
}
EOF
assert_fail "a one-line lambda that calls keyPressed and then pumps for a fixed time is flagged" "Tests/ATests.cpp:2:"

reset_repo
write_file Tests/ATests.cpp <<EOF
void run(juce::ApplicationCommandManager& m) {
    m.invokeDirectly(id, true);
    $PUMP(kSettleMs);
}
EOF
assert_fail "invokeDirectly followed by a k-constant pump at the end of a helper is flagged" "Tests/ATests.cpp:3:"

reset_repo
write_file Tests/ATests.cpp <<EOF
void settleAfter(juce::Button& button) {
    button.triggerClick();
    synth::test::flushMessageQueue();
}
void idle() {
    $PUMP(30);
}
EOF
assert_pass "flushMessageQueue after a click, and a pump with no dispatch before it, are not flagged"

reset_repo
write_file Tests/ATests.cpp <<EOF
void clickNow(juce::Button& button) {
    button.triggerClick();
    $PUMP(30);
}
EOF
printf 'Tests/ATests.cpp | %s(30); | a reason\n' "$PUMP" >"$BASELINE"
assert_pass "a flagged dispatch helper with a baseline entry passes"

# --- hook environment ----------------------------------------------------------------------------------------

reset_repo
write_file Tests/ATests.cpp <<EOF
TEST(A, B) {
    $PUMP(50);
    EXPECT_FALSE(fired());
}
EOF
commit_all
hook_output="$(GIT_DIR="$TMPROOT/does-not-exist" GIT_WORK_TREE="$TMPROOT" bash "$CHECK" --root "$REPO" 2>&1)"
hook_status=$?
if [ "$hook_status" -ne 0 ] && grep -qF "Tests/ATests.cpp:2:" <<<"$hook_output"; then
    record 0 "an exported GIT_DIR/GIT_WORK_TREE (git hook env) does not blind the scan"
else
    record 1 "an exported GIT_DIR/GIT_WORK_TREE (git hook env) does not blind the scan"
    echo "$hook_output"
fi

# --- the real thing -----------------------------------------------------------------------------------------------
if output="$( (unset FIXED_WAITS_BASELINE && bash "$CHECK" --root "$SCRIPT_DIR") 2>&1)"; then
    record 0 "the real repo tree passes its own committed baseline"
else
    record 1 "the real repo tree passes its own committed baseline"
    echo "$output"
fi

echo
echo "check-fixed-waits.test: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
