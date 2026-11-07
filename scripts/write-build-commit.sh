#!/usr/bin/env bash
#
# write-build-commit.sh -- record which source commit a built app came from.
#
# Writes the key BuildCommit into <app bundle>/Contents/Info.plist: the full `git rev-parse HEAD` of the source tree,
# with "-dirty" appended when tracked files differ from HEAD, or "unknown" outside git. It runs as a POST_BUILD step of
# the AgentSynth target (cmake/BuildCommit.cmake), before the dev-signing step, so the signature covers the edit.
#
# Usage: bash scripts/write-build-commit.sh <path-to-app-bundle> [source-dir]

set -uo pipefail

bundle="${1:?usage: write-build-commit.sh <app bundle> [source dir]}"
src="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
plist="$bundle/Contents/Info.plist"

commit="$(git -C "$src" rev-parse HEAD 2>/dev/null)" || commit=""
if [ -z "$commit" ]; then
    commit="unknown"
elif ! git -C "$src" diff --quiet HEAD 2>/dev/null; then
    commit="$commit-dirty"
fi

if [ ! -f "$plist" ]; then
    echo "write-build-commit: no $plist" >&2
    exit 1
fi
/usr/libexec/PlistBuddy -c "Delete :BuildCommit" "$plist" >/dev/null 2>&1 || true
/usr/libexec/PlistBuddy -c "Add :BuildCommit string $commit" "$plist"
