#!/usr/bin/env bash
#
# Guard + unit tests for scripts/check-ui-caps.sh.
#
# THE RULE. UI text is never written in ALL CAPS: no toUpperCase() in Source/UI, and no string literal
# there with a 4+ letter capitals word that is not a known abbreviation (MIDI, ADSR, ...). Text that is
# not UI (a hex colour code) opts out with a trailing `// not-ui-text: <reason>` marker.
#
# Comments are exempt. Runs in the Lint job: no compiler, ~1 s.
#
# Usage: bash scripts/tests/check-ui-caps.test.sh

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$REPO_ROOT/scripts/check-ui-caps.sh"
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

# flagged <file> -> "yes" when the check reports that fixture, "no" when it is clean.
flagged() {
    if bash "$CHECK" "$1" >/dev/null 2>&1; then echo "no"; else echo "yes"; fi
}

fixture() { # fixture <name> <content>
    printf '%s\n' "$2" >"$WORK/$1"
    echo "$WORK/$1"
}

# --- what must be FLAGGED ------------------------------------------------------------------
check "toUpperCase() is flagged" "verdict" \
    "$(flagged "$(fixture a.cpp 'g.drawText(name.toUpperCase(), r, j);')")" "yes"
check "a caps literal is flagged" "verdict" \
    "$(flagged "$(fixture b.cpp 'g.drawText("MODULATION MATRIX", r, j);')")" "yes"
check "a caps word inside a longer literal is flagged" "verdict" \
    "$(flagged "$(fixture c.h 'label.setText("Patch REJECTED: no", n);')")" "yes"
check "a caps literal below a comment line is flagged" "verdict" \
    "$(flagged "$(fixture d.cpp $'// MIDI\nauto s = juce::String("EXPAND ALL");')")" "yes"
check "a marker without a reason does not exempt" "verdict" \
    "$(flagged "$(fixture e.cpp 'auto s = x.toUpperCase(); // not-ui-text:')")" "yes"
check "a caps literal after a closed block comment is flagged" "verdict" \
    "$(flagged "$(fixture f.cpp $'/* MIDI */\nauto s = juce::String("SOURCE");')")" "yes"

# --- what must NOT be flagged --------------------------------------------------------------
check "the opt-out marker exempts toUpperCase()" "verdict" \
    "$(flagged "$(fixture g.cpp 'auto hex = s.toUpperCase(); // not-ui-text: hex colour code')")" "no"
check "the opt-out marker exempts a caps literal" "verdict" \
    "$(flagged "$(fixture h.cpp 'auto t = juce::String("TITLE"); // not-ui-text: pending de-cap')")" "no"
check "known abbreviations pass" "verdict" \
    "$(flagged "$(fixture i.cpp 'auto s = juce::String("MIDI Learn: ADSR, JSON and NRPN");')")" "no"
check "normal case passes" "verdict" \
    "$(flagged "$(fixture j.cpp 'g.drawText("Modulation Matrix", r, j);')")" "no"
check "a comment mentioning caps and toUpperCase() passes" "verdict" \
    "$(flagged "$(fixture k.cpp $'// was COLLAPSE ALL via toUpperCase()\n/* EXPAND ALL */\nint x = 0;')")" "no"
check "an identifier in capitals (not a literal) passes" "verdict" \
    "$(flagged "$(fixture l.cpp 'constexpr int MAXIMUM_ROWS = 4;')")" "no"
check "short capitals words (3 letters) pass" "verdict" \
    "$(flagged "$(fixture m.cpp 'auto s = juce::String("ATK DEC SUS REL");')")" "no"

# --- the real thing: the repo's own Source/UI tree must be clean ---------------------------
output="$(bash "$CHECK" "$REPO_ROOT/Source/UI" 2>&1)"
status=$?
if [ "$status" -ne 0 ]; then
    printf 'FAIL  Source/UI contains all-caps UI text\n\n%s\n\n' "$output"
    fail=$((fail + 1))
else
    printf 'ok    Source/UI has no all-caps UI text\n'
    pass=$((pass + 1))
fi

printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
