// ControllerSurfaceGroupDragTests.cpp -- FRO270 (docs/control/midi-remote-ui.md#surface-centre):
// dragging any cell in a multi-selection moves the whole group together, keeping relative layout,
// clamped as a block at the grid's top-left bound, and refused outright if it would land a moved
// control on a cell an unselected control already occupies. Driven through the real
// mouseDown/mouseDrag/mouseUp path, same convention as ControllerSurfaceSelectionTests.cpp.

#include "ControllerSurfaceTestHelpers.h"

#include <gtest/gtest.h>

#include <algorithm>

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
    const int cellSize = ControllerSurfaceCell::kCellSize;
    const juce::Point<float> downPos = a->getLocalBounds().getCentre().toFloat();
    // Drag "a" down by two cells -- both members must move by the same delta.
    const juce::Point<float> dragPos = downPos + juce::Point<float>(0.0f, (float)cellSize * 2.0f);

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

    auto* a = findCell(surface, "a"); // at (0,0) -- the group's leftmost/topmost member
    const int cellSize = ControllerSurfaceCell::kCellSize;
    const juce::Point<float> downPos = a->getLocalBounds().getCentre().toFloat();
    // Try to drag left AND up by three cells each -- "a" is already at col 0/row 0, so the whole
    // block must be held at its current position (delta clamped to 0,0), not just "a" alone.
    const juce::Point<float> dragPos = downPos - juce::Point<float>((float)cellSize * 3.0f, (float)cellSize * 3.0f);

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
    const juce::Point<float> downPos = a->getLocalBounds().getCentre().toFloat();
    // Drag the group +5 cols/+5 rows -- "b" (1,0) would land on (6,5), "a" (0,0) on (5,5), which is
    // exactly where the unselected "bystander" sits.
    const juce::Point<float> dragPos = downPos + juce::Point<float>((float)cellSize * 5.0f, (float)cellSize * 5.0f);

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
