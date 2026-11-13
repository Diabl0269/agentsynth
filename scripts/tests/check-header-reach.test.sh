#!/usr/bin/env bash
#
# Tests scripts/check-header-reach.sh on a small fixture tree: it passes unchanged, fails when an
# include adds reach, fails for a new header over the cap, and --update lowers what fell and leaves what rose.

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

# 12 .cpp files include Wide.h (over the cap of 10); 12 more include Small.h only through Sub/Mid.h;
# 176 more include nothing, so the tree has 200 .cpp files and a few new ones barely move the scale.
printf '#pragma once\n' >"$ROOT/Source/Wide.h"
printf '#pragma once\n' >"$ROOT/Source/Small.h"
printf '#pragma once\n#include "../Small.h"\n' >"$ROOT/Source/Sub/Mid.h"
printf '#pragma once\n' >"$ROOT/Source/Extra.h"
for i in $(seq 1 12); do
    printf '#include "Wide.h"\n' >"$ROOT/Source/A$i.cpp"
    printf '#include <Sub/Mid.h>\n#include <juce_core/juce_core.h>\n' >"$ROOT/Tests/B$i.cpp"
done
for i in $(seq 1 176); do printf '#include <juce_core/juce_core.h>\n' >"$ROOT/Tests/Z$i.cpp"; done

guard --update >/dev/null
check "the first --update writes the baseline" test -f "$TMP/baseline.txt"
check "Wide.h is baselined at its count" grep -qx '12 Source/Wide.h' "$TMP/baseline.txt"
check "an include through Sub/Mid.h counts at the end of the chain" grep -qx '12 Source/Small.h' "$TMP/baseline.txt"
check "JUCE and unresolved includes are ignored" test "$(guard --list | wc -l | tr -d ' ')" = 3

out="$(guard 2>&1)"
check "an unchanged tree passes" test $? -eq 0

# --- the tree grows: limits scale with the .cpp total -----------------------------------------
check "the baseline records the .cpp total" grep -qx '# total-cpp: 200' "$TMP/baseline.txt"
# 6 new .cpp files include Wide.h and 94 include nothing: the tree grows by half and Wide.h's reach
# from 12 to 18, which is growth in proportion. Without scaling it would be 6 over the slack of 5.
for i in $(seq 1 6); do printf '#include "Wide.h"\n' >"$ROOT/Source/G$i.cpp"; done
for i in $(seq 1 94); do printf '#include <juce_core/juce_core.h>\n' >"$ROOT/Tests/Y$i.cpp"; done
out="$(guard 2>&1)"
check "growth in proportion to the tree passes" test $? -eq 0
# One include of Wide.h in a header the 12 Tests files already reach adds 12 more.
printf '#pragma once\n#include "../Small.h"\n#include "../Wide.h"\n' >"$ROOT/Source/Sub/Mid.h"
out="$(guard 2>&1)"
check "one wide include still fails when the tree has grown" test $? -ne 0
check "the failure states the scaled limit" grep -q 'scaled for 300 .cpp files now vs 200 then: 18, allowed up to 23' <<<"$out"
printf '#pragma once\n#include "../Small.h"\n' >"$ROOT/Source/Sub/Mid.h"
guard --update >/dev/null 2>&1
check "--update accepts the proportional growth" test $? -eq 0
check "--update rewrites the total" grep -qx '# total-cpp: 300' "$TMP/baseline.txt"
rm "$ROOT"/Source/G?.cpp "$ROOT"/Tests/Y*.cpp
guard --update >/dev/null 2>&1
check "--update after the tree shrinks lowers the total" grep -qx '# total-cpp: 200' "$TMP/baseline.txt"
check "the entries are back at their counts" grep -qx '12 Source/Wide.h' "$TMP/baseline.txt"

