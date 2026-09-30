// MacroGroupControllerDisplacement.cpp
//
// Making room when a macro or module grows: the sibling "layout units" at one nesting level, moving a
// unit rigidly, and the inside-out pass that pushes neighbours clear of a grown hull or card. The
// geometry itself is the pure LayoutUtil::resolveDisplacement; this unit only turns the canvas into
// units and applies the resulting moves. MacroGroupController is declared in MacroGroupController.h.
// (docs/layout/layout.md#making-room-when-something-grows)

#include "MacroGroupController.h"
#include "MacroNesting.h"

#include "Mixer/MasterSplice.h"

#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

#include <algorithm>
#include <set>

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
        // The output dock (Master / Rec Tap / Audio Output) is pinned: makeRoomFor never pushes it, the dock is
        // re-derived to the right of whatever grew instead (GraphEditor::reflowOutputDock).
        units.push_back(
            {nodeKey(comp->getNodeId()), comp->getBounds(), synth::isOutputDockProcessor(comp->getModule())});
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
// When the grower is a macro, every push (at every level) is recorded on it so that collapsing it, or removing a
// port, can offer the neighbours their way back (returnDisplacedNeighbours).
void MacroGroupController::makeRoomFor(const juce::String& growerKey) {
    CardGlideAnimator::Scope glide(host_.cardGlide());
    auto& macros = host_.getMacros();
    const auto growerMacroId =
        growerKey.startsWith("m:") ? growerKey.fromFirstOccurrenceOf("m:", false, false) : juce::String();
    bool movedAny = false;
    juce::String key = growerKey;
    for (int depth = 0; depth < 64; ++depth) {
        const auto container = containerOfKey(*this, macros, key);
        const auto units = buildLayoutUnits(container);
        for (const auto& move : synth::LayoutUtil::resolveDisplacement(key, units)) {
            const auto unit =
                std::find_if(units.begin(), units.end(), [&move](const LayoutUnit& u) { return u.key == move.key; });
            moveUnitBy(move.key, move.delta);
            movedAny = true;
            if (auto* grower = growerMacroId.isNotEmpty() ? macros.find(growerMacroId) : nullptr;
                grower != nullptr && unit != units.end())
                grower->displaced.push_back({move.key, move.delta, unit->rect.getPosition() + move.delta});
        }
        if (container.isEmpty())
            break;
        key = macroKey(container);
    }

    if (movedAny)
        refreshAfterMove();
    // Last, and even when nothing moved: a grown hull that now overlaps or passes the pinned output dock moves the
    // DOCK (right of everything), never the other way round.
    host_.reflowOutputDock();
}

// Only geometry changed (no node appeared or vanished), so refresh what depends on it rather than running a full
// component reconcile: re-sync the macro cards, re-dock the port widgets, and repaintCanvas() drops the cable memo
// before repainting.
void MacroGroupController::refreshAfterMove() {
    host_.syncMacroCards();
    dockMacroPortWidgets();
    host_.reflowOutputDock(); // a moved/returned unit changes what the dock must clear; dock cards are pinned, never
                              // moved here
    host_.repaintCanvas();
}

// The card is seeded at the member union's top-left, which an expand's canvas nudge shifted; if nothing has moved the
// members since, translating everything back by the nudge puts the card exactly where it was before expanding.
void MacroGroupController::restoreCardAfterCollapse(const juce::String& macroId) {
    CardGlideAnimator::Scope glide(host_.cardGlide());
    auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || !macro->hasExpandRecord)
        return;
    const auto nudge = macro->expandNudge;
    const bool untouched = macro->bounds.getTopLeft() == macro->preExpandCardOrigin + nudge;
    const auto target = macro->bounds.translated(-nudge.x, -nudge.y);
    macro->hasExpandRecord = false;
    if ((nudge.x == 0 && nudge.y == 0) || !untouched || target.getX() < 0 || target.getY() < 0)
        return;

    const auto self = macroKey(macroId);
    const auto units = buildLayoutUnits(macro->parentId);
    const bool blocked = std::any_of(units.begin(), units.end(), [&](const LayoutUnit& other) {
        return other.key != self && target.expanded(synth::LayoutUtil::kCollisionGap).intersects(other.rect);
    });
    if (blocked)
        return;
    moveUnitBy(self, {-nudge.x, -nudge.y});
    refreshAfterMove();
}

