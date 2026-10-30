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
# WHAT IT CHECKS (mirrors ci.yml -- see docs/development/local-ci.md for the mapping):
#   1. clang-format --dry-run --Werror over Source/ Tests/ Tools/, exactly like the Lint job's
#      "Check Formatting" step. CHECK-ONLY, never -i: a violation fails loudly here rather than
#      being silently rewritten.
#   2. scripts/utf8-literal-check.sh against the real tree -- the Lint job's "Check for
#      un-decoded UTF-8 escapes" step, run directly (not just its unit test).
#   3. scripts/check-file-sizes.sh against the real tree -- the Lint job's "Check file sizes"
#      step: hard 1000-line cap + strict ratchet baseline, run directly (not just its unit test).
#   3b. scripts/ci-cache-budget.sh against the real workflows -- the Lint job's "Check CI cache
#      budget" step (FRO341): sums one cache generation's worth of every ccache max_size plus a
#      deps allowance and fails over an 8 GiB budget, run directly (not just its unit test).
#   4. scripts/check-function-sizes.sh against the real tree -- the Lint job's "Check function
#      sizes" step: hard 200-line-per-function cap + strict ratchet baseline, run directly (not
#      just its unit test).
#   5. scripts/check-header-comments.sh against the real tree -- the Lint job's "Check header
#      comment placement" step: comment-PLACEMENT threshold (comments outnumber code, floor 60,
#      has a sibling .cpp) + strict ratchet baseline, run directly (not just its unit test).
#   5b. scripts/check-comment-provenance.sh against the real tree -- the Lint job's "Check comment
#      provenance" step: per-file count of comment lines carrying a ticket id or date + strict
#      ratchet baseline, run directly (not just its unit test).
#   5c. scripts/check-fixed-waits.sh against the real tree -- the Lint job's "Check fixed-time
#      waits" step: tests that pump the message loop for a fixed time and then assert, plus a
#      strict ratchet baseline, run directly (not just its unit test).
#   6. scripts/check-docs.sh against the real tree -- docs link/anchor/naming integrity: markdown
#      link targets, docs/... path mentions, section (§) references, and the docs/ filename
#      convention, run directly (not just its unit test). See that script's own doc comment.
#   7. Every scripts/tests/*.test.sh -- ci-cache-check, ci-install-linux-deps,
#      check-nonascii-literals, check-ui-caps, ai-eval-ratchet, utf8-literal-check, check-file-sizes,
#      check-function-sizes, check-header-comments, check-comment-provenance, check-fixed-waits, check-docs as of this writing, globbed so a
#      newly added one is picked up automatically. check-nonascii-literals.test.sh's own last case
#      scans the real Source/ tree, so this also covers the Lint job's ASCII-literal gate.
#   8. Configure (-DENABLE_TESTS=ON -DENABLE_AI_HARNESS=ON, Release) and build EVERY target the
#      macOS/Windows build-and-test jobs build with a plain
#      `cmake --build` -- Core, AppUI, AgentSynth, AgentSynthPlugin, Tests -- into
#      build-ci-local/. ccache and Ninja are picked up automatically when present (see the
#      top of CMakeLists.txt), so repeat runs are incremental.
#   8b. Configure-only into a scratch build-ci-local-ci-flags/ with CI's own flags (AGENTSYNTH_PCH,
#       explicit compiler launchers), so a configure break only CI's flags hit shows up before the push.
#   9. Dev-sign the built app bundle with scripts/dev-sign-app.sh (macOS only -- not a CI check,
#      but the point where a local build exists to sign; see that script's header for why).
#   10. Run the full suite as parallel GoogleTest shards (scripts/lib/test-shards.sh): each shard has
#       its own settings folder, temp folder and hard link of the binary, and holds one of the
#       machine-wide test slots while it runs, so several gates at once overlap rather than queue.
#
# Speed: lint checks run in parallel; builds use half the cores (CI_LOCAL_JOBS); step 7 skips the script tests when nothing they read changed since
# they last passed on this machine, except the ones that scan the real tree. Every step prints its
# time and the run ends with a table.
#
# --fast is the iteration mode, not a replacement for CI: clang-format on changed files only, the
# Tests target only, and only the test suites the change can reach (scripts/lib/changed-tests.py).
#
# NOT reproduced here (deliberately -- see docs/development/local-ci.md): the Ubuntu coverage gate
# (a separate opt-in, `bash scripts/coverage.sh`), the label-gated ASAN job, and actual
# cross-platform compilation -- this only exercises the toolchain installed on THIS machine.
#
# Usage:
#   bash scripts/ci-local.sh [--fast] [--open] [--skip-tests] [-h|--help]
#
#   --fast        Check only what the change reaches, against origin/main; CI still runs the rest.
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
FAST=false