# --- an include that adds reach ----------------------------------------------------------------
# Wide.h baselined at 12 may grow by 5 (plus a sliver of scale): add Wide.h to 7 more files.
for i in $(seq 1 7); do printf '#include "Wide.h"\n' >"$ROOT/Source/C$i.cpp"; done
out="$(guard 2>&1)"
check "growth past the slack fails" test $? -ne 0
check "the failure names the header" grep -q 'Source/Wide.h reaches 19 .cpp files, up from 12' <<<"$out"
check "the failure lists the direct includers" grep -q 'Source/A1.cpp' <<<"$out"
for i in 1 2 3 4 5 6 7; do rm "$ROOT/Source/C$i.cpp"; done

for i in $(seq 1 5); do printf '#include "Wide.h"\n' >"$ROOT/Source/C$i.cpp"; done
guard >/dev/null 2>&1
check "growth within the slack passes" test $? -eq 0
out="$(guard --update 2>&1)"
check "--update succeeds when an entry rose" test $? -eq 0
check "--update leaves a risen entry at its count scaled for the tree" grep -qx '13 Source/Wide.h' "$TMP/baseline.txt"
check "--update says how many it left" grep -q 'left 1 entries that rose' <<<"$out"
guard --update --allow-raise >/dev/null 2>&1
check "--allow-raise lets a raise through" grep -qx '17 Source/Wide.h' "$TMP/baseline.txt"

# --- one --update with an entry that rose, one that fell and one that went under the cap ---------
# Wide.h is at 17. Two new files lift it to 19, past its 18 scaled for the tree. Three B files include
# Small.h directly and B12 goes, so Small.h falls to 11 and Sub/Mid.h falls under the cap to 8.
for i in 1 2 3; do printf '#include "Small.h"\n' >"$ROOT/Tests/B$i.cpp"; done
rm "$ROOT/Tests/B12.cpp"
for i in 6 7; do printf '#include "Wide.h"\n' >"$ROOT/Source/C$i.cpp"; done
guard --update >"$TMP/mixed.out" 2>&1
check "a mixed --update succeeds" test $? -eq 0
check "mixed: the entry that rose is left" grep -qx '18 Source/Wide.h' "$TMP/baseline.txt"
check "mixed: the entry that fell is lowered" grep -qx '11 Source/Small.h' "$TMP/baseline.txt"
check "mixed: the entry under the cap is dropped" test -z "$(grep Mid.h "$TMP/baseline.txt")"
check "mixed: the count of entries left is printed" grep -q 'left 1 entries that rose' "$TMP/mixed.out"
guard >/dev/null 2>&1
check "mixed: the result passes the check" test $? -eq 0
rm "$ROOT/Source/C6.cpp" "$ROOT/Source/C7.cpp"
for i in 1 2 3 12; do printf '#include <Sub/Mid.h>\n#include <juce_core/juce_core.h>\n' >"$ROOT/Tests/B$i.cpp"; done
guard --update --allow-raise >/dev/null 2>&1

# --- a header that is new and over the cap ------------------------------------------------------
for i in $(seq 1 11); do printf '#include "Extra.h"\n' >"$ROOT/Source/D$i.cpp"; done
guard >/dev/null 2>&1
check "a new header just over the cap passes inside the slack" test $? -eq 0
guard --update >/dev/null 2>&1
check "--update leaves a header inside the slack out, and succeeds" test $? -eq 0
check "the baseline did not gain the header inside the slack" test -z "$(grep Extra.h "$TMP/baseline.txt")"
for i in $(seq 12 16); do printf '#include "Extra.h"\n' >"$ROOT/Source/D$i.cpp"; done
out="$(guard 2>&1)"
check "a new header past the slack fails" test $? -ne 0
check "the failure says it is not in the baseline" grep -q 'Source/Extra.h reaches 16 .cpp files (cap 10) and is not in the baseline' <<<"$out"
guard --update >/dev/null 2>&1
check "--update does not add a header" test $? -ne 0
check "the baseline did not gain it" test -z "$(grep Extra.h "$TMP/baseline.txt")"
for i in $(seq 1 16); do rm "$ROOT/Source/D$i.cpp"; done

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
