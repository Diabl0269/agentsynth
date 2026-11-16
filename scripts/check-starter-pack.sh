#!/usr/bin/env bash
#
# check-starter-pack.sh -- a built app carries the starter sound pack (assets/sound-packs/starter), complete and within
# the size budget. CMakeLists.txt copies it in as a POST_BUILD step; this proves the copy happened and is whole, so a
# build that silently dropped it (a changed bundle layout, a missed target) cannot ship an empty Sampler list.
#
# Usage: bash scripts/check-starter-pack.sh <path to "Agent Synth.app", or the folder holding the Agent Synth executable>
# Environment: STARTER_PACK_SRC overrides the source folder (script tests); STARTER_PACK_BUDGET_BYTES (default 8388608).
# Exit: 0 ok, 1 the bundle is missing or differs from the source or is over budget, 2 bad usage.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${STARTER_PACK_SRC:-$ROOT/assets/sound-packs/starter}"
BUDGET="${STARTER_PACK_BUDGET_BYTES:-8388608}"

[ $# -eq 1 ] || { echo "usage: check-starter-pack.sh <app bundle or executable folder>" >&2; exit 2; }
APP="$1"
if [ -d "$APP/Contents/Resources" ]; then
    DEST="$APP/Contents/Resources/SoundPacks/starter"
else
    DEST="$APP/SoundPacks/starter"
fi

# A file the working tree has but git ignores passes here and fails CI, which clones without it.
if [ -z "${STARTER_PACK_SRC:-}" ] && git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    ignored=$(git -C "$ROOT" ls-files --others --ignored --exclude-standard -- assets/sound-packs/starter)
    if [ -n "$ignored" ]; then
        echo "check-starter-pack: FAIL gitignored files in the starter pack (they would not reach CI or a release build):"
        echo "$ignored" | head -10
        exit 1
    fi
fi

[ -f "$DEST/pack.json" ] || { echo "check-starter-pack: FAIL no pack.json at $DEST (the starter pack is not in the build)"; exit 1; }
if ! diff -rq "$SRC" "$DEST" >/dev/null 2>&1; then
    echo "check-starter-pack: FAIL $DEST differs from $SRC:"
    diff -rq "$SRC" "$DEST" | head -10
    exit 1
fi
bytes=$(find "$DEST" -type f -exec wc -c {} + | awk 'END { print $1 }')
if [ "$bytes" -gt "$BUDGET" ]; then
    echo "check-starter-pack: FAIL $DEST is $bytes bytes, over the $BUDGET budget"
    exit 1
fi
echo "check-starter-pack: ok ($DEST, $bytes bytes)"
