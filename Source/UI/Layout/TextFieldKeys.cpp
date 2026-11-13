#include "UI/Layout/TextFieldKeys.h"

#include <algorithm>

// Concern: the text-field editing keys juce::TextEditor does not provide (see the header).

namespace synth::ui {

namespace {
struct AppWideInstance : juce::DeletedAtShutdown {
    TextFieldKeys keys;
    static inline AppWideInstance* current = nullptr;
    AppWideInstance() { current = this; }
    ~AppWideInstance() override { current = nullptr; }
};
} // namespace

void TextFieldKeys::install() {
    if (AppWideInstance::current == nullptr)
        new AppWideInstance(); // owned by DeletedAtShutdown
}

TextFieldKeys::TextFieldKeys() { juce::Desktop::getInstance().addFocusChangeListener(this); }

TextFieldKeys::~TextFieldKeys() {
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    for (auto& component : attached_)
        if (component != nullptr)
            component->removeKeyListener(this);
}

bool TextFieldKeys::isListeningForTest(const juce::Component* component) const {
    return std::any_of(attached_.begin(), attached_.end(),
                       [component](const auto& entry) { return entry.getComponent() == component; });
}

void TextFieldKeys::globalFocusChanged(juce::Component* focused) {
    if (dynamic_cast<juce::TextEditor*>(focused) == nullptr || isListeningForTest(focused))
        return;
    focused->addKeyListener(this);
    attached_.emplace_back(focused);
    attached_.erase(std::remove_if(attached_.begin(), attached_.end(), [](const auto& e) { return e == nullptr; }),
                    attached_.end());
}

bool TextFieldKeys::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) {
    auto* editor = dynamic_cast<juce::TextEditor*>(originatingComponent);
    return editor != nullptr && handleKey(*editor, key);
}

bool TextFieldKeys::handleKey(juce::TextEditor& editor, const juce::KeyPress& key) {
#if JUCE_MAC
    const auto mods = key.getModifiers();
    if (editor.isReadOnly() || !key.isKeyCode(juce::KeyPress::backspaceKey) || !mods.isCommandDown() ||
        mods.isShiftDown() || mods.isAltDown() || mods.isCtrlDown())
        return false;

    auto region = editor.getHighlightedRegion();
    if (region.isEmpty()) {
        // To the start of the caret's line: after the previous newline, or the very start of a one-line field.
        const auto text = editor.getText();
        const int caret = editor.getCaretPosition();
        region = {text.substring(0, caret).lastIndexOfChar('\n') + 1, caret};
    }
    if (!region.isEmpty()) {
        editor.setHighlightedRegion(region);
        editor.cut();
    }
    return true; // consumed even with nothing to delete, so no shortcut on the key fires from a text field
#else
    juce::ignoreUnused(editor, key);
    return false;
#endif
}

} // namespace synth::ui
