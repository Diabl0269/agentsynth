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
    const auto code = (juce::juce_wchar)key.getKeyCode();
    const auto upper = juce::CharacterFunctions::toUpperCase(code); // not-ui-text: key code, not displayed
    return upper == (juce::juce_wchar)'T';
}

/** One press of the shared key map (docs/control/point-editing.md), decoded once for every point editor. Each editor
 *  switches on `kind` and consumes only the actions it supports; a new key is added here and in that switch. */
struct PointKeyAction {
    enum class Kind {
        none,
        pick,      // Left / Right
        extend,    // Shift+Left / Shift+Right
        value,     // Up / Down (Shift: coarse)
        move,      // Alt+arrows (Alt+Shift: fine)
        selectAll, // Cmd+A
        copy,      // Cmd+C
        paste,     // Cmd+V
        duplicate, // Cmd+D
        insert,    // Ctrl+T
        remove,    // Delete / Backspace
        clear      // Escape
    };
    enum class Size { normal, coarse, fine };
    Kind kind = Kind::none;
    int side = 0;      // -1 Left / Down, +1 Right / Up
    bool time = false; // a move along time (Left / Right) rather than value
    Size size = Size::normal;
    bool plain = false; // no modifier held at all
};

inline PointKeyAction pointKeyAction(const juce::KeyPress& key) {
    using K = PointKeyAction::Kind;
    using S = PointKeyAction::Size;
    const auto mods = key.getModifiers();
    const bool shift = mods.isShiftDown();
    const bool alt = mods.isAltDown();
    PointKeyAction action;
    action.plain = !mods.isAnyModifierKeyDown();
    if (mods.isCommandDown() || mods.isCtrlDown()) {
        if (shift || alt)
            return action;
        if (isInsertPointKey(key)) {
            action.kind = K::insert;
            return action;
        }
        const int code = key.getKeyCode();
        action.kind = code == 'A'   ? K::selectAll
                      : code == 'C' ? K::copy
                      : code == 'V' ? K::paste
                      : code == 'D' ? K::duplicate
                                    : K::none;
        return action;
    }
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) {
        action.kind = K::remove;
        return action;
    }
    if (code == juce::KeyPress::escapeKey) {
        action.kind = K::clear;
        return action;
    }
    const bool left = code == juce::KeyPress::leftKey, right = code == juce::KeyPress::rightKey;
    const bool up = code == juce::KeyPress::upKey, down = code == juce::KeyPress::downKey;
    if (!left && !right && !up && !down)
        return action;
    action.time = left || right;
    action.side = right || up ? 1 : -1;
    if (alt) {
        action.kind = K::move;
        action.size = shift ? S::fine : S::normal;
    } else if (action.time) {
        action.kind = shift ? K::extend : K::pick;
    } else {
        action.kind = K::value;
        action.size = shift ? S::coarse : S::normal;
    }
    return action;
}

} // namespace synth::ui
