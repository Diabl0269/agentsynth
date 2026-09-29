// Concern: the text a shortcut key cap shows for a KeyPress.
#include "UI/Chrome/ShortcutHint/ShortcutHintText.h"
#include <gtest/gtest.h>

namespace {
using synth::ui::hint::formatKeyCapText;
constexpr auto kCmd = juce::ModifierKeys::commandModifier;
constexpr auto kShift = juce::ModifierKeys::shiftModifier;
constexpr auto kAlt = juce::ModifierKeys::altModifier;

juce::String utf8(const char* bytes) { return juce::String::fromUTF8(bytes); }
} // namespace

TEST(ShortcutHintText, MacGlyphsUseMacOrderAndUppercaseTheKey) {
    EXPECT_EQ(formatKeyCapText(juce::KeyPress('t', kCmd, 0), true), utf8("\xe2\x8c\x98") + "T");
    EXPECT_EQ(formatKeyCapText(juce::KeyPress('z', kCmd | kShift, 0), true),
              utf8("\xe2\x87\xa7") + utf8("\xe2\x8c\x98") + "Z");
    EXPECT_EQ(formatKeyCapText(juce::KeyPress('s', kCmd | kAlt, 0), true),
              utf8("\xe2\x8c\xa5") + utf8("\xe2\x8c\x98") + "S");
    EXPECT_EQ(formatKeyCapText(juce::KeyPress('1', kCmd, 0), true), utf8("\xe2\x8c\x98") + "1");
}

TEST(ShortcutHintText, PlainStyleSpellsModifiersOut) {
    EXPECT_EQ(formatKeyCapText(juce::KeyPress('t', kCmd, 0), false), "Ctrl+T");
    EXPECT_EQ(formatKeyCapText(juce::KeyPress('z', kCmd | kShift, 0), false), "Ctrl+Shift+Z");
}

TEST(ShortcutHintText, NamedKeysAndFunctionKeys) {
    EXPECT_EQ(formatKeyCapText(juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys(), 0), true), "Space");
    EXPECT_EQ(formatKeyCapText(juce::KeyPress(juce::KeyPress::F5Key, juce::ModifierKeys(), 0), true), "F5");
    EXPECT_EQ(formatKeyCapText(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys(), 0), false), "Left");
    EXPECT_EQ(formatKeyCapText(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys(), 0), true),
              utf8("\xe2\x86\x90"));
}

TEST(ShortcutHintText, InvalidKeyGivesNothing) { EXPECT_TRUE(formatKeyCapText(juce::KeyPress(), true).isEmpty()); }
