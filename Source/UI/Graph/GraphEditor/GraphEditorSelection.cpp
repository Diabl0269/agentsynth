// GraphEditorSelection.cpp
//
// Selection model (collectModuleBoxes/applySelectionChange/selectModule/etc.), marquee
// selection, and selection group-drag. GraphEditor is declared in GraphEditor.h; sibling
// GraphEditor*.cpp files in this directory hold the rest of the class.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "GraphEditorInternal.h" // GraphEditor::HealSplice's full definition (captureHealSplices)

#include "CanvasAccessibilityClip.h"
#include "UI/Graph/MacroGroupController/MacroNesting.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleStepOrder.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

// ---------------------------------------------------------------------------------------
// Selection
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

// Snapshots the cards of `ids` for the exit animation (CardGlideAnimatorGhosts.cpp); call it inside an open glide
// Scope, before the removal. The model change itself stays immediate.
void GraphEditor::noteCardExits(const std::vector<juce::AudioProcessorGraph::NodeID>& ids) {
    for (auto id : ids)
        if (auto* card = moduleComponentFor(id))
            cardGlide_.noteExit(card, id.uid);
}

// Removes every selected module as ONE undoable change, so Cmd+Z restores the whole group.
// Also GraphCanvasHost::deleteSelection() — MacroGroupController's deleteMacroAndMembers/
// removeMacroPort select the nodes to remove, then call this through the host.
void GraphEditor::deleteSelection() {
    auto ids = selection.getSelected();
    removeUndeletableOutputNodes(ids); // Audio Output never; Master while the mixer has channels
    if (ids.empty())
        return;

    auto& graph = audioEngine.getGraph();

    // One transaction for the whole group: Cmd+Z must bring back every module at once, not peel
    // them back one at a time. Deleting a collapsed macro card's members (see
    // deleteMacroAndMembers) flows through here too, so the macro half of the change has to be
    // captured in the SAME undo step as the graph half — recordGraphAndMacroChange, not
    // recordStructuralChange. updateComponents() prunes `macros` against whatever survives. This batch can also strand
    // a DIFFERENT macro port that isn't itself being deleted (an ordinary member was that port's only remaining
    // connection) — macroPortDeletionNeighbors() captures the candidates before removal, then
    // autoDeleteOrphanedMacroPort sweeps them after.
    auto doDelete = [this, ids, &graph] {
        modMatrix.clearRows();
        const auto portNeighbors = macroController_.macroPortDeletionNeighbors(ids); // Capture BEFORE removal
        // Capture BEFORE removal too -- walking off each deleted node's own connections to
        // find its surviving neighbours (and healing a whole run of them in one pass) needs the
        // graph as it stood before any of `ids` was removed.
        const auto healSplices = captureHealSplices(ids);
        // The macros that lose a member (a port included) shrink, so neighbours pushed aside when they grew may return.
        std::set<juce::String> shrunkMacros;
        for (auto id : ids)
            if (const auto* owner = macroController_.macroForNode(id))
                shrunkMacros.insert(owner->id);
        // graph.removeNode() below frees each node's processor
        // synchronously, same as a full graph-replacing restore -- but nothing here reaches
        // MixerPanelComponent::rebuild() until the NEXT unrelated graph edit (this path's own
        // reconcileTimelineBindingsOnly(), installed on onGraphStructureChanged, deliberately does
        // not rebuild the mixer -- see its own comment). A mixer column bound to one of these
        // NodeIDs (fader/pan/EQ thumbnail/send rows) would sit on a dangling pointer until that
        // eventual rebuild destroys it and dereferences it. Reuse the exact seam every
        // graph-replacing restore already unbinds through, same as MixerInsertList::
        // onBeforeNodeRemoved does for a single mixer-row removal -- unbind BEFORE freeing, not
        // after.
        fireBeforeDetachAllModuleComponents();
        for (auto id : ids)
            graph.removeNode(id);
        // Heal BEFORE the macro-port sweeps below, so a macro port a heal just gave a fresh
        // cable to is no longer orphaned by the time they run.
        healDeletedChain(healSplices);
        // The bounded one-extra-hop-through-an-attenuverter case runs on the same
        // pre-captured neighbour list, alongside (order doesn't matter — they touch disjoint node
        // kinds) the plain direct-neighbour sweep below.
        for (auto n : portNeighbors)
            macroController_.autoDeleteOrphanedAttenuverter(n);
        for (auto n : portNeighbors)
            macroController_.autoDeleteOrphanedMacroPort(n);
        selection.clear();
        updateComponents();
        for (const auto& macroId : shrunkMacros)
            macroController_.returnDisplacedNeighbours(macroId, /*keepBlocked=*/true);
    };

    CardGlideAnimator::Scope glideScope(cardGlide_); // each card shrinks away, see noteCardExits
    noteCardExits(ids);

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

namespace {
using DragStarts = std::vector<std::pair<juce::AudioProcessorGraph::NodeID, juce::Point<int>>>;

// Uuids of the nodes a selection drag is moving; empty when no macro is nested, since only a nested
// collapsed macro can be carried (macro_nesting::collapsedMacrosCarriedBy), so a flat patch pays nothing.
std::set<juce::String> movedUuidsForCarry(const synth::MacroSet& macros, const MacroGroupController& controller,
                                          const DragStarts& starts) {
    std::set<juce::String> uuids;
    const auto& all = macros.getAll();
    if (std::none_of(all.begin(), all.end(), [](const synth::Macro& m) { return m.parentId.isNotEmpty(); }))
        return uuids;
    for (const auto& [nodeId, startPos] : starts) {
        juce::ignoreUnused(startPos);
        if (auto uuid = controller.nodeUuidFor(nodeId); uuid.isNotEmpty())
            uuids.insert(uuid);
    }
    return uuids;
}

// A nested collapsed macro riding inside a moving container is drawn as its own card component,
// which the member drag does not move. Mid-drag (`commit` false) its live card follows at
// bounds + delta — `bounds` is untouched until the drop, so it is still the start position; on the
// drop (`commit` true) `bounds` itself shifts, so the macro does not reappear in its old place when
// its parent expands, and the card lands on it.
void shiftCarriedMacroCards(synth::MacroSet& macros, juce::OwnedArray<MacroCardComponent>& cards,
                            const std::set<juce::String>& movedUuids, juce::Point<int> delta, bool commit) {
    if (movedUuids.empty())
        return;
    for (const auto& id : macro_nesting::collapsedMacrosCarriedBy(macros, movedUuids)) {
        auto* macro = macros.find(id);
        if (commit)
            macro->bounds.setPosition(macro->bounds.getPosition() + delta);
        const auto cardPos = commit ? macro->bounds.getPosition() : macro->bounds.getPosition() + delta;
        for (auto* card : cards)
            if (card != nullptr && card->getMacroId() == id)
                card->setTopLeftPosition(cardPos);
    }
}
} // namespace

void GraphEditor::beginSelectionDrag(juce::AudioProcessorGraph::NodeID initiator) {
    selectionDragStartPositions.clear();
    for (auto* comp : content.getModules()) {
        // Dock cards (Master / Rec Tap / Audio Output) never travel with a selection: they stay put.
        if (comp != nullptr && selection.contains(comp->getNodeId()) && !isOutputDockNode(comp->getNodeId()))
            selectionDragStartPositions.emplace_back(comp->getNodeId(), comp->getPosition());
    }
    selectionDragActive = selectionDragStartPositions.size() > 1 && !isOutputDockNode(initiator);
    beginCanvasEdgeDrag();
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
    shiftCarriedMacroCards(macros, content.getMacroCards(),
                           movedUuidsForCarry(macros, macroController_, selectionDragStartPositions), delta, false);
}

void GraphEditor::finalizeSelectionDrag() {
    // Every exit path: a drop can both grow and shrink the canvas frame.
    const juce::ScopeGuard frameRefresh{[this] { refreshCanvasFrame(CanvasFrame::Mode::Animate); }};
    if (!selectionDragActive) {
        selectionDragStartPositions.clear();
        return;
    }
    slidePatchForEdgeDrop(); // a drag held at the canvas origin: the rest of the patch makes room

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
        // Layout units, like a single drop (resolvePlacement): collapsed cards and open hulls block, hidden members do
        // not.
        std::vector<juce::AudioProcessorGraph::NodeID> movingIds;
        for (const auto& [nodeId, startPos] : selectionDragStartPositions) {
            juce::ignoreUnused(startPos);
            movingIds.push_back(nodeId);
        }
        auto obstacles = macroController_.placementBlockers(movingIds);
        auto clear = synth::LayoutUtil::findFreeSlot(snapped, groupBounds.getWidth(), groupBounds.getHeight(),
                                                     obstacles, juce::AudioProcessorGraph::NodeID{});
        auto offset = clear - groupBounds.getTopLeft();

        for (auto* comp : members) {
            comp->setTopLeftPosition(comp->getPosition() + offset);
            updateModulePosition(comp);
        }

        // Every member moved by the same drag delta + snap offset; carried nested cards take exactly that.
        for (const auto& [nodeId, startPos] : selectionDragStartPositions)
            if (members.front()->getNodeId() == nodeId) {
                shiftCarriedMacroCards(macros, content.getMacroCards(),
                                       movedUuidsForCarry(macros, macroController_, selectionDragStartPositions),
                                       members.front()->getPosition() - startPos, true);
                break;
            }
    }

    // A WHOLE-macro selection (selectMacro() — chip drag, or Cmd/Shift-selecting a macro's every
    // member) moves the hull by the same uniform delta+offset every port widget above just moved
    // by, so it stays consistent for free (macroHullBounds' docs/macros/ports.md#how-a-port-is-drawn doc). A PARTIAL
    // selection — a marquee that happens to catch one port widget plus an unrelated module, without the macro's other
    // members — has no such guarantee: the hull (built from non-port members, selected or not) may not have moved by
    // that same delta, desyncing the port from its dock. Re-deriving here (idempotent — a no-op for the whole-macro
    // case, which already agrees) is the guard for that gap.
    macroController_.dockMacroPortWidgets();

    // A group drag can carry the whole selection outside the visible rect in one gesture.
    detail::applyCanvasAccessibilityClip(content.getModules(), content.getMacroCards(), getVisibleCanvasRect());

    selectionDragActive = false;
    selectionDragStartPositions.clear();
    reflowOutputDock(); // the group may now be the rightmost thing on the canvas
    repaintCanvas();
}

