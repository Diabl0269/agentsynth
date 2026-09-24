// GraphEditorSelection.cpp
//
// Selection model (collectModuleBoxes/applySelectionChange/selectModule/etc.), marquee
// selection, and selection group-drag. GraphEditor is declared in GraphEditor.h; sibling
// GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleStepOrder.h"

// ---------------------------------------------------------------------------------------
// Selection (issue #156)
// ---------------------------------------------------------------------------------------
//
// Gesture contract, chosen so the existing drag-to-pan muscle memory is untouched:
//   plain drag on empty canvas          -> pan (unchanged)
//   Shift + drag on empty canvas        -> marquee select, replacing the selection
//   Cmd/Ctrl + Shift + drag             -> marquee select, adding to the selection
//   click a module                      -> select just it
//   Shift/Cmd + click a module          -> toggle it in the selection
//   drag any selected module            -> moves the whole selection together
//   Escape / click empty canvas         -> clear
//   Delete / Backspace                  -> delete the selection

// Bounding boxes of every rendered module, for marquee hit-testing and group collision.
std::vector<synth::LayoutUtil::Box> GraphEditor::collectModuleBoxes(bool selectedOnly, bool excludeSelected) const {
    std::vector<synth::LayoutUtil::Box> boxes;
    for (auto* comp : const_cast<GraphContentComponent&>(content).getModules()) {
        // A hidden member of a collapsed macro is not "on the canvas" as far as marquee
        // hit-testing or group-drag collision are concerned — the visible collapsed card stands
        // in for it (see syncMacroCards).
        if (comp == nullptr || comp->getModule() == nullptr || !comp->isVisible())
            continue;
        const bool isSel = selection.contains(comp->getNodeId());
        if (selectedOnly && !isSel)
            continue;
        if (excludeSelected && isSel)
            continue;
        boxes.push_back({comp->getNodeId(), comp->getBounds()});
    }
    return boxes;
}

// Repaints only the module components whose selected state actually changed. Selection
// changes must never trigger a full-canvas repaint storm during a marquee drag.
void GraphEditor::applySelectionChange(const std::vector<juce::AudioProcessorGraph::NodeID>& newSelection) {
    std::set<juce::AudioProcessorGraph::NodeID> before;
    for (auto id : selection.getSelected())
        before.insert(id);

    selection.setSelection(newSelection);

    std::set<juce::AudioProcessorGraph::NodeID> after;
    for (auto id : selection.getSelected())
        after.insert(id);

    if (before == after)
        return;

    // Repaint ONLY the cards that changed state. ModuleComponent is setBufferedToImage(true), so a
    // blanket repaint of every card would re-rasterize the whole canvas on each marquee frame.
    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        const auto id = comp->getNodeId();
        if ((before.count(id) > 0) != (after.count(id) > 0))
            comp->repaint();
    }
}

// Selects a single module. When additive, toggles it instead and leaves the rest alone.
void GraphEditor::selectModule(juce::AudioProcessorGraph::NodeID nodeId, bool additive) {
    if (nodeId.uid == 0)
        return;

    auto next = selection.getSelected();
    if (additive) {
        if (selection.contains(nodeId))
            next.erase(std::remove(next.begin(), next.end(), nodeId), next.end());
        else
            next.push_back(nodeId);
    } else {
        next = {nodeId};
    }
    applySelectionChange(next);
}

void GraphEditor::setSelectedNodes(const std::vector<juce::AudioProcessorGraph::NodeID>& ids) {
    applySelectionChange(ids);
}

void GraphEditor::clearSelection() { applySelectionChange({}); }

void GraphEditor::selectAllModules() {
    std::vector<juce::AudioProcessorGraph::NodeID> all;
    for (auto* comp : content.getModules()) {
        if (comp != nullptr && comp->getModule() != nullptr)
            all.push_back(comp->getNodeId());
    }
    applySelectionChange(all);
}

