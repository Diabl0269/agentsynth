#!/usr/bin/env bash
#
# Unit tests for Tools/ui-verify/run.py (the scenario runner): YAML parsing, the expect logic, the run loop, against a
# fake app. No Agent Synth is started. Needs python3 with PyYAML; without PyYAML the test says so and skips.
#
# Usage: bash scripts/tests/ui-verify-run.test.sh

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if ! command -v python3 >/dev/null 2>&1 || ! python3 -c 'import yaml' >/dev/null 2>&1; then
    echo "SKIP: python3 with PyYAML is not available"
    echo
    echo "0 passed, 0 failed"
    exit 0
fi

if output="$(python3 "$REPO_ROOT/Tools/ui-verify/tests/test_run.py" 2>&1)"; then
    echo "PASS: Tools/ui-verify/run.py"
    echo
    echo "1 passed, 0 failed"
    exit 0
fi
echo "$output"
echo
echo "FAIL: Tools/ui-verify/run.py"
echo "0 passed, 1 failed"
exit 1
