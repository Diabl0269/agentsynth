#!/bin/bash
set -e

BUILD_DIR="build-release"

echo "Pre-push: Checking formatting..."
if ! find Source Tests -name "*.h" -o -name "*.cpp" | xargs clang-format --dry-run --Werror 2>&1; then
    echo "Pre-push: Formatting check failed. Run: find Source Tests -name '*.h' -o -name '*.cpp' | xargs clang-format -i"
    exit 1
fi
echo "Pre-push: Formatting OK."

echo "Pre-push: Running Release build + tests..."

# Configure on first run
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Pre-push: Configuring Release build (first run)..."
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi

# Incremental build
cmake --build "$BUILD_DIR" --target AgentSynthTests -- -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)

# Run tests
"$BUILD_DIR/Tests/AgentSynthTests"

echo "Pre-push: All tests passed in Release mode."
