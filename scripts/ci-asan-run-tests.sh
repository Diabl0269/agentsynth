#!/usr/bin/env bash
# Runs the ASAN Tests binary as sequential GoogleTest shards, each a fresh process, so the ASAN
# quarantine and the heavy JUCE components of ~8.5k tests never pile up in one 1.1 GB process (a
# single process stalled the runner late in the suite). Each shard gets its own settings folder
# (AGENTSYNTH_SETTINGS_DIR), as the macOS job does. A background sampler logs memory so a future
# stall shows its cause in the log. See docs/development/ci-pipeline.md, "ASAN job".
set -uo pipefail

total="${ASAN_TEST_SHARDS:-6}"
binary="${TESTS_BINARY:-./build/Tests/Tests}"
base="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"

sampler() {
  while sleep 30; do
    echo "mem: $(free -m | awk 'NR==2 {print "used=" $3 "M free=" $4 "M avail=" $7 "M"}') swap_used=$(free -m | awk 'NR==3 {print $3}')M tests_rss=$(ps -C Tests -o rss= 2>/dev/null | awk '{s+=$1} END {print int(s/1024)}')M"
  done
}
sampler &
sampler_pid=$!
trap 'kill "$sampler_pid" 2>/dev/null' EXIT

failed=0
for i in $(seq 0 $((total - 1))); do
  dir="$base/agentsynth-asan-settings-$i"
  mkdir -p "$dir"
  echo "::group::ASAN shard $i of $total"
  GTEST_TOTAL_SHARDS="$total" GTEST_SHARD_INDEX="$i" AGENTSYNTH_SETTINGS_DIR="$dir" "$binary"
  rc=$?
  echo "::endgroup::"
  if [ "$rc" -ne 0 ]; then
    echo "::error::ASAN shard $i failed (exit $rc)"
    failed=1
  fi
done
dmesg 2>/dev/null | grep -i -E 'out of memory|oom|killed process' | tail -5 || true
exit "$failed"
