# scripts/lib/deps-reuse.sh — sourced by scripts/ci-local.sh, not executed directly.
#
# WHY THIS EXISTS: a freshly created git worktree's first `cmake` configure has nothing in its
# own build-ci-local/_deps -- FetchContent re-downloads JUCE (~600 MB), GoogleTest and Sparkle
# from scratch, originally observed taking 15-20 minutes before a single line of C++ compiles
# (network-dependent -- measured at 4m 12.7s on the machine this was fixed on, see
# docs/development/local-ci.md
# "Worktree dependency-source reuse"), even though the main checkout sitting right next to it
# (under agentsynth-suite/) already has those exact sources on disk. Passing
# -DFETCHCONTENT_SOURCE_DIR_<NAME>=<path> makes FetchContent use that path directly instead of
# downloading -- CMake still configures and verifies the sources, it just skips the fetch. Only
# worth doing on the FIRST configure: once build-ci-local/_deps exists in the worktree itself,
# later configures are already fast.
#
# SAFETY: only ever reuse when cmake/DependencyVersions.cmake is BYTE-IDENTICAL between the
# worktree and the main checkout. A dependency-pin bump made in the worktree (not yet merged to
# main) must not silently build against the main checkout's OLD sources -- that would pass
# locally and fail in CI, or worse, pass locally against the wrong dependency version. A mismatch
# (or missing main-checkout sources) is never an error: this is a speed optimization only, and
# the caller must configure exactly as it would have otherwise when this doesn't apply.
#
# Usage (from scripts/ci-local.sh, before the first `cmake -S .` call):
#   source scripts/lib/deps-reuse.sh
#   deps_reuse_compute "$worktree_root" "$main_checkout_root" "$build_dir_name"
#   # "${DEPS_REUSE_ARGS[@]}"  -- extra cmake args to append (empty array when not reusing)
#   # "$DEPS_REUSE_MESSAGE"    -- always set: one line, what was reused or why not
#
# main_checkout_root should be "" (or equal to worktree_root) when the caller is not running
# inside a git worktree at all -- deps_reuse_compute treats that the same as "nothing to reuse"
# rather than requiring the caller to special-case it. CI_LOCAL_NO_DEPS_REUSE=1 opts out
# unconditionally; deps_reuse_compute checks it itself so callers never duplicate that logic.

DEPS_REUSE_NAMES=(JUCE GOOGLETEST SPARKLE)

# deps_reuse_compute <worktree_root> <main_checkout_root> <build_dir_name>
#
# Sets DEPS_REUSE_ARGS (array) and DEPS_REUSE_MESSAGE (string) as plain globals -- bash 3.2
# (macOS's default /bin/bash) has no `declare -g`, and a function without `local` already writes
# its parent/global scope, so this needs nothing special to work there.
deps_reuse_compute() {
    local worktree_root="$1" main_root="$2" build_dir_name="$3"
    DEPS_REUSE_ARGS=()
    DEPS_REUSE_MESSAGE=""

    if [ -n "${CI_LOCAL_NO_DEPS_REUSE:-}" ]; then
        DEPS_REUSE_MESSAGE="ci-local: dependency-source reuse skipped (CI_LOCAL_NO_DEPS_REUSE is set)."
        return 0
    fi

    if [ -z "$main_root" ] || [ "$main_root" = "$worktree_root" ]; then
        DEPS_REUSE_MESSAGE="ci-local: dependency-source reuse skipped (not running inside a git worktree)."
        return 0
    fi

    local worktree_pins="$worktree_root/cmake/DependencyVersions.cmake"
    local main_pins="$main_root/cmake/DependencyVersions.cmake"
    if [ ! -f "$worktree_pins" ] || [ ! -f "$main_pins" ]; then
        DEPS_REUSE_MESSAGE="ci-local: dependency-source reuse skipped (cmake/DependencyVersions.cmake missing)."
        return 0
    fi

    if ! cmp -s "$worktree_pins" "$main_pins"; then
        DEPS_REUSE_MESSAGE="ci-local: dependency-source reuse skipped (cmake/DependencyVersions.cmake differs from the main checkout -- pins changed)."
        return 0
    fi

    local deps_dir="$main_root/$build_dir_name/_deps"
    local name lower
    local found=()
    for name in "${DEPS_REUSE_NAMES[@]}"; do
        lower="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"
        if [ -d "$deps_dir/${lower}-src" ]; then
            DEPS_REUSE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_${name}=$deps_dir/${lower}-src")
            found+=("$name")
        fi
    done

    if [ "${#found[@]}" -eq 0 ]; then
        DEPS_REUSE_MESSAGE="ci-local: dependency-source reuse skipped (no fetched sources found under $deps_dir)."
        return 0
    fi

    local joined
    joined="$(printf '%s, ' "${found[@]}")"
    joined="${joined%, }"
    DEPS_REUSE_MESSAGE="ci-local: reusing $joined source(s) from the main checkout ($deps_dir) -- pins match."
}
