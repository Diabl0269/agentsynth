#!/bin/bash
set -e

BUILD_DIR="build-release"

# clang-format is pinned (see .clang-format-version); warn on a version mismatch
# so a local version that disagrees with CI is obvious before pushing.
pinned="$(cat .clang-format-version 2>/dev/null || true)"
have="$(clang-format --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
if [ -n "$pinned" ] && [ "$have" != "$pinned" ]; then
    echo "Pre-push: clang-format $have differs from pinned $pinned (CI uses $pinned). Match: pip install clang-format==$pinned"
fi

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
    # ENABLE_TESTS defaults OFF, so it must be set explicitly or the
    # Tests target is never generated ("No rule to make target").
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
fi

# Incremental build
cmake --build "$BUILD_DIR" --target Tests -- -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)

# Run tests
"$BUILD_DIR/Tests/Tests"

echo "Pre-push: All tests passed in Release mode."
