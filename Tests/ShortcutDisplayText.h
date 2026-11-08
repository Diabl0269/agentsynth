#pragma once

// ShortcutDisplayText.h: the platform's spelling of a key chord as the app prints it in tooltips and Settings
// (ShortcutManager::keyPressToDisplayString), so a test never hard-codes "Shift + 8" or "Alt + Return" -- on macOS
// those read with the Mac modifier symbols, elsewhere with the words.
#include "ShortcutManager/ShortcutManager.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

namespace synth::test {

/** How the app prints `keyCode` held with `modifiers` (a juce::ModifierKeys flag mask). */
inline juce::String keyText(int keyCode, int modifiers = 0) {
    return ShortcutManager::keyPressToDisplayString(juce::KeyPress(keyCode, juce::ModifierKeys(modifiers), 0));
}

/** The modifiers' part of a chord including its trailing " + ", e.g. "Shift + " or the Mac Shift symbol and " + ". */
inline juce::String modifierPrefix(int modifiers) { return keyText('x', modifiers).dropLastCharacters(1); }

/** True when `text` holds `part`. Use this, not juce::String::contains, for a part that has a Mac symbol in it:
 *  juce::String::contains compares only as many bytes as the part has characters, so a part that starts with a
 *  three-byte symbol matches any text sharing its first few bytes. */
inline bool has(const juce::String& text, const juce::String& part) {
    return text.toStdString().find(part.toStdString()) != std::string::npos;
}

/** The swap icons' own key. */
inline juce::String shiftReturnText() { return keyText(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier); }

} // namespace synth::test
