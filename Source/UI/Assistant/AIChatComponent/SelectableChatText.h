#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

// The text of one chat message: a read-only, transparent, wrapping text editor sized by its owner to the whole text, so
// the mouse selects it, Cmd+C copies it, Cmd+A selects all of it and a right click offers Copy and Select all. It has
// no border or indent, so its wrapping matches AIChatComponent::computeWrappedTextHeight at the width less the 2 px
// the editor keeps free at its right edge.
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
        // The editor draws no outline of its own: the message bubble draws the one focus ring, and only after keyboard
        // focus (a click to select text is not a reason for a box). Two rings were painted before.
        getProperties().set("bareTextEditor", true); // AppLookAndFeel::kBareTextEditorProperty, kept out of this header
    }

    // A text that is not a Tab stop (a plan card's text: Tab goes through its buttons) is still click-focusable, so a
    // drag selects and Cmd+C copies: JUCE only grabs focus for a component that wants it, so mouseDown lends it that
    // for the grab and takes it back; the focus stays until it moves elsewhere.
    void setTabStop(bool isTabStop) {
        tabStop_ = isTabStop;
        setWantsKeyboardFocus(isTabStop);
    }

    // True when the focus it holds came from Tab or the arrow keys, not from a click.
    bool focusedByKeyboard() const { return focusByKeyboard_; }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!tabStop_ && !hasKeyboardFocus(false)) {
            clickGrab_ = true;
            setWantsKeyboardFocus(true);
            grabKeyboardFocus();
            setWantsKeyboardFocus(false);
            clickGrab_ = false;
        }
        juce::TextEditor::mouseDown(e);
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
        focusByKeyboard_ = !clickGrab_ && type != focusChangedByMouseClick;
        if (onFocusChanged)
            onFocusChanged();
    }

    void focusLost(FocusChangeType type) override {
        juce::TextEditor::focusLost(type);
        focusByKeyboard_ = false;
        if (onFocusChanged)
            onFocusChanged();
    }

private:
    bool tabStop_ = true;
    bool clickGrab_ = false;
    bool focusByKeyboard_ = false;
};

} // namespace synth
