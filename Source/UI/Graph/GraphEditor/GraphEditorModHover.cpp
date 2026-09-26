// GraphEditorModHover.cpp -- FRO288/FRO312: re-anchoring a cable's destination endpoint onto its
// target knob's modulation-ring landing point (docs/layout/cables.md#knob-landing),
// and the two-directional hover correlation between a cable and the knob it lands on
// (docs/modules/modulation.md#modulation-rings-on-knobs). GraphEditor is declared in GraphEditor.h;
// sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"

#include "UI/Graph/ModuleComponent/ModuleComponent.h"

// Shared by the re-anchor pass and the hover setter below: the ModuleComponent hosting a given
// graph node, or nullptr if none exists right now (a routing to a node whose card hasn't been
// created/synced yet -- transient during a graph rebuild). O(nodes * components); only called from
// the memoized cable rebuild and from a hover change, never per-paint-frame.
ModuleComponent* GraphEditor::moduleComponentForNode(juce::AudioProcessorGraph::NodeID id) {
    auto& graph = audioEngine.getGraph();
    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        for (auto* node : graph.getNodes()) {
            if (node->nodeID == id && node->getProcessor() == comp->getModule())
                return comp;
        }
    }
    return nullptr;
}

// For every cable (FRO312: every KIND now, not just AttenuverterChain -- once a knob-bound jack's
// gutter dot is hidden, ANY routing landing on it -- DirectCV, PolyBus or AttenuverterChain --
// lands on the knob, since there is no gutter position left for it to draw at; portPos
// (GraphEditorCables.cpp) already resolves this through ModuleComponent::getPortCenter's own
// knob-redirect, so cable.p2 is normally already correct by the time this runs), ask the
// destination card whether its logical destination channel (cable.destChannel, a RAW channel)
// resolves to a bound, visible knob; if so, re-anchor cable.p2 onto that knob's ring-landing point
// (canvas space, defensively recomputed here too -- cheap, and the one source of truth either way)
// and mark landsOnKnob so paint can draw the landing dot (GraphEditorCables.cpp's
// paintOverChildren). A hidden-page knob (sliderIndexForModTarget says -1) is left alone -- it
// keeps the gutter jack pass 1/2 already gave it, exactly as before.
void GraphEditor::reanchorCablesToKnobTargets(std::vector<VisibleCable>& cables) {
    for (auto& cable : cables) {
        auto* dstComp = moduleComponentForNode(juce::AudioProcessorGraph::NodeID{cable.destNodeId});
        if (dstComp == nullptr)
            continue;
        auto anchor = dstComp->getModTargetKnobAnchor(cable.destChannel);
        if (!anchor.has_value())
            continue;
        cable.p2 = dstComp->getBounds().getPosition().toFloat() + *anchor;
        cable.landsOnKnob = true;
    }
}

// Sets/clears the shared hover-correlation target (see the HoveredModTarget doc comment in
// GraphEditor.h) and repaints exactly the card(s) whose ring treatment that changes -- the OLD
// target's card (if the highlight is going away or moving elsewhere) and the NEW one -- never the
// whole canvas. repaintCanvas() still runs afterwards for the cable side of the correlation (the
// cable paint loop reads getHoveredModTarget() every frame, same as hoveredCableId).
void GraphEditor::setHoveredModTarget(std::optional<HoveredModTarget> target) {
    if (hoveredModTarget_ == target)
        return;

    auto repaintTargetCard = [this](const std::optional<HoveredModTarget>& t) {
        if (!t.has_value())
            return;
        if (auto* comp = moduleComponentForNode(t->nodeId))
            comp->repaint();
    };
    repaintTargetCard(hoveredModTarget_);
    hoveredModTarget_ = target;
    repaintTargetCard(hoveredModTarget_);

    repaintCanvas();
}

bool GraphEditor::isCableHovered(const VisibleCable& cable) const {
    if (hoveredCableId.has_value() && *hoveredCableId == cable.id)
        return true;
    return cable.landsOnKnob && hoveredModTarget_.has_value() &&
           hoveredModTarget_->nodeId == juce::AudioProcessorGraph::NodeID{cable.destNodeId} &&
           hoveredModTarget_->channel == cable.destChannel;
}