// Discards the recorded drag origins without re-resolving any position — for a press that
// never moved (positions loaded from a preset are not necessarily grid-aligned, so a
// finalize on a zero-delta drag would visibly nudge the group).
void GraphEditor::cancelSelectionDrag() {
    edgeDrag_.reset();
    selectionDragActive = false;
    selectionDragStartPositions.clear();
    // A drag that came back to where it started grew the frame on the way: fit it again.
    refreshCanvasFrame(CanvasFrame::Mode::Animate);
}

// Cancels a live drag when the component that armed it (ModuleComponent or
// MacroCardComponent) is destroyed/detached mid-gesture (see docs/layout/selection.md).
//
// ---- Live-drag cancellation on component destruction/detach ----------------------------------
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
    // A component destroyed mid-Cmd/Ctrl-drag would otherwise leave the candidate hull
    // highlighted forever — no-op when nothing was armed, same as the two clears above.
    clearMacroDragCandidate();
}

// ---- Macro card drag (MacroCardComponent's own ComponentDragger calls these) -------------------
//
// Thin wrappers over the plain multi-select drag primitives above — kept on GraphEditor rather
// than moved into MacroGroupController, since moving them would need four more GraphCanvasHost
// methods (beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag/cancelSelectionDrag) purely to
// call straight back into selection machinery that isn't a macro concern at all. See
// MacroGroupController.h's class comment.

