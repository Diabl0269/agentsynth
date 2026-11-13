#pragma once

// Real-gesture helpers for dragging a whole macro (MacroDropIntoMacroTests.cpp, MacroLeaveDragTests.cpp,
// MacroDragMatrixTests.cpp): a folded card dragged by its body through MacroCardComponent's own mouse callbacks, and an
// open macro dragged by its name chip through GraphEditor's. Header-only; not compiled on its own.

#include "MacroContainerTestHelpers.h"

#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <functional>
#include <gtest/gtest.h>

namespace {

juce::MouseEvent cardEvent(juce::Component& card, juce::Point<int> position, juce::Point<int> downPosition,
                           bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &card, &card, juce::Time::getCurrentTime(), downPosition.toFloat(),
                            juce::Time::getCurrentTime(), 1, dragged);
}

juce::MouseEvent canvasEvent(juce::Component& editor, juce::Point<int> position) {
    const auto pos = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &editor, &editor, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1,
                            false);
}

// Drags `macroId`'s folded card by its body so the card's centre lands on `centre`; `midDrag` runs before the release.
void dragCardCentreTo(GraphEditor& editor, const juce::String& macroId, juce::Point<int> centre,
                      const std::function<void()>& midDrag = {}) {
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const juce::Point<int> press(140, 60); // the body, below the title row
    const auto delta = centre - card->getBounds().getCentre();
    card->mouseDown(cardEvent(*card, press, press, false));
    card->mouseDrag(cardEvent(*card, press + delta, press, true));
    if (midDrag)
        midDrag();
    card->mouseUp(cardEvent(*card, press + delta, press, true));
}

// Drags an open macro by its name chip, moving the pointer by `delta` (canvas == editor coordinates: no zoom).
void dragChipBy(GraphEditor& editor, const juce::String& macroId, juce::Point<int> delta,
                const std::function<void()>& midDrag = {}) {
    const auto press = editor.getMacroController().macroChipBounds(macroId).getCentre();
    editor.mouseDown(canvasEvent(editor, press));
    editor.mouseDrag(canvasEvent(editor, press + delta));
    if (midDrag)
        midDrag();
    editor.mouseUp(canvasEvent(editor, press + delta));
}

// Drags an open macro by its name chip so the POINTER lands on `point` (the chip drag's targets follow the pointer).
void dragChipTo(GraphEditor& editor, const juce::String& macroId, juce::Point<int> point,
                const std::function<void()>& midDrag = {}) {
    dragChipBy(editor, macroId, point - editor.getMacroController().macroChipBounds(macroId).getCentre(), midDrag);
}

} // namespace
