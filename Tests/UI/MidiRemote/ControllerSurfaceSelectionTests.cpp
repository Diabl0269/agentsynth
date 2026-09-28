// ControllerSurfaceSelectionTests.cpp -- FRO270 (docs/control/midi-remote-ui.md#surface-centre):
// multi-select on the MIDI Remote surface -- plain/shift/cmd click, a marquee drag over empty grid
// space, Esc/empty-click-to-clear, and selection surviving a live rebuild -- driven through the
// real mouseDown/mouseDrag/mouseUp path (Source/UI/CLAUDE.md's "test the real mouse path"
// convention, Tests/Macros/MacroPortRealMouseDragTests.cpp's template).

#include "ControllerSurfaceTestHelpers.h"

#include <gtest/gtest.h>

#include <algorithm>

using midiremote_surface_test::fourControlModel;
using midiremote_surface_test::surfaceMouseEvent;
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

} // namespace

TEST(ControllerSurfaceSelectionTest, ShiftClickAddsToSelection) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    click(*findCell(surface, "knob1"));
    EXPECT_EQ(surface.getSelectedControlIds(), std::vector<juce::String>{"knob1"});

    click(*findCell(surface, "fader1"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    EXPECT_EQ(surface.getSelectedControlIds(), (std::vector<juce::String>{"knob1", "fader1"}));

    // Shift-clicking an already-selected cell doesn't remove it -- only cmd toggles.
    click(*findCell(surface, "knob1"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    EXPECT_EQ(surface.getSelectedControlIds(), (std::vector<juce::String>{"knob1", "fader1"}));
}

TEST(ControllerSurfaceSelectionTest, CmdClickTogglesSelection) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    click(*findCell(surface, "knob1"));
    click(*findCell(surface, "fader1"), juce::ModifierKeys(juce::ModifierKeys::commandModifier));
    EXPECT_EQ(surface.getSelectedControlIds(), (std::vector<juce::String>{"knob1", "fader1"}));

    // Cmd-click on an already-selected cell removes just that one.
    click(*findCell(surface, "knob1"), juce::ModifierKeys(juce::ModifierKeys::commandModifier));
    EXPECT_EQ(surface.getSelectedControlIds(), std::vector<juce::String>{"fader1"});
}

TEST(ControllerSurfaceSelectionTest, PlainClickOnAMultiSelectedCellDoesNotCollapseTheSelection) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    click(*findCell(surface, "knob1"));
    click(*findCell(surface, "fader1"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    ASSERT_EQ(surface.getSelectedControlIds().size(), 2u);

    // A plain (no-modifier) press on a cell already part of the multi-selection must not shrink it
    // on the press alone -- it may be the start of a group drag (ControllerSurfaceGroupDragTests.cpp).
    click(*findCell(surface, "knob1"));
    EXPECT_EQ(surface.getSelectedControlIds().size(), 2u);

    // A plain click on an UNselected cell still replaces the selection as before FRO270.
    click(*findCell(surface, "pad1"));
    EXPECT_EQ(surface.getSelectedControlIds(), std::vector<juce::String>{"pad1"});
}

// FRO331: a marquee now requires Shift (a plain drag on empty space pans instead -- see
// ControllerSurfaceViewTests.cpp) -- matching the module canvas's own Shift-starts-marquee
// convention (GraphEditorCanvas.cpp).
TEST(ControllerSurfaceSelectionTest, MarqueeOverEmptySpaceSelectsIntersectingCells) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    // fourControlModel()'s four cells occupy a 2x2 block starting at the grid margin -- drag a
    // rectangle covering the whole block from a point above/left of it to one below/right of it.
    const int margin = ControllerSurfaceComponent::kCellMargin;
    const int size = ControllerSurfaceComponent::kCellSize;
    const juce::Point<float> anchor(0.0f, 0.0f);
    const juce::Point<float> corner((float)(margin * 3 + size * 2), (float)(margin * 3 + size * 2));
    const juce::ModifierKeys shift(juce::ModifierKeys::shiftModifier);

    surface.mouseDown(surfaceMouseEvent(surface, anchor, anchor, false, shift));
    EXPECT_FALSE(surface.isMarqueeActiveForTest()) << "a press alone is not yet a marquee";
    surface.mouseDrag(surfaceMouseEvent(surface, corner, anchor, true, shift));
    EXPECT_TRUE(surface.isMarqueeActiveForTest());
    surface.mouseUp(surfaceMouseEvent(surface, corner, anchor, true, shift));

    EXPECT_FALSE(surface.isMarqueeActiveForTest()) << "the marquee itself clears once the drag ends";
    auto selected = surface.getSelectedControlIds();
    std::sort(selected.begin(), selected.end());
    EXPECT_EQ(selected, (std::vector<juce::String>{"button1", "fader1", "knob1", "pad1"}));
}

// FRO331: plain Shift+drag REPLACES the selection -- only Cmd+Shift+drag adds to it (below). A
// pre-existing selection outside the marquee's rectangle is dropped, same as a plain click would.
TEST(ControllerSurfaceSelectionTest, MarqueeWithPlainShiftReplacesTheSelection) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    surface.setSelectedControlId("button1"); // far corner, outside the marquee below

    const juce::Point<float> anchor(0.0f, 0.0f);
    const int margin = ControllerSurfaceComponent::kCellMargin;
    const int size = ControllerSurfaceComponent::kCellSize;
    const juce::Point<float> corner((float)(margin + size), (float)(margin + size)); // covers only knob1
    const juce::ModifierKeys shift(juce::ModifierKeys::shiftModifier);

    surface.mouseDown(surfaceMouseEvent(surface, anchor, anchor, false, shift));
    surface.mouseDrag(surfaceMouseEvent(surface, corner, anchor, true, shift));
    surface.mouseUp(surfaceMouseEvent(surface, corner, anchor, true, shift));

    EXPECT_EQ(surface.getSelectedControlIds(), std::vector<juce::String>{"knob1"});
}

TEST(ControllerSurfaceSelectionTest, MarqueeWithCmdShiftAddsToTheExistingSelection) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    surface.setSelectedControlId("button1"); // far corner, outside the marquee below

    const juce::Point<float> anchor(0.0f, 0.0f);
    const int margin = ControllerSurfaceComponent::kCellMargin;
    const int size = ControllerSurfaceComponent::kCellSize;
    const juce::Point<float> corner((float)(margin + size), (float)(margin + size)); // covers only knob1
    const juce::ModifierKeys cmdShift((int)(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::commandModifier));

    surface.mouseDown(surfaceMouseEvent(surface, anchor, anchor, false, cmdShift));
    surface.mouseDrag(surfaceMouseEvent(surface, corner, anchor, true, cmdShift));
    surface.mouseUp(surfaceMouseEvent(surface, corner, anchor, true, cmdShift));

    auto selected = surface.getSelectedControlIds();
    std::sort(selected.begin(), selected.end());
    EXPECT_EQ(selected, (std::vector<juce::String>{"button1", "knob1"}));
}

