#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

/** True when JUCE knows a main display. A launch from a background process, or while displays are being
 *  rearranged or unplugged, can find none for a moment. */
inline bool hasMainDisplay() { return juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr; }

/** Sizes a top-level window and, when a main display exists, centres it on it.
 *
 *  `Component::centreWithSize` reads the main display's bounds without a null check, so with no main display
 *  it crashes the launch (SIGSEGV at address 0x34 inside `getParentOrMainMonitorBounds`). With none, the window
 *  only gets its size and keeps its position; the OS places it once a display is there.
 *
 *  `mainDisplayAvailable` is a parameter so tests can simulate "no main display" without hardware. */
inline void sizeAndCentreWindow(juce::Component& window, int width, int height, bool mainDisplayAvailable) {
    if (mainDisplayAvailable)
        window.centreWithSize(width, height);
    else
        window.setSize(width, height);
}

inline void sizeAndCentreWindow(juce::Component& window, int width, int height) {
    sizeAndCentreWindow(window, width, height, hasMainDisplay());
}

} // namespace synth
