// CardLayoutOnCardEditorTestSeams.cpp -- headless test seams for the on-card editor: each reads back or
// drives the real state, never a copy of it.
#include "CardLayoutOnCardEditor.h"

namespace synth::ui {

juce::Rectangle<int> CardLayoutOnCardEditor::getCellRectForTest(const juce::String& paramId) const {
    const int cell = indexOfCell(paramId);
    return cell >= 0 ? cells_[(size_t)cell].rect : juce::Rectangle<int>();
}

juce::Rectangle<int> CardLayoutOnCardEditor::getHomeRectForTest(const juce::String& paramId) const {
    const int cell = indexOfCell(paramId);
    return cell >= 0 ? homeRectOf(cell) : juce::Rectangle<int>();
}

bool CardLayoutOnCardEditor::sendEscapeToDragForTest() {
    return escapeKey_.isArmed() && escapeKey_.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), nullptr);
}

void CardLayoutOnCardEditor::finishMotionForTest() {
    if (finishAddFade_)
        std::exchange(finishAddFade_, nullptr)();
    ghostPump_.stop();
    ghosts_.clear();
    flushPendingHide(); // the picture has gone: the hide is written, and the gap it left may glide shut
    if (finishGlide_)
        std::exchange(finishGlide_, nullptr)();
}

void CardLayoutOnCardEditor::setShrinkGhostProgressForTest(float t) {
    for (auto* ghost : ghosts_)
        ghost->setProgress(t);
}

void CardLayoutOnCardEditor::applyAddFrameForTest(float t) {
    if (addFrame_)
        addFrame_(t);
}

} // namespace synth::ui
