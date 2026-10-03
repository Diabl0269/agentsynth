// CardLayoutOnCardEditorTestSeams.cpp -- headless test seams for the on-card editor: each reads back or
// drives the real state, never a copy of it.
#include "CardLayoutOnCardEditor.h"

namespace synth::ui {

juce::Rectangle<int> CardLayoutOnCardEditor::getCellRectForTest(const juce::String& paramId) const {
    const int cell = indexOfCell(paramId);
    return cell >= 0 ? cells_[(size_t)cell].rect : juce::Rectangle<int>();
}

bool CardLayoutOnCardEditor::sendEscapeToDragForTest() {
    return escapeKey_.isArmed() && escapeKey_.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), nullptr);
}

void CardLayoutOnCardEditor::finishMotionForTest() {
    if (finishGlide_)
        std::exchange(finishGlide_, nullptr)();
    if (finishAddFade_)
        std::exchange(finishAddFade_, nullptr)();
}

} // namespace synth::ui
