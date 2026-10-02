// GraphEditorCanvasFrame.cpp
//
// Glue between GraphEditor and CanvasFrame (the growing patch frame): wires the frame's repaint, feeds it the union of
// every card on the canvas, and sizes the content component to the frame TARGET plus slack. GraphEditor is declared in
// GraphEditor.h. (docs/layout/layout.md#canvas-frame)

#include "GraphEditor.h"

#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/MacroGroupController/MacroNesting.h"
#include "UI/Layout/LayoutUtil.h"

#include <set>
#include <tuple>

void GraphEditor::configureCanvasFrame() {
    canvasFrame_.onChanged = [this] { content.repaint(); };
}

void GraphEditor::applyContentBounds() {
    const auto target = canvasFrame_.target();
    content.setBounds(0, 0, target.getWidth() + CanvasFrame::kContentSlack,
                      target.getHeight() + CanvasFrame::kContentSlack);
}

// Live component bounds via buildLayoutUnits: loose modules, collapsed macro cards, open macro hulls, the output dock.
void GraphEditor::refreshCanvasFrame(CanvasFrame::Mode mode) {
    juce::Rectangle<int> extent;
    for (const auto& unit : macroController_.buildLayoutUnits({}))
        extent = extent.getUnion(unit.rect);
    canvasFrame_.update(extent, mode);
    applyContentBounds();
}

// ---- Left/top: hold the drag at the origin, slide the rest of the patch on drop (CanvasEdgeDrag.h) ----

namespace {
// How many of a macro's (non-port) members the drag moves, and how many it leaves behind.
std::pair<int, int> countMovingMembers(const synth::MacroSet& macros, const MacroGroupController& controller,
                                       const juce::String& macroId, const std::set<juce::uint32>& movingIds) {
    int in = 0, out = 0;
    for (const auto& uuid : macro_nesting::orderedDescendantMembers(macros, macroId)) {
        const auto* owner = macros.findByMember(uuid);
        if (owner != nullptr && owner->memberIsPort(uuid))
            continue;
        const auto nodeId = controller.resolveMemberNodeId(uuid);
        if (nodeId.uid != 0)
            ++(movingIds.count(nodeId.uid) != 0 ? in : out);
    }
    return {in, out};
}
} // namespace

// Arms the edge hold for the units the drag moves. A drag that moves only part of a macro (one member out of an open
// hull) stays unheld: its hull's edge is the macro's own concern (MacroGroupController::nudgeHullIntoCanvas).
void GraphEditor::beginCanvasEdgeDrag() {
    edgeDrag_.reset();
    std::set<juce::uint32> movingIds;
    for (const auto& [nodeId, startPos] : selectionDragStartPositions)
        movingIds.insert(nodeId.uid);
    if (movingIds.empty())
        return;

    juce::Rectangle<int> movingUnion;
    std::set<juce::String> movingKeys;
    bool openHull = false;
    for (const auto& unit : macroController_.buildLayoutUnits({})) {
        int in = 0, out = 0;
        if (unit.key.startsWith("n:")) {
            const auto uid = (juce::uint32)unit.key.fromFirstOccurrenceOf("n:", false, false).getLargeIntValue();
            (movingIds.count(uid) != 0 ? in : out) = 1;
        } else {
            const auto macroId = unit.key.fromFirstOccurrenceOf("m:", false, false);
            std::tie(in, out) = countMovingMembers(macros, macroController_, macroId, movingIds);
            if (in > 0 && macros.find(macroId) != nullptr && !macros.find(macroId)->collapsed)
                openHull = true;
        }
        if (in == 0)
            continue;
        if (out > 0)
            return;
        movingKeys.insert(unit.key);
        movingUnion = movingUnion.isEmpty() ? unit.rect : movingUnion.getUnion(unit.rect);
    }
    edgeDrag_.begin(movingUnion, {openHull ? synth::LayoutUtil::kMacroPortOverhang : 0, 0}, std::move(movingKeys));
}

// The drop of a drag that was held at the origin: everything the drag did not move slides right/down by the
// overshoot, so the dragged cards end up where the pointer left them relative to the rest. The view pans by the same
// amount, so nothing moves on screen except the frame growing out to the left/top. Runs inside the finalize's own
// undo record, before placement resolves the drop against the slid neighbours.
void GraphEditor::slidePatchForEdgeDrop() {
    const auto d = edgeDrag_.takeOvershoot();
    if (d.isOrigin())
        return;
    for (const auto& unit : macroController_.buildLayoutUnits({}))
        if (!edgeDrag_.isMoving(unit.key))
            macroController_.moveUnitBy(unit.key, d);
    macroController_.dockMacroPortWidgets();

    panOffset -= d.toFloat() * zoomLevel;
    canvasFrame_.shiftCurrentBy(d.toFloat());
    updateTransform();
}
