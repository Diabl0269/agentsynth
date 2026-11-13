// PhysicalKeyState.cpp
//
// On macOS JUCE reports a key pressed with Cmd as released straight away: Cocoa drops the key-up of
// a Command chord, so NSViewComponentPeer::redirectKeyDown fakes one right after the key-down and
// isKeyCurrentlyDown() turns false while the key is still held. A Cmd+arrow hold therefore asks the
// window server for the key's real state. No JUCE header here: CoreGraphics' global names stay out
// of every other unit.

#include "PhysicalKeyState.h"

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace synth::ui {

std::optional<bool> physicalArrowKeyDown(ArrowKey key) {
#if defined(__APPLE__)
    // Carbon virtual key codes (kVK_LeftArrow ... kVK_UpArrow). The combined session state also
    // counts posted events, so synthetic input used for in-app checks reads the same as a keyboard.
    CGKeyCode code = 0;
    switch (key) {
    case ArrowKey::Left:
        code = 123;
        break;
    case ArrowKey::Right:
        code = 124;
        break;
    case ArrowKey::Down:
        code = 125;
        break;
    case ArrowKey::Up:
        code = 126;
        break;
    }
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, code);
#else
    (void)key;
    return std::nullopt;
#endif
}

} // namespace synth::ui
