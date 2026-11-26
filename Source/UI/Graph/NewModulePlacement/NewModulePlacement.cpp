// NewModulePlacement.cpp -- see NewModulePlacement.h and docs/ai/timeline-ops.md#where-things-land.
#include "NewModulePlacement.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/PolyMidiModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/MacroGroupController/MacroNesting.h"
#include "UI/Graph/MacroGroupController/ModelCardBounds.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace synth {

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using Graph = juce::AudioProcessorGraph;

struct Edge {
    NodeID src, dst;
};

bool isAttenuverter(const Graph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr && dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr;
}

// Every audio/CV cable as source -> destination, with a modulation's attenuverter looked through (an LFO
// modulating a Filter is LFO -> Filter here). MIDI cables and anything touching the output dock are left out:
// the merge auto-wires MIDI from whichever source it finds first, and the dock is pinned right of everything.
std::vector<Edge> anchorEdges(GraphEditor& editor, const Graph& graph) {
    std::vector<Edge> edges;
    auto keep = [&](NodeID src, NodeID dst) {
        if (src != dst && !editor.isOutputDockNode(src) && !editor.isOutputDockNode(dst))
            edges.push_back({src, dst});
    };
    for (const auto& c : graph.getConnections()) {
        if (c.source.isMIDI() || isAttenuverter(graph, c.source.nodeID))
            continue;
        if (!isAttenuverter(graph, c.destination.nodeID)) {
            keep(c.source.nodeID, c.destination.nodeID);
            continue;
        }
        for (const auto& out : graph.getConnections())
            if (out.source.nodeID == c.destination.nodeID && !out.source.isMIDI())
                keep(c.source.nodeID, out.destination.nodeID);
    }
    return edges;
}

class Placer {
public:
    Placer(GraphEditor& editor, const std::vector<NodeID>& created)
        : editor_(editor)
        , graph_(editor.getAudioEngine().getGraph())
        , edges_(anchorEdges(editor, graph_)) {
        for (auto id : created)
            if (auto* node = graph_.getNodeForId(id); node != nullptr && needsPlacing(*node))
                pending_.push_back(id);
    }

    void run() {
        while (!pending_.empty()) {
            auto it = std::find_if(pending_.begin(), pending_.end(), [this](NodeID id) { return anchorFor(id); });
            if (it == pending_.end())
                it = pending_.begin();
            const NodeID id = *it;
            pending_.erase(it);
            place(id, anchorFor(id));
        }
        // Joins last: addSelectionToMacro rebuilds the cards, which would give a still-unplaced node a stride
        // fallback position. Each join makes room for the grown macro, and the cards it pushes glide.
        for (const auto& [macroId, uuids] : joins_)
            editor_.getMacroController().addSelectionToMacro(macroId, uuids, /*recordUndo=*/false);
    }

private:
    struct Anchor {
        NodeID node;
        bool leftOf = false;
    };

    bool needsPlacing(const Graph::Node& node) const {
        auto* processor = node.getProcessor();
        return processor != nullptr && dynamic_cast<AttenuverterModule*>(processor) == nullptr &&
               !editor_.isOutputDockNode(node.nodeID) && (int)node.properties.getWithDefault("x", -1) == -1;
    }

    bool isPending(NodeID id) const { return std::find(pending_.begin(), pending_.end(), id) != pending_.end(); }

    std::optional<Anchor> anchorFor(NodeID id) const {
        for (const auto& e : edges_)
            if (e.src == id && !isPending(e.dst) && !rectOf(e.dst).isEmpty())
                return Anchor{e.dst, true};
        for (const auto& e : edges_)
            if (e.dst == id && !isPending(e.src) && !rectOf(e.src).isEmpty())
                return Anchor{e.src, false};
        return std::nullopt;
    }

    juce::Rectangle<int> rectOf(NodeID id) const {
        for (auto* comp : editor_.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == id)
                return comp->getBounds();
        auto* node = graph_.getNodeForId(id);
        return node != nullptr ? modelCardBounds(*node, &GraphEditor::estimateModuleSize) : juce::Rectangle<int>();
    }

    juce::String uuidOf(NodeID id) const {
        auto* node = graph_.getNodeForId(id);
        return node != nullptr ? node->properties["uuid"].toString() : juce::String();
    }

    // What the new card must keep clear of: the usual placement blockers (cards, collapsed macro cards, open hulls,
    // and model rects for nodes with no card yet) minus the hull it is joining, plus that macro's own members even
    // while it is collapsed, so the node never lands on a hidden card that reappears on expand.
    std::vector<LayoutUtil::Box> blockersFor(NodeID self, const juce::String& joinMacroId) const {
        // Not the node being placed, nor those still waiting: with no position yet their model rects sit at (-1,-1)
        // and would block the slots nearest the origin wall.
        std::vector<NodeID> unplaced{self};
        unplaced.insert(unplaced.end(), pending_.begin(), pending_.end());
        auto boxes = editor_.getMacroController().placementBlockers(unplaced, joinMacroId);
        if (joinMacroId.isEmpty())
            return boxes;
        const auto& macros = editor_.getMacros();
        for (const auto& uuid : macro_nesting::orderedDescendantMembers(macros, joinMacroId))
            for (auto* node : graph_.getNodes())
                if (node->properties["uuid"].toString() == uuid)
                    if (const auto rect = rectOf(node->nodeID); !rect.isEmpty())
                        boxes.push_back({node->nodeID, rect});
        return boxes;
    }

