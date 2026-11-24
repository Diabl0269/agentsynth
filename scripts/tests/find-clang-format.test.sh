#!/usr/bin/env bash
#
# Unit tests for scripts/lib/find-clang-format.sh: the pinned clang-format is found in a pip user
# install even when that directory is not on PATH, a wrong version is skipped, and nothing found
# leaves PATH alone. A fake HOME holds stub clang-format binaries that print a version.
#
# Usage: bash scripts/tests/find-clang-format.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/find-clang-format.XXXXXX")"
trap 'rm -rf "${TMPROOT:?}"' EXIT

pass=0
fail=0
record() {
    if [ "$1" -eq 0 ]; then
        echo "PASS: $2"
        pass=$((pass + 1))
    else
        echo "FAIL: $2"
        fail=$((fail + 1))
    fi
}

# stub <dir> <version> -- a clang-format that prints "clang-format version <version>".
stub() {
    mkdir -p "$1"
    printf '#!/bin/sh\necho "clang-format version %s"\n' "$2" >"$1/clang-format"
    chmod +x "$1/clang-format"
}

# found <home> <path> -- sources the helper in a clean shell and prints the clang-format version it then finds.
found() {
    env -i HOME="$1" PATH="$2" bash -c '
        source "$1/scripts/lib/find-clang-format.sh"
        ensure_pinned_clang_format 22.1.2
        command -v clang-format >/dev/null || { echo none; exit 0; }
        clang-format --version | grep -oE "[0-9]+\.[0-9]+\.[0-9]+"
    ' _ "$SCRIPT_DIR"
}

BASE_PATH="/usr/bin:/bin"

H1="$TMPROOT/h1"
stub "$H1/Library/Python/3.14/bin" 22.1.2
[ "$(found "$H1" "$BASE_PATH")" = "22.1.2" ]
record $? "none on PATH: the pinned one in ~/Library/Python/*/bin is used"

H2="$TMPROOT/h2"
stub "$H2/Library/Python/3.9/bin" 18.1.0
stub "$H2/Library/Python/3.14/bin" 22.1.2
[ "$(found "$H2" "$BASE_PATH")" = "22.1.2" ]
record $? "an older user install is skipped for the pinned one"

H3="$TMPROOT/h3"
stub "$H3/Library/Python/3.14/bin" 22.1.2
stub "$TMPROOT/old" 19.1.0
[ "$(found "$H3" "$TMPROOT/old:$BASE_PATH")" = "22.1.2" ]
record $? "a wrong version on PATH is replaced by the pinned user install"

H4="$TMPROOT/h4"
stub "$H4/Library/Python/3.14/bin" 22.1.2
stub "$TMPROOT/good" 22.1.2
out="$(env -i HOME="$H4" PATH="$TMPROOT/good:$BASE_PATH" bash -c 'source "$1/scripts/lib/find-clang-format.sh"; ensure_pinned_clang_format 22.1.2; echo "$PATH"' _ "$SCRIPT_DIR")"
[ "$out" = "$TMPROOT/good:$BASE_PATH" ]
record $? "the pinned version already on PATH leaves PATH untouched"

H5="$TMPROOT/h5"
mkdir -p "$H5"
[ "$(found "$H5" "$BASE_PATH")" = "none" ]
record $? "nothing found: clang-format stays missing (the caller's install hint runs)"

H6="$TMPROOT/h6"
stub "$H6/Library/Python/3.14/bin" 18.1.0
[ "$(found "$H6" "$BASE_PATH")" = "none" ]
record $? "only a wrong version installed: it is not put on PATH"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
