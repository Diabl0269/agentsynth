#!/bin/bash
set -e

REPORT_ONLY=false
for arg in "$@"; do
  case $arg in
    --report-only) REPORT_ONLY=true ;;
  esac
done

if [ "$REPORT_ONLY" = false ]; then
  # Configure with coverage enabled
  cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON

  # Build the tests
  cmake --build build --target Tests

  # Run the tests
  export LLVM_PROFILE_FILE="default.profraw"
  ./build/Tests/Tests
fi

# Generate coverage report
echo "Merging profile data..."

# Detect tools
if command -v xcrun &> /dev/null; then
    PROFDATA="xcrun llvm-profdata"
    COV="xcrun llvm-cov"
else
    PROFDATA="llvm-profdata"
    COV="llvm-cov"
fi

$PROFDATA merge -sparse default.profraw -o default.profdata

echo "Generating coverage report..."
# Capture the report output.
# Source/Plugin/PluginEditor is excluded for the same reason as Source/UI and Source/MainComponent:
# it is pure JUCE Component wiring, not headless-testable to the 85% bar. Source/Plugin/
# PluginProcessor is deliberately NOT excluded — it is engine/state logic and is covered by
# Tests/PluginProcessorTests.cpp.
REPORT=$($COV report \
    ./build/Tests/Tests \
    -instr-profile=default.profdata \
    -ignore-filename-regex="JuceLibraryCode|build/_deps|Tests|Source/UI|Source/Main\.cpp|Source/MainComponent|Source/Plugin/PluginEditor")

echo "$REPORT"

# Parse total line coverage
# Column 10 corresponds to Line Coverage percentage in the standard report format.
TOTAL_COVERAGE=$(echo "$REPORT" | grep "TOTAL" | awk '{print $10}' | sed 's/%//')

echo "Total Line Coverage: $TOTAL_COVERAGE%"

# Threshold check (awk for float comparison to avoid installing bc)
PASS=$(awk -v cov="$TOTAL_COVERAGE" 'BEGIN {print (cov >= 85.0) ? 1 : 0}')

if [ "$PASS" -eq 1 ]; then
  echo "Coverage check passed."
  exit 0
else
  echo "Error: Coverage ($TOTAL_COVERAGE%) is below threshold (85.0%)"
  exit 1
fi
