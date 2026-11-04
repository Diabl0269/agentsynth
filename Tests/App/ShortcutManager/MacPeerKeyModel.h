#pragma once

#include "ShortcutManager/MacPeerDroppedKey.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::test {

/** What AppKit reports for one physical key, as measured with NSEvent(cgEvent:) on a US layout. */
struct MacKeyEvent {
    const char* name;
    int firstCharacter;  // [ev characters][0]
    int firstUnmodified; // [ev charactersIgnoringModifiers][0]
};

/** Ctrl+Shift+1..8 as macOS reports them (US layout). Ctrl+Shift+2 alone has NUL for `characters`. */
inline const MacKeyEvent kCtrlShiftDigits[] = {
    {"Ctrl+Shift+1", 0x21, '!'}, {"Ctrl+Shift+2", 0x00, '@'}, {"Ctrl+Shift+3", 0x23, '#'}, {"Ctrl+Shift+4", 0x24, '$'},
    {"Ctrl+Shift+5", 0x25, '%'}, {"Ctrl+Shift+6", 0x1e, '^'}, {"Ctrl+Shift+7", 0x26, '&'}, {"Ctrl+Shift+8", 0x2a, '*'},
};

inline juce::ModifierKeys ctrlShift() {
    return juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier);
}

/** The KeyPress JUCE's NSView peer builds for the event (handleKeyEvent + getKeyCodeFromEvent in
 *  juce_NSViewComponentPeer_mac.mm), or nothing when it builds none: it calls handleKeyPress once per
 *  character of `characters`, and a NUL character reads as an empty string, so the loop never runs. */
inline std::optional<juce::KeyPress> keyPressAsThePeerBuildsIt(const MacKeyEvent& event, juce::ModifierKeys mods) {
    if (event.firstCharacter == 0)
        return std::nullopt;
    const int keyCode =
        static_cast<int>(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(event.firstUnmodified)));
    return juce::KeyPress(keyCode, mods, static_cast<juce::juce_wchar>(event.firstCharacter));
}

/** What the app ends up dispatching: the peer's own key press, or the relay's rebuilt one. */
inline std::optional<juce::KeyPress> keyPressTheAppReceives(const MacKeyEvent& event, juce::ModifierKeys mods) {
    if (auto peerKey = keyPressAsThePeerBuildsIt(event, mods))
        return peerKey;
    return recoverKeyPressDroppedByMacPeer(event.firstCharacter, event.firstUnmodified, mods);
}

} // namespace synth::test
