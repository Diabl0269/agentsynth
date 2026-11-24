#!/usr/bin/env bash
# Sourced helper: make the pinned clang-format (.clang-format-version) the one on PATH.
#
# The pinned build is a pip user install (~/Library/Python/<ver>/bin/clang-format on macOS), and agent
# shells often do not have that directory on PATH, so `command -v clang-format` fails or finds an
# older copy. ensure_pinned_clang_format prepends the first user bin directory whose clang-format is
# the pinned version, only when the one on PATH is missing or is a different version. It never
# fails: with nothing found, PATH is untouched and the caller's own install hint runs.
#
# Usage: source scripts/lib/find-clang-format.sh; ensure_pinned_clang_format "$(cat .clang-format-version)"

# _cf_version <binary> -- prints its X.Y.Z, or nothing.
_cf_version() {
    "$1" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true
}

ensure_pinned_clang_format() {
    local pinned="${1:-}" onpath have dir user_base candidates=()
    [ -n "$pinned" ] || return 0
    onpath="$(command -v clang-format 2>/dev/null || true)"
    if [ -n "$onpath" ] && [ "$(_cf_version "$onpath")" = "$pinned" ]; then
        return 0
    fi
    user_base="$(python3 -m site --user-base 2>/dev/null || true)"
    for dir in "$HOME"/Library/Python/*/bin ${user_base:+"$user_base/bin"} "$HOME/.local/bin"; do
        [ -x "$dir/clang-format" ] || continue
        candidates+=("$dir")
    done
    for dir in ${candidates[@]+"${candidates[@]}"}; do
        have="$(_cf_version "$dir/clang-format")"
        if [ "$have" = "$pinned" ]; then
            PATH="$dir:$PATH"
            export PATH
            return 0
        fi
    done
    return 0
}
