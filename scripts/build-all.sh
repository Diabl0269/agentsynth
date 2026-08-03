#!/bin/bash
# Script to build Agent Synth
set -e
echo "Building project..."
cmake -S . -B build
cmake --build build
echo "Build complete."