usage() {
    cat <<'EOF'
Usage: bash scripts/ci-local.sh [--fast] [--open] [--skip-tests] [-h|--help]

Reproduces .github/workflows/ci.yml's Lint job plus this machine's platform
build-and-test job: clang-format check, the UTF-8/ASCII literal checks, the
file-size guard, the function-size guard, the header-comment-placement guard,
the comment-provenance guard, the docs integrity guard, every scripts/tests/*.test.sh, then a full
Release build of every CMake target CI builds (Core, AppUI, AgentSynth,
AgentSynthPlugin, Tests) with
ENABLE_TESTS=ON, followed by the full test suite. See the header comment in
this file, and docs/development/local-ci.md, for the
full mapping to what CI actually runs.

  --fast        Check only what the change reaches, against origin/main:
                formatting of changed files, the Tests target, and the test
                suites that include a changed file. CI runs the rest.
  --open        After every check passes, `open` the built app bundle
                (macOS only). Default off, since this also runs headless
                in the pre-push hook.
  --skip-tests  Skip the Tests suite (step 10), often the slowest step.
                The suite runs as shards limited by test slots shared by every
                worktree of this repository (see scripts/lib/test-shards.sh).
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
        --fast)
            FAST=true
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

CORES="$( (command -v nproc >/dev/null 2>&1 && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
RUN_START="$(date +%s)"
STEP_NAME=""
STEP_START=0
STEP_TIMES=""

end_step() {
    if [ -n "$STEP_NAME" ]; then
        STEP_TIMES="$STEP_TIMES$(printf '%6s s  %s' "$(($(date +%s) - STEP_START))" "$STEP_NAME")"$'\n'
    fi
    STEP_NAME=""
}

step() {
    end_step
    STEP_NAME="$1"
    STEP_START="$(date +%s)"
    printf '\n=== %s ===\n' "$1"
}

# content_key <git pathspec>... -- a hash of what those paths hold now, committed or not.
content_key() {
    { git ls-files -s -- "$@"; git diff HEAD -- "$@"; } | git hash-object --stdin
}

# Stamps remember "this exact content passed on this machine", across worktrees and gates.
STAMP_DIR="$(git rev-parse --git-common-dir 2>/dev/null || echo .git)"
case "$STAMP_DIR" in /*) : ;; *) STAMP_DIR="$REPO_ROOT/$STAMP_DIR" ;; esac
STAMP_DIR="$STAMP_DIR/agentsynth-gate-stamps"
mkdir -p "$STAMP_DIR"
find "$STAMP_DIR" -type f -mtime +14 -delete 2>/dev/null || true

# run_parallel <max> <label::command>... -- runs them together; a failure prints its log and fails the gate.
run_parallel() {
    local max="$1" dir item i=0 rc=0 running=0
    shift
    dir="$(mktemp -d)"
    local pids=() labels=()
    for item in "$@"; do
        bash -c "${item#*::}" >"$dir/$i.log" 2>&1 &
        pids+=("$!")
        labels+=("${item%%::*}")
        i=$((i + 1))
        running=$((running + 1))
        if [ "$running" -ge "$max" ]; then
            # bash 3.2 has no `wait -n`; the oldest of a batch is a fine throttle for a handful of checks.
            wait "${pids[$((i - running))]}" || rc=1
            [ "$rc" -ne 0 ] && { echo "--- ${labels[$((i - running))]} FAILED ---"; cat "$dir/$((i - running)).log"; }
            running=$((running - 1))
        fi
    done
    while [ "$running" -gt 0 ]; do
        local at=$((i - running)) one=0
        wait "${pids[$at]}" || one=1
        if [ "$one" -ne 0 ]; then
            rc=1
            echo "--- ${labels[$at]} FAILED ---"
            cat "$dir/$at.log"
        fi
        running=$((running - 1))
    done
    rm -rf "$dir"
    return "$rc"
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

if [ "$FAST" = true ]; then
    CHANGE_BASE="$(git merge-base origin/main HEAD 2>/dev/null || echo HEAD)"
    { git diff --name-only "$CHANGE_BASE" -- Source Tests Tools; git ls-files -o --exclude-standard -- Source Tests Tools; } |
        grep -E '\.(h|cpp)$' >"$REPO_ROOT/$BUILD_DIR.format-list" || true
else
    find Source Tests Tools -name "*.h" -o -name "*.cpp" >"$REPO_ROOT/$BUILD_DIR.format-list"
fi
format_ok=true
# A deleted file in the fast list is skipped by the `test -f` filter.
while IFS= read -r f; do [ -f "$f" ] && printf '%s\n' "$f"; done <"$REPO_ROOT/$BUILD_DIR.format-list" |
    xargs -n 64 -P "$CORES" clang-format --dry-run --Werror || format_ok=false
rm -f "$REPO_ROOT/$BUILD_DIR.format-list"
if [ "$format_ok" = false ]; then
    fail "clang-format violations found (see above). Fix: find Source Tests Tools -name '*.h' -o -name '*.cpp' | xargs clang-format -i"
fi
echo "ci-local: formatting OK."

# --- 2-6. Guards against the real tree, together: they only read the tree -----------------------
step "Guards: UTF-8 literals, file sizes, cache budget, function sizes, header comments, comment provenance, fixed waits, docs"
run_parallel "$CORES" \
    "utf8-literal-check::bash scripts/utf8-literal-check.sh" \
    "check-file-sizes::bash scripts/check-file-sizes.sh" \
    "ci-cache-budget::bash scripts/ci-cache-budget.sh" \
    "check-function-sizes::bash scripts/check-function-sizes.sh" \
    "check-header-comments::bash scripts/check-header-comments.sh" \
    "check-comment-provenance::bash scripts/check-comment-provenance.sh" \
    "check-fixed-waits::bash scripts/check-fixed-waits.sh" \
    "check-docs::bash scripts/check-docs.sh" || fail "a guard failed (see above)."

# --- 7. Every scripts/tests/*.test.sh ----------------------------------------------------------
step "scripts/tests/*.test.sh"
shopt -s nullglob
test_scripts=(scripts/tests/*.test.sh)
shopt -u nullglob
if [ "${#test_scripts[@]}" -eq 0 ]; then
    fail "no scripts/tests/*.test.sh found -- expected at least ci-cache-check.test.sh etc."
fi
# These scan the real tree, so they answer a new question on every run. The rest test scripts against
# fixtures, so the same scripts give the same answer: they are skipped when these paths are unchanged
# since they last passed on this machine. CI's Lint job always runs them all.
tree_scanning="check-nonascii-literals check-ui-caps no-pipe-into-grep-q"
scripts_key="$(content_key scripts .github/workflows .clang-format .clang-format-version cmake/ApplyJucePatch.cmake cmake/patches Tests/CMakeLists.txt)"
scripts_stamp="$STAMP_DIR/script-tests-$scripts_key"
to_run=()
for t in "${test_scripts[@]}"; do
    name="$(basename "$t" .test.sh)"
    case " $tree_scanning " in
        *" $name "*) to_run+=("$name::bash $t") ;;
        *) [ -f "$scripts_stamp" ] || to_run+=("$name::bash $t") ;;
    esac
done
[ -f "$scripts_stamp" ] && echo "ci-local: scripts unchanged since they last passed; running only the tests that scan the real tree."
run_parallel "$CORES" "${to_run[@]}" || fail "a script test failed (see above)."
touch "$scripts_stamp"

# --- 8. Configure + build every target the CI build-and-test jobs build -----------------------
step "Configure ($BUILD_DIR)"

# ENABLE_AI_HARNESS is ON here even though the macOS/Windows CI jobs no longer set it (only the
# Linux job does, for compile coverage). This is the pre-push gate, so catching a harness that
# stopped compiling BEFORE the push is worth the extra targets; ccache makes repeat runs cheap.
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

# A plain build re-runs CMake by itself when a CMake input changed, so an explicit configure only
# matters the first time and when the flags here change.
cmake_key="$(content_key CMakeLists.txt cmake Tests/CMakeLists.txt 'Tests/*.cmake' scripts/ci-local.sh)"
if [ "$FIRST_CONFIGURE" = true ] || [ ! -f "$BUILD_DIR/.gate-configured-$cmake_key" ]; then
    rm -f "$BUILD_DIR"/.gate-configured-*
    cmake -S . "${cmake_args[@]}"
    touch "$BUILD_DIR/.gate-configured-$cmake_key"
else
    echo "ci-local: CMake inputs unchanged, build re-runs CMake itself if needed."
fi

# --- 8b. Configure-only with CI's own flags (FRO1300) -------------------------------------------
# The build above configures the way a developer does, so a problem that only CI's flags hit (the
# explicit compiler launchers JUCE 9's juceaide bootstrap choked on, AGENTSYNTH_PCH=ON) showed
# up in CI as three red build jobs while this script was green. This repeats ci.yml's macOS/Windows
# "Configure CMake" step into a scratch dir and builds nothing. It reuses the sources the build
# above already fetched, so it costs about 25 s warm.
ci_flags_stamp="$STAMP_DIR/ci-flags-$cmake_key"
if [ "$FAST" = true ]; then
    step "Configure with CI's flags (skipped: --fast)"
elif [ -f "$ci_flags_stamp" ]; then
    step "Configure with CI's flags (unchanged since it last passed)"
else
    step "Configure with CI's flags ($BUILD_DIR-ci-flags, configure only)"
    ci_flags_dir="$BUILD_DIR-ci-flags"
    ci_flags_args=(-B "$ci_flags_dir" -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DAGENTSYNTH_PCH=ON)
    if command -v ninja >/dev/null 2>&1; then
        ci_flags_args+=(-G Ninja)
    fi
    if command -v ccache >/dev/null 2>&1; then
        ci_flags_args+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
        if [ "$(uname)" = "Darwin" ]; then
            ci_flags_args+=(-DCMAKE_OBJC_COMPILER_LAUNCHER=ccache -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache)
        fi
    fi
    for dep_src in "$BUILD_DIR"/_deps/*-src; do
        [ -d "$dep_src" ] || continue
        dep="$(basename "$dep_src" -src | tr '[:lower:]' '[:upper:]')"
        ci_flags_args+=("-DFETCHCONTENT_SOURCE_DIR_$dep=$REPO_ROOT/$dep_src")
    done
    # Pch.cmake refuses a precompiled header next to a ccache base_dir, which CI never sets but a
    # developer's ~/.ccache may; clear it for this one configure only.
    CCACHE_BASEDIR="" cmake -S . "${ci_flags_args[@]}" || fail "configure with CI's flags failed (see above): CI's configure step would fail the same way."
    touch "$ci_flags_stamp"
fi

step "Build (all CMake targets: Core, AppUI, AgentSynth, AgentSynthPlugin, Tests)"
# Half the cores, with no load limit. Measured against 60 uncached compiles on a busy 16-core machine:
# twice as fast as a quarter of the cores for one gate and no slower for six, where every core made
# six gates 2-3x slower, and a load limit (-l) starved a gate whenever other sessions held the load
# above it. CI_LOCAL_JOBS overrides.
JOBS=$((CORES / 2))
[ "$JOBS" -lt 2 ] && JOBS=2
case "${CI_LOCAL_JOBS:-}" in
    ''|*[!0-9]*|0) ;;
    *) JOBS="$CI_LOCAL_JOBS" ;;
esac
build_args=(--parallel "$JOBS")
[ "$FAST" = true ] && build_args+=(--target Tests)
cmake --build "$BUILD_DIR" "${build_args[@]}"

# --- 9. Dev-sign the app bundle (macOS only, before tests so a signing failure surfaces early) --
step "Dev-sign app bundle (stable TCC identity)"
# `|| true`: under `set -o pipefail`, `head -n 1` closing the pipe after its first line can make
# `find` see SIGPIPE and exit non-zero, which -- since this is a plain assignment, not `local` --
# would trip `set -e` and abort the whole script here instead of just leaving APP_PATH empty.
APP_PATH="$( (find "$BUILD_DIR" -name "Agent Synth.app" -type d 2>/dev/null || true) | head -n 1)"
if [ "$FAST" = true ]; then
    echo "ci-local: --fast builds no app bundle, skipping dev-sign."
elif [ "$(uname)" = "Darwin" ]; then
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
    # shellcheck source=scripts/lib/test-shards.sh
    source "$REPO_ROOT/scripts/lib/test-shards.sh"
    # --gtest_brief=1 prints only failures and the summary. The suite keeps its windows invisible and unfocused
    # (Tests/TestMain.cpp); AGENTSYNTH_TESTS_VISIBLE=1 in the environment shows them.
    test_args=(--gtest_brief=1)
    run_suite=true
    if [ "$FAST" = true ]; then
        filter="$(python3 scripts/lib/changed-tests.py --build "$BUILD_DIR" --base "$CHANGE_BASE")"
        case "$filter" in
            ALL) echo "ci-local: --fast: the change reaches too much, running the full suite." ;;
            NONE)
                echo "ci-local: --fast: the change reaches no tests."
                run_suite=false
                ;;
            *)
                echo "ci-local: --fast: running $(tr ':' '\n' <<<"$filter" | wc -l | tr -d ' ') suites; CI runs the full suite."
                test_args+=("--gtest_filter=$filter")
                ;;
        esac
    fi
    if [ "$run_suite" = true ]; then
        test_shards_run "$REPO_ROOT" "$REPO_ROOT/$BUILD_DIR/test-shards" "$REPO_ROOT/$TESTS_BIN" "${test_args[@]}" ||
            fail "test suite failed (see above)."
    fi
fi

# --- Done: point at a build the user can actually launch ---------------------------------------
end_step
printf '\n=== Time per step ===\n%s%6s s  total\n' "$STEP_TIMES" "$(($(date +%s) - RUN_START))"
[ "$FAST" = true ] && echo "ci-local: --fast passed; CI still runs every test and target."
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
elif [ "$FAST" = false ]; then
    echo "ci-local: WARNING could not find 'Agent Synth.app' under $BUILD_DIR -- the AgentSynth target may not have built an app bundle on this platform."
fi

echo
echo "ci-local: all checks passed."