// Steps the selection to the next/previous module (see ModuleStepOrder.h for the order). Only
// visible cards count: a collapsed macro hides its members, and stepping onto one the user cannot
// see would select something invisible. The view pans only when the target is not already fully
// on screen, so stepping through a patch that fits does not make the canvas jump.
bool GraphEditor::selectAdjacentModule(int direction) {
    std::vector<synth::ui::StepModule> modules;
    for (auto* comp : content.getModules()) {
        if (comp != nullptr && comp->getModule() != nullptr && comp->isVisible())
            modules.push_back({comp->getNodeId(), comp->getBounds().toFloat()});
    }

    const auto target = synth::ui::adjacentModule(modules, selection.getSelected(), direction);
    if (target.uid == 0)
        return false;

    selectModule(target, /*additive=*/false);
    for (const auto& m : modules) {
        if (m.id == target && !getVisibleCanvasRect().contains(m.bounds))
            centreViewOn(m.bounds.getCentre());
    }
    return true;
}

// Drops selected ids whose nodes no longer exist. Called after any graph mutation that can
// remove nodes (delete, undo/redo, preset load) — a stale id would otherwise be handed to
// snippet extraction or a group drag.
void GraphEditor::pruneSelection() {
    if (selection.isEmpty())
        return;

    std::vector<juce::AudioProcessorGraph::NodeID> alive;
    for (auto* node : audioEngine.getGraph().getNodes())
        alive.push_back(node->nodeID);

    if (selection.retainOnly(alive))
        repaintCanvas();
}

// Removes every selected module as ONE undoable change, so Cmd+Z restores the whole group.
// Also GraphCanvasHost::deleteSelection() — MacroGroupController's deleteMacroAndMembers/
// removeMacroPort (FRO77 PR2) select the nodes to remove, then call this through the host.
void GraphEditor::deleteSelection() {
    auto ids = selection.getSelected();
    if (ids.empty())
        return;

    auto& graph = audioEngine.getGraph();

    // One transaction for the whole group: Cmd+Z must bring back every module at once, not peel
    // them back one at a time. Deleting a collapsed macro card's members (see
    // deleteMacroAndMembers) flows through here too, so the macro half of the change has to be
    // captured in the SAME undo step as the graph half — recordGraphAndMacroChange, not
    // recordStructuralChange. updateComponents() prunes `macros` against whatever survives. T154:
    // this batch can also strand a DIFFERENT macro port that isn't itself being deleted (an
    // ordinary member was that port's only remaining connection) — macroPortDeletionNeighbors()
    // captures the candidates before removal, then autoDeleteOrphanedMacroPort sweeps them after.
    auto doDelete = [this, ids, &graph] {
        modMatrix.clearRows();
        const auto portNeighbors = macroController_.macroPortDeletionNeighbors(ids); // T154: capture BEFORE removal
        // FRO16 review follow-up: graph.removeNode() below frees each node's processor
        // synchronously, same as a full graph-replacing restore -- but nothing here reaches
        // MixerPanelComponent::rebuild() until the NEXT unrelated graph edit (this path's own
        // reconcileTimelineBindingsOnly(), installed on onGraphStructureChanged, deliberately does
        // not rebuild the mixer -- see its own comment). A mixer column bound to one of these
        // NodeIDs (fader/pan/EQ thumbnail/send rows) would sit on a dangling pointer until that
        // eventual rebuild destroys it and dereferences it. Reuse the exact seam every
        // graph-replacing restore already unbinds through, same as MixerInsertList::
        // onBeforeNodeRemoved does for a single mixer-row removal -- unbind BEFORE freeing, not
        // after.
        if (onBeforeDetachAllModuleComponents)
            onBeforeDetachAllModuleComponents();
        for (auto id : ids)
            graph.removeNode(id);
        for (auto n : portNeighbors)
            macroController_.autoDeleteOrphanedMacroPort(n);
        selection.clear();
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doDelete);
    else
        doDelete();

    repaint();
}

// ---- Marquee ----

void GraphEditor::beginMarquee(juce::Point<int> canvasAnchor, bool additive) {
    marqueeActive = true;
    marqueeAdditive = additive;
    marqueeAnchor = canvasAnchor;
    marqueeRect = juce::Rectangle<int>(canvasAnchor, canvasAnchor);
    marqueeBaseSelection = additive ? selection.getSelected() : std::vector<juce::AudioProcessorGraph::NodeID>{};

    // A non-additive marquee starts from nothing, so dragging over no module deselects.
    if (!additive)
        applySelectionChange({});
}

