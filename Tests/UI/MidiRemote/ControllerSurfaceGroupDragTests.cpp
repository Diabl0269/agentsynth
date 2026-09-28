// ControllerSurfaceGroupDragTests.cpp -- FRO270 (docs/control/midi-remote-ui.md#surface-centre):
// dragging any cell in a multi-selection moves the whole group together, keeping relative layout,
// clamped as a block at the grid's top-left bound, and refused outright if it would land a moved
// control on a cell an unselected control already occupies. Driven through the real
// mouseDown/mouseDrag/mouseUp path, same convention as ControllerSurfaceSelectionTests.cpp.

#include "ControllerSurfaceTestHelpers.h"

#include <gtest/gtest.h>

#include <algorithm>

using midiremote_surface_test::DragDriver;
using midiremote_surface_test::eightPadsInARowModel;
using midiremote_surface_test::surfaceMouseEvent;
using midiremote_surface_test::twoAdjacentPlusBystanderModel;
using synth::ui::ControllerSurfaceCell;
using synth::ui::ControllerSurfaceComponent;

namespace {

ControllerSurfaceCell* findCell(ControllerSurfaceComponent& surface, const juce::String& controlId) {
    return midiremote_surface_test::findCell(surface, controlId);
}

void click(ControllerSurfaceCell& cell, juce::ModifierKeys mods = juce::ModifierKeys()) {
    const auto pos = cell.getLocalBounds().getCentre().toFloat();
    cell.mouseDown(surfaceMouseEvent(cell, pos, pos, false, mods));
    cell.mouseUp(surfaceMouseEvent(cell, pos, pos, false, mods));
}

ControllerSurfaceComponent::MovedCell* findMove(std::vector<ControllerSurfaceComponent::MovedCell>& moves,
                                                const juce::String& id) {
    for (auto& m : moves)
        if (m.controlId == id)
            return &m;
    return nullptr;
}

} // namespace

// twoAdjacentPlusBystanderModel(): "a" at (0,0), "b" at (1,0), "bystander" at (5,5).

TEST(ControllerSurfaceGroupDragTest, DraggingASelectedMemberMovesTheWholeGroupKeepingRelativeLayout) {
    ControllerSurfaceComponent surface;
    surface.setSize(600, 600);
    surface.setControls("profileA", twoAdjacentPlusBystanderModel());

    click(*findCell(surface, "a"));
    click(*findCell(surface, "b"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    ASSERT_EQ(surface.getSelectedControlIds().size(), 2u);

    std::vector<ControllerSurfaceComponent::MovedCell> moves;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>& m) { moves = m; };

    auto* a = findCell(surface, "a");
    const int cellPitch = ControllerSurfaceCell::kCellPitch; // on-screen distance between cells
    const juce::Point<float> downPos = a->getLocalBounds().getCentre().toFloat();
    // Drag "a" down by two cells -- both members must move by the same delta.
    const juce::Point<float> dragPos = downPos + juce::Point<float>(0.0f, (float)cellPitch * 2.0f);

    a->mouseDown(surfaceMouseEvent(*a, downPos, downPos, false));
    a->mouseDrag(surfaceMouseEvent(*a, dragPos, downPos, true));
    a->mouseUp(surfaceMouseEvent(*a, dragPos, downPos, true));

    ASSERT_EQ(moves.size(), 2u);
    auto* movedA = findMove(moves, "a");
    auto* movedB = findMove(moves, "b");
    ASSERT_NE(movedA, nullptr);
    ASSERT_NE(movedB, nullptr);
    EXPECT_EQ(movedA->col, 0);
    EXPECT_EQ(movedA->row, 2);
    EXPECT_EQ(movedB->col, 1); // relative offset from "a" (originally +1 col) is preserved
    EXPECT_EQ(movedB->row, 2);
}

TEST(ControllerSurfaceGroupDragTest, GroupDragIsClampedAsABlockAtTheGridsTopLeftBound) {
    ControllerSurfaceComponent surface;
    surface.setSize(600, 600);
    surface.setControls("profileA", twoAdjacentPlusBystanderModel());

    click(*findCell(surface, "a"));
    click(*findCell(surface, "b"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));

    std::vector<ControllerSurfaceComponent::MovedCell> moves;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>& m) { moves = m; };

    auto* a = findCell(surface, "a");                        // at (0,0) -- the group's leftmost/topmost member
    const int cellPitch = ControllerSurfaceCell::kCellPitch; // on-screen distance between cells
    const juce::Point<float> downPos = a->getLocalBounds().getCentre().toFloat();
    // Try to drag left AND up by three cells each -- "a" is already at col 0/row 0, so the whole
    // block must be held at its current position (delta clamped to 0,0), not just "a" alone.
    const juce::Point<float> dragPos = downPos - juce::Point<float>((float)cellPitch * 3.0f, (float)cellPitch * 3.0f);

    a->mouseDown(surfaceMouseEvent(*a, downPos, downPos, false));
    a->mouseDrag(surfaceMouseEvent(*a, dragPos, downPos, true));
    a->mouseUp(surfaceMouseEvent(*a, dragPos, downPos, true));

    // The clamped delta is (0,0) -- a "drag that ends back where it started" still fires
    // onControlsMoved (mirroring the single-control drop-on-own-cell case); both members must
    // report their ORIGINAL positions, block-clamped together.
    ASSERT_EQ(moves.size(), 2u);
    EXPECT_EQ(findMove(moves, "a")->col, 0);
    EXPECT_EQ(findMove(moves, "a")->row, 0);
    EXPECT_EQ(findMove(moves, "b")->col, 1);
    EXPECT_EQ(findMove(moves, "b")->row, 0);
}

