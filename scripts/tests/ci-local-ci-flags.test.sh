#!/usr/bin/env bash
#
# Guard for ci-local.sh's "Configure with CI's flags" step: every -D flag the macOS job's
# "Configure CMake" step in ci.yml passes must also be passed by that step, so a flag added to CI
# cannot go unchecked locally. Plain text comparison, no cmake, no network, under a second.
#
# Usage: bash scripts/tests/ci-local-ci-flags.test.sh

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CI_YML="$ROOT/.github/workflows/ci.yml"
LOCAL="$ROOT/scripts/ci-local.sh"

# The macOS job is the one whose configure step passes -DCMAKE_OBJCXX_COMPILER_LAUNCHER.
ci_flags="$(awk '
    /cmake -B build -G Ninja/ {block=""; grab=1}
    grab {block = block $0 "\n"}
    grab && $0 !~ /\\$/ {if (block ~ /OBJCXX_COMPILER_LAUNCHER/) {printf "%s", block} grab=0}
' "$CI_YML" | grep -oE -- '-D[A-Z_]+' | sort -u)"

fail=0
if [ -z "$ci_flags" ]; then
    echo "FAIL: found no macOS configure flags in ci.yml (did the step move?)"
    exit 1
fi
step="$(sed -n '/^# --- 8b/,/^step "Build/p' "$LOCAL")"
for flag in $ci_flags; do
    if ! grep -qF -- "$flag" <<<"$step"; then
        echo "FAIL: ci.yml's macOS configure passes $flag but ci-local.sh's CI-flags step does not"
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "PASS: ci-local.sh's CI-flags step covers every -D flag of ci.yml's macOS configure"
fi
exit "$fail"
