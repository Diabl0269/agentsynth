#pragma once

// NativeWindowTestGuard.h
//
// The label-gated ASAN job's Xvfb display rejects a temporary native window with an X11 BadAtom error, which ends the
// whole test process. A test that really needs a native window (addToDesktop) calls nativeWindowsAbortHere() first and
// skips there; every other job still runs it.

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define AGENTSYNTH_TESTS_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(AGENTSYNTH_TESTS_ASAN)
#define AGENTSYNTH_TESTS_ASAN 1
#endif

inline bool nativeWindowsAbortHere() {
#if JUCE_LINUX && defined(AGENTSYNTH_TESTS_ASAN)
    return true;
#else
    return false;
#endif
}

// Base fixture for a test that builds a window or an alert (an AlertWindow lays itself out against the monitor it sits
// on, and a headless runner has none, so building one there segfaults): it skips with no display, or where native
// windows abort, and otherwise starts the message manager. Derive from it (or use TEST_F with it) rather than
// repeating the checks; the AlertDisplayFixtureGuard test (AlertIconDepthTests.cpp) fails a test file that builds an
// alert without one.
class NeedsDisplayTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
    }
};