// FRO331: same coordinate hazard as the single-cell case (ControllerSurfaceTests.cpp's
// MultiStepDragLandsExactlyOnOriginPlusDelta), but for a group -- the delta a multi-step drag on
// the origin cell reports is what every group member's move is computed from, so a group drag is
// exactly as exposed to it as a lone one.
TEST(ControllerSurfaceGroupDragTest, MultiStepGroupDragLandsExactlyOnOriginPlusDeltaForEveryMember) {
    ControllerSurfaceComponent surface;
    surface.setSize(600, 600);
    surface.setControls("profileA", twoAdjacentPlusBystanderModel());

    click(*findCell(surface, "a"));
    click(*findCell(surface, "b"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));

    std::vector<ControllerSurfaceComponent::MovedCell> moves;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>& m) { moves = m; };

    auto* a = findCell(surface, "a"); // (0,0); "b" is (1,0)
    DragDriver drag(*a);
    drag.stepToOffset(0, 1);
    drag.stepToOffset(0, 2);
    drag.stepToOffset(1, 2);
    drag.end();

    ASSERT_EQ(moves.size(), 2u);
    EXPECT_EQ(findMove(moves, "a")->col, 1);
    EXPECT_EQ(findMove(moves, "a")->row, 2);
    EXPECT_EQ(findMove(moves, "b")->col, 2); // relative offset from "a" preserved
    EXPECT_EQ(findMove(moves, "b")->row, 2);
}

TEST(ControllerSurfaceGroupDragTest, GroupDragOntoAnUnselectedControlIsRefused) {
    ControllerSurfaceComponent surface;
    surface.setSize(600, 600);
    surface.setControls("profileA", twoAdjacentPlusBystanderModel());

    click(*findCell(surface, "a"));
    click(*findCell(surface, "b"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));

    bool movedFired = false;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>&) { movedFired = true; };

    auto* a = findCell(surface, "a"); // (0,0); "b" is (1,0)
    const int cellSize = ControllerSurfaceCell::kCellSize;
    const int cellPitch = ControllerSurfaceCell::kCellPitch; // on-screen distance between cells
    const juce::Point<float> downPos = a->getLocalBounds().getCentre().toFloat();
    // Drag the group +5 cols/+5 rows -- "b" (1,0) would land on (6,5), "a" (0,0) on (5,5), which is
    // exactly where the unselected "bystander" sits.
    const juce::Point<float> dragPos = downPos + juce::Point<float>((float)cellPitch * 5.0f, (float)cellPitch * 5.0f);

    a->mouseDown(surfaceMouseEvent(*a, downPos, downPos, false));
    a->mouseDrag(surfaceMouseEvent(*a, dragPos, downPos, true));
    a->mouseUp(surfaceMouseEvent(*a, dragPos, downPos, true));

    EXPECT_FALSE(movedFired) << "landing on an unselected control's cell must refuse the whole group move";
    // Both cells must have sprung back to their pre-drag bounds.
    const int margin = ControllerSurfaceComponent::kCellMargin;
    EXPECT_EQ(findCell(surface, "a")->getBounds(), juce::Rectangle<int>(margin, margin, cellSize, cellSize));
    EXPECT_EQ(findCell(surface, "b")->getBounds(),
              juce::Rectangle<int>(margin + (cellSize + margin), margin, cellSize, cellSize));
}

