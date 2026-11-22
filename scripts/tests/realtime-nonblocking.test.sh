#!/usr/bin/env bash
# Tests the compile-time audio-thread check (FRO453): with a compiler that knows
# [[clang::nonblocking]] (clang 20+), a function marked AS_NONBLOCKING that allocates fails the build with
# -Werror=function-effects and the message names the line, while the marked kernels in the tree compile clean.
# A compiler without the attribute skips the test: AS_NONBLOCKING is defined empty there (cmake/RealtimeChecks.cmake).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/realtime-nonblocking.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
CXX_BIN="${CXX:-c++}"
FLAGS=(-std=c++20 -fsyntax-only -Wfunction-effects -Werror=function-effects "-DAS_NONBLOCKING=__attribute__((nonblocking))" -I"$ROOT/Source")

if ! "$CXX_BIN" ${FLAGS[@]+"${FLAGS[@]}"} -x c++ /dev/null >/dev/null 2>&1; then
    echo "SKIP: $CXX_BIN does not know -Werror=function-effects (needs clang 20+)"
    exit 0
fi

pass=0
fail=0
check() { # check <description> <command...>
    local desc="$1"
    shift
    if "$@"; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc"
        fail=$((fail + 1))
    fi
}

cat >"$WORK/bad.cpp" <<'SRC'
#include <vector>
struct BadModule {
    std::vector<float> scratch;
    void process(int n) noexcept AS_NONBLOCKING {
        scratch.resize(static_cast<std::size_t>(n)); // line 5: allocates on the audio thread
    }
};
SRC

cat >"$WORK/good.cpp" <<'SRC'
#include "AudioEngine/SpscRing.h"
#include "Modules/GateEdges.h"
struct Payload { int a; };
void run(synth::SpscRing<Payload>& ring, GateEdges& edges, const float* gate) noexcept AS_NONBLOCKING {
    ring.push({1});
    edges.scan(gate, 8);
    edges.firesAt(0);
}
SRC

BAD_OUT="$("$CXX_BIN" ${FLAGS[@]+"${FLAGS[@]}"} "$WORK/bad.cpp" 2>&1)"
BAD_RC=$?
check "a marked function that allocates fails the build" test "$BAD_RC" -ne 0
check "the error names the line" grep -q "bad.cpp:5" <<<"$BAD_OUT"
check "the error says why" grep -q "nonblocking" <<<"$BAD_OUT"
check "the marked kernels in the tree compile clean" "$CXX_BIN" ${FLAGS[@]+"${FLAGS[@]}"} "$WORK/good.cpp"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
