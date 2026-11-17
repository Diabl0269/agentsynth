#!/usr/bin/env bash
# Runs the suites listed in scripts/ci-early-suites.list on their own, before the full test run, and
# fails at once if one breaks (AGE128). Usage: ci-early-suites.sh <path-to-Tests-binary>. Honours
# AGENTSYNTH_SETTINGS_DIR and LLVM_PROFILE_FILE from the caller. See docs/development/ci-pipeline.md.
set -uo pipefail

binary="${1:?usage: ci-early-suites.sh <Tests binary>}"
list="$(dirname "${BASH_SOURCE[0]}")/ci-early-suites.list"

filter=""
while IFS= read -r line || [ -n "$line" ]; do
  line="${line%%#*}"
  line="${line//[[:space:]]/}"
  [ -n "$line" ] && filter="${filter:+$filter:}$line.*"
done <"$list"
[ -n "$filter" ] || { echo "::error::$list lists no suites"; exit 1; }

log="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/agentsynth-early-suites.log"
echo "Early pass: --gtest_filter=$filter"
rc=0
"$binary" --gtest_filter="$filter" >"$log" 2>&1 || rc=$?
echo "::group::Early suites (exit $rc)"
cat "$log"
echo "::endgroup::"

if [ "$rc" -ne 0 ]; then
  echo "::error::Early keyboard/focus suites failed (exit $rc) -- see the log group above"
  exit 1
fi
if ! grep -qE '^\[==========\] [1-9][0-9]* tests? from .* ran' "$log"; then
  echo "::error::Early suites ran no tests -- is a name in scripts/ci-early-suites.list stale?"
  exit 1
fi
