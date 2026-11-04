// MacroGroupControllerGroupDrop.cpp
//
// Dragging a group (several selected modules, macros, or both) into, out of or between open macros: the group twin of a
// single module's reparent drag and a single macro's unit drag
// (docs/macros/menu-and-membership.md#dragging-a-group-into-or-out-of-a-macro). The drag itself is GraphEditor's
// selection drag; this unit holds the borders held still at press, the live target, and the membership walk the drop
// runs inside that drag's undo record. The target query, groupDropTargetAt, sits with the other hull hit tests in
// MacroGroupControllerGeometry.cpp. MacroGroupController is declared in MacroGroupController.h.

#include "MacroGroupController.h"
#include "MacroSelectionUnits.h"

#include "AI/AIStateMapper/GraphRebuildBatch.h"

#include <algorithm>
#include <utility>

// The selection resolves into units the way grouping reads it (a whole macro, or a loose module), each with the macro
// that directly holds it. That macro and its ancestors are live unions that follow the group, so their borders are held
// at press: the group only leaves once the anchor leaves the border it had.
void MacroGroupController::beginGroupDrag(const std::vector<juce::AudioProcessorGraph::NodeID>& movingNodes) {
    endGroupDrag();
    const auto& macros = host_.getMacros();
    for (const auto& unit : macro_units::unitsFor(*this, macros, movingNodes)) {
        if (!unit.isMacro() && unit.uuid.isEmpty())
            continue; // a node with no uuid can never be a member
        groupDrag_.units.push_back({unit.macroId, unit.uuid, unit.container});
        if (unit.container.isEmpty())
            continue;
        auto chain = macros.ancestorChain(unit.container);
        chain.push_back(unit.container);
        for (const auto& id : chain)
            if (groupDrag_.held.count(id) == 0)
                if (const auto hull = macroHullBounds(id); !hull.isEmpty())
                    groupDrag_.held[id] = hull;
    }
    groupDrag_.active = !groupDrag_.units.empty();
}

// Outlined exactly like a single drag: the target with the join stroke (only when some unit would really move into it),
// and every macro a unit would walk out of with the leave stroke. Stepping down into a macro nested in a unit's own
// container leaves nothing.
void MacroGroupController::updateGroupDropCandidate(juce::Point<int> anchor) {
    if (!groupDrag_.active)
        return;
    const auto target = groupDropTargetAt(anchor);
    if (groupDrag_.aimed && target == groupDrag_.target)
        return;
    groupDrag_.aimed = true;
    groupDrag_.target = target;
    groupDrag_.leaving.clear();
    const auto& macros = host_.getMacros();
    const auto targetChain = target.isNotEmpty() ? macros.ancestorChain(target) : std::vector<juce::String>();
    bool joins = false;
    for (const auto& unit : groupDrag_.units) {
        if (unit.container == target)
            continue;
        joins = joins || target.isNotEmpty();
        if (unit.container.isNotEmpty() &&
            std::find(targetChain.begin(), targetChain.end(), unit.container) == targetChain.end() &&
            std::find(groupDrag_.leaving.begin(), groupDrag_.leaving.end(), unit.container) == groupDrag_.leaving.end())
            groupDrag_.leaving.push_back(unit.container);
    }
    if (const auto shown = joins ? target : juce::String(); shown != groupDrag_.shownJoin) {
        groupDrag_.shownJoin = shown;
        host_.setMacroDropCandidate(shown);
    }
    host_.requestRepaint();
}

bool MacroGroupController::isGroupDropLeaving(const juce::String& macroId) const {
    return std::find(groupDrag_.leaving.begin(), groupDrag_.leaving.end(), macroId) != groupDrag_.leaving.end();
}

juce::Rectangle<int> MacroGroupController::groupDragHeldHull(const juce::String& macroId) const {
    const auto it = groupDrag_.held.find(macroId);
    return it != groupDrag_.held.end() ? it->second : juce::Rectangle<int>();
}

void MacroGroupController::endGroupDrag() {
    const auto shown = std::exchange(groupDrag_, {}).shownJoin;
    if (shown.isNotEmpty())
        host_.setMacroDropCandidate({});
}

// Each unit takes the same walk its single drag would: a macro through moveMacroInto (out level by level, then down
// level by level, ports spliced at each), a module out of each macro the target is not in (removeSelectionFromMacro)
// and, for a step down into a macro nested in its own, straight out of that owner's member list, as the single drag's
// applyMacroMembershipChange does. The modules then join the target in ONE addSelectionToMacro, so a cable between two
// of them never gets a port. Nothing records undo here: the caller's record holds the whole drop.
void MacroGroupController::applyGroupDrop() {
    const auto drag = groupDrag_;
    endGroupDrag();
    if (!drag.aimed)
        return;
    auto& macros = host_.getMacros();
    const auto& target = drag.target;
    if (target.isNotEmpty() && macros.find(target) == nullptr)
        return;
    const auto targetChain = target.isNotEmpty() ? macros.ancestorChain(target) : std::vector<juce::String>();
    const auto onPath = [&](const juce::String& id) {
        return id == target || std::find(targetChain.begin(), targetChain.end(), id) != targetChain.end();
    };
    for (const auto& unit : drag.units)
        if (unit.macroId.isNotEmpty() && onPath(unit.macroId))
            return; // never into itself or a macro inside it (groupDropTargetAt already skips those)

    const synth::GraphRebuildScope rebuildScope{host_.graph()};
    // Room is made once the whole group is in. Made per step, the border a macro or module just grew would push the
    // group's other units (still outside) off the spot the rigid-body placement gave them, away from the landing ghost.
    const bool wasDeferred = std::exchange(makeRoomDeferred_, true);
    std::vector<juce::String> joining, nested;
    for (const auto& unit : drag.units) {
        if (unit.macroId.isNotEmpty()) {
            if (macros.find(unit.macroId) != nullptr && macros.parentOf(unit.macroId) != target &&
                moveMacroInto(unit.macroId, macros.parentOf(unit.macroId), target))
                nested.push_back(unit.macroId);
            continue;
        }
        for (const auto* owner = macros.findByMember(unit.uuid); owner != nullptr && owner->id != target;
             owner = macros.findByMember(unit.uuid)) {
            const auto ownerId = owner->id;
            if (onPath(ownerId)) {
                auto* live = macros.find(ownerId);
                live->members.erase(std::remove(live->members.begin(), live->members.end(), unit.uuid),
                                    live->members.end());
                break;
            }
            removeSelectionFromMacro(ownerId, {unit.uuid}, /*recordUndo=*/false);
            if (const auto* now = macros.findByMember(unit.uuid); now != nullptr && now->id == ownerId)
                break; // refused: never loop
        }
        if (target.isNotEmpty() && macros.findByMember(unit.uuid) == nullptr)
            joining.push_back(unit.uuid);
    }
    if (!joining.empty())
        addSelectionToMacro(target, joining, /*recordUndo=*/false);
    makeRoomDeferred_ = wasDeferred;
    for (const auto& id : nested)
        makeRoomFor("m:" + id);
    if (!wasDeferred)
        for (const auto& grower : takeDeferredMakeRoom())
            makeRoomFor(grower);
}
