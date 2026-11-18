#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

// The text of one chat message: a read-only, transparent, wrapping text editor sized by its owner to the whole text, so
// the mouse selects it, Cmd+C copies it, Cmd+A selects all of it and a right click offers Copy and Select all. It has
// no border or indent, so its wrapping matches AIChatComponent::computeWrappedTextHeight at the same width.
class SelectableChatText : public juce::TextEditor {
public:
    SelectableChatText() {
        setMultiLine(true, true);
        setReadOnly(true);
        setScrollbarsShown(false);
        setCaretVisible(false);
        setPopupMenuEnabled(true);
        setSelectAllWhenFocused(false);
        setIndents(0, 0);
        setBorder(juce::BorderSize<int>(0));
        setLineSpacing(1.0f);
        setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    }

    // Replaces the text with `text` in `font` and `colour`, keeping no selection.
    void setMessage(const juce::String& text, const juce::Font& font, juce::Colour colour) {
        setFont(font);
        setText(text, juce::dontSendNotification);
        setTextColour(colour);
    }

    void setTextColour(juce::Colour colour) {
        setColour(juce::TextEditor::textColourId, colour);
        applyColourToAllText(colour, false);
    }

    // Cmd+C with nothing selected copies the whole message, so a keyboard user who Tabs to a message can copy it.
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0) && getHighlightedRegion().isEmpty()) {
            juce::SystemClipboard::copyTextToClipboard(getText());
            return true;
        }
        return juce::TextEditor::keyPressed(key);
    }

    // Called when keyboard focus enters or leaves, so the owner can draw its focus ring.
    std::function<void()> onFocusChanged;

    void focusGained(FocusChangeType type) override {
        juce::TextEditor::focusGained(type);
        if (onFocusChanged)
            onFocusChanged();
    }

    void focusLost(FocusChangeType type) override {
        juce::TextEditor::focusLost(type);
        if (onFocusChanged)
            onFocusChanged();
    }
};

} // namespace synth
