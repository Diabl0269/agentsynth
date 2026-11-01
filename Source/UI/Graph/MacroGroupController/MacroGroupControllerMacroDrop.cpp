// MacroGroupControllerMacroDrop.cpp
//
// Dragging a whole macro (its folded card, or an open macro dragged by its name chip) into, out of or between open
// macros, the macro-unit twin of a module's reparent drag
// (docs/macros/menu-and-membership.md#dropping-a-macro-into-another-or-out-of-its-parent).
// The drag itself is GraphEditor's macro body drag; this unit holds the borders held still at press, the live
// leave/join targets, and the membership walk the drop runs inside that drag's undo record. The target queries,
// macroNestTargetAt and macroUnitDragTargets, sit with the other hull hit tests in MacroGroupControllerGeometry.cpp.
// MacroGroupController is declared in MacroGroupController.h.

#include "MacroGroupController.h"

#include "AI/AIStateMapper/GraphRebuildBatch.h"

#include <algorithm>
#include <utility>

// A press on a macro that is already selected together with other macros or modules keeps that selection, so the drag
// moves all of it, the way a press on one module of a multi-selection does; any other press selects just this macro.
void MacroGroupController::selectMacroForDrag(const juce::String& macroId) {
    if (!isMacroInSelection(macroId))
        selectMacro(macroId, false);
}

// The parent's border is a live union that includes the dragged macro, so without a snapshot it would follow the drag
// (it looked as if the parent's border was being dragged) and the macro could never leave it. Every ancestor is held
// at its press-time border until the drop; the drop's glide then takes each to where the new membership puts it.
// A group drag (more than this macro selected) is no unit drag: like a module group drag it never joins or leaves, so
// nothing is held and no target is outlined.
void MacroGroupController::beginMacroUnitDrag(const juce::String& draggedMacroId) {
    clearMacroNestCandidate();
    frozenDragHulls_.clear();
    frozenDragNode_ = {};
    if (!isMacroSelected(draggedMacroId))
        return;
    draggedMacroUnit_ = draggedMacroId;
    for (const auto& id : host_.getMacros().ancestorChain(draggedMacroId))
        if (const auto hull = macroHullBounds(id); !hull.isEmpty())
            frozenDragHulls_[id] = hull;
}

// The live targets are shown exactly like a module drag's: GraphEditor paints the macro named by the join id, and the
// one named by macroLeaveCandidate, with the heavier outline, so dragging a macro and dragging a module look the same.
void MacroGroupController::updateMacroNestCandidate(const juce::String& draggedMacroId, juce::Point<int> canvasPoint) {
    if (draggedMacroUnit_ != draggedMacroId)
        return; // a group drag, or no press behind it: nothing to join or leave
    const auto targets = macroUnitDragTargets(draggedMacroId, canvasPoint);
    if (targets.join == macroNestCandidate_ && targets.leave == macroLeaveCandidate_)
        return;
    macroLeaveCandidate_ = targets.leave;
    macroNestCandidate_ = targets.join;
    host_.setMacroDropCandidate(targets.join);
    host_.requestRepaint();
}

void MacroGroupController::clearMacroNestCandidate() {
    const bool hadUnit = draggedMacroUnit_.isNotEmpty();
    if (hadUnit) {
        draggedMacroUnit_.clear();
        frozenDragHulls_.clear();
    }
    if (macroNestCandidate_.isEmpty() && macroLeaveCandidate_.isEmpty()) {
        if (hadUnit)
            host_.requestRepaint();
        return;
    }
    macroNestCandidate_.clear();
    macroLeaveCandidate_.clear();
    host_.setMacroDropCandidate({});
    host_.requestRepaint();
}

