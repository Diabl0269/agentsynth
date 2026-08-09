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
