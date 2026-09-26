// GraphEditorModHover.cpp -- FRO288: re-anchoring an AttenuverterChain cable's destination
// endpoint onto its target knob's modulation-ring start point (docs/layout/cables.md#knob-landing),
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

// For every AttenuverterChain cable, ask the destination card whether its logical destination
// channel (cable.destChannel, a RAW channel) resolves to a bound, visible knob; if so, re-anchor
// cable.p2 onto that knob's ring-start point (canvas space) and mark landsOnKnob so paint can draw
// the landing dot (GraphEditorCables.cpp's paintOverChildren). DirectCV/PolyBus cables and a
// hidden-page knob are left alone -- they keep the gutter jack pass 1/2 already gave them.
void GraphEditor::reanchorCablesToKnobTargets(std::vector<VisibleCable>& cables) {
    for (auto& cable : cables) {
        if (cable.kind != VisibleCable::Kind::AttenuverterChain)
            continue;
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