void GraphEditor::beginMacroCardDrag(const juce::String& macroId) {
    macroController_.selectMacro(macroId, false);
    // Every drag tick writes the members' live positions into the graph, so undo's "before" is taken here, not at drop.
    if (undoManager)
        undoManager->captureBeforeState(audioEngine.getGraph());
    beginSelectionDrag();
}

void GraphEditor::dragMacroCardBy(const juce::String&, juce::Point<int> delta) {
    dragSelectionBy(delta, nullptr);
    refreshCanvasFrame(CanvasFrame::Mode::GrowOnly); // the frame steps out ahead of the card
    // Matches ModuleComponent::mouseDrag's own per-frame repaint call exactly (one repaint per
    // drag tick), but goes through repaintCanvas() rather than a bare Component::repaint(): once
    // rebuildVisibleCables() anchors a collapsed macro's boundary cables on the LIVE
    // MacroCardComponent bounds (macroCableAnchorBounds), a bare repaint() would just re-paint
    // whatever cable geometry is already cached rather than recomputing it against the card's new
    // position. MacroCardComponent::mouseDrag deliberately does NOT also call
    // getParentComponent()->repaint() — this is the one repaint call for the gesture.
    repaintCanvas();
}

// Resolves the card's own drop slot AND moves the hidden members with it as one undo step.
//
// The card is placed like any other drop: its rect goes through findFreeSlot against the placement blockers (visible
// modules, other collapsed cards, open hulls; never the macro itself, its hidden members or its ancestors). The members
// then take exactly the card's resolved delta from their recorded start positions, so card and members never drift
// apart. They are NOT resolved as a group of their own the way finalizeSelectionDrag does: they are hidden under the
// card, so a collision test on their own bounds would shove them away from a card that landed in free space.
void GraphEditor::finalizeMacroCardDrag(const juce::String& macroId, juce::Point<int> newCardTopLeft) {
    // Every exit path: a drop can both grow and shrink the canvas frame.
    const juce::ScopeGuard frameRefresh{[this] { refreshCanvasFrame(CanvasFrame::Mode::Animate); }};
    auto& graph = audioEngine.getGraph();
    auto doFinalize = [this, macroId, newCardTopLeft] {
        slidePatchForEdgeDrop(); // a card held at the canvas origin: the rest of the patch makes room
        auto* m = macros.find(macroId);
        if (m == nullptr || selectionDragStartPositions.empty()) {
            finalizeSelectionDrag();
            if (m != nullptr)
                m->bounds.setPosition(synth::LayoutUtil::snap(newCardTopLeft));
            reflowOutputDock();
            return;
        }

        std::vector<juce::AudioProcessorGraph::NodeID> movingIds;
        for (const auto& [nodeId, startPos] : selectionDragStartPositions) {
            juce::ignoreUnused(startPos);
            movingIds.push_back(nodeId);
        }
        const auto cardSize = macroController_.macroCableAnchorBounds(*m);
        const auto resolved = synth::LayoutUtil::findFreeSlot(
            synth::LayoutUtil::snap(newCardTopLeft), cardSize.getWidth(), cardSize.getHeight(),
            macroController_.placementBlockers(movingIds), juce::AudioProcessorGraph::NodeID{});
        const auto delta = resolved - m->bounds.getPosition();

        for (const auto& [nodeId, startPos] : selectionDragStartPositions)
            for (auto* comp : content.getModules())
                if (comp != nullptr && comp->getNodeId() == nodeId) {
                    comp->setTopLeftPosition(startPos + delta);
                    updateModulePosition(comp);
                    break;
                }
        shiftCarriedMacroCards(macros, content.getMacroCards(),
                               movedUuidsForCarry(macros, macroController_, selectionDragStartPositions), delta, true);
        m->bounds.setPosition(resolved);
        for (auto* card : content.getMacroCards())
            if (card != nullptr && card->getMacroId() == macroId)
                card->setTopLeftPosition(resolved);

        macroController_.dockMacroPortWidgets();
        detail::applyCanvasAccessibilityClip(content.getModules(), content.getMacroCards(), getVisibleCanvasRect());
        selectionDragActive = false;
        selectionDragStartPositions.clear();
        reflowOutputDock(); // a collapsed card dropped right of the dock pushes the dock along
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doFinalize, undoManager->takeCapturedGraphBeforeState());
    else
        doFinalize();

    repaintCanvas();
}

// A press that never moved — mirrors cancelSelectionDrag, no re-resolve.
void GraphEditor::cancelMacroCardDrag(const juce::String&) {
    if (undoManager)
        undoManager->takeCapturedGraphBeforeState(); // discard the unused "before" capture
    cancelSelectionDrag();
}
