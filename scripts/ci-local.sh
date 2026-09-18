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
#   3. scripts/check-file-sizes.sh against the real tree -- the Lint job's "Check file sizes"
#      step: hard 1000-line cap + strict ratchet baseline, run directly (not just its unit test).
#   4. scripts/check-function-sizes.sh against the real tree -- the Lint job's "Check function
#      sizes" step: hard 200-line-per-function cap + strict ratchet baseline, run directly (not
#      just its unit test).
#   5. scripts/check-header-comments.sh against the real tree -- the Lint job's "Check header
#      comment placement" step: comment-PLACEMENT threshold (comments outnumber code, floor 60,
#      has a sibling .cpp) + strict ratchet baseline, run directly (not just its unit test).
#   6. scripts/check-docs.sh against the real tree -- docs link/anchor/naming integrity: markdown
#      link targets, docs/... path mentions, section (§) references, and the docs/ filename
#      convention, run directly (not just its unit test). See that script's own doc comment.
#   7. Every scripts/tests/*.test.sh -- ci-cache-check, ci-install-linux-deps,
#      check-nonascii-literals, ai-eval-ratchet, utf8-literal-check, check-file-sizes,
#      check-function-sizes, check-header-comments, check-docs as of this writing, globbed so a
#      newly added one is picked up automatically. check-nonascii-literals.test.sh's own last case
#      scans the real Source/ tree, so this also covers the Lint job's ASCII-literal gate.
#   8. Configure (-DENABLE_TESTS=ON -DENABLE_AI_HARNESS=ON, Release, matching the macOS/Windows
#      build-and-test jobs) and build EVERY target those jobs build with a plain
#      `cmake --build` -- Core, AppUI, AgentSynth, AgentSynthPlugin, Tests -- into
#      build-ci-local/. ccache and Ninja are picked up automatically when present (see the
#      top of CMakeLists.txt), so repeat runs are incremental.
#   9. Dev-sign the built app bundle with scripts/dev-sign-app.sh (macOS only -- not a CI check,
#      but the point where a local build exists to sign; see that script's header for why).
#   10. Run the full suite: build-ci-local/Tests/Tests.
#
# NOT reproduced here (deliberately -- see docs/testing.md): the Ubuntu coverage gate
# (a separate opt-in, `bash scripts/coverage.sh`), the label-gated ASAN job, and actual
# cross-platform compilation -- this only exercises the toolchain installed on THIS machine.
#
# Usage:
#   bash scripts/ci-local.sh [--open] [--skip-tests] [-h|--help]
#
#   --open        After every check passes, `open` the built app bundle (macOS only). Default
#                 off, since this also runs headless in the pre-push hook.
#   --skip-tests  Skip step 10 (the full Tests suite, often the slowest step). Everything else
#                 still runs, including the dev-sign step, so a rebuild still keeps the same
#                 TCC identity for a live/manual app run. Default off: the pre-push hook and CI
#                 both expect the full suite, so leave this off unless you're iterating locally
#                 and will run the suite before pushing.
#   -h, --help    Show this message and exit.

set -euo pipefail

# When this runs from a git hook (scripts/install-hooks.sh wires it into pre-push), git exports
# GIT_DIR and friends into the environment. Anything below that creates or inspects a git repo
# -- the scripts/tests/*.test.sh harnesses build throwaway repos -- would then act on THIS
# repository instead (FRO82: a hook-run harness staged 683 real files as deleted). Strip the
# inherited git env up front; every git call below does normal discovery from the cwd.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build-ci-local"
OPEN_APP=false
SKIP_TESTS=false

