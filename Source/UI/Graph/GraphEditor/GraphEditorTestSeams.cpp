// GraphEditorTestSeams.cpp
//
// Out-of-line bodies of GraphEditor's test seams. Production code never calls these; they live
// here so the header stays a declaration list.

#include "GraphEditor.h"

void GraphEditor::setShowCanvasContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
    showCanvasContextMenuHook_ = std::move(hook);
}

// Lets a test exercise a press/release that happens without any mouse movement, without needing a
// real 30 Hz timer.
void GraphEditor::pumpDragModifierTickForTests() {
    smartConnections_.refreshSuggestionsIfInsertModifierChanged(dragDropController_.buildDragPreviewState());
}

// For a test driving a SmartConnectionEngine of its own directly against the private base.
GraphCanvasHost& GraphEditor::getCanvasHostForTest() { return *this; }

// The VBlank driver doesn't tick headless, so a test ends the gesture as the settle timer would.
void GraphEditor::settleZoomNowForTest() { endZoomGesture(); }

// Same calls, including the repaint that invalidates the cable memo, that the real driver's
// onUpdate/onComplete make each frame.
void GraphEditor::advanceMacroCrossingAnimForTest(float t) {
    macroCrossingAnim_.applyTweenAt(t);
    repaintCanvas();
}

void GraphEditor::finishMacroCrossingAnimForTest() {
    macroCrossingAnim_.finish();
    repaintCanvas();
}

void GraphEditor::advanceCardGlideForTest(float t) {
    cardGlide_.applyTweenAt(t);
    repaintCanvas();
}

void GraphEditor::finishCardGlideForTest() {
    cardGlide_.finish();
    repaintCanvas();
}
