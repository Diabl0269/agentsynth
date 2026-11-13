#include "ShortcutHintText.h"

#include <cctype>

namespace synth::ui::hint {

namespace {

#if JUCE_MAC
constexpr bool kIsMac = true;
#else
constexpr bool kIsMac = false;
#endif

juce::String keyName(const juce::KeyPress& key, bool macGlyphs) {
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::spaceKey)
        return "Space";
    if (code == juce::KeyPress::returnKey)
        return "Return";
    if (code == juce::KeyPress::escapeKey)
        return "Esc";
    if (code == juce::KeyPress::tabKey)
        return "Tab";
    if (code == juce::KeyPress::backspaceKey)
        return "Backspace";
    if (code == juce::KeyPress::deleteKey)
        return "Delete";
    if (code == juce::KeyPress::homeKey)
        return "Home";
    if (code == juce::KeyPress::endKey)
        return "End";
    if (code == juce::KeyPress::pageUpKey)
        return "PgUp";
    if (code == juce::KeyPress::pageDownKey)
        return "PgDn";
    if (code == juce::KeyPress::leftKey)
        return macGlyphs ? juce::String(juce::CharPointer_UTF8("\xe2\x86\x90")) : juce::String("Left");
    if (code == juce::KeyPress::upKey)
        return macGlyphs ? juce::String(juce::CharPointer_UTF8("\xe2\x86\x91")) : juce::String("Up");
    if (code == juce::KeyPress::rightKey)
        return macGlyphs ? juce::String(juce::CharPointer_UTF8("\xe2\x86\x92")) : juce::String("Right");
    if (code == juce::KeyPress::downKey)
        return macGlyphs ? juce::String(juce::CharPointer_UTF8("\xe2\x86\x93")) : juce::String("Down");
    if (code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F35Key)
        return "F" + juce::String(code - juce::KeyPress::F1Key + 1);
    if (code >= 33 && code < 127)
        return juce::String::charToString(
            static_cast<juce::juce_wchar>(std::toupper(code))); // not-ui-text: single-character key cap legend
    return key.getTextDescription();
}

} // namespace

juce::String formatKeyCapText(const juce::KeyPress& key, bool macGlyphs) {
    if (!key.isValid())
        return {};

    const auto mods = key.getModifiers();
    juce::String text;
    if (macGlyphs) {
        // Mac order: control, option, shift, command.
        // Off the Mac the control and command flags are the same bit, so only the command glyph applies.
        if (mods.isCtrlDown() && (kIsMac || !mods.isCommandDown()))
            text += juce::String(juce::CharPointer_UTF8("\xe2\x8c\x83"));
        if (mods.isAltDown())
            text += juce::String(juce::CharPointer_UTF8("\xe2\x8c\xa5"));
        if (mods.isShiftDown())
            text += juce::String(juce::CharPointer_UTF8("\xe2\x87\xa7"));
        if (mods.isCommandDown())
            text += juce::String(juce::CharPointer_UTF8("\xe2\x8c\x98"));
        return text + keyName(key, true);
    }

    if (mods.isCtrlDown() || mods.isCommandDown())
        text += "Ctrl+";
    if (mods.isAltDown())
        text += "Alt+";
    if (mods.isShiftDown())
        text += "Shift+";
    return text + keyName(key, false);
}

juce::String formatKeyCapTextForPlatform(const juce::KeyPress& key) {
#if JUCE_MAC
    return formatKeyCapText(key, true);
#else
    return formatKeyCapText(key, false);
#endif
}

} // namespace synth::ui::hint
