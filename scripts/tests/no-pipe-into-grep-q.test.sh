#!/usr/bin/env bash
# Guard: no script that runs under `set -o pipefail` feeds a variable into `grep -q` through a pipe.
# grep -q exits at its first match; if the writer (echo/printf) is still writing, it dies of SIGPIPE and
# pipefail turns the match into a failure. It only happens when the machine is busy, so it shows up as a
# flaky test. Use a here-string instead: grep -qF "text" <<<"$output".
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

hits="$(grep -rlE 'pipefail' scripts | sort | xargs grep -nE '(^|[^[:alnum:]_])(echo|printf)[^|]*\|[[:space:]]*grep[[:space:]]+-[[:alpha:]]*q' || true)"
if [ -n "$hits" ]; then
    echo "FAIL: pipe into grep -q under pipefail (use a here-string: grep -q PATTERN <<<\"\$var\"):"
    echo "$hits"
    echo
    echo "0 passed, 1 failed"
    exit 1
fi
echo "PASS: no echo/printf piped into grep -q in pipefail scripts"
echo
echo "1 passed, 0 failed"
