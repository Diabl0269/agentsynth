#!/usr/bin/env bash
#
# Unit tests for scripts/run-latest.sh (FRO2362, kind stale-tool-binary): the launcher builds, refuses a bundle
# whose BuildCommit is not HEAD, quits a running app first, and starts the new one with `open -n`.
# Fake cmake/open/pgrep/osascript shims on PATH; a throwaway git repo. No compiler, ~1s.
#
# Usage: bash scripts/tests/run-latest.test.sh

set -uo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/run-latest.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
pass=0
fail=0
ok() { pass=$((pass + 1)); echo "PASS: $1"; }
bad() { fail=$((fail + 1)); echo "FAIL: $1"; }

REPO="$WORK/repo"
mkdir -p "$REPO/scripts" "$WORK/bin"
cp "$SRC/scripts/run-latest.sh" "$REPO/scripts/"
g() { git -C "$REPO" -c user.email=t@t -c user.name=t "$@"; }
g init -q
g commit -q --allow-empty -m one
APP="$REPO/build/AgentSynth_artefacts/Release/Agent Synth.app"
mkdir -p "$APP/Contents/MacOS"
touch "$APP/Contents/MacOS/Agent Synth"
write_plist() {
    printf '<plist><dict>\n<key>BuildCommit</key>\n<string>%s</string>\n</dict></plist>\n' "$1" >"$APP/Contents/Info.plist"
}
LOG="$WORK/log"
for t in cmake open; do
    printf '#!/usr/bin/env bash\necho "%s $*" >>"%s"\n' "$t" "$LOG" >"$WORK/bin/$t"
done
# pgrep: "running" while $WORK/running exists; the osascript shim "quits" it.
printf '#!/usr/bin/env bash\n[ -f "%s/running" ]\n' "$WORK" >"$WORK/bin/pgrep"
printf '#!/usr/bin/env bash\necho "osascript quit" >>"%s"\nrm -f "%s/running"\n' "$LOG" "$WORK" >"$WORK/bin/osascript"
chmod +x "$WORK"/bin/*
run() { : >"$LOG"; PATH="$WORK/bin:$PATH" bash "$REPO/scripts/run-latest.sh" "$@" 2>&1; }

HEAD_SHA="$(g rev-parse HEAD)"
write_plist "$HEAD_SHA"
out="$(run)"; rc=$?
if [ $rc -eq 0 ] && grep -q "^open -n " "$LOG"; then ok "current build starts with open -n"; else bad "current build starts ($rc): $out"; fi
if grep -q "^cmake --build .*--target AgentSynth" "$LOG"; then ok "builds the AgentSynth target first"; else bad "no build step"; fi
if grep -q "started $HEAD_SHA" <<<"$out"; then ok "prints the commit it started"; else bad "commit not printed"; fi

write_plist "0000000000000000000000000000000000000000"
out="$(run)"; rc=$?
if [ $rc -eq 3 ] && ! grep -q "^open" "$LOG"; then ok "refuses a BuildCommit that is not HEAD"; else bad "stale bundle not refused ($rc)"; fi
out="$(RUN_LATEST_ALLOW_STALE=1 run)"; rc=$?
if [ $rc -eq 0 ] && grep -q "^open -n " "$LOG"; then ok "RUN_LATEST_ALLOW_STALE launches anyway"; else bad "override ignored ($rc)"; fi

write_plist "$HEAD_SHA"
touch "$WORK/running"
out="$(run)"; rc=$?
quit_line="$(grep -n 'osascript quit' "$LOG" | head -1 | cut -d: -f1)"
open_line="$(grep -n '^open -n' "$LOG" | head -1 | cut -d: -f1)"
if [ $rc -eq 0 ] && [ -n "$quit_line" ] && [ -n "$open_line" ] && [ "$quit_line" -lt "$open_line" ]; then
    ok "quits the running app before opening the new one"
else bad "no quit-then-open ($rc): $out"; fi

# An app that never quits: no second instance.
printf '#!/usr/bin/env bash\necho "osascript quit" >>"%s"\n' "$LOG" >"$WORK/bin/osascript"
touch "$WORK/running"
out="$(RUN_LATEST_QUIT_WAIT=1 run)"; rc=$?
if [ $rc -eq 4 ] && ! grep -q "^open" "$LOG"; then ok "an app that will not quit is never doubled"; else bad "stuck app ($rc)"; fi
rm -f "$WORK/running"

# HEAD moved on after the bundle was built (the FRO2362 case): refused.
echo x >"$REPO/f"
g add f
g commit -q -m two
out="$(run)"; rc=$?
if [ $rc -eq 3 ]; then ok "HEAD moved past the build: refused"; else bad "HEAD ahead not refused ($rc)"; fi

echo "run-latest tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
