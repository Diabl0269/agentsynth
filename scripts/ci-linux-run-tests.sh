#!/usr/bin/env bash
# Runs the Linux coverage job's tests as parallel GoogleTest shards, one per runner core: run one at
# a time they took 13 to over 23 minutes depending on the runner. Each shard gets its own settings
# folder, as the macOS job does, and its own profile for scripts/coverage.sh to merge. A hung shard
# dies with thread stacks instead of a silent step timeout. The 2400 s budget is 40% over the
# slowest runner seen (a shard needs up to ~1700 s there); the step timeout in ci.yml must stay above it. See docs/development/ci-pipeline.md.
set -uo pipefail

total="${LINUX_TEST_SHARDS:-4}"
binary="${TESTS_BINARY:-./build/Tests/Tests}"
limit="${LINUX_TEST_SHARD_SECONDS:-2400}"
base="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"

if [ -n "${GITHUB_ACTIONS:-}" ]; then
  sudo apt-get install -y --no-install-recommends gdb >/dev/null 2>&1 || true
  ulimit -c unlimited
  echo 'core.%p' | sudo tee /proc/sys/kernel/core_pattern >/dev/null
fi

# The keyboard/focus suites first (AGE128): a break there fails in minutes, not after the full run.
early_dir="$base/agentsynth-settings-early"
mkdir -p "$early_dir"
AGENTSYNTH_SETTINGS_DIR="$early_dir" LLVM_PROFILE_FILE="default-early.profraw" \
  bash "$(dirname "$0")/ci-early-suites.sh" "$binary" || exit 1

pids=()
for i in $(seq 0 $((total - 1))); do
  dir="$base/agentsynth-settings-$i"
  mkdir -p "$dir"
  GTEST_TOTAL_SHARDS="$total" GTEST_SHARD_INDEX="$i" AGENTSYNTH_SETTINGS_DIR="$dir" \
    LLVM_PROFILE_FILE="default-$i.profraw" \
    timeout -s ABRT "$limit" "$binary" >"$base/agentsynth-tests-shard-$i.log" 2>&1 &
  pids+=("$!")
done

failed=0
for i in $(seq 0 $((total - 1))); do
  rc=0
  wait "${pids[$i]}" || rc=$?
  log="$base/agentsynth-tests-shard-$i.log"
  echo "::group::Shard $i (exit $rc)"
  cat "$log"
  echo "::endgroup::"
  if [ "$rc" -ne 0 ]; then
    echo "::error::Shard $i failed (exit $rc) -- see the log group above"
    failed=1
  elif ! grep -qE '^\[==========\] [1-9][0-9]* tests? from .* ran' "$log"; then
    echo "::error::Shard $i reported no tests ran -- see the log group above"
    failed=1
  fi
done

if [ "$failed" -ne 0 ]; then
  for core in core.*; do
    [ -f "$core" ] || continue
    echo "=== hang/crash backtraces: $core ==="
    gdb -batch -ex 'thread apply all bt' "$binary" "$core" 2>/dev/null | tail -300
  done
fi
exit "$failed"
