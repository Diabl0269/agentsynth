#!/usr/bin/env bash
#
# Tests scripts/check-unity-clashes.sh on a small fixture tree: two files of one unity group that
# declare the same anonymous-namespace name fail and are named, a file in SYNTH_UNITY_SKIP is
# ignored, legal repeats (identical aliases, overloads, named-namespace names) pass, and the baseline
# ratchets (never adds, stale entries fail, --update drops them). Also: an anonymous-namespace name
# that a header included by another source of the group declares in a named namespace (FRO1043 kGap).

set -uo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GUARD="$REPO_ROOT/scripts/check-unity-clashes.sh"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
failures=0
check() { # check <name> <condition...>
    local name="$1"
    shift
    if "$@"; then echo "ok: $name"; else echo "FAIL: $name"; failures=$((failures + 1)); fi
}
lacks() { ! grep -q "$1" <<<"$2"; } # lacks <pattern> <text>

export UNITY_CLASHES_BASELINE="$TMP/baseline.txt"
ROOT="$TMP/repo"
mkdir -p "$ROOT/cmake" "$ROOT/Source/UI"
# Runs from a decoy directory with its own cmake/Extra.cmake: includes must resolve against --root, not the cwd.
mkdir -p "$TMP/decoy/cmake"
: >"$TMP/decoy/cmake/Extra.cmake"
guard() { (cd "$TMP/decoy" && bash "$GUARD" --root "$ROOT" "$@"); }

cat >"$ROOT/CMakeLists.txt" <<'CMAKE'
add_library(Core STATIC
    Source/UI/A.cpp
    Source/UI/B.cpp
    Source/UI/Skipped.cpp
    Source/UI/Ok.cpp
    Source/UI/AddedLater.cpp
    Source/UI/C.cpp
    Source/UI/D.cpp
    Source/UI/Grid/GridSame.cpp
    Source/UI/Grid/GridModel.cpp
    Source/UI/Grid/GridLayout.cpp
    Source/UI/Grid/GridPolyHint.cpp
)
include(cmake/Extra.cmake)
synth_enable_unity(Core)
target_sources(Core PRIVATE Source/UI/Late.cpp)
CMAKE
printf 'target_sources(Core PRIVATE Source/UI/E.cpp Source/UI/F.cpp)\n' >"$ROOT/cmake/Extra.cmake"
printf 'namespace {\nconstexpr int kViaInclude = 1;\n}\n' >"$ROOT/Source/UI/E.cpp"
printf 'namespace {\nconstexpr int kViaInclude = 2;\n}\n' >"$ROOT/Source/UI/F.cpp"
printf 'set(AGENTSYNTH_UNITY_GROUP_SIZE 12 CACHE STRING "x")\n' >"$ROOT/cmake/Pch.cmake"
printf 'set(SYNTH_UNITY_SKIP\n    Source/UI/Skipped.cpp\n)\n' >"$ROOT/cmake/UnityExclusions.cmake"

printf 'namespace synth::ui {\nnamespace {\nconstexpr float kGlyphSize = 12.0f;\nstruct Cell { int x; };\n}\n}\n' >"$ROOT/Source/UI/A.cpp"
printf 'namespace synth::ui {\nnamespace {\nconstexpr float kGlyphSize = 9.0f;\nstruct Cell { int y; };\n}\n}\n' >"$ROOT/Source/UI/B.cpp"
printf 'namespace synth::ui {\nnamespace {\nconstexpr float kGlyphSize = 1.0f;\n}\n}\n' >"$ROOT/Source/UI/Skipped.cpp"
# Legal repeats: an identical alias, an overload, a function-local name and a named-namespace name.
printf 'namespace synth::ui {\nnamespace {\nusing Id = int;\nint twice(int v) { return v; }\n}\nconstexpr int kPublic = 1;\n}\n' >"$ROOT/Source/UI/Ok.cpp"
printf 'namespace synth::ui {\nnamespace {\nusing Id = int;\nint twice(float v) { int kLocal = 0; return int(v) + kLocal; }\n}\nconstexpr int kPublic = 2;\n}\n' >"$ROOT/Source/UI/AddedLater.cpp"
printf 'namespace {\nconstexpr float kGlyphSize = 1.0f;\n}\n' >"$ROOT/Source/UI/Late.cpp"

# The shape from the FRO1496 Windows failure: a named namespace wrapping an anonymous one.
printf 'namespace a::b {\nnamespace {\nconstexpr float k = 1;\n}\n}\n' >"$ROOT/Source/UI/C.cpp"
printf 'namespace a::b {\nnamespace {\nconstexpr float k = 2;\n}\n}\n' >"$ROOT/Source/UI/D.cpp"