TEST(ControllerSurfaceSelectionTest, ClickOnEmptyGridWithNoDragClearsSelection) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());
    surface.setSelectedControlId("knob1");

    const juce::Point<float> pos(390.0f, 390.0f); // far from every cell
    surface.mouseDown(surfaceMouseEvent(surface, pos, pos, false));
    surface.mouseUp(surfaceMouseEvent(surface, pos, pos, false));

    EXPECT_TRUE(surface.getSelectedControlIds().empty());
}

TEST(ControllerSurfaceSelectionTest, SelectionSurvivesALiveRefreshRebuildDroppingVanishedIds) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    click(*findCell(surface, "knob1"));
    click(*findCell(surface, "fader1"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    click(*findCell(surface, "pad1"), juce::ModifierKeys(juce::ModifierKeys::shiftModifier));
    ASSERT_EQ(surface.getSelectedControlIds().size(), 3u);

    // A live refresh of the SAME profile that no longer has "pad1" (e.g. it was deleted from
    // elsewhere) -- the surviving two ids stay selected, "pad1" is silently dropped.
    auto cells = fourControlModel();
    cells.erase(std::remove_if(cells.begin(), cells.end(), [](const auto& c) { return c.control.id == "pad1"; }),
                cells.end());
    surface.setControls("profileA", cells);

    auto selected = surface.getSelectedControlIds();
    std::sort(selected.begin(), selected.end());
    EXPECT_EQ(selected, (std::vector<juce::String>{"fader1", "knob1"}));

    // A DIFFERENT profile clears the selection entirely, matching the pre-FRO270 behaviour.
    surface.setControls("profileB", fourControlModel());
    EXPECT_TRUE(surface.getSelectedControlIds().empty());
}
