#!/usr/bin/env bash
#
# Self-test for scripts/check-wording.sh (the guard for "casual UI wording bypasses the shared vocabulary"):
# a planted hard-coded tooltip next to a vocabulary header fails it, reading from the vocabulary passes, the
# `// wording-ok:` marker needs a reason, a retired casual phrase fails, and the baseline only ratchets down.
# The last case runs it on the real tree. No compiler, ~1 s.
#
# Usage: bash scripts/tests/check-wording.test.sh

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$REPO_ROOT/scripts/check-wording.sh"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/check-wording.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0

check() { # check <name> <actual> <expected>
    if [ "$2" = "$3" ]; then
        printf 'ok    %s\n' "$1"
        pass=$((pass + 1))
    else
        printf 'FAIL  %s\n      expected %q, got %q\n' "$1" "$3" "$2"
        fail=$((fail + 1))
    fi
}

# tree <consumer-line> -> a fresh fake repo root whose one consumer file includes FooWords.h and holds that line.
tree() {
    local dir
    dir="$(mktemp -d "$WORK/t.XXXXXX")"
    mkdir -p "$dir/Source/UI" "$dir/scripts"
    printf '%s\n' '#pragma once' 'struct FooWords { static const char* tip() { return "Insert here"; } };' >"$dir/Source/UI/FooWords.h"
    printf '%s\n' '#include "UI/FooWords.h"' "$1" >"$dir/Source/UI/Consumer.cpp"
    printf '%s\n' 'replace what is there' >"$dir/scripts/wording-informal-phrases.txt"
    : >"$dir/scripts/wording-informal-baseline.txt"
    echo "$dir"
}

verdict() { # verdict <root> -> yes when the guard fails
    if bash "$CHECK" --root "$1" >/dev/null 2>&1; then echo "no"; else echo "yes"; fi
}

check "a hard-coded tooltip beside a vocabulary include fails" \
    "$(verdict "$(tree 'void f() { button.setTooltip("Insert the section here"); }')")" "yes"
check "a hard-coded choice (addItem) fails" \
    "$(verdict "$(tree 'void f() { combo.addItem("Replace the sections under it", 2); }')")" "yes"
check "a tooltip read from the vocabulary passes" \
    "$(verdict "$(tree 'void f() { button.setTooltip(FooWords::tip()); }')")" "no"
check "a wording-ok marker with a reason passes" \
    "$(verdict "$(tree 'void f() { button.setTooltip("Zoom"); } // wording-ok: no vocabulary covers zoom')")" "no"
check "a wording-ok marker without a reason does not exempt" \
    "$(verdict "$(tree 'void f() { button.setTooltip("Zoom"); } // wording-ok:')")" "yes"
check "a commented-out hard-coded tooltip passes" \
    "$(verdict "$(tree '// button.setTooltip("Zoom");')")" "no"
check "a retired casual phrase in a literal fails" \
    "$(verdict "$(tree 'auto s = juce::String("Replace what is there");')")" "yes"
check "the retired phrase in a comment passes" \
    "$(verdict "$(tree '// was "replace what is there"')")" "no"

# A file that does not include a vocabulary header is not a consumer: its literal tooltip is out of scope.
dir="$(tree 'int x = 0;')"
printf '%s\n' 'void g() { b.setTooltip("Zoom"); }' >"$dir/Source/UI/Other.cpp"
check "a file with no vocabulary include is not scanned for tooltips" "$(verdict "$dir")" "no"

# Baseline: an existing literal is allowed up to its count, no more, and a stale entry fails.
dir="$(tree 'void f() { b.setTooltip("Zoom"); }')"
printf 'Source/UI/Consumer.cpp\t1\n' >"$dir/scripts/wording-informal-baseline.txt"
check "a baselined literal passes" "$(verdict "$dir")" "no"
printf '%s\n' 'void g() { b.setTooltip("Pan"); }' >>"$dir/Source/UI/Consumer.cpp"
check "a second literal beyond the baseline fails" "$(verdict "$dir")" "yes"
printf '%s\n' '#include "UI/FooWords.h"' >"$dir/Source/UI/Consumer.cpp"
check "a baseline entry whose literal is gone fails until --update" "$(verdict "$dir")" "yes"
bash "$CHECK" --root "$dir" --update >/dev/null 2>&1
check "--update drops the entry" "$(verdict "$dir")" "no"
check "--update never adds a file" "$(grep -c 'Consumer.cpp' "$dir/scripts/wording-informal-baseline.txt")" "0"

# The real tree.
output="$(bash "$CHECK" 2>&1)"
if [ $? -ne 0 ]; then
    printf 'FAIL  the repo tree has wording that bypasses its vocabulary\n\n%s\n\n' "$output"
    fail=$((fail + 1))
else
    printf 'ok    the repo tree reads its wording from the vocabularies\n'
    pass=$((pass + 1))
fi

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