usage() {
    cat <<'EOF'
Usage: bash scripts/ci-local.sh [--open] [--skip-tests] [-h|--help]

Reproduces .github/workflows/ci.yml's Lint job plus this machine's platform
build-and-test job: clang-format check, the UTF-8/ASCII literal checks, the
file-size guard, the function-size guard, the header-comment-placement guard,
the docs integrity guard, every scripts/tests/*.test.sh, then a full Release
build of every CMake target CI builds (Core, AppUI, AgentSynth,
AgentSynthPlugin, Tests) with
ENABLE_TESTS=ON, followed by the full test suite. See the header comment in
this file, and docs/testing.md's "Local CI reproduction" section, for the
full mapping to what CI actually runs.

  --open        After every check passes, `open` the built app bundle
                (macOS only). Default off, since this also runs headless
                in the pre-push hook.
  --skip-tests  Skip the Tests suite (step 10), often the slowest step.
                Everything else still runs, including the dev-sign step,
                so a rebuild still keeps the same TCC identity for a
                live/manual app run. Default off -- the pre-push hook and
                CI both expect the full suite; only pass this for a quick
                local iteration loop, and run the suite before pushing.
  -h, --help    Show this message and exit.
EOF
}

for arg in "$@"; do
    case "$arg" in
        --open)
            OPEN_APP=true
            ;;
        --skip-tests)
            SKIP_TESTS=true
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

# --- 3. File-size guard, against the real tree -------------------------------------------------
step "scripts/check-file-sizes.sh (real tree, 1000-line cap + ratchet baseline)"
bash scripts/check-file-sizes.sh || fail "file-size guard failed (see above)."

# --- 4. Function-size guard, against the real tree -----------------------------------------------
step "scripts/check-function-sizes.sh (real tree, 200-line-per-function cap + ratchet baseline)"
bash scripts/check-function-sizes.sh || fail "function-size guard failed (see above)."

# --- 5. Header-comment-placement guard, against the real tree -----------------------------------
step "scripts/check-header-comments.sh (real tree, comment-placement threshold + ratchet baseline)"
bash scripts/check-header-comments.sh || fail "header-comment-placement guard failed (see above)."

# --- 6. Docs guard, against the real tree -------------------------------------------------------
step "scripts/check-docs.sh (docs link/anchor/naming integrity)"
bash scripts/check-docs.sh || fail "docs integrity guard failed (see above)."

# --- 7. Every scripts/tests/*.test.sh ----------------------------------------------------------
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

# --- 8. Configure + build every target the CI build-and-test jobs build -----------------------
step "Configure ($BUILD_DIR)"

cmake_args=(-B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DENABLE_AI_HARNESS=ON)
# Only pick a generator on the FIRST configure. Once build-ci-local/CMakeCache.txt exists, CMake
# is locked to whatever generator created it -- re-passing -G Ninja against an existing Makefiles
# (or vice versa) cache is a hard "does not match the generator used previously" error with no
# recovery short of deleting the directory, which would defeat the point of an incremental hook.
FIRST_CONFIGURE=false
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    FIRST_CONFIGURE=true
    if command -v ninja >/dev/null 2>&1; then
        cmake_args+=(-G Ninja)
    fi
fi

# Worktree dependency-source reuse (FIRST configure only): a fresh worktree's build-ci-local/_deps
# starts empty, so FetchContent would otherwise re-download JUCE/GoogleTest/Sparkle from scratch
# even though the main checkout right next to it already has them on disk. See
# scripts/lib/deps-reuse.sh for the full rationale and the pin-match safety check.
# CI_LOCAL_NO_DEPS_REUSE=1 opts out.
if [ "$FIRST_CONFIGURE" = true ]; then
    # shellcheck source=scripts/lib/deps-reuse.sh
    source "$REPO_ROOT/scripts/lib/deps-reuse.sh"
    # Deliberately lowercase, unlike the real GIT_COMMON_DIR/GIT_WORK_TREE/etc. env vars this
    # script `unset`s above (FRO82) -- keeping this a plain local avoids ever re-creating one of
    # those exact names in a script that exists partly to strip them.
    git_common_dir="$(git rev-parse --git-common-dir 2>/dev/null || true)"
    main_checkout_root=""
    if [ -n "$git_common_dir" ]; then
        case "$git_common_dir" in
            /*) : ;;
            *) git_common_dir="$REPO_ROOT/$git_common_dir" ;;
        esac
        main_checkout_root="$(cd "$git_common_dir/.." && pwd)"
    fi
    deps_reuse_compute "$REPO_ROOT" "$main_checkout_root" "$BUILD_DIR"
    echo "$DEPS_REUSE_MESSAGE"
    # bash 3.2 (macOS's default /bin/bash) treats "${arr[@]}" on a zero-element array as an
    # unbound-variable error under `set -u` -- guard the expansion rather than relying on the
    # bash 4.4+ fix this script cannot assume.
    if [ "${#DEPS_REUSE_ARGS[@]}" -gt 0 ]; then
        cmake_args+=("${DEPS_REUSE_ARGS[@]}")
    fi
fi

cmake -S . "${cmake_args[@]}"

step "Build (all CMake targets: Core, AppUI, AgentSynth, AgentSynthPlugin, Tests)"
JOBS="$( (command -v nproc >/dev/null 2>&1 && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
# --parallel (not the native `-- -jN`) works across every generator CMake might pick here --
# Ninja, Unix Makefiles, and on Windows/macOS potentially Visual Studio or Xcode, none of which
# understand a raw `-jN` passed through to the native tool.
cmake --build "$BUILD_DIR" --parallel "$JOBS"

# --- 9. Dev-sign the app bundle (macOS only, before tests so a signing failure surfaces early) --
step "Dev-sign app bundle (stable TCC identity)"
# `|| true`: under `set -o pipefail`, `head -n 1` closing the pipe after its first line can make
# `find` see SIGPIPE and exit non-zero, which -- since this is a plain assignment, not `local` --
# would trip `set -e` and abort the whole script here instead of just leaving APP_PATH empty.
APP_PATH="$( (find "$BUILD_DIR" -name "Agent Synth.app" -type d 2>/dev/null || true) | head -n 1)"
if [ "$(uname)" = "Darwin" ]; then
    if [ -n "$APP_PATH" ]; then
        bash scripts/dev-sign-app.sh "$APP_PATH" || fail "scripts/dev-sign-app.sh failed (see above)."
    else
        echo "ci-local: WARNING could not find 'Agent Synth.app' under $BUILD_DIR -- skipping dev-sign."
    fi
else
    echo "ci-local: not macOS, skipping dev-sign."
fi

# --- 10. Run the full test suite -----------------------------------------------------------------
if [ "$SKIP_TESTS" = true ]; then
    step "Run tests (skipped: --skip-tests)"
else
    step "Run tests"
    TESTS_BIN="$BUILD_DIR/Tests/Tests"
    if [ ! -x "$TESTS_BIN" ]; then
        fail "$TESTS_BIN not found or not executable -- the Tests target did not build."
    fi
    "$TESTS_BIN" || fail "test suite failed (see above)."
fi

# --- Done: point at a build the user can actually launch ---------------------------------------
step "All checks passed"

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
