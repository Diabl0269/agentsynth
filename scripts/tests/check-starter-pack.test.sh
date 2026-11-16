#!/usr/bin/env bash
#
# Unit tests for scripts/check-starter-pack.sh: a complete copy passes; a missing pack, a missing file, a changed file
# and an over-budget pack each fail. No compiler, no network.
#
# Usage: bash scripts/tests/check-starter-pack.test.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$SCRIPT_DIR/scripts/check-starter-pack.sh"
TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/starter-pack-test.XXXXXX")"
trap 'rm -rf "$TMPROOT"' EXIT
export STARTER_PACK_SRC="$TMPROOT/src"

pass=0
fail=0
expect() { # expect <wanted exit> <name> -- runs the check on $APP
    local want="$1" name="$2" got
    bash "$CHECK" "$APP" >/dev/null 2>&1
    got=$?
    if [ "$got" -eq "$want" ]; then pass=$((pass + 1)); echo "PASS: $name"; else fail=$((fail + 1)); echo "FAIL: $name (exit $got, wanted $want)"; fi
}

mkdir -p "$STARTER_PACK_SRC/kit/samples"
echo '{"id":"starter"}' >"$STARTER_PACK_SRC/pack.json"
echo '<region>' >"$STARTER_PACK_SRC/kit/kit.sfz"
head -c 1000 /dev/zero >"$STARTER_PACK_SRC/kit/samples/a.flac"

fresh_mac_app() {
    APP="$TMPROOT/Agent Synth.app"
    rm -rf "$APP"
    mkdir -p "$APP/Contents/Resources/SoundPacks"
    cp -R "$STARTER_PACK_SRC" "$APP/Contents/Resources/SoundPacks/starter"
    DEST="$APP/Contents/Resources/SoundPacks/starter"
}

fresh_mac_app;   expect 0 "a complete macOS bundle passes"
rm -rf "$DEST"; expect 1 "no starter folder in the bundle fails"
fresh_mac_app; rm "$DEST/kit/samples/a.flac"; expect 1 "a missing sample fails"
fresh_mac_app; echo x >>"$DEST/kit/kit.sfz"; expect 1 "a changed file fails"
fresh_mac_app; STARTER_PACK_BUDGET_BYTES=100 expect 1 "an over-budget pack fails"
APP="$TMPROOT/win"; mkdir -p "$APP/SoundPacks"; cp -R "$STARTER_PACK_SRC" "$APP/SoundPacks/starter"; expect 0 "a folder beside the executable passes"
if bash "$CHECK" >/dev/null 2>&1; then fail=$((fail + 1)); echo "FAIL: no argument"; else [ $? -eq 2 ] && { pass=$((pass + 1)); echo "PASS: no argument is a usage error"; } || { fail=$((fail + 1)); echo "FAIL: no argument exit code"; }; fi

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
