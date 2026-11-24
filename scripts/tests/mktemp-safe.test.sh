#!/usr/bin/env bash
# Guard: (1) no script calls a bare `mktemp` -- in a sandbox TMPDIR is the only writable place, and bare
# mktemp on macOS ignores it when it has no template; write mktemp "${TMPDIR:-/tmp}/<name>.XXXXXX" (or
# mktemp -d ...). (2) every guard that walks a file list fails, with a clear message, when it scanned
# nothing, instead of passing vacuously. Run against an empty git repo holding a copy of scripts/.
#
# Not covered by (2), because they do not walk a file list: check-prompt-pins.sh (reads one header),
# check-starter-pack.sh (takes a built bundle), check-unity-clashes.sh (reads cmake/Pch.cmake, which
# the empty fixture lacks, so it already fails for another reason).
set -uo pipefail
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
pass=0
fail=0

# --- (1) bare mktemp ----------------------------------------------------------------------------
# A call is safe when it passes a path template ("${TMPDIR:-/tmp}/x.XXXXXX", "$TMP/x.XXXXXX"). Flag a
# mktemp (optionally with -d) that is followed by nothing but the end of the command.
hits="$(grep -rnE '(^|[^[:alnum:]_.-])mktemp([[:space:]]+-d)?[[:space:]]*($|[)`;|&])' scripts .githooks 2>/dev/null |
    grep -v 'mktemp-safe.test.sh' | grep -vE '^[^:]+:[0-9]+:[[:space:]]*#' || true)"
if [ -n "$hits" ]; then
    echo "FAIL: bare mktemp (use mktemp \"\${TMPDIR:-/tmp}/<name>.XXXXXX\"):"
    echo "$hits"
    fail=$((fail + 1))
else
    echo "PASS: no bare mktemp under scripts/ or .githooks/"
    pass=$((pass + 1))
fi

# --- (2) zero scanned files ---------------------------------------------------------------------
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mktemp-safe.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
git -C "$WORK" init -q
mkdir -p "$WORK/Source"
cp -R scripts "$WORK/scripts"

for name in check-comment-provenance check-docs check-file-sizes check-fixed-waits check-function-sizes \
    check-header-comments check-header-reach check-ui-caps; do
    args=()
    [ "$name" = check-ui-caps ] && args=("$WORK/Source")
    out="$(cd "$WORK" && bash "scripts/$name.sh" ${args[@]+"${args[@]}"} 2>&1)"
    rc=$?
    if [ "$rc" -ne 0 ] && grep -qE "$name: 0 .*scanned" <<<"$out"; then
        echo "PASS: $name fails on a tree with nothing to scan"
        pass=$((pass + 1))
    else
        echo "FAIL: $name did not fail with a '0 ... scanned' message on an empty tree (exit $rc):"
        echo "$out" | tail -4
        fail=$((fail + 1))
    fi
done

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
