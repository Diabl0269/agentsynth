// GraphEditorCanvasFrame.cpp
//
// Glue between GraphEditor and CanvasFrame (the growing patch frame): wires the frame's repaint, feeds it the union of
// every card on the canvas, and sizes the content component to the frame TARGET plus slack. GraphEditor is declared in
// GraphEditor.h. (docs/layout/layout.md#canvas-frame)

#include "GraphEditor.h"

#include "TestMode/TestModeStateSource.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/MacroGroupController/MacroNesting.h"
#include "UI/Layout/LayoutUtil.h"

#include <set>
#include <tuple>

namespace {
// A hidden 1x1 child (id "canvasFrameState") reporting the frame and every layout unit for test mode: where the frame
// is drawn now, the leftmost/topmost unit edge, and how many units sit outside it.
class CanvasFrameStateProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit CanvasFrameStateProbe(const GraphEditor& editor)
        : editor_(editor) {
        setComponentID("canvasFrameState");
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
        setAccessible(false);
    }
    void addTestModeState(juce::DynamicObject& into) const override {
        const auto frame = editor_.getCanvasFrameRect();
        int outside = 0, leftmost = 0, topmost = 0, count = 0;
        for (const auto& unit : editor_.getMacroController().buildLayoutUnits({})) {
            outside += frame.contains(unit.rect.toFloat()) ? 0 : 1;
            leftmost = count == 0 ? unit.rect.getX() : juce::jmin(leftmost, unit.rect.getX());
            topmost = count == 0 ? unit.rect.getY() : juce::jmin(topmost, unit.rect.getY());
            ++count;
        }
        into.setProperty("frameX", frame.getX());
        into.setProperty("frameY", frame.getY());
        into.setProperty("frameWidth", frame.getWidth());
        into.setProperty("frameHeight", frame.getHeight());
        into.setProperty("unitCount", count);
        into.setProperty("leftmostUnitX", leftmost);
        into.setProperty("topmostUnitY", topmost);
        into.setProperty("unitsOutsideFrame", outside);
        into.setProperty("allInsideFrame", outside == 0 && frame.getX() <= 0.0f && frame.getY() <= 0.0f);
    }

private:
    const GraphEditor& editor_;
};
} // namespace

void GraphEditor::configureCanvasFrame() {
    canvasFrame_.onChanged = [this] { content.repaint(); };
    canvasFrameProbe_ = std::make_unique<CanvasFrameStateProbe>(*this);
    addChildComponent(*canvasFrameProbe_);
    canvasFrameProbe_->setBounds(0, 0, 1, 1);
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
// A card that has no room left of the card it feeds without crossing the origin wall (Poly MIDI turned on beside a card
// near the left edge): every card on the canvas slides right by `dx` so the new card lands in the room made at x >= 0.
// The view stays put, so the new card shows at the wall and the rest of the patch moves right. Runs inside the
// caller's undo record.
void GraphEditor::slidePatchRightForNewCard(int dx) {
    if (dx <= 0)
        return;
    for (const auto& unit : macroController_.buildLayoutUnits({}))
        macroController_.moveUnitBy(unit.key, {dx, 0});
    macroController_.dockMacroPortWidgets();
}

void GraphEditor::slidePatchForEdgeDrop() {
    const auto d = edgeDrag_.takeOvershoot();
    if (d.isOrigin())
        return;
    for (const auto& unit : macroController_.buildLayoutUnits({}))
        if (!edgeDrag_.isMoving(unit.key))
            macroController_.moveUnitBy(unit.key, d);
    // A dragged card that joined an open macro while it was held is now part of a unit that just slid: it stays where
    // the pointer left it, so the macro's other cards slide and it does not.
    for (const auto& key : edgeDrag_.movingKeys())
        if (key.startsWith("n:") &&
            macroController_.isMemberOfOpenMacro(juce::AudioProcessorGraph::NodeID(
                static_cast<juce::uint32>(key.fromFirstOccurrenceOf("n:", false, false).getLargeIntValue()))))
            macroController_.moveUnitBy(key, {-d.x, -d.y});
    macroController_.dockMacroPortWidgets();

    panOffset -= d.toFloat() * zoomLevel;
    canvasFrame_.shiftCurrentBy(d.toFloat());
    updateTransform();
}
