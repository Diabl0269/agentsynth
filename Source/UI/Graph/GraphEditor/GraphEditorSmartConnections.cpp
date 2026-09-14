// GraphEditorSmartConnections.cpp
//
// GraphEditor's own smart-connection surface: one-line forwarders onto SmartConnectionEngine
// (Source/UI/Graph/SmartConnectionEngine/SmartConnectionEngine.{h,cpp}, which now owns the mode,
// the drag-tick insert-modifier re-sample, and the suggestion list), the DragPreviewState builder
// those forwarders share, and the couple of members that stayed on GraphEditor because they need
// nothing SmartConnectionEngine has that GraphEditor doesn't already have more directly
// (nodeHasCables, estimatePortCenter — both pure reads of audioEngine / a bounds rect, no drag or
// suggestion state, and estimatePortCenter is called statically by tests as
// GraphEditor::estimatePortCenter). GraphEditor is declared in GraphEditor.h.
//
// (Custom module titles, formerly also in this file, moved to GraphEditorModuleTitles.cpp —
// titles were never a smart-connections concern.)

#include "GraphEditor.h"

#include "Modules/MacroControlModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

GraphEditor::SmartConnectionMode GraphEditor::smartConnectionModeFromString(const juce::String& s) {
    return SmartConnectionEngine::smartConnectionModeFromString(s);
}

juce::String GraphEditor::smartConnectionModeToString(SmartConnectionMode mode) {
    return SmartConnectionEngine::smartConnectionModeToString(mode);
}

SmartConnectionEngine::DragPreviewState GraphEditor::buildDragPreviewState() const {
    SmartConnectionEngine::DragPreviewState state;
    state.active = dragPreviewActive;
    state.ghost = dragPreviewGhost;
    state.aim = dragPreviewAim;
    state.selfId = dragPreviewSelfId;
    state.isSnippet = dragPreviewIsSnippet;
    state.probe = dragPreviewProbe.get();
    // shouldOfferSmartConnections' selection-drag guard: selection itself is not on
    // GraphCanvasHost in this PR, so GraphEditor precomputes the one bit the engine needs from it.
    state.selectionDragBlocksSuggestions = selectionDragActive && selection.size() > 1;
    return state;
}

void GraphEditor::refreshSmartSuggestions() { smartConnections_.refreshSmartSuggestions(buildDragPreviewState()); }

void GraphEditor::applySmartSuggestions(juce::AudioProcessorGraph::NodeID ghostNodeId, bool recordUndo) {
    smartConnections_.applySmartSuggestions(ghostNodeId, recordUndo);
}

void GraphEditor::clearSmartSuggestions() { smartConnections_.clearSmartSuggestions(); }

void GraphEditor::refreshSuggestionsIfInsertModifierChanged() {
    smartConnections_.refreshSuggestionsIfInsertModifierChanged(buildDragPreviewState());
}

bool GraphEditor::nodeHasCables(juce::AudioProcessorGraph::NodeID nodeId) const {
    auto& graph = audioEngine.getGraph();
    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID == nodeId || c.destination.nodeID == nodeId)
            return true;
    }
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if ((r.hasSource && r.sourceNodeID == nodeId) || (r.hasDest && r.destNodeID == nodeId))
            return true;
    }
    return false;
}

juce::Point<int> GraphEditor::estimatePortCenter(juce::AudioProcessor* proc, juce::Rectangle<int> bounds, int jack,
                                                 bool isInput, bool isMidi) {
    if (proc == nullptr)
        return bounds.getCentre();

    if (isMidi) {
        if (isInput)
            return {bounds.getX() + 10, bounds.getY() + ModuleComponent::kPortGutterHeaderHeight};
        return {bounds.getRight() - 10, bounds.getY() + ModuleComponent::kPortGutterHeaderHeight};
    }

    const int yStep = 20;
    // MUST equal ModuleComponent::getPortCenter's headerHeight. It read 30 while the real card used
    // 38, so every ghost preview cable terminated 8px ABOVE the jack dot it claimed to land on —
    // visibly floating over the jack's label row. Pinned by
    // GraphEditorTest.GhostPortEstimateMatchesTheRealJackCentre.
    const int headerHeight = ModuleComponent::kPortGutterHeaderHeight;
    int portOffset = 0;
    if (proc->producesMidi())
        portOffset = 20;

    int visible = 0;
    if (auto* mb = dynamic_cast<ModuleBase*>(proc))
        visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
    else
        visible = isInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();

    const int clamped = (visible > 0) ? juce::jlimit(0, visible - 1, jack) : 0;

    if (auto* macro = dynamic_cast<MacroControlModule*>(proc)) {
        if (!isInput) {
            return {bounds.getRight() - 10, bounds.getY() + synth::LayoutUtil::macroRowCentreY(clamped)};
        }
    }

    if (isInput) {
        int columns = 1;
        if (auto* mb = dynamic_cast<ModuleBase*>(proc))
            if (mb->getVisibleInputPortCount() > 10 && bounds.getWidth() >= synth::LayoutUtil::kDoubleWidth)
                columns = 2;
        if (columns > 1 && visible > 0) {
            const int rows = (visible + columns - 1) / columns;
            const int col = clamped / rows;
            const int row = clamped % rows;
            return {bounds.getX() + 10 + col * 100, bounds.getY() + headerHeight + portOffset + row * yStep + 20};
        }
        return {bounds.getX() + 10, bounds.getY() + headerHeight + portOffset + clamped * yStep + 20};
    }
    return {bounds.getRight() - 10, bounds.getY() + headerHeight + portOffset + clamped * yStep + 20};
}