void GraphEditor::updateMarquee(juce::Point<int> canvasCurrent) {
    if (!marqueeActive)
        return;

    marqueeRect = synth::ui::marqueeRectFrom(marqueeAnchor, canvasCurrent);
    auto hits = synth::ui::hitTestMarquee(marqueeRect, collectModuleBoxes(false, false));
    applySelectionChange(marqueeAdditive ? synth::ui::unionSelection(marqueeBaseSelection, hits) : hits);

    repaintCanvas();
}

void GraphEditor::endMarquee() {
    if (!marqueeActive)
        return;
    marqueeActive = false;
    marqueeAdditive = false;
    marqueeRect = {};
    marqueeBaseSelection.clear();
    repaintCanvas();
}

// ---- Group drag ----

void GraphEditor::beginSelectionDrag() {
    selectionDragStartPositions.clear();
    for (auto* comp : content.getModules()) {
        if (comp != nullptr && selection.contains(comp->getNodeId()))
            selectionDragStartPositions.emplace_back(comp->getNodeId(), comp->getPosition());
    }
    selectionDragActive = selectionDragStartPositions.size() > 1;
}

void GraphEditor::dragSelectionBy(juce::Point<int> delta, ModuleComponent* initiator) {
    if (!selectionDragActive)
        return;

    // Place each follower from ITS OWN recorded origin. Applying per-frame deltas incrementally
    // would accumulate rounding error at non-1.0 zoom and shear the group apart.
    for (auto& [nodeId, startPos] : selectionDragStartPositions) {
        for (auto* comp : content.getModules()) {
            if (comp == nullptr || comp == initiator || comp->getNodeId() != nodeId)
                continue;
            comp->setTopLeftPosition(startPos + delta);
            break;
        }
    }
}

void GraphEditor::finalizeSelectionDrag() {
    if (!selectionDragActive) {
        selectionDragStartPositions.clear();
        return;
    }

    // Resolve the group as a single rigid body: snap and de-overlap its bounding box, then apply
    // that one offset to every member. Running finalizeModuleDrag() per module would let members
    // spiral away from each other and destroy the layout the user arranged.
    std::vector<ModuleComponent*> members;
    juce::Rectangle<int> groupBounds;
    for (auto& [nodeId, startPos] : selectionDragStartPositions) {
        juce::ignoreUnused(startPos);
        for (auto* comp : content.getModules()) {
            if (comp == nullptr || comp->getNodeId() != nodeId)
                continue;
            members.push_back(comp);
            groupBounds = groupBounds.isEmpty() ? comp->getBounds() : groupBounds.getUnion(comp->getBounds());
            break;
        }
    }

    if (!members.empty() && !groupBounds.isEmpty()) {
        auto snapped = synth::LayoutUtil::snap(groupBounds.getTopLeft());
        // Collide against unselected modules only — members are moving together and must not be
        // treated as obstacles by one another.
        auto obstacles = collectModuleBoxes(/*selectedOnly=*/false, /*excludeSelected=*/true);
        auto clear = synth::LayoutUtil::findFreeSlot(snapped, groupBounds.getWidth(), groupBounds.getHeight(),
                                                     obstacles, juce::AudioProcessorGraph::NodeID{});
        auto offset = clear - groupBounds.getTopLeft();

        for (auto* comp : members) {
            comp->setTopLeftPosition(comp->getPosition() + offset);
            updateModulePosition(comp);
        }
    }

    // A WHOLE-macro selection (selectMacro() — chip drag, or Cmd/Shift-selecting a macro's every
    // member) moves the hull by the same uniform delta+offset every port widget above just moved
    // by, so it stays consistent for free (macroHullBounds' docs/macros/ports.md#how-a-port-is-drawn doc). A PARTIAL
    // selection — a marquee that happens to catch one port widget plus an unrelated module, without the macro's other
    // members — has no such guarantee: the hull (built from non-port members, selected or not) may not have moved by
    // that same delta, desyncing the port from its dock. Re-deriving here (idempotent — a no-op for the whole-macro
    // case, which already agrees) is the P8-15 fix F2 guard for that gap.
    macroController_.dockMacroPortWidgets();

    selectionDragActive = false;
    selectionDragStartPositions.clear();
    repaintCanvas();
}