// A neighbour comes home only if the user has not touched it (still exactly where the push left it) and its old spot
// is clear of every other unit at that level, the shrunken macro or its collapsed card included. Newest push first,
// so a unit pushed twice retraces its steps and each landedAt is checked against where the last return left it.
// `keepBlocked` (the macro stays open, e.g. a port was deleted) keeps the records that only lacked room, so a later
// shrink can still return them; a collapse clears everything.
void MacroGroupController::returnDisplacedNeighbours(const juce::String& macroId, bool keepBlocked) {
    CardGlideAnimator::Scope glide(host_.cardGlide());
    auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || macro->displaced.empty())
        return;
    const auto records = std::move(macro->displaced);
    macro->displaced.clear();

    // Newest first. A cascade tail (C pushed by B pushed by A) is checked while B still sits on C's home, so a single
    // pass would strand it: repeat over what is still blocked until a pass returns nothing.
    bool movedAny = false;
    std::vector<synth::Macro::DisplacedNeighbour> kept(records.rbegin(), records.rend());
    for (size_t pass = 0; pass <= records.size() && !kept.empty(); ++pass) {
        std::vector<synth::Macro::DisplacedNeighbour> stillBlocked;
        bool movedThisPass = false;
        for (const auto& rec : kept) {
            const auto units = buildLayoutUnits(containerOfKey(*this, host_.getMacros(), rec.unitKey));
            const auto self =
                std::find_if(units.begin(), units.end(), [&rec](const LayoutUnit& u) { return u.key == rec.unitKey; });
            if (self == units.end() || self->rect.getPosition() != rec.landedAt)
                continue; // gone or moved by the user: never returns

            const auto home = self->rect.translated(-rec.delta.x, -rec.delta.y);
            const bool blocked = home.getX() < 0 || home.getY() < 0 ||
                                 std::any_of(
                                     units.begin(), units.end(),
                                     [&](const LayoutUnit& other) {
                                         return other.key != rec.unitKey &&
                                                home.expanded(synth::LayoutUtil::kCollisionGap).intersects(other.rect);
                                     });
            if (blocked) {
                stillBlocked.push_back(rec);
                continue;
            }
            moveUnitBy(rec.unitKey, {-rec.delta.x, -rec.delta.y});
            movedThisPass = movedAny = true;
            refreshAfterMove(); // the next record's units must see this geometry
        }
        kept = std::move(stillBlocked);
        if (!movedThisPass)
            break;
    }

    if (keepBlocked)
        if (auto* live = host_.getMacros().find(macroId))
            live->displaced.assign(kept.rbegin(), kept.rend());
    if (movedAny)
        host_.requestRepaint();
}

// Boxes for findFreeSlot, built like buildLayoutUnits but flattened over every level instead of one: a module being
// placed must clear a collapsed card, an open hull and every visible module wherever they nest. The hidden members of a
// collapsed macro sit at their pre-collapse positions and are skipped (their card stands in for them).
std::vector<synth::LayoutUtil::Box>
MacroGroupController::placementBlockers(const std::vector<juce::AudioProcessorGraph::NodeID>& excludeNodes,
                                        const juce::String& joinMacroId) const {
    const auto& macros = host_.getMacros();
    auto isExcludedNode = [&excludeNodes](juce::AudioProcessorGraph::NodeID id) {
        return std::find(excludeNodes.begin(), excludeNodes.end(), id) != excludeNodes.end();
    };

    std::set<juce::String> skippedMacros;
    std::set<juce::String> movedUuids;
    for (auto id : excludeNodes)
        if (const auto uuid = nodeUuidFor(id); uuid.isNotEmpty()) {
            movedUuids.insert(uuid);
            for (const auto& owner : macro_nesting::ownerChain(macros, uuid))
                skippedMacros.insert(owner);
        }
    for (const auto& carried : macro_nesting::collapsedMacrosCarriedBy(macros, movedUuids))
        skippedMacros.insert(carried);
    if (joinMacroId.isNotEmpty()) {
        skippedMacros.insert(joinMacroId);
        for (const auto& ancestor : macros.ancestorChain(joinMacroId))
            skippedMacros.insert(ancestor);
    }

    std::vector<synth::LayoutUtil::Box> boxes;
    for (auto* comp : host_.modules())
        if (comp != nullptr && comp->getModule() != nullptr && comp->isVisible() && !isExcludedNode(comp->getNodeId()))
            boxes.push_back({comp->getNodeId(), comp->getBounds()});

    // No real node id is anywhere near the top of the range, so a sentinel is never mistaken for `selfId`.
    juce::uint32 sentinel = 0xFFFF0000u;
    for (const auto& macro : macros.getAll()) {
        if (skippedMacros.count(macro.id) > 0)
            continue;
        juce::Rectangle<int> rect;
        if (!macros.isEffectivelyCollapsed(macro.id))
            rect = macroHullBounds(macro.id);
        else if (macro.collapsed && macros.isVisible(macro.id))
            rect = macroCableAnchorBounds(macro);
        if (!rect.isEmpty())
            boxes.push_back({juce::AudioProcessorGraph::NodeID(sentinel++), rect});
    }
    return boxes;
}
