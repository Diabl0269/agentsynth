#!/usr/bin/env bash
#
# run-latest.sh -- build the app from this checkout and start the fresh build, never an old window.
#
# Replaces the `agr` alias (build, then `open "<app>"`). `open` on a bundle that is already running only brings the
# old process forward, so a new feature never showed (FRO2362; kind stale-tool-binary). This script:
#   1. builds the AgentSynth target in <build dir> (default: build),
#   2. refuses a bundle whose BuildCommit is not this checkout's HEAD (warns when the tree has uncommitted changes),
#   3. asks a running Agent Synth to quit (the app's own save prompt; nothing is ever killed) and waits for it,
#   4. starts the new build with `open -n`,
#   5. prints the commit and build time it started.
#
# Usage: bash scripts/run-latest.sh [build-dir]
# Env (for tests and unusual setups): RUN_LATEST_BUILD_CMD (replaces the cmake build), RUN_LATEST_APP (bundle path),
#   RUN_LATEST_QUIT_WAIT (seconds to wait for the old app, default 30),
#   RUN_LATEST_ALLOW_STALE=1 (launch even when BuildCommit is not HEAD).
# The one line for ~/.zshrc:  alias agr='bash <path to your checkout>/scripts/run-latest.sh'

set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-build}"
case "$build_dir" in /*) ;; *) build_dir="$repo/$build_dir" ;; esac
app="${RUN_LATEST_APP:-$build_dir/AgentSynth_artefacts/Release/Agent Synth.app}"
plist="$app/Contents/Info.plist"

echo "run-latest: checkout $repo"
if [ -n "${RUN_LATEST_BUILD_CMD:-}" ]; then
    bash -c "$RUN_LATEST_BUILD_CMD" || { echo "run-latest: build failed" >&2; exit 1; }
else
    cmake --build "$build_dir" --target AgentSynth || { echo "run-latest: build failed" >&2; exit 1; }
fi
[ -f "$plist" ] || { echo "run-latest: no $plist after the build" >&2; exit 2; }

read_commit() {
    if [ -x /usr/libexec/PlistBuddy ]; then
        /usr/libexec/PlistBuddy -c "Print :BuildCommit" "$plist" 2>/dev/null
    else
        awk '/<key>BuildCommit<\/key>/{f=1;next} f{gsub(/.*<string>|<\/string>.*/,"");print;exit}' "$plist"
    fi
}
built="$(read_commit)"
head_commit="$(git -C "$repo" rev-parse HEAD 2>/dev/null || echo unknown)"
built_hash="${built%-dirty}"
if [ -z "$built" ] || [ "$built_hash" != "$head_commit" ]; then
    echo "run-latest: the bundle's BuildCommit (${built:-none}) is not HEAD ($head_commit): this is not the latest build." >&2
    if [ "${RUN_LATEST_ALLOW_STALE:-0}" != "1" ]; then
        echo "run-latest: refusing to start it. Rebuild, or set RUN_LATEST_ALLOW_STALE=1." >&2
        exit 3
    fi
fi
if ! git -C "$repo" diff --quiet HEAD 2>/dev/null; then
    echo "run-latest: warning: uncommitted changes in this checkout (the build is ${built_hash:0:9}-dirty)." >&2
fi

if pgrep -x "Agent Synth" >/dev/null 2>&1; then
    echo "run-latest: asking the running Agent Synth to quit (answer its save prompt if it shows one)"
    osascript -e 'tell application "Agent Synth" to quit' >/dev/null 2>&1 || true
    waited=0
    while pgrep -x "Agent Synth" >/dev/null 2>&1; do
        if [ "$waited" -ge "${RUN_LATEST_QUIT_WAIT:-30}" ]; then
            echo "run-latest: the old Agent Synth is still open; not starting a second one. Quit it and run again." >&2
            exit 4
        fi
        sleep 1
        waited=$((waited + 1))
    done
fi

open -n "$app" || { echo "run-latest: open failed" >&2; exit 5; }
echo "run-latest: started ${built:-unknown} (built $(date -r "$app/Contents/MacOS/Agent Synth" '+%Y-%m-%d %H:%M' 2>/dev/null || echo '?'))"
