#!/usr/bin/env bash
#
# Guard: test mode is opt-in. AGENTSYNTH_AUTOMATION defaults to OFF in cmake/TestModeSources.cmake, so a build nobody
# asked for refuses `--automation`; scripts/ci-local.sh (the build ui-verify drives) configures it ON, and the
# packaged-build and Linux workflows keep passing OFF. No compiler, no network.
#
# Usage: bash scripts/tests/automation-default-off.test.sh

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fail=0
check() { if ! "$@"; then echo "FAIL: $*"; fail=1; fi; }

check grep -Eq '^option\(AGENTSYNTH_AUTOMATION "[^"]*" OFF\)' "$ROOT/cmake/TestModeSources.cmake"
check grep -q -- '-DAGENTSYNTH_AUTOMATION=ON' "$ROOT/scripts/ci-local.sh"
check grep -q -- '-DAGENTSYNTH_AUTOMATION=OFF' "$ROOT/.github/workflows/build-artifacts.yml"
check grep -q -- '-DAGENTSYNTH_AUTOMATION=OFF' "$ROOT/.github/workflows/ci-gcc.yml"

[ "$fail" -eq 0 ] && echo "PASS: automation-default-off"
exit "$fail"