// Discards the recorded drag origins without re-resolving any position — for a press that
// never moved (positions loaded from a preset are not necessarily grid-aligned, so a
// finalize on a zero-delta drag would visibly nudge the group).
void GraphEditor::cancelSelectionDrag() {
    selectionDragActive = false;
    selectionDragStartPositions.clear();
}

// FRO19: cancels a live drag when the component that armed it (ModuleComponent or
// MacroCardComponent) is destroyed/detached mid-gesture (see docs/layout/selection.md).
//
// ---- Live-drag cancellation on component destruction/detach (FRO19) --------------------------
//
// ModuleComponent::mouseDown arms selectionDragActive/dragPreviewActive itself and clears them only
// from its own mouseUp (ModuleComponentInteraction.cpp); MacroCardComponent::mouseDown arms
// selectionDragActive the same way via beginMacroCardDrag. Both rely on a real mouseUp that JUCE
// never delivers to a component already destroyed. An async graph rebuild landing mid-gesture (an
// AI patch apply's detachAllModuleComponents(), or the dragged node/macro itself vanishing via undo
// or a doc removal, which updateComponents()/syncMacroCards() then prune) can destroy that component
// with no mouseUp ever coming, leaving the drag-preview ghost and/or selection-drag bookkeeping
// stuck armed forever. Call sites cancel this unconditionally when EVERY component is going
// (detachAllModuleComponents), or only when the specific component being removed is the drag's own
// initiator (updateComponents' dragPreviewSelfId check, syncMacroCards' isBodyDragActive() check) —
// a non-initiating group member vanishing on its own is harmless: the initiator survives, its real
// mouseUp is still coming, and finalizeSelectionDrag's lookup simply skips a stale id it can't find.
// See docs/layout/selection.md.
void GraphEditor::cancelLiveDragGestures() {
    if (selectionDragActive)
        cancelSelectionDrag();
    if (dragDropController_.isDragPreviewActive())
        dragDropController_.endDragPreview();
    // FRO40: a component destroyed mid-Cmd/Ctrl-drag would otherwise leave the candidate hull
    // highlighted forever — no-op when nothing was armed, same as the two clears above.
    clearMacroDragCandidate();
}

// ---- Macro card drag (MacroCardComponent's own ComponentDragger calls these; FRO77 PR2) --------
//
// Thin wrappers over the plain multi-select drag primitives above — kept on GraphEditor rather
// than moved into MacroGroupController, since moving them would need four more GraphCanvasHost
// methods (beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag/cancelSelectionDrag) purely to
// call straight back into selection machinery that isn't a macro concern at all. See
// MacroGroupController.h's class comment.

void GraphEditor::beginMacroCardDrag(const juce::String& macroId) {
    macroController_.selectMacro(macroId, false);
    beginSelectionDrag();
}

void GraphEditor::dragMacroCardBy(const juce::String&, juce::Point<int> delta) {
    dragSelectionBy(delta, nullptr);
    // Matches ModuleComponent::mouseDrag's own per-frame repaint call exactly (one repaint per
    // drag tick), but goes through repaintCanvas() rather than a bare Component::repaint(): once
    // rebuildVisibleCables() anchors a collapsed macro's boundary cables on the LIVE
    // MacroCardComponent bounds (macroCableAnchorBounds), a bare repaint() would just re-paint
    // whatever cable geometry is already cached rather than recomputing it against the card's new
    // position. MacroCardComponent::mouseDrag deliberately does NOT also call
    // getParentComponent()->repaint() — this is the one repaint call for the gesture.
    repaintCanvas();
}

// Resolves the members' rigid-body snap AND the card's own position as one undo step.
void GraphEditor::finalizeMacroCardDrag(const juce::String& macroId, juce::Point<int> newCardTopLeft) {
    auto& graph = audioEngine.getGraph();
    auto doFinalize = [this, macroId, newCardTopLeft] {
        finalizeSelectionDrag();
        if (auto* m = macros.find(macroId))
            m->bounds.setPosition(synth::LayoutUtil::snap(newCardTopLeft));
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doFinalize);
    else
        doFinalize();

    repaintCanvas();
}

// A press that never moved — mirrors cancelSelectionDrag, no re-resolve.
void GraphEditor::cancelMacroCardDrag(const juce::String&) { cancelSelectionDrag(); }
