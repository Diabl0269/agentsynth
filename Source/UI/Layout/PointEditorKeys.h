#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** True for the "insert a point after the cursor point" key of every point editor with ordered dots (the curve
 *  editor and the automation lanes): the physical Control key with T and no Shift or Alt. On the Mac Control is
 *  distinct from Cmd (Cmd+T is the app's Add Track, so it never counts here); elsewhere Cmd is Ctrl, so the one
 *  flag stands for both. docs/control/point-editing.md is the key map this belongs to. */
inline bool isInsertPointKey(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isShiftDown() || mods.isAltDown() || !mods.isCtrlDown())
        return false;
#if JUCE_MAC
    if (mods.isCommandDown())
        return false;
#endif
    return juce::CharacterFunctions::toUpperCase((juce::juce_wchar)key.getKeyCode()) == (juce::juce_wchar)'T';
}

} // namespace synth::ui
