#!/bin/bash
#
# ci-local.sh — reproduce, on this machine, everything .github/workflows/ci.yml's `Lint` job
# and the current platform's build-and-test job check, so a formatting slip or a missing
# CMakeLists.txt entry is caught before `git push` instead of ten minutes later in CI.
#
# WHY THIS EXISTS: CI has repeatedly caught things a local build didn't -- a new source file
# missing from a CMakeLists.txt (compiles locally against a stale target, fails to link in a
# clean CI checkout), clang-format drift, and platform-specific build failures. This script is
# the single source of truth for "what CI will check, run locally"; scripts/pre-push-release-test.sh
# is gone -- scripts/install-hooks.sh now wires this script into the pre-push hook directly. When
# ci.yml's Lint job or a build-and-test job's flags change, update THIS script (and re-read
# ci.yml -- don't guess), not the hook.
#
# WHAT IT CHECKS (mirrors ci.yml -- see docs/testing.md "Local CI reproduction" for the mapping):
#   1. clang-format --dry-run --Werror over Source/ Tests/ Tools/, exactly like the Lint job's
#      "Check Formatting" step. CHECK-ONLY, never -i: a violation fails loudly here rather than
#      being silently rewritten.
#   2. scripts/utf8-literal-check.sh against the real tree -- the Lint job's "Check for
#      un-decoded UTF-8 escapes" step, run directly (not just its unit test).
#   3. Every scripts/tests/*.test.sh -- ci-cache-check, ci-install-linux-deps,
#      check-nonascii-literals, ai-eval-ratchet, utf8-literal-check as of this writing, globbed
#      so a newly added one is picked up automatically. check-nonascii-literals.test.sh's own
#      last case scans the real Source/ tree, so this also covers the Lint job's ASCII-literal
#      gate.
#   4. Configure (-DENABLE_TESTS=ON -DENABLE_AI_HARNESS=ON, Release, matching the macOS/Windows
#      build-and-test jobs) and build EVERY target those jobs build with a plain
#      `cmake --build` -- Core, AppUI, AgentSynth, AgentSynthPlugin, Tests -- into
#      build-ci-local/. ccache and Ninja are picked up automatically when present (see the
#      top of CMakeLists.txt), so repeat runs are incremental.
#   5. Run the full suite: build-ci-local/Tests/Tests.
#
# NOT reproduced here (deliberately -- see docs/testing.md): the Ubuntu coverage gate
# (a separate opt-in, `bash scripts/coverage.sh`), the label-gated ASAN job, and actual
# cross-platform compilation -- this only exercises the toolchain installed on THIS machine.
#
# Usage:
#   bash scripts/ci-local.sh [--open] [-h|--help]
#
#   --open       After every check passes, `open` the built app bundle (macOS only). Default
#                off, since this also runs headless in the pre-push hook.
#   -h, --help   Show this message and exit.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build-ci-local"
OPEN_APP=false

