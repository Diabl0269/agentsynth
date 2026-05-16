#!/bin/bash
# Script to build Gravisynth
set -e
echo "Building project..."
cmake -S . -B build
cmake --build build
echo "Build complete."
