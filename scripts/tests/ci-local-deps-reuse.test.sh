#!/usr/bin/env bash
#
# Unit tests for scripts/lib/deps-reuse.sh -- the worktree dependency-source reuse helper
# scripts/ci-local.sh sources on its first configure (see that script's "Worktree dependency-
# source reuse" step). Exercises deps_reuse_compute directly against fixture directory trees, so
# the reuse/no-reuse decision is tested without invoking cmake, a compiler, or a real git
# worktree. Runs in the Lint job -- no compiler, no network, ~1s.
#
# Usage: bash scripts/tests/ci-local-deps-reuse.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LIB="$SCRIPT_DIR/scripts/lib/deps-reuse.sh"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# shellcheck source=scripts/lib/deps-reuse.sh
source "$LIB"

pass=0
fail=0

# make_pins <root> <content> -- writes cmake/DependencyVersions.cmake under <root>.
make_pins() {
    local root="$1" content="$2"
    mkdir -p "$root/cmake"
    printf '%s\n' "$content" >"$root/cmake/DependencyVersions.cmake"
}

# make_dep_src <main_root> <build_dir> <lower_name> -- fakes an already-fetched dependency source.
make_dep_src() {
    local main_root="$1" build_dir="$2" lower="$3"
    mkdir -p "$main_root/$build_dir/_deps/${lower}-src"
    : >"$main_root/$build_dir/_deps/${lower}-src/marker"
}

reset() {
    rm -rf "$WORK/worktree" "$WORK/main"
    mkdir -p "$WORK/worktree" "$WORK/main"
    unset CI_LOCAL_NO_DEPS_REUSE
}

# assert_args <desc> [expected cmake arg]...  -- compares DEPS_REUSE_ARGS set by the last call.
assert_args() {
    local desc="$1"
    shift
    local expected=("$@")
    if [ "${#DEPS_REUSE_ARGS[@]}" -eq "${#expected[@]}" ]; then
        local i=0 ok=1
        while [ "$i" -lt "${#expected[@]}" ]; do
            [ "${DEPS_REUSE_ARGS[$i]}" = "${expected[$i]}" ] || ok=0
            i=$((i + 1))
        done
        if [ "$ok" -eq 1 ]; then
            echo "PASS: $desc"
            pass=$((pass + 1))
            return
        fi
    fi
    echo "FAIL: $desc"
    echo "  expected: ${expected[*]:-<none>}"
    echo "  actual:   ${DEPS_REUSE_ARGS[*]:-<none>}"
    fail=$((fail + 1))
}

assert_message() {
    local desc="$1" expect_grep="$2"
    if printf '%s' "$DEPS_REUSE_MESSAGE" | grep -qF -- "$expect_grep"; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc"
        echo "  expected message to contain: $expect_grep"
        echo "  actual message: $DEPS_REUSE_MESSAGE"
        fail=$((fail + 1))
    fi
}

# --- reuse when pins match, all three sources already fetched in the main checkout -----------
reset
make_pins "$WORK/worktree" "set(JUCE_TAG 1.2.3)"
make_pins "$WORK/main" "set(JUCE_TAG 1.2.3)"
make_dep_src "$WORK/main" "build-ci-local" "juce"
make_dep_src "$WORK/main" "build-ci-local" "googletest"
make_dep_src "$WORK/main" "build-ci-local" "sparkle"
deps_reuse_compute "$WORK/worktree" "$WORK/main" "build-ci-local"
assert_args "reuse: all three sources, pins match" \
    "-DFETCHCONTENT_SOURCE_DIR_JUCE=$WORK/main/build-ci-local/_deps/juce-src" \
    "-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$WORK/main/build-ci-local/_deps/googletest-src" \
    "-DFETCHCONTENT_SOURCE_DIR_SPARKLE=$WORK/main/build-ci-local/_deps/sparkle-src"
assert_message "reuse: message names all three sources" "JUCE, GOOGLETEST, SPARKLE"

# --- partial reuse: only some sources have been fetched in the main checkout ------------------
reset
make_pins "$WORK/worktree" "set(JUCE_TAG 1.2.3)"
make_pins "$WORK/main" "set(JUCE_TAG 1.2.3)"
make_dep_src "$WORK/main" "build-ci-local" "juce"
deps_reuse_compute "$WORK/worktree" "$WORK/main" "build-ci-local"
assert_args "partial reuse: only JUCE has been fetched in the main checkout" \
    "-DFETCHCONTENT_SOURCE_DIR_JUCE=$WORK/main/build-ci-local/_deps/juce-src"

# --- no reuse when the dependency pins differ (a pin bump must not reuse the old sources) -----
reset
make_pins "$WORK/worktree" "set(JUCE_TAG 1.2.3)"
make_pins "$WORK/main" "set(JUCE_TAG 9.9.9)"
make_dep_src "$WORK/main" "build-ci-local" "juce"
deps_reuse_compute "$WORK/worktree" "$WORK/main" "build-ci-local"
assert_args "no reuse: pins differ between worktree and main checkout"
assert_message "no reuse: pins differ, message explains why" "DependencyVersions.cmake differs"

# --- no reuse outside a worktree (main root equals the worktree root) -------------------------
reset
make_pins "$WORK/worktree" "set(JUCE_TAG 1.2.3)"
make_dep_src "$WORK/worktree" "build-ci-local" "juce"
deps_reuse_compute "$WORK/worktree" "$WORK/worktree" "build-ci-local"
assert_args "no reuse: not a worktree (main root equals worktree root)"
assert_message "no reuse: not a worktree, message explains why" "not running inside a git worktree"

# --- no reuse when the caller has no main root at all (e.g. git-common-dir resolution failed) --
reset
make_pins "$WORK/worktree" "set(JUCE_TAG 1.2.3)"
deps_reuse_compute "$WORK/worktree" "" "build-ci-local"
assert_args "no reuse: empty main root"
assert_message "no reuse: empty main root, message explains why" "not running inside a git worktree"

# --- opt-out env wins even when pins match and sources are fetched ----------------------------
reset
make_pins "$WORK/worktree" "set(JUCE_TAG 1.2.3)"
make_pins "$WORK/main" "set(JUCE_TAG 1.2.3)"
make_dep_src "$WORK/main" "build-ci-local" "juce"
CI_LOCAL_NO_DEPS_REUSE=1 deps_reuse_compute "$WORK/worktree" "$WORK/main" "build-ci-local"
assert_args "opt-out: CI_LOCAL_NO_DEPS_REUSE set, no reuse despite matching pins"
assert_message "opt-out: message names the env var" "CI_LOCAL_NO_DEPS_REUSE"

# --- no reuse when cmake/DependencyVersions.cmake is missing on either side --------------------
reset
make_pins "$WORK/main" "set(JUCE_TAG 1.2.3)"
make_dep_src "$WORK/main" "build-ci-local" "juce"
deps_reuse_compute "$WORK/worktree" "$WORK/main" "build-ci-local"
assert_args "no reuse: worktree has no cmake/DependencyVersions.cmake"
assert_message "no reuse: missing pins file, message explains why" "DependencyVersions.cmake missing"

# --- no reuse when the main checkout has never fetched anything --------------------------------
reset
make_pins "$WORK/worktree" "set(JUCE_TAG 1.2.3)"
make_pins "$WORK/main" "set(JUCE_TAG 1.2.3)"
deps_reuse_compute "$WORK/worktree" "$WORK/main" "build-ci-local"
assert_args "no reuse: main checkout has no fetched _deps sources"
assert_message "no reuse: nothing fetched yet, message explains why" "no fetched sources found"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
