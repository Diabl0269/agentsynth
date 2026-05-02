#!/usr/bin/env bash
set -euo pipefail

# Regenerate audio reference files for snapshot tests.
# Usage: bash scripts/update-reference.sh
#
# This deletes existing reference files and re-runs the snapshot tests,
# which creates fresh reference files on first run.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
REF_DIR="$REPO_ROOT/Tests/reference"

echo "==> Removing old reference files..."
rm -f "$REF_DIR"/*.raw

echo "==> Building tests..."
cmake --build "$REPO_ROOT/build" --target GravisynthTests -j"$(sysctl -n hw.logicalcpu 2>/dev/null || nproc)"

echo "==> Running snapshot tests to generate new references..."
"$REPO_ROOT/build/Tests/GravisynthTests" --gtest_filter="AudioRenderingTest.Snapshot*"

echo "==> Done. New reference files:"
ls -la "$REF_DIR"/*.raw 2>/dev/null || echo "(none generated — check test output)"
