#pragma once

// NativeWindowTestGuard.h
//
// The label-gated ASAN job's Xvfb display rejects a temporary native window with an X11 BadAtom error, which ends the
// whole test process. A test that really needs a native window (addToDesktop) calls nativeWindowsAbortHere() first and
// skips there; every other job still runs it.

#include <juce_core/juce_core.h>

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
