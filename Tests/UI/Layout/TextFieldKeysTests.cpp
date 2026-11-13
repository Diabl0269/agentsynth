// TextFieldKeysTests.cpp -- Cmd+Backspace deletes to the start of the line in every text field, and
// Option/Ctrl+Backspace still deletes a word. A headless window cannot hold keyboard focus, so focus
// gained is delivered through the listener's test seam; the key then travels as ComponentPeer does it
// (the field's key listeners, then its own keyPressed).
#include "UI/Layout/TextFieldKeys.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {
using synth::ui::TextFieldKeys;

juce::KeyPress backspace(int modifiers) {
    return juce::KeyPress(juce::KeyPress::backspaceKey, juce::ModifierKeys(modifiers), 0);
}

bool deliver(TextFieldKeys& keys, juce::TextEditor& editor, const juce::KeyPress& key) {
    if (keys.isListeningForTest(&editor) && keys.keyPressed(key, &editor))
        return true;
    return editor.keyPressed(key);
}

void typeAndPlaceCaretAtEnd(juce::TextEditor& editor, const juce::String& text) {
    editor.setText(text, false);
    editor.setCaretPosition(text.length());
}
} // namespace

TEST(TextFieldKeysTest, OnlyAFocusedTextFieldIsListenedTo) {
    TextFieldKeys keys;
    juce::TextEditor editor;
    juce::Component other;
    keys.focusGainedForTest(&other);
    keys.focusGainedForTest(nullptr);
    EXPECT_FALSE(keys.isListeningForTest(&other));
    keys.focusGainedForTest(&editor);
    keys.focusGainedForTest(&editor); // twice is harmless
    EXPECT_TRUE(keys.isListeningForTest(&editor));
}

TEST(TextFieldKeysTest, ListeningStopsWhenTheOwnerGoesFirstAndWhenTheFieldDoes) {
    auto editor = std::make_unique<juce::TextEditor>();
    {
        TextFieldKeys keys;
        keys.focusGainedForTest(editor.get());
    }
    editor->setText("still fine", false);
    {
        TextFieldKeys keys;
        auto gone = std::make_unique<juce::TextEditor>();
        keys.focusGainedForTest(gone.get());
        gone.reset();
        keys.focusGainedForTest(editor.get());
        EXPECT_TRUE(keys.isListeningForTest(editor.get()));
    }
}

#if JUCE_MAC
TEST(TextFieldKeysTest, CmdBackspaceDeletesEverythingLeftOfTheCaret) {
    TextFieldKeys keys;
    juce::TextEditor editor;
    typeAndPlaceCaretAtEnd(editor, "filter cut");
    keys.focusGainedForTest(&editor);

    EXPECT_TRUE(deliver(keys, editor, backspace(juce::ModifierKeys::commandModifier)));
    EXPECT_EQ(editor.getText(), "");
}

TEST(TextFieldKeysTest, CmdBackspaceKeepsTextRightOfTheCaret) {
    TextFieldKeys keys;
    juce::TextEditor editor;
    editor.setText("left right", false);
    editor.setCaretPosition(4);
    keys.focusGainedForTest(&editor);

    EXPECT_TRUE(deliver(keys, editor, backspace(juce::ModifierKeys::commandModifier)));
    EXPECT_EQ(editor.getText(), " right");
    EXPECT_EQ(editor.getCaretPosition(), 0);
}

TEST(TextFieldKeysTest, CmdBackspaceStopsAtTheStartOfTheCaretsLineInAMultiLineField) {
    juce::TextEditor editor;
    editor.setMultiLine(true);
    typeAndPlaceCaretAtEnd(editor, "first line\nsecond line");
    EXPECT_TRUE(TextFieldKeys::handleKey(editor, backspace(juce::ModifierKeys::commandModifier)));
    EXPECT_EQ(editor.getText(), "first line\n");
}

TEST(TextFieldKeysTest, CmdBackspaceWithASelectionDeletesJustTheSelection) {
    juce::TextEditor editor;
    editor.setText("keep this and that", false);
    editor.setHighlightedRegion({10, 18});
    EXPECT_TRUE(TextFieldKeys::handleKey(editor, backspace(juce::ModifierKeys::commandModifier)));
    EXPECT_EQ(editor.getText(), "keep this ");
}

TEST(TextFieldKeysTest, CmdBackspaceAtTheStartIsStillConsumed) {
    juce::TextEditor editor;
    editor.setText("abc", false);
    editor.setCaretPosition(0);
    EXPECT_TRUE(TextFieldKeys::handleKey(editor, backspace(juce::ModifierKeys::commandModifier)));
    EXPECT_EQ(editor.getText(), "abc");
}

TEST(TextFieldKeysTest, AReadOnlyFieldIsLeftAlone) {
    juce::TextEditor editor;
    editor.setText("read only", false);
    editor.setReadOnly(true);
    editor.setCaretPosition(9);
    EXPECT_FALSE(TextFieldKeys::handleKey(editor, backspace(juce::ModifierKeys::commandModifier)));
    EXPECT_EQ(editor.getText(), "read only");
}

TEST(TextFieldKeysTest, OtherBackspacesAreNotTaken) {
    juce::TextEditor editor;
    typeAndPlaceCaretAtEnd(editor, "one two");
    EXPECT_FALSE(TextFieldKeys::handleKey(editor, backspace(0)));
    EXPECT_FALSE(TextFieldKeys::handleKey(editor, backspace(juce::ModifierKeys::altModifier)));
    EXPECT_FALSE(TextFieldKeys::handleKey(
        editor, backspace(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)));
    EXPECT_FALSE(TextFieldKeys::handleKey(
        editor, juce::KeyPress('a', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 'a')));
}

TEST(TextFieldKeysTest, OptionBackspaceDeletesTheLastWord) {
    TextFieldKeys keys;
    juce::TextEditor editor;
    typeAndPlaceCaretAtEnd(editor, "two words");
    keys.focusGainedForTest(&editor);
    EXPECT_TRUE(deliver(keys, editor, backspace(juce::ModifierKeys::altModifier)));
    EXPECT_EQ(editor.getText(), "two ");
}
#else
TEST(TextFieldKeysTest, OtherPlatformsKeepTheirOwnWordDeleteAndTakeNoKey) {
    juce::TextEditor editor;
    typeAndPlaceCaretAtEnd(editor, "two words");
    EXPECT_FALSE(TextFieldKeys::handleKey(editor, backspace(juce::ModifierKeys::ctrlModifier)));
    EXPECT_TRUE(editor.keyPressed(backspace(juce::ModifierKeys::ctrlModifier)));
    EXPECT_EQ(editor.getText(), "two ");
}
#endif
