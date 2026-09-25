// GraphEditorModAmount.cpp -- the shared mod-amount drag gesture (FRO287): adjusting an
// attenuverter's "amount" param from a mouse drag, whether that drag started on the cable
// midpoint knob (GraphEditorCanvas.cpp) or on a card knob's ring annulus / Alt-drag
// (ModuleComponent's CardKnobSlider, see CardKnobSlider.h). GraphEditor is declared in
// GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "Modules/AttenuverterModule.h"

// Captures the undo snapshot BEFORE any adjustment lands, mirroring every other drag gesture in
// this file (e.g. the macro chip drag's captureBeforeState in mouseDown) -- the whole point of
// "before" is that undo restores the value the knob had when the finger first touched it, not
// wherever the drag happened to start adjusting from.
void GraphEditor::beginModAmountGesture() {
    if (undoManager)
        undoManager->captureBeforeState(audioEngine.getGraph());
}

// One adjust step: read-modify-write the attenuverter's "amount" (-1..1), same increment/clamp the
// cable midpoint knob has always used. A vanished node (the routing was deleted mid-drag) is a
// silent no-op -- there is nothing left to adjust and nothing left to repaint.
void GraphEditor::adjustModAmount(juce::AudioProcessorGraph::NodeID attenuverterNodeID, float delta) {
    auto& graph = audioEngine.getGraph();
    auto* node = graph.getNodeForId(attenuverterNodeID);
    if (node == nullptr)
        return;
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"));
    if (p == nullptr)
        return;
    const float clamped = juce::jlimit(-1.0f, 1.0f, p->get() + delta);
    p->setValueNotifyingHost(p->convertTo0to1(clamped));
    // The card knob's own value never moves during this gesture, but its depth band DOES (the
    // amount just changed) -- repaintCanvas() covers both the midpoint knob and every card.
    repaintCanvas();
}

void GraphEditor::commitModAmountGesture() {
    if (undoManager)
        undoManager->pushSnapshotFromCapture(audioEngine.getGraph());
}
