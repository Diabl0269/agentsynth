#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

// Gives every text field in the app the Mac editing key juce::TextEditor lacks: Cmd+Backspace deletes
// everything left of the caret back to the start of the line (or just the selection when there is one).
// Option+Backspace (a word) and Ctrl+Backspace on Windows/Linux (a word) are already juce::TextEditor's own.
//
// One instance watches Desktop focus: whenever a juce::TextEditor takes focus the instance listens on it,
// ahead of the field's own key handling, so no field needs a subclass and fields added later are covered
// without a call. A key listener also runs before the app's shortcut map further up the parent chain, so a
// shortcut on Cmd+Backspace (delete the selected track or module) never fires while typing.
class TextFieldKeys
    : public juce::KeyListener
    , private juce::FocusChangeListener {
public:
    TextFieldKeys();
    ~TextFieldKeys() override;

    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    // Starts the one app-wide instance (idempotent). It is deleted with the rest of JUCE's GUI at shutdown.
    static void install();

    // Applies the key to `editor` when it is Cmd+Backspace (macOS only) on an editable field; answers true
    // when it did. Public so a test can drive it without a window.
    static bool handleKey(juce::TextEditor& editor, const juce::KeyPress& key);

    // Delivers the focus-gained callback a real window would (a headless window cannot hold focus).
    void focusGainedForTest(juce::Component* focused) { globalFocusChanged(focused); }

    // Whether this instance is listening on `component`.
    bool isListeningForTest(const juce::Component* component) const;

private:
    void globalFocusChanged(juce::Component* focused) override;

    std::vector<juce::Component::SafePointer<juce::Component>> attached_;
};

} // namespace synth::ui
