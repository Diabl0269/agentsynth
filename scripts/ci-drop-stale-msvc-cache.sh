#!/usr/bin/env bash
# Windows CI: drop restored CMake configure state that was made by another MSVC toolset.
#
# build/_deps is restored by a pins-only cache key, so it can hold sub-build trees (JUCE's juceaide)
# configured on an older runner image. Their CMakeCache.txt records that image's compiler path
# (.../VC/Tools/MSVC/14.51.x/bin/Hostx64/x64/cl.exe). When the image ships a different toolset CMake fails with
# "CMAKE_CXX_COMPILER ... is not a full path to an existing compiler tool". Sources and ccache objects are
# kept; only the CMakeCache.txt and CMakeFiles of a sub-build naming another toolset are removed.
# Run after "Setup MSVC" (needs VCToolsVersion) and before "Configure CMake".
set -euo pipefail

tools="${VCToolsVersion:-}"
if [ -z "$tools" ]; then
  echo "VCToolsVersion is unset; run this after Setup MSVC" >&2
  exit 1
fi
echo "Active MSVC toolset: $tools"

[ -d build/_deps ] || exit 0
while IFS= read -r -d '' cache; do
  if grep -qF "VC/Tools/MSVC/" "$cache" && ! grep -qF "VC/Tools/MSVC/$tools/" "$cache"; then
    dir=$(dirname "$cache")
    echo "Stale MSVC toolset in $cache; resetting $dir"
    rm -rf "$cache" "$dir/CMakeFiles"
  fi
done < <(find build/_deps -name CMakeCache.txt -print0)