# Header names (FRO1043): kGap in the header lives in synth::ui::grid; the anonymous kGap sits in synth::ui.
mkdir -p "$ROOT/Source/UI/Grid"
printf 'namespace synth::ui::grid {\nconstexpr int kGap = 4;\nconstexpr int kSame = 1;\nconstexpr int kElsewhere = 1;\nusing Id = int;\n}\nnamespace synth::other {\nconstexpr int kUnrelated = 1;\n}\n' >"$ROOT/Source/UI/Grid/GridModel.h"
printf 'namespace synth::ui::grid {\nnamespace {\nconstexpr int kSame = 2;\n}\n}\n' >"$ROOT/Source/UI/Grid/GridSame.cpp"
printf '#include "UI/Grid/GridModel.h"\nnamespace synth::ui::grid { int g() { return kGap + kSame; } }\n' >"$ROOT/Source/UI/Grid/GridModel.cpp"
printf '#include "GridModel.h"\nnamespace synth::ui {\nusing namespace grid;\nint g2() { return 1; }\n}\n' >"$ROOT/Source/UI/Grid/GridLayout.cpp"
printf 'namespace synth::ui {\nnamespace {\nconstexpr int kGap = 6;\nconstexpr int kUnrelated = 3;\nusing Id = int;\n}\n}\n' >"$ROOT/Source/UI/Grid/GridPolyHint.cpp"

out="$(guard 2>&1)"
check "a clash fails" test $? -ne 0
check "an anonymous name against a header name under a using-directive is found (FRO1043 kGap)" grep -q "'synth::ui::(anon)::kGap' in an anonymous namespace in Source/UI/Grid/GridPolyHint.cpp is also declared in the header Source/UI/Grid/GridModel.h" <<<"$out"
check "the same namespace needs no using-directive" grep -q "synth::ui::grid::(anon)::kSame' in an anonymous namespace in Source/UI/Grid/GridSame.cpp is also declared in the header" <<<"$out"
check "a header name in an unrelated namespace is not a clash" lacks kUnrelated "$out"
check "an identical alias against the header alias is not a clash" lacks "::Id" "$out"
check "the failure names the constant" grep -q "synth::ui::(anon)::kGlyphSize" <<<"$out"
check "the failure names both files" grep -q 'Source/UI/A.cpp and Source/UI/B.cpp' <<<"$out"
check "the struct clash is found too" grep -q "synth::ui::(anon)::Cell" <<<"$out"
check "an excluded file is not named" lacks Skipped.cpp "$out"
check "a source added after synth_enable_unity is not named" lacks Late.cpp "$out"
check "identical aliases, overloads and named-namespace names pass" lacks 'Ok.cpp\|AddedLater.cpp' "$out"
check "a nested namespace a::b { namespace { k } } clash is found" grep -q "a::b::(anon)::k" <<<"$out"
check "sources from an included cmake file resolve against --root, not the cwd" grep -q "(anon)::kViaInclude" <<<"$out"
check "--list prints the clashes" test "$(guard --list | wc -l | tr -d ' ')" = 6

printf '%s\n' \
    'synth::ui::(anon)::kGlyphSize | Source/UI/A.cpp | Source/UI/B.cpp | old' \
    'synth::ui::(anon)::Cell | Source/UI/A.cpp | Source/UI/B.cpp | old' \
    '(anon)::kViaInclude | Source/UI/E.cpp | Source/UI/F.cpp | old' \
    'a::b::(anon)::k | Source/UI/C.cpp | Source/UI/D.cpp | old' \
    'synth::ui::(anon)::kGap | Source/UI/Grid/GridPolyHint.cpp | Source/UI/Grid/GridModel.h | old' \
    'synth::ui::grid::(anon)::kSame | Source/UI/Grid/GridSame.cpp | Source/UI/Grid/GridModel.h | old' >"$TMP/baseline.txt"
guard >/dev/null 2>&1
check "baselined clashes pass" test $? -eq 0

printf 'namespace synth::ui {\nnamespace {\nconstexpr float kOther = 9.0f;\n}\n}\n' >"$ROOT/Source/UI/B.cpp"
printf 'namespace synth::ui {\nnamespace {\nconstexpr float kOther = 9.0f;\n}\n}\n' >"$ROOT/Source/UI/A.cpp"
out="$(guard 2>&1)"
check "a baseline entry that no longer clashes is stale and fails" grep -q 'stale unity-clash baseline entry' <<<"$out"
check "a new clash is not covered by the old entries" grep -q "kOther" <<<"$out"
guard --update >/dev/null 2>&1
check "--update never adds a new clash (exits 1)" test $? -ne 0
check "--update dropped the stale entries" lacks kGlyphSize "$(cat "$TMP/baseline.txt")"
check "--update did not add the new clash" lacks kOther "$(cat "$TMP/baseline.txt")"

[ "$failures" -eq 0 ] && echo "check-unity-clashes tests: all passed" || echo "check-unity-clashes tests: $failures failed"
exit "$failures"
