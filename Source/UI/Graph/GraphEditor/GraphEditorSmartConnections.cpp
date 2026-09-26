// GraphEditorSmartConnections.cpp
//
// GraphEditor's own smart-connection surface. FRO254: the plain one-line forwarders onto
// SmartConnectionEngine (Source/UI/Graph/SmartConnectionEngine/SmartConnectionEngine.{h,cpp})
// are gone now that every call site reaches it directly, either through
// GraphEditor::getSmartConnections() (external callers) or the smartConnections_ member itself
// (GraphEditor's own other .cpp files). What's left: the three GraphCanvasHost pure-virtual
// overrides below (refreshSmartSuggestions/clearSmartSuggestions/seedInsertModifierSample), which
// stay on GraphEditor because GraphDragDropController calls them polymorphically through the
// host interface, and
// the couple of members that stayed on GraphEditor because they need nothing
// SmartConnectionEngine has that GraphEditor doesn't already have more directly (nodeHasCables,
// estimatePortCenter — both pure reads of audioEngine / a bounds rect, no drag or suggestion
// state, and estimatePortCenter is called statically by tests as GraphEditor::estimatePortCenter).
// GraphEditor is declared in GraphEditor.h.
//
// (Custom module titles, formerly also in this file, moved to GraphEditorModuleTitles.cpp —
// titles were never a smart-connections concern.)

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"

#include "Modules/MacroControlModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

GraphEditor::SmartConnectionMode GraphEditor::smartConnectionModeFromString(const juce::String& s) {
    return SmartConnectionEngine::smartConnectionModeFromString(s);
}

juce::String GraphEditor::smartConnectionModeToString(SmartConnectionMode mode) {
    return SmartConnectionEngine::smartConnectionModeToString(mode);
}

// GraphCanvasHost pure-virtual override — GraphDragDropController calls this polymorphically
// through the host interface, so it can't move onto SmartConnectionEngine even though every
// other forwarder in this file already has (FRO254).
void GraphEditor::refreshSmartSuggestions() {
    smartConnections_.refreshSmartSuggestions(dragDropController_.buildDragPreviewState());
}

// GraphCanvasHost pure-virtual override — see refreshSmartSuggestions() above.
void GraphEditor::clearSmartSuggestions() { smartConnections_.clearSmartSuggestions(); }

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

// GraphCanvasHost pure-virtual override — see refreshSmartSuggestions() above.
void GraphEditor::seedInsertModifierSample() { smartConnections_.seedInsertModifierSample(); }

// FRO312: the drop-in-a-live-component check ModuleComponent::isInputJackKnobBound makes (a
// ModulationTarget resolving to a VISIBLE slider) isn't available for a ghost preview, which has
// no live component -- there is no tab-page/poly-visibility state to ask. "Has a bound parameter
// at all" (parameterForModTarget != nullptr) is the same proxy GraphEditor::estimateModuleSize
// uses for the same reason: it agrees with the live check for every module in practice (the only
// divergence is a knob on a currently-hidden tab page, where the ghost estimate is very slightly
// optimistic about how much the column has packed) and needs nothing but the processor itself.
static bool jackIsKnobBoundForEstimate(ModuleBase* mb, int visibleIndex) {
    for (const auto& t : mb->getModulationTargets())
        if (mb->mapInputChannel(t.channelIndex).visibleJackIndex == visibleIndex &&
            mb->parameterForModTarget(t) != nullptr)
            return true;
    return false;
}

// Port centre inside a bounds rect — must agree with ModuleComponent::getPortCenter (this ghost
// preview ages the real thing before a component exists, so the two can never fully share code,
// but every rule getPortCenter follows this mirrors, including FRO312's knob-jack packing).
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

    auto* mb = dynamic_cast<ModuleBase*>(proc);
    int visible = 0;
    if (mb != nullptr)
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
        // FRO312: a knob-bound jack draws no gutter dot -- its ghost preview cable lands on the
        // (not-yet-real) knob's own estimated position instead. Since there is no live slider to
        // ask, this falls back to the ring geometry directly: a default-radius rotary knob sits in
        // the standard 3-per-row body grid, which for a drag ghost is close enough (the exact spot
        // only matters once the drop lands and the real component measures itself).
        if (mb != nullptr && jackIsKnobBoundForEstimate(mb, clamped))
            return bounds.getCentre(); // approximate -- a real card resolves the exact knob anchor

        int packedIndex = 0;
        int drawnCount = 0;
        if (mb != nullptr) {
            for (int i = 0; i < visible; ++i) {
                const bool bound = jackIsKnobBoundForEstimate(mb, i);
                if (!bound) {
                    if (i == clamped)
                        packedIndex = drawnCount;
                    ++drawnCount;
                }
            }
        } else {
            packedIndex = clamped;
            drawnCount = visible;
        }

        int columns = 1;
        if (drawnCount > 10 && bounds.getWidth() >= synth::LayoutUtil::kDoubleWidth)
            columns = 2;
        if (columns > 1 && drawnCount > 0) {
            const int rows = (drawnCount + columns - 1) / columns;
            const int col = packedIndex / rows;
            const int row = packedIndex % rows;
            return {bounds.getX() + 10 + col * 100, bounds.getY() + headerHeight + portOffset + row * yStep + 20};
        }
        return {bounds.getX() + 10, bounds.getY() + headerHeight + portOffset + packedIndex * yStep + 20};
    }
    return {bounds.getRight() - 10, bounds.getY() + headerHeight + portOffset + clamped * yStep + 20};
}
