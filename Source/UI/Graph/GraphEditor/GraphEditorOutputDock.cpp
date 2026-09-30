// GraphEditorOutputDock.cpp
//
// The "output dock": Master (once any mixer channel exists), Rec Tap (if the project has one) and Audio Output,
// left to right in chain order, always the rightmost cards on the canvas. Their x is DERIVED here, never user-set:
// right of everything else (LayoutUtil::computeOutputDock). Their shared top y is the Audio Output node's own stored
// "y" -- no extra persisted field -- and a vertical drag of any dock card moves them all together.
// GraphEditor is declared in GraphEditor.h. (docs/layout/layout.md#output-dock)

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"

#include "Mixer/MasterSplice.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"

namespace {
juce::String nodeKey(juce::AudioProcessorGraph::NodeID id) { return "n:" + juce::String((juce::int64)id.uid); }
} // namespace

bool GraphEditor::isOutputDockNode(juce::AudioProcessorGraph::NodeID nodeId) const {
    auto* node = audioEngine.getGraph().getNodeForId(nodeId);
    return node != nullptr && synth::isOutputDockProcessor(node->getProcessor());
}

// One function owns every dock position. Node x/y AND the live component bounds are written synchronously: macro
// hulls and cables read live bounds, so nothing may see a stale dock. Never opens an undo record and never marks the
// project dirty (dirtiness follows the undo edit serial), so it is safe on load; inside a caller's undo record the
// positions simply become part of that record.
void GraphEditor::reflowOutputDock() {
    auto& graph = audioEngine.getGraph();
    const auto nodes = synth::outputDockNodes(graph);
    if (nodes.empty())
        return;

    // Shared top y: Audio Output's own stored y (the last dock node); the seed when Master first appears is that
    // same y, because Master joins the dock on it.
    auto* anchor = nodes.back();
    const int topY = static_cast<int>(anchor->properties.getWithDefault("y", synth::LayoutUtil::kArrangeOriginY));

    std::vector<juce::String> dockKeys;
    std::vector<juce::Point<int>> sizes;
    for (auto* node : nodes) {
        dockKeys.push_back(nodeKey(node->nodeID));
        auto* comp = moduleComponentFor(node->nodeID);
        sizes.push_back(comp != nullptr ? juce::Point<int>(comp->getWidth(), comp->getHeight())
                                        : juce::Point<int>(synth::LayoutUtil::kSingleWidth, 0));
    }

    std::vector<synth::LayoutUtil::LayoutUnit> content;
    for (auto& unit : macroController_.buildLayoutUnits({}))
        if (std::find(dockKeys.begin(), dockKeys.end(), unit.key) == dockKeys.end())
            content.push_back(std::move(unit));

    const auto positions = synth::LayoutUtil::computeOutputDock(content, sizes, topY);

    bool moved = false;
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto* node = nodes[i];
        const juce::Point<int> current(static_cast<int>(node->properties.getWithDefault("x", -1)),
                                       static_cast<int>(node->properties.getWithDefault("y", -1)));
        auto* comp = moduleComponentFor(node->nodeID);
        if (current == positions[i] && (comp == nullptr || comp->getPosition() == positions[i]))
            continue;
        node->properties.set("x", positions[i].x);
        node->properties.set("y", positions[i].y);
        if (comp != nullptr)
            comp->setTopLeftPosition(positions[i]);
        moved = true;
    }

    if (moved)
        repaintCanvas(); // drops the cable memo: the dock's cables just changed length
}

// Live half of a vertical dock drag: the grabbed card has already been constrained to its own x, so every other dock
// card just takes its y (they all share one top y).
void GraphEditor::carryOutputDockWith(ModuleComponent* initiator) {
    if (initiator == nullptr)
        return;
    for (auto* node : synth::outputDockNodes(audioEngine.getGraph()))
        if (node->nodeID != initiator->getNodeId())
            if (auto* comp = moduleComponentFor(node->nodeID))
                comp->setTopLeftPosition(comp->getX(), initiator->getY());
}

// Release half: y snapped once for the whole dock, written to every dock node, then x re-derived. No de-overlap
// push beyond what makeRoomFor already provides -- a dock card is a pinned layout unit, nothing pushes it.
void GraphEditor::finalizeOutputDockDrag(ModuleComponent* module) {
    if (module == nullptr)
        return;
    const int snappedY = synth::LayoutUtil::snap(module->getY());
    for (auto* node : synth::outputDockNodes(audioEngine.getGraph())) {
        node->properties.set("y", snappedY);
        if (auto* comp = moduleComponentFor(node->nodeID))
            comp->setTopLeftPosition(comp->getX(), snappedY);
    }
    reflowOutputDock();
    repaintCanvas();
}

juce::String GraphEditor::outputDockDeleteRefusal(juce::AudioProcessorGraph::NodeID nodeId) const {
    return synth::outputDockDeleteRefusal(audioEngine.getGraph(), nodeId);
}

void GraphEditor::removeUndeletableOutputNodes(std::vector<juce::AudioProcessorGraph::NodeID>& ids) {
    juce::String refusal;
    ids.erase(std::remove_if(ids.begin(), ids.end(),
                             [&](juce::AudioProcessorGraph::NodeID id) {
                                 const auto why = outputDockDeleteRefusal(id);
                                 if (why.isEmpty())
                                     return false;
                                 refusal = why;
                                 return true;
                             }),
              ids.end());
    if (refusal.isNotEmpty() && onStatusMessage)
        onStatusMessage(refusal);
}

// The dock is owned by reflowOutputDock, so a dock card is never a macro member. Runs on every reconcile, which is
// what cleans a saved project that already has one; it edits the MacroSet only, so it opens no undo step.
void GraphEditor::evictOutputDockFromMacros() {
    for (auto* node : synth::outputDockNodes(audioEngine.getGraph())) {
        const auto uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty() && macros.findByMember(uuid) != nullptr)
            macros.removeMemberEverywhere(uuid);
    }
}

// "Go to Output": centres the view on the whole dock, not one card.
void GraphEditor::frameOutputDock() {
    juce::Rectangle<int> frame;
    for (auto* node : synth::outputDockNodes(audioEngine.getGraph()))
        if (auto* comp = moduleComponentFor(node->nodeID))
            frame = frame.isEmpty() ? comp->getBounds() : frame.getUnion(comp->getBounds());
    if (!frame.isEmpty())
        centreViewOn(frame.toFloat().getCentre());
}
