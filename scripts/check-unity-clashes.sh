#!/usr/bin/env bash
#
# check-unity-clashes.sh -- catches on any machine what only the Windows unity build catches: two
# sources of one unity group that declare the same name at anonymous-namespace scope (a constexpr or
# const variable, a function, a struct/class/enum or its enumerators, a using-alias). Merged into one
# translation unit those two declarations redefine each other. A clash fails MSVC only because macOS
# and Linux compile each file alone.
#
# The groups are the ones cmake/Pch.cmake (synth_enable_unity) builds: per target (Core, AppUI,
# Tests), a source's folder plus md5 of its path, with AGENTSYNTH_UNITY_GROUP_SIZE sources per group.
# The source lists are read from the CMake files, only those added before the target's
# synth_enable_unity() call, and files in SYNTH_UNITY_SKIP (cmake/UnityExclusions.cmake) are left out.
# Two identical using-aliases are legal C++ and are not reported. Function overloads differ by their
# parameter types. It is a text scan, not a C++ parser (scripts/lib/unity-clashes.py).
#
# BASELINE. scripts/unity-clashes-baseline.txt lists clashes that exist today, one per line:
#     <name> | <fileA> | <fileB> | <reason>
# STRICT ratchet: a clash without an entry fails, an entry that no longer clashes is stale and fails
# (--update drops it), and --update never adds one. Fix a new clash by renaming the name in one file
# (or adding the file to SYNTH_UNITY_SKIP, which also stops its group merging it).
#
# Usage:
#   bash scripts/check-unity-clashes.sh             # check the tree against the baseline
#   bash scripts/check-unity-clashes.sh --update    # drop stale baseline entries (never adds)
#   bash scripts/check-unity-clashes.sh --list      # every clash, ignoring the baseline
#   bash scripts/check-unity-clashes.sh --root <dir>
#
# Environment: UNITY_CLASHES_BASELINE  baseline path (default <root>/scripts/unity-clashes-baseline.txt)
# Exit status (check mode): 0 no violation, 1 a new clash or a stale entry.

set -uo pipefail
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

MODE="check"
ROOT_OVERRIDE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --update) MODE="update"; shift ;;
        --list) MODE="list"; shift ;;
        --root)
            shift
            [ $# -gt 0 ] || { echo "check-unity-clashes: --root requires an argument" >&2; exit 1; }
            ROOT_OVERRIDE="$1"; shift ;;
        -h | --help) sed -n '2,/^set -uo/p' "$0" | grep '^#' | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "check-unity-clashes: unknown argument '$1' (see --help)" >&2; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "$ROOT_OVERRIDE" ]; then
    ROOT="$(cd "$ROOT_OVERRIDE" && pwd)"
elif ! ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel)"; then
    echo "::error::check-unity-clashes: 'git rev-parse --show-toplevel' failed -- pass --root" >&2
    exit 1
fi
BASELINE_FILE="${UNITY_CLASHES_BASELINE:-$ROOT/scripts/unity-clashes-baseline.txt}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if ! python3 -B -I "$SCRIPT_DIR/lib/unity-clashes.py" "$ROOT" >"$work/found.tsv"; then
    echo "::error::check-unity-clashes: the scan failed (python3 missing?)" >&2
    exit 1
fi
# found: name <TAB> fileA <TAB> fileB <TAB> group  ->  "name | fileA | fileB"
awk -F'\t' '{ print $1 " | " $2 " | " $3 }' "$work/found.tsv" | sort >"$work/found.txt"

if [ "$MODE" = "list" ]; then
    awk -F'\t' '{ print $1 "  " $2 "  " $3 "  (" $4 ")" }' "$work/found.tsv"
    exit 0
fi

# baseline keys (the first three fields), reasons kept for --update.
if [ -f "$BASELINE_FILE" ]; then
    grep -v '^#' "$BASELINE_FILE" | grep -v '^[[:space:]]*$' >"$work/base.full" || true
else
    : >"$work/base.full"
fi
awk -F' [|] ' '{ print $1 " | " $2 " | " $3 }' "$work/base.full" | sort >"$work/base.txt"

comm -23 "$work/found.txt" "$work/base.txt" >"$work/new.txt"
comm -13 "$work/found.txt" "$work/base.txt" >"$work/stale.txt"

if [ "$MODE" = "update" ]; then
    if [ -s "$work/stale.txt" ]; then
        awk -F' [|] ' 'NR==FNR { stale[$0] = 1; next } { k = $1 " | " $2 " | " $3; if (!(k in stale)) print }' \
            "$work/stale.txt" "$work/base.full" >"$work/kept.txt"
        { grep '^#' "$BASELINE_FILE"; cat "$work/kept.txt"; } >"$work/new-baseline.txt"
        cp "$work/new-baseline.txt" "$BASELINE_FILE"
    fi
    echo "check-unity-clashes: dropped $(wc -l <"$work/stale.txt" | tr -d ' ') stale entr(ies); $(wc -l <"$work/new.txt" | tr -d ' ') new clash(es) not added"
    [ -s "$work/new.txt" ] && exit 1
    exit 0
fi

status=0
while IFS= read -r line; do
    [ -n "$line" ] || continue
    name="${line%% | *}"
    rest="${line#* | }"
    echo "::error::unity clash: '$name' is declared in an anonymous namespace in both ${rest%% | *} and ${rest##* | } (one Windows unity group) -- rename it in one file, or move it into a named namespace"
    status=1
done <"$work/new.txt"
while IFS= read -r line; do
    [ -n "$line" ] || continue
    echo "::error::stale unity-clash baseline entry (no longer a clash): $line -- run bash scripts/check-unity-clashes.sh --update"
    status=1
done <"$work/stale.txt"

echo "check-unity-clashes: $(wc -l <"$work/found.txt" | tr -d ' ') clash(es) found, $(grep -c . "$work/base.txt") baselined"
exit "$status"
