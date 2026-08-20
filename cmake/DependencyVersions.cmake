# Single source of truth for the third-party dependency pins fetched via FetchContent.
#
# WHY THIS FILE EXISTS
# CI keys its `build/_deps` cache on hashFiles() of THIS FILE — see the "Cache FetchContent
# Dependencies" steps in .github/workflows/ci.yml and .github/workflows/build-artifacts.yml.
# Until Aug 2026 that cache was keyed on the whole of CMakeLists.txt + Tests/CMakeLists.txt,
# so every module that added a source line minted a brand-new ~350 MB cache entry per platform
# per workflow. CMakeLists.txt changed 10x in the first nine days of Aug 2026, which pushed the
# repo past GitHub's 10 GB cache budget and LRU-evicted every ccache entry along with it.
#
# build/_deps holds only the *fetched sources* of the dependencies below, so its cache key must
# depend only on the pins below. Adding a module must not invalidate it.
#
# RULE: every pin that changes the contents of build/_deps belongs here — and nothing else does.
# The FetchContent_Declare() calls read these variables, so the pins and the cache key can never
# drift apart.

set(SYNTH_JUCE_GIT_REPOSITORY "https://github.com/juce-framework/JUCE.git"
    CACHE STRING "JUCE upstream fetched via FetchContent")

set(SYNTH_JUCE_GIT_TAG "8.0.3"
    CACHE STRING "JUCE release tag fetched via FetchContent (a specific tag, for stability)")

set(SYNTH_GOOGLETEST_URL "https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip"
    CACHE STRING "googletest archive fetched via FetchContent (test builds only)")

# Sparkle ships as a prebuilt .xcframework, not a CMake project — fetched only on APPLE (see
# CMakeLists.txt), so this adds ~15 MB to build/_deps on macOS only, well under the budget that
# made the CMakeLists-wide cache key a problem (see header comment above).
set(SYNTH_SPARKLE_URL "https://github.com/sparkle-project/Sparkle/releases/download/2.9.5/Sparkle-2.9.5.tar.xz"
    CACHE STRING "Sparkle (macOS auto-update) binary distribution fetched via FetchContent")

set(SYNTH_SPARKLE_SHA256 "015336b601493e05c237964954bff6191370003d94edefe663724c88840d73cc"
    CACHE STRING "SHA-256 of SYNTH_SPARKLE_URL — verified against the GitHub release asset at pin time")

# WinSparkle (Windows auto-update, P5-6) ships as a prebuilt binary distribution (headers, import
# lib, DLL, and its own signing CLI) rather than a CMake project — fetched only on WIN32 (see
# CMakeLists.txt), same "keep build/_deps cache key stable" rationale as the Sparkle pin above.
set(SYNTH_WINSPARKLE_URL "https://github.com/vslavik/winsparkle/releases/download/v0.9.4/WinSparkle-0.9.4.zip"
    CACHE STRING "WinSparkle (Windows auto-update) binary distribution fetched via FetchContent")

set(SYNTH_WINSPARKLE_SHA256 "6037df37fc263bd1650a1c4949681a9d40ffe991d01f35892a406cb5d103c976"
    CACHE STRING "SHA-256 of SYNTH_WINSPARKLE_URL — verified against the GitHub release asset at pin time")
