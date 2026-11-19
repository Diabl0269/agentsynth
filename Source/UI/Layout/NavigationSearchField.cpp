// NavigationSearchField.cpp -- the pointer-state read of NavigationSearchField.h, out of the header so it does not
// pull in PointerState.h.
#include "UI/Layout/NavigationSearchField.h"
#include "UI/Layout/PointerState.h"

namespace synth::ui {

void NavigationSearchField::insertTextAtCaret(const juce::String& text) {
    if (onShiftSpaceTyped && text == " " && currentModifiers().isShiftDown() && onShiftSpaceTyped())
        return;
    juce::TextEditor::insertTextAtCaret(text);
}

} // namespace synth::ui
