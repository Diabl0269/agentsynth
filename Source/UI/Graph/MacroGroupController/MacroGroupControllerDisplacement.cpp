// MacroGroupControllerDisplacement.cpp
//
// Making room when a macro or module grows: the sibling "layout units" at one nesting level, moving a
// unit rigidly, and the inside-out pass that pushes neighbours clear of a grown hull or card. The
// geometry itself is the pure LayoutUtil::resolveDisplacement; this unit only turns the canvas into
// units and applies the resulting moves. MacroGroupController is declared in MacroGroupController.h.
// (docs/layout/layout.md#making-room-when-something-grows)

#include "MacroGroupController.h"
#include "MacroNesting.h"

#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

namespace {
using synth::LayoutUtil::LayoutUnit;

juce::String nodeKey(juce::AudioProcessorGraph::NodeID id) { return "n:" + juce::String((juce::int64)id.uid); }
juce::String macroKey(const juce::String& macroId) { return "m:" + macroId; }
} // namespace

// Units are the things that move as one: a loose module, or a whole macro (open: its hull, collapsed: its card).
// A module hidden inside a collapsed macro is never a unit -- the card stands in for it -- and a macro's port
// widgets live docked inside its hull, so they never are either.
std::vector<synth::LayoutUtil::LayoutUnit>
MacroGroupController::buildLayoutUnits(const juce::String& containerId) const {
    const auto& macros = host_.getMacros();
    if (containerId.isNotEmpty() && (macros.find(containerId) == nullptr || macros.isEffectivelyCollapsed(containerId)))
        return {};

    std::vector<LayoutUnit> units;
    for (auto* comp : host_.modules()) {
        if (comp == nullptr || comp->getModule() == nullptr || !comp->isVisible())
            continue;
        const auto uuid = nodeUuidFor(comp->getNodeId());
        const auto* owner = uuid.isEmpty() ? nullptr : macros.findByMember(uuid);
        if ((owner != nullptr ? owner->id : juce::String()) != containerId)
            continue;
        if (owner != nullptr && (owner->memberIsPort(uuid) || macros.outermostCollapsedAncestorOf(uuid).isNotEmpty()))
            continue;
        units.push_back({nodeKey(comp->getNodeId()), comp->getBounds(), false});
    }
    for (const auto& macro : macros.getAll()) {
        if (macro.parentId != containerId)
            continue;
        const auto rect = macro.collapsed ? macroCableAnchorBounds(macro) : macroHullBounds(macro.id);
        if (!rect.isEmpty())
            units.push_back({macroKey(macro.id), rect, false});
    }
    return units;
}

namespace {
// `macroId` and every macro nested under it.
void collectMacroSubtree(const synth::MacroSet& macros, const juce::String& macroId, std::vector<juce::String>& out) {
    out.push_back(macroId);
    for (const auto& child : macros.childrenOf(macroId))
        collectMacroSubtree(macros, child, out);
}
} // namespace

// Geometry is written synchronously and finally: node x/y, the live component, and (for collapsed macros) the
// persisted bounds plus the live card. macroHullBounds reads live component bounds, so the inside-out pass in
// makeRoomFor must never see a stale position.
void MacroGroupController::moveUnitBy(const juce::String& key, juce::Point<int> delta) {
    if (delta.x == 0 && delta.y == 0)
        return;

    auto moveNode = [this, delta](juce::AudioProcessorGraph::NodeID nodeId) {
        auto* node = host_.graph().getNodeForId(nodeId);
        if (node == nullptr)
            return;
        // One new position, written once to both the node property and the live component.
        const juce::Point<int> pos = juce::Point<int>((int)node->properties["x"], (int)node->properties["y"]) + delta;
        node->properties.set("x", pos.x);
        node->properties.set("y", pos.y);
        if (auto* comp = host_.moduleComponentFor(nodeId))
            comp->setTopLeftPosition(pos);
    };

    if (key.startsWith("n:")) {
        moveNode(juce::AudioProcessorGraph::NodeID(
            (juce::uint32)key.fromFirstOccurrenceOf("n:", false, false).getLargeIntValue()));
        return;
    }

    auto& macros = host_.getMacros();
    const auto macroId = key.fromFirstOccurrenceOf("m:", false, false);
    if (macros.find(macroId) == nullptr)
        return;

    // Ports dock against the hull afterwards, so they are not moved themselves.
    for (const auto& uuid : macro_nesting::orderedDescendantMembers(macros, macroId)) {
        const auto* owner = macros.findByMember(uuid);
        if (owner == nullptr || !owner->memberIsPort(uuid))
            moveNode(resolveMemberNodeId(uuid));
    }
    std::vector<juce::String> subtree;
    collectMacroSubtree(macros, macroId, subtree);
    for (const auto& id : subtree) {
        auto* macro = macros.find(id);
        if (macro == nullptr || !macro->collapsed)
            continue;
        macro->bounds.setPosition(macro->bounds.getPosition() + delta);
        for (auto* card : host_.macroCards())
            if (card != nullptr && card->getMacroId() == id)
                card->setTopLeftPosition(macro->bounds.getPosition());
    }
}

// The container a unit sits in: the macro owning a module, or a macro's parent (empty at the top level).
static juce::String containerOfKey(const MacroGroupController& controller, const synth::MacroSet& macros,
                                   const juce::String& key) {
    if (key.startsWith("m:")) {
        const auto* macro = macros.find(key.fromFirstOccurrenceOf("m:", false, false));
        return macro != nullptr ? macro->parentId : juce::String();
    }
    const auto uuid = controller.nodeUuidFor(juce::AudioProcessorGraph::NodeID(
        (juce::uint32)key.fromFirstOccurrenceOf("n:", false, false).getLargeIntValue()));
    const auto* owner = uuid.isEmpty() ? nullptr : macros.findByMember(uuid);
    return owner != nullptr ? owner->id : juce::String();
}

// Inside-out: settle the grower's own level, then, because a pushed neighbour may have widened the enclosing
// macro's hull, treat that macro as the grower one level up, to the root. Runs inside the caller's undo record.
void MacroGroupController::makeRoomFor(const juce::String& growerKey) {
    auto& macros = host_.getMacros();
    bool movedAny = false;
    juce::String key = growerKey;
    for (int depth = 0; depth < 64; ++depth) {
        const auto container = containerOfKey(*this, macros, key);
        for (const auto& move : synth::LayoutUtil::resolveDisplacement(key, buildLayoutUnits(container))) {
            moveUnitBy(move.key, move.delta);
            movedAny = true;
        }
        if (container.isEmpty())
            break;
        key = macroKey(container);
    }

    if (movedAny) {
        // Only geometry changed (no node appeared or vanished), so refresh what depends on it rather than
        // running a full component reconcile: re-sync the macro cards, re-dock the port widgets, and
        // repaintCanvas() drops the cable memo before repainting.
        host_.syncMacroCards();
        dockMacroPortWidgets();
        host_.repaintCanvas();
    }
}