    // The box the members of `macroId` (and its nested macros) span, or empty when it has none placed yet.
    juce::Rectangle<int> memberUnion(const juce::String& macroId) const {
        juce::Rectangle<int> box;
        for (const auto& uuid : macro_nesting::orderedDescendantMembers(editor_.getMacros(), macroId))
            for (auto* node : graph_.getNodes())
                if (node->properties["uuid"].toString() == uuid)
                    box = box.getUnion(rectOf(node->nodeID));
        return box;
    }

    // A Poly MIDI card goes just before the card it feeds (LayoutUtil::findPolyMidiSlotBefore, shared with a new poly
    // track). A macro that has no room gets the usual walk-down (it grows).
    std::optional<juce::Point<int>> slotInsideMacro(const juce::Rectangle<int>& fed, juce::Point<int> desired,
                                                    juce::Point<int> size, const juce::String& macroId,
                                                    const std::vector<LayoutUtil::Box>& blockers) const {
        const auto span = macroId.isEmpty() ? juce::Rectangle<int>() : memberUnion(macroId);
        return LayoutUtil::findPolyMidiSlotBefore(fed, desired, size, span, blockers);
    }

    // No room left of `fed` without crossing the origin wall: the whole patch slides right by just enough to make it
    // (one undo step with the caller's), keeping "Poly MIDI sits just before the card it feeds". Looked for on the
    // slid layout first, so nothing moves when even then no spot would be clear. nullopt: walk down instead.
    std::optional<juce::Point<int>> slideForPolyMidi(const juce::Rectangle<int>& fed, juce::Point<int> size,
                                                     const juce::String& joinMacroId,
                                                     const std::vector<LayoutUtil::Box>& blockers) {
        const int xMin = joinMacroId.isEmpty() ? 0 : LayoutUtil::kMacroHullSideOutset;
        const int need = xMin + size.x + LayoutUtil::kLayerGapX - fed.getX();
        if (need <= 0)
            return std::nullopt;
        const int dx = ((need + LayoutUtil::kGridSize - 1) / LayoutUtil::kGridSize) * LayoutUtil::kGridSize;
        auto slid = blockers;
        for (auto& box : slid)
            box.rect.translate(dx, 0);
        const auto fedSlid = fed.translated(dx, 0);
        const auto span = joinMacroId.isEmpty() ? juce::Rectangle<int>() : memberUnion(joinMacroId).translated(dx, 0);
        const auto slot = LayoutUtil::findPolyMidiSlotBefore(
            fedSlid, {fedSlid.getX() - LayoutUtil::kLayerGapX - size.x, fedSlid.getY()}, size, span, slid);
        if (!slot.has_value())
            return std::nullopt;
        editor_.slidePatchRightForNewCard(dx);
        // Cards placed earlier in this run have no component yet, so the slide did not reach them.
        for (const auto earlier : placed_)
            if (auto* node = graph_.getNodeForId(earlier))
                node->properties.set("x", (int)node->properties.getWithDefault("x", 0) + dx);
        lastPlaced_.translate(dx, 0);
        return slot;
    }

    void place(NodeID id, const std::optional<Anchor>& anchor) {
        auto* node = graph_.getNodeForId(id);
        const auto size = GraphEditor::estimateModuleSize(AIStateMapper::getFactoryTypeName(node->getProcessor()));
        juce::String joinMacroId;
        juce::Point<int> desired;
        if (anchor.has_value()) {
            const auto a = rectOf(anchor->node);
            desired = anchor->leftOf ? juce::Point<int>(a.getX() - LayoutUtil::kLayerGapX - size.x, a.getY())
                                     : juce::Point<int>(a.getRight() + LayoutUtil::kLayerGapX, a.getY());
            if (const auto* macro = editor_.getMacros().findByMember(uuidOf(anchor->node))) {
                joinMacroId = macro->id;
                // A member's open hull reaches kMacroHullSideOutset past it: keep that hull off the canvas edge.
                desired.x = juce::jmax(desired.x, LayoutUtil::kMacroHullSideOutset);
            }
        } else if (!lastPlaced_.isEmpty()) {
            desired = {lastPlaced_.getX(), lastPlaced_.getBottom() + LayoutUtil::kIntraLayerGapY};
        } else {
            desired = editor_.findLeftEdgeSlotBelowModules(size.x, size.y);
        }
        const auto blockers = blockersFor(id, joinMacroId);
        std::optional<juce::Point<int>> before;
        if (anchor.has_value() && anchor->leftOf && dynamic_cast<PolyMidiModule*>(node->getProcessor()) != nullptr)
            before = slotInsideMacro(rectOf(anchor->node), desired, size, joinMacroId, blockers);
        if (!before.has_value() && anchor.has_value() && anchor->leftOf &&
            dynamic_cast<PolyMidiModule*>(node->getProcessor()) != nullptr)
            before = slideForPolyMidi(rectOf(anchor->node), size, joinMacroId, blockers);
        const auto spot = before.value_or(LayoutUtil::findFreeSlotBelow(desired, size.x, size.y, blockers));
        node->properties.set("x", spot.x);
        node->properties.set("y", spot.y);
        lastPlaced_ = {spot.x, spot.y, size.x, size.y};
        placed_.push_back(id);
        if (joinMacroId.isNotEmpty())
            joins_[joinMacroId].push_back(uuidOf(id));
    }

    GraphEditor& editor_;
    Graph& graph_;
    std::vector<Edge> edges_;
    std::vector<NodeID> pending_;
    std::vector<NodeID> placed_;
    juce::Rectangle<int> lastPlaced_;
    std::map<juce::String, std::vector<juce::String>> joins_;
};

} // namespace

void placeNewModulesBesideConnections(GraphEditor& editor,
                                      const std::vector<juce::AudioProcessorGraph::NodeID>& created) {
    Placer(editor, created).run();
}

} // namespace synth
