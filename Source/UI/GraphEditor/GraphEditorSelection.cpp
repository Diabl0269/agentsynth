// GraphEditorSelection.cpp
//
// Selection model (collectModuleBoxes/applySelectionChange/selectModule/etc.), marquee
// selection, and selection group-drag. GraphEditor is declared in GraphEditor.h; sibling
// GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "../ModuleComponent.h"

// ---------------------------------------------------------------------------------------
// Selection (issue #156)
// ---------------------------------------------------------------------------------------

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

void GraphEditor::pruneSelection() {
    if (selection.isEmpty())
        return;

    std::vector<juce::AudioProcessorGraph::NodeID> alive;
    for (auto* node : audioEngine.getGraph().getNodes())
        alive.push_back(node->nodeID);

    if (selection.retainOnly(alive))
        repaintCanvas();
}

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
        const auto portNeighbors = macroPortDeletionNeighbors(ids); // T154: capture BEFORE removal
        for (auto id : ids)
            graph.removeNode(id);
        for (auto n : portNeighbors)
            autoDeleteOrphanedMacroPort(n);
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
    // by, so it stays consistent for free (macroHullBounds' §5.4 doc). A PARTIAL selection — a
    // marquee that happens to catch one port widget plus an unrelated module, without the macro's
    // other members — has no such guarantee: the hull (built from non-port members, selected or
    // not) may not have moved by that same delta, desyncing the port from its dock. Re-deriving
    // here (idempotent — a no-op for the whole-macro case, which already agrees) is the P8-15 fix
    // F2 guard for that gap.
    dockMacroPortWidgets();

    selectionDragActive = false;
    selectionDragStartPositions.clear();
    repaintCanvas();
}

void GraphEditor::cancelSelectionDrag() {
    selectionDragActive = false;
    selectionDragStartPositions.clear();
}
