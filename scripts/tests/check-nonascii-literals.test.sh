#!/usr/bin/env bash
#
# Guard + unit tests for scripts/nonascii-literals.py.
#
# THE BUG THIS PREVENTS. juce::String's `const char*` constructor decodes its bytes as the system
# encoding (CharPointer_ASCII, i.e. Latin-1) — NOT as UTF-8. So a literal carrying a non-ASCII
# character reaches the UI as one mojibake glyph per byte: a marker menu built from "Rename…"
# rendered "Renameâ€¦" in the app. Spelling the same character as a hex escape
# ("Rename\xe2\x80\xa6") changes nothing — it is the identical three bytes — which is exactly how
# the first fix for this went in and shipped the bug anyway.
#
# So: no raw non-ASCII byte, and no \x/\u escape above 0x7F, inside a double-quoted literal under
# Source/. Use plain ASCII, or say the encoding out loud with juce::CharPointer_UTF8 /
# juce::String::fromUTF8 (both exempt this check).
#
# Comments are exempt — this codebase writes prose em dashes throughout them and they never reach
# juce::String. Tests/ is out of scope: its non-ASCII lives in gtest `<<` streams, which go to a
# std::ostream and render fine.
#
# Runs in the Lint job — no compiler, no runner-specific state, ~1 s.
#
# Usage: bash scripts/tests/check-nonascii-literals.test.sh

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$REPO_ROOT/scripts/nonascii-literals.py"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

check() { # check <name> <what-is-being-compared> <actual> <expected>
    if [ "$3" = "$4" ]; then
        printf 'ok    %s\n' "$1"
        pass=$((pass + 1))
    else
        printf 'FAIL  %s\n      %s: expected %q, got %q\n' "$1" "$2" "$4" "$3"
        fail=$((fail + 1))
    fi
}

# flagged <file> -> "yes" when the scanner reports that fixture, "no" when it is clean.
flagged() {
    if python3 "$CHECK" "$1" >/dev/null 2>&1; then echo "no"; else echo "yes"; fi
}

# --- self-test: what must be FLAGGED -------------------------------------------------------
BAD="$WORK/bad"
mkdir -p "$BAD"

# A typed non-ASCII character in a literal — the original report.
printf 'auto a = juce::String("Rename\xe2\x80\xa6");\n' >"$BAD/typed.cpp"
check "typed non-ASCII in a literal is flagged" "verdict" "$(flagged "$BAD/typed.cpp")" "yes"
rm -f "$BAD/typed.cpp"

# The SAME bytes as a hex escape — the spelling that shipped the bug a second time.
printf 'auto a = juce::String("Rename\\xe2\\x80\\xa6");\n' >"$BAD/escaped.cpp"
check "hex-escaped high byte is flagged" "verdict" "$(flagged "$BAD/escaped.cpp")" "yes"
rm -f "$BAD/escaped.cpp"

# A \u escape above ASCII (what "·" was doing in ModMatrixComponent).
printf 'auto a = juce::String("a \\u00B7 b");\n' >"$BAD/uescape.cpp"
check "\\u escape above 0x7F is flagged" "verdict" "$(flagged "$BAD/uescape.cpp")" "yes"
rm -f "$BAD/uescape.cpp"

# A header, not just a .cpp.
printf 'constexpr const char* k = "hint \xe2\x80\x94 more";\n' >"$BAD/hdr.h"
check "headers are scanned too" "verdict" "$(flagged "$BAD/hdr.h")" "yes"
rm -f "$BAD/hdr.h"

# Code AFTER a comment on the same line still counts.
printf 'int x = 0; // an em dash \xe2\x80\x94 in a comment\nauto a = juce::String("bad \xe2\x80\x94 here");\n' >"$BAD/mixed.cpp"
check "a bad literal below an exempt comment is flagged" "verdict" "$(flagged "$BAD/mixed.cpp")" "yes"
rm -f "$BAD/mixed.cpp"

# --- self-test: what must NOT be flagged ---------------------------------------------------
GOOD="$WORK/good"
mkdir -p "$GOOD"

printf 'auto a = juce::String("Rename...");\n' >"$GOOD/ascii.cpp"
printf '// A line comment with an em dash \xe2\x80\x94 and an ellipsis\xe2\x80\xa6\n' >"$GOOD/linecomment.cpp"
printf '/* A block comment\n * spanning lines with \xe2\x80\x94 dashes\n */\nint x = 0;\n' >"$GOOD/blockcomment.cpp"
printf 'auto a = juce::String::fromUTF8("\xe2\x80\xa6");\n' >"$GOOD/fromutf8.cpp"
printf 'auto a = juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));\n' >"$GOOD/charpointer.cpp"
printf 'label.setText(\n    juce::String::fromUTF8(\n        "an em dash \xe2\x80\x94 wrapped call "\n        "spanning \xe2\x80\x94 lines"),\n    juce::dontSendNotification);\nauto after = juce::String("plain ascii after the call");\n' >"$GOOD/multiline_fromutf8.cpp"
printf 'auto a = juce::String("\\x41\\x7F");\n' >"$GOOD/lowescapes.cpp"
printf "char c = '\\\\';\nauto a = juce::String(\"an escaped quote \\\\\" then ascii\");\n" >"$GOOD/escapes.cpp"
check "the whole clean fixture set passes" "verdict" "$(flagged "$GOOD")" "no"

# Each clean fixture also passes on its own, so a pass above cannot be one file masking another.
for f in "$GOOD"/*; do
    check "clean: $(basename "$f")" "verdict" "$(flagged "$f")" "no"
done

# A block comment must not swallow the rest of the file: real code after it is still checked.
printf '/* dash \xe2\x80\x94 */\nauto a = juce::String("bad \xe2\x80\x94 here");\n' >"$WORK/afterblock.cpp"
check "code after a closed block comment is still scanned" "verdict" "$(flagged "$WORK/afterblock.cpp")" "yes"

# The multi-line fromUTF8 exemption must end with the call: a raw literal after it is still bad.
printf 'auto a = juce::String::fromUTF8(\n    "ok \xe2\x80\x94 inside");\nauto b = juce::String("bad \xe2\x80\x94 after");\n' >"$WORK/afterutf8call.cpp"
check "a raw literal after a closed fromUTF8 call is still flagged" "verdict" "$(flagged "$WORK/afterutf8call.cpp")" "yes"

# --- the real thing: the repo's own Source/ tree must be clean -----------------------------
output="$(python3 "$CHECK" "$REPO_ROOT/Source" 2>&1)"
status=$?
if [ "$status" -ne 0 ]; then
    printf 'FAIL  Source/ contains non-ASCII string literals\n'
    printf '      juce::String decodes a `const char*` as Latin-1, so these render as mojibake.\n'
    printf '      Use plain ASCII, or juce::CharPointer_UTF8 / juce::String::fromUTF8.\n\n'
    printf '%s\n\n' "$output"
    fail=$((fail + 1))
else
    printf 'ok    Source/ has no non-ASCII string literals\n'
    pass=$((pass + 1))
fi

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
