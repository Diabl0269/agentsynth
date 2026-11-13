// PianoRoll velocity-strip polish tests: the pen cursor over the strip under the Draw
// tool, the top-edge resize handle (driven with real mouse events on the handle: grows and shrinks
// the strip, the grid moves with it, clamps, persists on release, restores in a new roll), the
// plot's top padding clearing the handle, and the hover readout's opacity contract.

#include "PianoRollVelocityTestHelpers.h"

namespace {
using synth::ui::EditTool;
using synth::ui::PanelResizeHandle;
using synth::ui::PianoRollVelocityLane;

// A press on the handle and a drag to an absolute SCREEN y (the handle moves under the pointer as
// the strip resizes, so each step re-derives its handle-local y).
struct HandleDrag {
    explicit HandleDrag(PianoRollVelocityLane& l)
        : lane(l)
        , handle(l.getResizeHandle()) {
        handle.mouseDown(leftClick(handle, {10.0f, 2.0f}));
    }
    void to(int screenY) {
        const float localY = (float)(screenY - handle.getScreenY());
        handle.mouseDrag(leftDrag(handle, {10.0f, localY}, {10.0f, 2.0f}));
        lastLocalY = localY;
    }
    void release() { handle.mouseUp(leftClick(handle, {10.0f, lastLocalY})); }

    PianoRollVelocityLane& lane;
    PanelResizeHandle& handle;
    float lastLocalY = 2.0f;
};
} // namespace

// ============================================================================
// Cursor
// ============================================================================

TEST(PianoRollVelocityStripPolishTest, TheStripShowsThePenCursorUnderTheDrawToolAndTheArrowOtherwise) {
    VelocityLaneFixture f;
    f.roll.setActiveTool(EditTool::Draw);
    EXPECT_TRUE(f.lane().getMouseCursor() == f.roll.getMouseCursor());
    EXPECT_FALSE(f.lane().getMouseCursor() == juce::MouseCursor(juce::MouseCursor::NormalCursor));
    f.roll.setActiveTool(EditTool::Select);
    EXPECT_TRUE(f.lane().getMouseCursor() == juce::MouseCursor(juce::MouseCursor::NormalCursor));
    f.roll.setActiveTool(EditTool::Erase);
    EXPECT_TRUE(f.lane().getMouseCursor() == juce::MouseCursor(juce::MouseCursor::NormalCursor))
        << "only Draw makes the strip a pen";
}

// ============================================================================
// Resize handle
// ============================================================================

TEST(PianoRollVelocityStripPolishTest, TheHandleSitsOnTheStripsTopEdgeAndTheHeadOf127ClearsIt) {
    VelocityLaneFixture f;
    auto& handle = f.lane().getResizeHandle();
    EXPECT_EQ(handle.getBounds(), juce::Rectangle<int>(0, 0, f.lane().getWidth(), PanelResizeHandle::kHeight));
    EXPECT_EQ(handle.getParentComponent(), &f.lane());
    EXPECT_GE(f.lane().yForVelocity(127) - 3.0f, (float)PanelResizeHandle::kHeight) << "head radius is 3";
    f.roll.setSize(700, 320);
    EXPECT_EQ(handle.getWidth(), f.lane().getWidth()) << "kept laid out on a lane resize";
}

TEST(PianoRollVelocityStripPolishTest, DraggingTheHandleGrowsAndShrinksTheStripAndTheGridFollows) {
    VelocityLaneFixture f;
    auto& lane = f.lane();
    const int before = lane.getHeight();
    const int bottom = lane.getBottom();
    const int gridBefore = f.roll.canvasBottom();

    HandleDrag drag(lane);
    drag.to(lane.getScreenY() + 2 - 20);
    EXPECT_EQ(lane.getHeight(), before + 20);
    EXPECT_EQ(lane.getBottom(), bottom) << "the bottom edge stays put";
    EXPECT_EQ(f.roll.canvasBottom(), lane.getY());
    EXPECT_EQ(f.roll.canvasBottom(), gridBefore - 20);
    EXPECT_EQ(f.roll.getNoteGridBounds().getBottom(), lane.getY());
    EXPECT_EQ(f.roll.getKeysColumnBounds().getBottom(), lane.getY());

    drag.to(lane.getScreenY() + 2 + 10);
    EXPECT_EQ(lane.getHeight(), before + 10);
    drag.release();
    EXPECT_EQ(f.roll.getVelocityLaneHeight(), before + 10);
}

