#!/usr/bin/env bash
#
# Unit tests for two helpers of scripts/ci-local.sh: scripts/lib/stale-binaries.sh (the --fast warning
# when the built app or AIEvalHarness is older than the changed code) and scripts/lib/compile-changed.py
# (the early compile of changed .cpp files from compile_commands.json). A throwaway git repo and stub
# compilers (`true`/`false`); no real compiler, no build.
#
# Usage: bash scripts/tests/ci-local-early-checks.test.sh

set -uo pipefail
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/ci-local-early-checks.XXXXXX")"
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

R="$TMPROOT/repo"
mkdir -p "$R/Source" "$R/build/AgentSynth_artefacts/Agent Synth.app/Contents/MacOS" "$R/build/Tools/AIEvalHarness"
cd "$R" || exit 1
git init -q . && git config user.email t@t && git config user.name t
echo "int a;" >Source/a.cpp
git add -A && git commit -qm base
BASE="$(git rev-parse HEAD)"
APP="$R/build/AgentSynth_artefacts/Agent Synth.app"
EXE="$APP/Contents/MacOS/Agent Synth"
HARNESS="$R/build/Tools/AIEvalHarness/AIEvalHarness"
printf '#!/bin/sh\n' >"$EXE"
printf '#!/bin/sh\n' >"$HARNESS"
chmod +x "$EXE" "$HARNESS"
touch -t 202001010000 "$EXE" "$HARNESS"

# shellcheck source=../lib/stale-binaries.sh
source "$SCRIPT_DIR/scripts/lib/stale-binaries.sh"

echo "int b;" >Source/a.cpp
out="$(warn_stale_binaries build "$BASE" "$APP")"
grep -q "STALE BINARY" <<<"$out" && grep -qF "$EXE" <<<"$out" && grep -qF "build/Tools/AIEvalHarness/AIEvalHarness" <<<"$out" && grep -q "ci-local.sh --skip-tests" <<<"$out"
record $? "stale: an older app and harness are named with the rebuild command"

touch Source/a.cpp "$EXE" "$HARNESS"
touch -t 202001010000 Source/a.cpp
out="$(warn_stale_binaries build "$BASE" "$APP")"
[ -z "$out" ]
record $? "fresh: binaries newer than the change print nothing"

out="$(warn_stale_binaries nobuild "$BASE" "")"
grep -q "built no app bundle" <<<"$out"
record $? "no app bundle: says none was built"

git checkout -q Source/a.cpp
out="$(warn_stale_binaries build "$BASE" "$APP")"
[ -z "$out" ]
record $? "no changed source: no warning"

# --- compile-changed.py ---
CC="$SCRIPT_DIR/scripts/lib/compile-changed.py"
mkdir -p cdb
printf 'int ok;\n' >Source/ok.cpp
printf 'int bad;\n' >Source/bad.cpp
cat >cdb/compile_commands.json <<JSON
[{"directory":"$R","command":"true -c Source/ok.cpp -MD -MF x.d -o ok.o","file":"Source/ok.cpp"},
 {"directory":"$R","command":"false -c Source/bad.cpp -o bad.o","file":"Source/bad.cpp"}]
JSON
python3 "$CC" --build cdb Source/ok.cpp >/dev/null
record $? "compile-changed: a compiling file passes"
out="$(python3 "$CC" --build cdb Source/ok.cpp Source/bad.cpp Source/ok.cpp)"
rc=$?
[ "$rc" -eq 1 ] && grep -q "bad.cpp does not compile" <<<"$out"
record $? "compile-changed: the first failing file stops it"
python3 "$CC" --build cdb Source/a.h Source/unknown.cpp >/dev/null
record $? "compile-changed: headers and files outside the build are skipped"
python3 "$CC" --build nodb Source/ok.cpp >/dev/null
record $? "compile-changed: no compile_commands.json skips with a note"
mkdir -p cdb2
cat >cdb2/compile_commands.json <<JSON
[{"directory":"$R","command":"false -c Source/bad.cpp -o never.o","file":"Source/bad.cpp","output":"never.o"}]
JSON
python3 "$CC" --build cdb2 Source/bad.cpp | grep -q "skip Source/bad.cpp"
record $? "compile-changed: a file whose target was never built here is skipped"
python3 - "$CC" <<'PY'
import sys
import importlib.util
spec = importlib.util.spec_from_file_location("cc", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
assert m.syntax_only("c++ -c a.cpp -MD -MT t -MF d -o a.o") == ["c++", "-c", "a.cpp", "-fsyntax-only"]
PY
record $? "compile-changed: output and depfile flags are dropped for -fsyntax-only"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