// Mirrors removeSelectionFromMacro for a whole child: the departing set is the child's every node (its ports
// included, since they sit inside the parent), so a cable from a member staying in the parent to the child gets a port
// on the parent's border, and a parent port whose inside leg was the child is spliced back out. The child then hangs
// from the grandparent (the top level when there is none), and the parent's border shrinks: neighbours its growth
// pushed aside may come back.
bool MacroGroupController::moveMacroOutOfParent(const juce::String& childMacroId) {
    auto& macros = host_.getMacros();
    const auto* child = macros.find(childMacroId);
    if (child == nullptr || child->parentId.isEmpty() || macros.find(child->parentId) == nullptr)
        return false;
    const synth::GraphRebuildScope rebuildScope{host_.graph()};
    const auto parentId = child->parentId;
    const auto inside = macros.descendantMembers(childMacroId);
    const std::vector<juce::String> leaving(inside.begin(), inside.end());
    const auto removePlan = buildMacroPortCrossingPlanForRemovedMembers(parentId, leaving);
    const auto obsoletePorts = macroPortsThatBecomeObsoleteOnRemove(parentId, leaving);

    if (!removePlan.empty())
        spliceMacroPorts(parentId, removePlan);
    for (const auto& portUuid : obsoletePorts)
        if (auto* liveParent = macros.find(parentId))
            spliceOutMacroPort(*liveParent, portUuid);
    macros.setParent(childMacroId, macros.parentOf(parentId));
    host_.updateComponents();
    returnDisplacedNeighbours(parentId, /*keepBlocked=*/true);
    return true;
}

// A leave walks the macro out one level at a time until it reaches the join target's chain (the top level with no
// target), each level planning its own ports, the way a module dragged out of a nested macro walks out. Nesting then
// reuses Add Selection to Macro, which nests a whole macro that sits beside the target (same container) and plans that
// level's port splices from the graph as it is then; a target deeper down is reached one level at a time. Every step
// runs inside the caller's undo record (recordUndo=false). The moved macro then makes room where it landed: the units
// it covers slide aside and a grown border pushes its own neighbours, level by level.
void MacroGroupController::moveDraggedMacroToCandidate(const juce::String& draggedMacroId) {
    const auto leave = std::exchange(macroLeaveCandidate_, {});
    const auto target = std::exchange(macroNestCandidate_, {});
    clearMacroNestCandidate(); // ends the unit drag: the borders are live again for the drop's glide
    host_.setMacroDropCandidate({});
    auto& macros = host_.getMacros();
    if (macros.find(draggedMacroId) == nullptr || (leave.isEmpty() && target.isEmpty()))
        return;
    if (target.isNotEmpty() && macros.find(target) == nullptr)
        return;

    std::vector<juce::String> path; // outermost -> the target itself
    if (target.isNotEmpty()) {
        const auto chain = macros.ancestorChain(target); // inner -> outer
        path.assign(chain.rbegin(), chain.rend());
        path.push_back(target);
    }
    const auto onPath = [&path](const juce::String& id) {
        return std::find(path.begin(), path.end(), id) != path.end();
    };
    bool moved = false;
    if (leave.isNotEmpty())
        while (macros.parentOf(draggedMacroId).isNotEmpty() && !onPath(macros.parentOf(draggedMacroId)) &&
               moveMacroOutOfParent(draggedMacroId))
            moved = true;

    if (const auto container = macros.parentOf(draggedMacroId); container.isNotEmpty()) {
        const auto at = std::find(path.begin(), path.end(), container);
        if (at == path.end())
            path.clear(); // reaching the target would take the macro out of its parent first: not a nest
        else
            path.erase(path.begin(), std::next(at));
    }

    const auto inside = macros.descendantMembers(draggedMacroId);
    const std::vector<juce::String> uuids(inside.begin(), inside.end());
    for (const auto& step : path) {
        addSelectionToMacro(step, uuids, /*recordUndo=*/false);
        if (host_.getMacros().parentOf(draggedMacroId) != step)
            break; // refused (its status message says why); the steps already taken stay in the one undo record
        moved = true;
    }
    if (moved)
        makeRoomFor("m:" + draggedMacroId);
}