TEST(PianoRollVelocityStripPolishTest, TheStripClampsAtTheMinimumAndAtHalfTheBand) {
    VelocityLaneFixture f;
    auto& lane = f.lane();
    const int half = (f.roll.getHeight() - f.roll.canvasTop()) / 2;
    ASSERT_GT(half, PianoRollVelocityLane::kDefaultHeight) << "room to grow in this fixture";

    HandleDrag drag(lane);
    drag.to(lane.getScreenY() - 1000);
    EXPECT_EQ(lane.getHeight(), half);
    drag.to(lane.getScreenBounds().getBottom() + 1000);
    EXPECT_EQ(lane.getHeight(), PianoRollVelocityLane::kMinHeight);
    drag.release();
}

TEST(PianoRollVelocityStripPolishTest, TheHeightIsWrittenOnReleaseAndRestoredByANewRoll) {
    auto props = makeScaleAssistTestProps("PianoRollVelocityStripHeight");
    int dragged = 0;
    {
        VelocityLaneFixture f;
        f.roll.setPropertiesFile(props.get());
        HandleDrag drag(f.lane());
        drag.to(f.lane().getScreenY() + 2 - 24);
        EXPECT_FALSE(props->containsKey("pianoRollVelocityLaneHeight")) << "nothing written mid-drag";
        drag.release();
        dragged = f.lane().getHeight();
        EXPECT_EQ(dragged, PianoRollVelocityLane::kDefaultHeight + 24);
        EXPECT_EQ(props->getIntValue("pianoRollVelocityLaneHeight", -1), dragged);
    }
    {
        VelocityLaneFixture g;
        ASSERT_EQ(g.lane().getHeight(), PianoRollVelocityLane::kDefaultHeight);
        g.roll.setPropertiesFile(props.get());
        EXPECT_EQ(g.lane().getHeight(), dragged) << "restored";
        EXPECT_EQ(g.roll.canvasBottom(), g.lane().getY());
    }
    props->getFile().deleteFile();
}

TEST(PianoRollVelocityStripPolishTest, AStrayClickOnTheHandleChangesAndWritesNothing) {
    auto props = makeScaleAssistTestProps("PianoRollVelocityStripHeightClick");
    VelocityLaneFixture f;
    f.roll.setPropertiesFile(props.get());
    HandleDrag drag(f.lane());
    drag.release();
    EXPECT_EQ(f.lane().getHeight(), PianoRollVelocityLane::kDefaultHeight);
    EXPECT_FALSE(props->containsKey("pianoRollVelocityLaneHeight"));
    props->getFile().deleteFile();
}

// ============================================================================
// Readout fade
// ============================================================================

TEST(PianoRollVelocityStripPolishTest, HeadlessHoverShowsTheReadoutAtFullOpacityAndMouseExitClearsIt) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 60});
    auto& lane = f.lane();
    EXPECT_FLOAT_EQ(lane.getReadoutOpacity(), 0.0f);

    lane.mouseMove(hover(lane, f.at(1.0, 100)));
    EXPECT_EQ(lane.getReadoutNote(), bed.notes[0]);
    EXPECT_FLOAT_EQ(lane.getReadoutOpacity(), 1.0f) << "not showing: lands at once";

    lane.mouseMove(hover(lane, f.at(2.0, 60)));
    EXPECT_EQ(lane.getReadoutNote(), bed.notes[1]);
    EXPECT_FLOAT_EQ(lane.getReadoutOpacity(), 1.0f) << "stick to stick just moves the label";

    lane.mouseExit(hover(lane, f.at(2.0, 60)));
    EXPECT_FALSE(lane.getReadoutNote().isValid());
    EXPECT_FLOAT_EQ(lane.getReadoutOpacity(), 0.0f);
}

TEST(PianoRollVelocityStripPolishTest, AGestureShowsTheReadoutAtFullOpacityAndItFadesWhenNothingIsHovered) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100});
    auto& lane = f.lane();
    const auto press = f.at(1.0, 100);
    lane.mouseDown(leftClick(lane, press));
    EXPECT_EQ(lane.getReadoutNote(), bed.notes[0]);
    EXPECT_FLOAT_EQ(lane.getReadoutOpacity(), 1.0f);
    lane.mouseUp(leftClick(lane, press));
    EXPECT_FALSE(lane.getReadoutNote().isValid()) << "nothing hovered once the gesture ends";
    EXPECT_FLOAT_EQ(lane.getReadoutOpacity(), 0.0f);
}
