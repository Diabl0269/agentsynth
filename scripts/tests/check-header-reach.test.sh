#!/usr/bin/env bash
#
# Tests scripts/check-header-reach.sh on a small fixture tree: it passes unchanged, fails when an
# include adds reach, fails for a new header over the cap, and --update only lowers.

set -uo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GUARD="$REPO_ROOT/scripts/check-header-reach.sh"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
failures=0
check() { # check <name> <condition...>
    local name="$1"
    shift
    if "$@"; then echo "ok: $name"; else echo "FAIL: $name"; failures=$((failures + 1)); fi
}

export HEADER_REACH_CAP=10
export HEADER_REACH_BASELINE="$TMP/baseline.txt"
ROOT="$TMP/repo"
mkdir -p "$ROOT/Source/Sub" "$ROOT/Tests"
git -C "$ROOT" init -q

guard() { bash "$GUARD" --root "$ROOT" "$@"; }

# 12 .cpp files include Wide.h (over the cap of 10); 12 more include Small.h only through Sub/Mid.h.
printf '#pragma once\n' >"$ROOT/Source/Wide.h"
printf '#pragma once\n' >"$ROOT/Source/Small.h"
printf '#pragma once\n#include "../Small.h"\n' >"$ROOT/Source/Sub/Mid.h"
printf '#pragma once\n' >"$ROOT/Source/Extra.h"
for i in $(seq 1 12); do
    printf '#include "Wide.h"\n' >"$ROOT/Source/A$i.cpp"
    printf '#include <Sub/Mid.h>\n#include <juce_core/juce_core.h>\n' >"$ROOT/Tests/B$i.cpp"
done

guard --update >/dev/null
check "the first --update writes the baseline" test -f "$TMP/baseline.txt"
check "Wide.h is baselined at its count" grep -qx '12 Source/Wide.h' "$TMP/baseline.txt"
check "an include through Sub/Mid.h counts at the end of the chain" grep -qx '12 Source/Small.h' "$TMP/baseline.txt"
check "JUCE and unresolved includes are ignored" test "$(guard --list | wc -l | tr -d ' ')" = 3

out="$(guard 2>&1)"
check "an unchanged tree passes" test $? -eq 0

# --- an include that adds reach ----------------------------------------------------------------
# Wide.h baselined at 12 may grow by 5: add Wide.h to 6 more files.
for i in $(seq 1 6); do printf '#include "Wide.h"\n' >"$ROOT/Source/C$i.cpp"; done
out="$(guard 2>&1)"
check "growth past the slack fails" test $? -ne 0
check "the failure names the header" grep -q 'Source/Wide.h reaches 18 .cpp files, up from 12' <<<"$out"
check "the failure lists the direct includers" grep -q 'Source/A1.cpp' <<<"$out"
for i in 1 2 3 4 5 6; do rm "$ROOT/Source/C$i.cpp"; done

for i in $(seq 1 5); do printf '#include "Wide.h"\n' >"$ROOT/Source/C$i.cpp"; done
guard >/dev/null 2>&1
check "growth within the slack passes" test $? -eq 0
out="$(guard --update 2>&1)"
check "--update refuses to raise an entry" test $? -ne 0
check "a refused --update leaves the baseline alone" grep -qx '12 Source/Wide.h' "$TMP/baseline.txt"
guard --update --allow-raise >/dev/null 2>&1
check "--allow-raise lets a raise through" grep -qx '17 Source/Wide.h' "$TMP/baseline.txt"

# --- a header that is new and over the cap ------------------------------------------------------
for i in $(seq 1 11); do printf '#include "Extra.h"\n' >"$ROOT/Source/D$i.cpp"; done
out="$(guard 2>&1)"
check "a new header over the cap fails" test $? -ne 0
check "the failure says it is not in the baseline" grep -q 'Source/Extra.h reaches 11 .cpp files (cap 10) and is not in the baseline' <<<"$out"
guard --update >/dev/null 2>&1
check "--update does not add a header" test $? -ne 0
check "the baseline did not gain it" test -z "$(grep Extra.h "$TMP/baseline.txt")"
for i in $(seq 1 11); do rm "$ROOT/Source/D$i.cpp"; done

# --- --update lowers -----------------------------------------------------------------------------
for i in $(seq 1 5); do rm "$ROOT/Source/C$i.cpp"; done
rm "$ROOT/Source/A1.cpp"
out="$(guard 2>&1)"
check "a big shrink still passes but says to tighten" grep -q 'run --update to tighten' <<<"$out"
guard --update >/dev/null 2>&1
check "--update lowers an entry" grep -qx '11 Source/Wide.h' "$TMP/baseline.txt"
rm "$ROOT/Source/A2.cpp"
guard --update >/dev/null 2>&1
check "an entry under the cap is dropped" test -z "$(grep Wide.h "$TMP/baseline.txt")"
guard >/dev/null 2>&1
check "the lowered tree passes" test $? -eq 0

if [ "$failures" -gt 0 ]; then echo "$failures check(s) failed"; exit 1; fi
echo "all checks passed"