usage() {
    cat <<'EOF'
Usage: bash scripts/ci-local.sh [--open] [-h|--help]

Reproduces .github/workflows/ci.yml's Lint job plus this machine's platform
build-and-test job: clang-format check, the UTF-8/ASCII literal checks,
every scripts/tests/*.test.sh, then a full Release build of every CMake
target CI builds (Core, AppUI, AgentSynth, AgentSynthPlugin, Tests) with
ENABLE_TESTS=ON, followed by the full test suite. See the header comment
in this file, and docs/testing.md's "Local CI reproduction" section, for
the full mapping to what CI actually runs.

  --open       After every check passes, `open` the built app bundle
               (macOS only). Default off, since this also runs headless
               in the pre-push hook.
  -h, --help   Show this message and exit.
EOF
}

for arg in "$@"; do
    case "$arg" in
        --open)
            OPEN_APP=true
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "ci-local: unknown argument '$arg' (see --help)" >&2
            usage >&2
            exit 1
            ;;
    esac
done

step() {
    printf '\n=== %s ===\n' "$1"
}

fail() {
    printf '\nci-local: FAILED -- %s\n' "$1" >&2
    exit 1
}

# --- 1. clang-format (check-only, pinned version) -------------------------------------------
step "clang-format --dry-run --Werror (Source/ Tests/ Tools/)"

pinned="$(cat .clang-format-version 2>/dev/null || true)"
if ! command -v clang-format >/dev/null 2>&1; then
    fail "clang-format not found on PATH. Install the pinned version: pip install \"clang-format==$pinned\""
fi
have="$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
if [ -n "$pinned" ] && [ "$have" != "$pinned" ]; then
    echo "ci-local: WARNING clang-format $have differs from the pinned $pinned (CI uses $pinned)."
    echo "          Match it: pip install \"clang-format==$pinned\""
fi

if ! find Source Tests Tools -name "*.h" -o -name "*.cpp" | xargs clang-format --dry-run --Werror; then
    fail "clang-format violations found (see above). Fix: find Source Tests Tools -name '*.h' -o -name '*.cpp' | xargs clang-format -i"
fi
echo "ci-local: formatting OK."

# --- 2. UTF-8 literal check, against the real tree -------------------------------------------
step "scripts/utf8-literal-check.sh (real Source/Tests tree)"
bash scripts/utf8-literal-check.sh || fail "un-decoded UTF-8 escape check failed (see above)."

# --- 3. Every scripts/tests/*.test.sh ----------------------------------------------------------
step "scripts/tests/*.test.sh"
shopt -s nullglob
test_scripts=(scripts/tests/*.test.sh)
shopt -u nullglob
if [ "${#test_scripts[@]}" -eq 0 ]; then
    fail "no scripts/tests/*.test.sh found -- expected at least ci-cache-check.test.sh etc."
fi
for t in "${test_scripts[@]}"; do
    echo "--- $t ---"
    bash "$t" || fail "$t failed (see above)."
done

# --- 4. Configure + build every target the CI build-and-test jobs build -----------------------
step "Configure ($BUILD_DIR)"

cmake_args=(-B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DENABLE_AI_HARNESS=ON)
# Only pick a generator on the FIRST configure. Once build-ci-local/CMakeCache.txt exists, CMake
# is locked to whatever generator created it -- re-passing -G Ninja against an existing Makefiles
# (or vice versa) cache is a hard "does not match the generator used previously" error with no
# recovery short of deleting the directory, which would defeat the point of an incremental hook.
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then
    cmake_args+=(-G Ninja)
fi
cmake -S . "${cmake_args[@]}"

step "Build (all CMake targets: Core, AppUI, AgentSynth, AgentSynthPlugin, Tests)"
JOBS="$( (command -v nproc >/dev/null 2>&1 && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
# --parallel (not the native `-- -jN`) works across every generator CMake might pick here --
# Ninja, Unix Makefiles, and on Windows/macOS potentially Visual Studio or Xcode, none of which
# understand a raw `-jN` passed through to the native tool.
cmake --build "$BUILD_DIR" --parallel "$JOBS"

# --- 5. Run the full test suite -----------------------------------------------------------------
step "Run tests"
TESTS_BIN="$BUILD_DIR/Tests/Tests"
if [ ! -x "$TESTS_BIN" ]; then
    fail "$TESTS_BIN not found or not executable -- the Tests target did not build."
fi
"$TESTS_BIN" || fail "test suite failed (see above)."

# --- Done: point at a build the user can actually launch ---------------------------------------
step "All checks passed"

APP_PATH="$(find "$BUILD_DIR" -name "Agent Synth.app" -type d 2>/dev/null | head -n 1)"
if [ -n "$APP_PATH" ]; then
    echo "App bundle: $APP_PATH"
    if [ "$OPEN_APP" = true ]; then
        if [ "$(uname)" = "Darwin" ]; then
            open "$APP_PATH"
        else
            echo "ci-local: --open is macOS-only; skipping (uname is $(uname))."
        fi
    else
        echo "Run it yourself:  open \"$APP_PATH\""
        echo "(or re-run with --open to launch it automatically once everything passes)"
    fi
else
    echo "ci-local: WARNING could not find 'Agent Synth.app' under $BUILD_DIR -- the AgentSynth target may not have built an app bundle on this platform."
fi

echo
echo "ci-local: all checks passed."
