#!/usr/bin/env bash
#
# Unit tests for scripts/utf8-literal-check.sh.
#
# The check exists so a mojibake regression (juce::String(const char*)'s ASCII-only decode
# silently corrupting a \xe2\x80\x94-style UTF-8 escape) gets caught at review time -- if the
# check itself has a hole, we're back to catching these only when someone screenshots the broken
# UI. Runs in the Lint job -- no compiler, no runner-specific state, ~1s.
#
# Usage: bash scripts/tests/utf8-literal-check.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/utf8-literal-check.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

reset_work() {
    rm -rf "$WORK"
    mkdir -p "$WORK/Source" "$WORK/Tests"
    (cd "$WORK" && git init -q && git -c user.email=t@t -c user.name=t commit -qm init --allow-empty)
}

commit_fixture() {
    (cd "$WORK" && git add -A && git -c user.email=t@t -c user.name=t commit -qm fixture)
}

assert_pass() {
    local desc="$1"
    if UTF8_CHECK_ROOT="$WORK" bash "$CHECK" >/dev/null 2>&1; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc (expected exit 0, got failure)"
        fail=$((fail + 1))
    fi
}

assert_fail() {
    local desc="$1"
    local expect_grep="$2"
    local output
    if output="$(UTF8_CHECK_ROOT="$WORK" bash "$CHECK" 2>&1)"; then
        echo "FAIL: $desc (expected non-zero exit, got success)"
        fail=$((fail + 1))
    elif echo "$output" | grep -qF "$expect_grep"; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc (exited non-zero but output didn't mention '$expect_grep')"
        echo "$output"
        fail=$((fail + 1))
    fi
}

# --- fixtures ---------------------------------------------------------------------------------

reset_work
cat > "$WORK/Source/Guarded.cpp" <<'EOF'
void f() {
    label.setText(juce::String::fromUTF8("Thanks \xe2\x80\x94 done."), juce::dontSendNotification);
}
EOF
commit_fixture
assert_pass "single-line fromUTF8(...) wrap is not flagged"

reset_work
cat > "$WORK/Source/Broken.cpp" <<'EOF'
void f() {
    label.setText("Thanks \xe2\x80\x94 done.", juce::dontSendNotification);
}
EOF
commit_fixture
assert_fail "bare literal with a high-byte escape is flagged" "Source/Broken.cpp:2"

reset_work
cat > "$WORK/Source/MultilineGuarded.cpp" <<'EOF'
void f() {
    toggle.setButtonText(juce::String::fromUTF8(
        "Help improve \xe2\x80\x94 share my prompts"));
}
EOF
commit_fixture
assert_pass "multi-line fromUTF8( call wrapping the literal on a later line is not flagged"

reset_work
cat > "$WORK/Source/PriorStatementGuard.cpp" <<'EOF'
void f() {
    auto unrelated = juce::String::fromUTF8("something else");
    label.setText("Thanks \xe2\x80\x94 done.", juce::dontSendNotification);
}
EOF
commit_fixture
assert_fail "a guard on an earlier, semicolon-terminated statement does not carry over" "Source/PriorStatementGuard.cpp:3"

reset_work
cat > "$WORK/Source/RawDeclaration.cpp" <<'EOF'
// A raw C-string constant -- not yet a juce::String, so the escape here isn't itself a bug.
constexpr const char* kHint = "Drop a file \xe2\x80\x94 or arm and record";

juce::String hint() {
    return juce::String::fromUTF8(kHint);
}
EOF
commit_fixture
assert_pass "a constexpr const char* declaration is not flagged (only a use site would be)"

reset_work
cat > "$WORK/Source/CommentOnly.cpp" <<'EOF'
/** One entry in the lane picker, labelled "NodeName \xC2\xB7 paramId". */
struct Entry {
    juce::String label; // "Module name \xC2\xB7 parameter name"
};
EOF
commit_fixture
assert_pass "an escape mentioned only inside a comment is not flagged"

reset_work
cat > "$WORK/Source/Clean.cpp" <<'EOF'
void f() {
    label.setText("plain ascii, nothing to see here", juce::dontSendNotification);
}
EOF
commit_fixture
assert_pass "a file with no high-byte escapes at all is not flagged"

reset_work
mkdir -p "$WORK/Tools"
cat > "$WORK/Tools/OutOfScope.cpp" <<'EOF'
void f() {
    label.setText("Thanks \xe2\x80\x94 done.", juce::dontSendNotification);
}
EOF
commit_fixture
assert_pass "a violation under Tools/ (out of scope) is not flagged"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
