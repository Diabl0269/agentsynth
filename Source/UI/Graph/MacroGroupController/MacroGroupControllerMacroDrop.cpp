// MacroGroupControllerMacroDrop.cpp
//
// Dropping a whole macro (its folded card, or an open macro dragged by its name chip) into an open macro nests it
// there (docs/macros/menu-and-membership.md#dropping-a-macro-into-another). The drag itself is GraphEditor's macro
// body drag; this unit holds the live target and the nesting the drop runs inside that drag's undo record. The target
// query, macroNestTargetAt, sits with the other hull hit tests in MacroGroupControllerGeometry.cpp.
// MacroGroupController is declared in MacroGroupController.h.

#include "MacroGroupController.h"

#include <algorithm>
#include <utility>

// The live target is shown exactly like a module drag's join candidate: GraphEditor paints the macro named by the join
// id with the heavier outline, so dropping a macro and dropping a module look the same.
void MacroGroupController::updateMacroNestCandidate(const juce::String& draggedMacroId, juce::Point<int> canvasPoint) {
    const auto target = macroNestTargetAt(draggedMacroId, canvasPoint);
    if (target == macroNestCandidate_)
        return;
    macroNestCandidate_ = target;
    host_.setMacroDropCandidate(target);
}

void MacroGroupController::clearMacroNestCandidate() {
    if (macroNestCandidate_.isEmpty())
        return;
    macroNestCandidate_.clear();
    host_.setMacroDropCandidate({});
}

// Nesting reuses Add Selection to Macro, which nests a whole macro that sits beside the target (same container) and
// plans that level's port splices from the graph as it is then. A target deeper down is reached one level at a time
// (into the outermost macro below the dragged macro's container first), each step planning its own ports, the way a
// module dragged into a nested macro walks down. Every step runs inside the caller's undo record (recordUndo=false).
// The dropped macro then makes room where it landed: the members it covers slide aside and the grown border pushes
// its own neighbours, level by level.
void MacroGroupController::nestDraggedMacroIntoCandidate(const juce::String& draggedMacroId) {
    const auto target = std::exchange(macroNestCandidate_, {});
    host_.setMacroDropCandidate({});
    auto& macros = host_.getMacros();
    const auto* dragged = macros.find(draggedMacroId);
    if (target.isEmpty() || dragged == nullptr || macros.find(target) == nullptr)
        return;

    const auto container = dragged->parentId;
    const auto chain = macros.ancestorChain(target); // inner -> outer
    std::vector<juce::String> path(chain.rbegin(), chain.rend());
    path.push_back(target);
    if (container.isNotEmpty()) {
        const auto at = std::find(path.begin(), path.end(), container);
        if (at == path.end())
            return; // reaching the target would take the macro out of its parent first: not a nest
        path.erase(path.begin(), std::next(at));
    }

    const auto inside = macros.descendantMembers(draggedMacroId);
    const std::vector<juce::String> uuids(inside.begin(), inside.end());
    for (const auto& step : path) {
        addSelectionToMacro(step, uuids, /*recordUndo=*/false);
        if (host_.getMacros().parentOf(draggedMacroId) != step)
            return; // refused (its status message says why); the steps already taken stay in the one undo record
    }
    makeRoomFor("m:" + draggedMacroId);
}