// FRO331 bugfix (in-app repro): a group of pad1-4 (cols 0-3) shift-marqueed then dragged right by 8
// whole cells, ONE STEP PER CELL CROSSED (matching the ~20-mouseDrag-steps-for-8-cells shape a real
// slow drag delivers) so the live path visually crosses pad5-8's OCCUPIED cells (cols 4-7) before
// landing on the free cols 8-11 -- twoAdjacentPlusBystanderModel()'s bystander at col 5 is never on
// the path of the tests above, so it can't catch a bug specific to CROSSING an occupied cell mid-drag.
TEST(ControllerSurfaceGroupDragTest, MultiStepGroupDragCrossingOccupiedCellsLandsExactlyOnOriginPlusDelta) {
    ControllerSurfaceComponent surface;
    surface.setSize(900, 200);
    surface.setControls("profileA", eightPadsInARowModel());

    click(*findCell(surface, "pad1"));
    click(*findCell(surface, "pad2"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    click(*findCell(surface, "pad3"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    click(*findCell(surface, "pad4"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    ASSERT_EQ(surface.getSelectedControlIds().size(), 4u);

    std::vector<ControllerSurfaceComponent::MovedCell> moves;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>& m) { moves = m; };

    auto* pad1 = findCell(surface, "pad1"); // (0,0); pad2/3/4 at (1,0)/(2,0)/(3,0); pad5-8 at (4..7,0)
    DragDriver drag(*pad1);
    for (int dCols = 1; dCols <= 8; ++dCols)
        drag.stepToOffset(dCols, 0);
    drag.end();

    ASSERT_EQ(moves.size(), 4u);
    EXPECT_EQ(findMove(moves, "pad1")->col, 8);
    EXPECT_EQ(findMove(moves, "pad2")->col, 9);
    EXPECT_EQ(findMove(moves, "pad3")->col, 10);
    EXPECT_EQ(findMove(moves, "pad4")->col, 11);
}

// Same shape, single cell (not a group): pad1 alone dragged across pad2..pad8's occupied cells (all
// of cols 1-7) to land on the free col 8.
TEST(ControllerSurfaceGroupDragTest, MultiStepSingleCellDragCrossingOccupiedCellsLandsExactlyOnOriginPlusDelta) {
    ControllerSurfaceComponent surface;
    surface.setSize(900, 200);
    surface.setControls("profileA", eightPadsInARowModel());

    std::vector<ControllerSurfaceComponent::MovedCell> moves;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>& m) { moves = m; };

    auto* pad1 = findCell(surface, "pad1"); // (0,0); pad2..pad8 occupy (1,0)..(7,0)
    DragDriver drag(*pad1);
    for (int dCols = 1; dCols <= 8; ++dCols)
        drag.stepToOffset(dCols, 0);
    drag.end();

    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].controlId, "pad1");
    EXPECT_EQ(moves[0].col, 8);
}

// Same repro, but with ~20 FRACTIONAL (sub-cell) mouseDrag steps rather than one call per whole
// cell -- the shape a real, slow manual drag actually delivers -- to rule out "the bug only shows
// up between whole-cell synthetic steps."
TEST(ControllerSurfaceGroupDragTest, ManySubCellStepsCrossingOccupiedCellsStillLandsExactlyOnOriginPlusDelta) {
    ControllerSurfaceComponent surface;
    surface.setSize(900, 200);
    surface.setControls("profileA", eightPadsInARowModel());

    click(*findCell(surface, "pad1"));
    click(*findCell(surface, "pad2"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    click(*findCell(surface, "pad3"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    click(*findCell(surface, "pad4"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    ASSERT_EQ(surface.getSelectedControlIds().size(), 4u);

    std::vector<ControllerSurfaceComponent::MovedCell> moves;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>& m) { moves = m; };

    auto* pad1 = findCell(surface, "pad1");
    const float cellPitch = (float)ControllerSurfaceCell::kCellPitch; // on-screen distance between cells
    const auto downLocal = pad1->getLocalBounds().getCentre().toFloat();
    const auto downParent = pad1->getPosition().toFloat() + downLocal;
    pad1->mouseDown(surfaceMouseEvent(*pad1, downLocal, downLocal, false));

    juce::Point<float> lastLocal = downLocal;
    constexpr int numSteps = 20;
    for (int i = 1; i <= numSteps; ++i) {
        const float fraction = (float)i / (float)numSteps;
        const auto targetParent = downParent + juce::Point<float>(8.0f * cellPitch * fraction, 0.0f);
        lastLocal = targetParent - pad1->getPosition().toFloat(); // pad1's CURRENT (possibly moved) bounds
        pad1->mouseDrag(surfaceMouseEvent(*pad1, lastLocal, downLocal, true));
    }
    pad1->mouseUp(surfaceMouseEvent(*pad1, lastLocal, downLocal, true));

    ASSERT_EQ(moves.size(), 4u);
    EXPECT_EQ(findMove(moves, "pad1")->col, 8);
    EXPECT_EQ(findMove(moves, "pad4")->col, 11);
}
