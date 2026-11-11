// ControllerSurfaceEdgeScrollTests.cpp (docs/layout/layout.md, "Every drag follows the view"): a marquee or a cell
// drag held at the edge of the controller surface pans it, faster nearer the edge, and the box or the cell keeps
// tracking the still pointer; moving back inward, Esc or releasing stops it, the pan never goes past the content's
// origin, and a cell move is still committed once. Driven through the real mouseDown/mouseDrag/mouseUp handlers; the 30
// Hz timer is ticked by hand.

#include "ControllerSurfaceTestHelpers.h"
#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceComponent.h"

#include <algorithm>
#include <gtest/gtest.h>

using midiremote_surface_test::makeCellModel;
using midiremote_surface_test::surfaceMouseEvent;
using synth::ui::ControllerSurfaceCell;
using synth::ui::ControllerSurfaceComponent;

namespace {

constexpr int kWidth = 400;
constexpr int kHeight = 300;
const juce::ModifierKeys kShift(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier);
const juce::ModifierKeys kLeft(juce::ModifierKeys::leftButtonModifier);

// A knob at the top-left and one far right of the view (col 10 starts at x = 626) on row 2.
std::vector<ControllerSurfaceComponent::CellModel> knobAndFarRightModel() {
    return {makeCellModel("knob", "Knob", synth::ControlKind::knob, 0, 0),
            makeCellModel("far", "Far", synth::ControlKind::knob, 10, 2)};
}

struct Surface {
    ControllerSurfaceComponent surface;
    std::vector<std::vector<ControllerSurfaceComponent::MovedCell>> commits;

    Surface() {
        surface.setSize(kWidth, kHeight);
        surface.setControls("profileA", knobAndFarRightModel());
        surface.onControlsMoved = [this](const std::vector<ControllerSurfaceComponent::MovedCell>& m) {
            commits.push_back(m);
        };
    }
    float panX() const { return surface.getPanOffsetForTest().x; }
    void tick(int times) {
        for (int i = 0; i < times; ++i)
            surface.tickEdgeScrollForTest();
    }

    // The marquee: a Shift-press on empty grid space, dragged in the surface's own space.
    const juce::Point<float> marqueeFrom{200.0f, 130.0f}; // above the edge point, so the box has a height
    void pressMarquee() { surface.mouseDown(surfaceMouseEvent(surface, marqueeFrom, marqueeFrom, false, kShift)); }
    void dragMarquee(juce::Point<float> to) {
        surface.mouseDrag(surfaceMouseEvent(surface, to, marqueeFrom, true, kShift));
    }
    void releaseMarquee(juce::Point<float> at) {
        surface.mouseUp(surfaceMouseEvent(surface, at, marqueeFrom, true, kShift));
    }

    // The knob cell, pressed at its centre and dragged to points given in the surface's space.
    ControllerSurfaceCell* cell = nullptr;
    juce::Point<float> cellDown;
    void pressCell() {
        cell = surface.findCellForTest("knob");
        cellDown = cell->getLocalBounds().getCentre().toFloat();
        cell->mouseDown(surfaceMouseEvent(*cell, cellDown, cellDown, false, kLeft));
    }
    void dragCell(juce::Point<float> to) {
        cell->mouseDrag(surfaceMouseEvent(*cell, cell->getLocalPoint(&surface, to), cellDown, true, kLeft));
    }
    void releaseCell(juce::Point<float> at) {
        cell->mouseUp(surfaceMouseEvent(*cell, cell->getLocalPoint(&surface, at), cellDown, true, kLeft));
    }
    // Where the knob's centre is drawn, in the surface's space (after the pan and zoom).
    juce::Point<float> cellCentreOnSurface() const {
        return surface.getLocalPoint(cell, cell->getLocalBounds().getCentre().toFloat());
    }
};

const juce::Point<float> kRightEdge{(float)kWidth - 2.0f, 150.0f};

} // namespace

TEST(ControllerSurfaceEdgeScroll, AMarqueeHeldAtTheRightEdgePansTheSurfaceAndSelectsWhatScrolledIn) {
    Surface s;
    s.pressMarquee();
    s.dragMarquee(kRightEdge);
    ASSERT_TRUE(s.surface.isEdgeScrollActiveForTest()) << "the marquee armed the edge scroll";
    s.tick(25);
    EXPECT_LT(s.panX(), -200.0f) << "the surface panned to the right";
    EXPECT_TRUE(s.surface.isMarqueeActiveForTest());
    s.releaseMarquee(kRightEdge);
    const auto& sel = s.surface.getSelectedControlIds();
    EXPECT_NE(std::find(sel.begin(), sel.end(), "far"), sel.end()) << "the box grew over the control that scrolled in";
    EXPECT_EQ(std::find(sel.begin(), sel.end(), "knob"), sel.end());
    EXPECT_FALSE(s.surface.isEdgeScrollActiveForTest());
}

TEST(ControllerSurfaceEdgeScroll, ACellHeldAtTheRightEdgePansAndStaysUnderThePointerAndCommitsOnce) {
    Surface s;
    s.pressCell();
    s.dragCell(kRightEdge);
    ASSERT_TRUE(s.surface.isEdgeScrollActiveForTest()) << "the cell drag armed the edge scroll";
    s.tick(25);
    EXPECT_LT(s.panX(), -200.0f) << "the surface panned to the right";
    // The cell moves in whole cells, so it stays within one pitch of the still pointer.
    EXPECT_NEAR(s.cellCentreOnSurface().x, kRightEdge.x, (float)ControllerSurfaceCell::kCellPitch);
    EXPECT_TRUE(s.commits.empty()) << "nothing is written until the release";
    s.releaseCell(kRightEdge);
    ASSERT_EQ(s.commits.size(), 1u) << "the whole drag is one move (one undo step in the panel)";
    ASSERT_EQ(s.commits[0].size(), 1u);
    EXPECT_GT(s.commits[0][0].col, 8) << "the knob landed far past where the unscrolled view ended";
    EXPECT_EQ(s.commits[0][0].row, 2);
    EXPECT_FALSE(s.surface.isEdgeScrollActiveForTest());
}

TEST(ControllerSurfaceEdgeScroll, TheCloserToTheEdgeTheFasterItPans) {
    float step[2] = {};
    for (int i = 0; i < 2; ++i) {
        Surface s;
        s.pressMarquee();
        const juce::Point<float> at(i == 0 ? (float)kWidth - 30.0f : (float)kWidth - 2.0f, 150.0f);
        s.dragMarquee(at);
        s.tick(1);
        step[i] = -s.panX();
        s.releaseMarquee(at);
    }
    EXPECT_GT(step[0], 0.0f);
    EXPECT_GT(step[1], step[0]);
}

TEST(ControllerSurfaceEdgeScroll, MovingBackInwardOrReleasingStopsThePan) {
    Surface s;
    s.pressCell();
    s.dragCell(kRightEdge);
    s.tick(3);
    s.dragCell({200.0f, 150.0f});
    const float inward = s.panX();
    s.tick(5);
    EXPECT_FLOAT_EQ(s.panX(), inward) << "back inside the view, nothing pans";

    s.dragCell(kRightEdge);
    s.tick(2);
    s.releaseCell(kRightEdge);
    const float released = s.panX();
    s.tick(5);
    EXPECT_FLOAT_EQ(s.panX(), released) << "the release stops it for good";
}

TEST(ControllerSurfaceEdgeScroll, ThePanNeverGoesPastTheContentOrigin) {
    Surface s;
    s.pressMarquee();
    s.dragMarquee({1.0f, 1.0f});
    s.tick(10);
    EXPECT_FLOAT_EQ(s.panX(), 0.0f);
    EXPECT_FLOAT_EQ(s.surface.getPanOffsetForTest().y, 0.0f);
    s.releaseMarquee({1.0f, 1.0f});
}

// Regression test for FRO1688: Esc did not stop a pan held at the edge, unlike every other surface.
TEST(ControllerSurfaceEdgeScroll, EscStopsThePanUntilTheReleaseAndTheMoveStillCommitsOnce) {
    Surface s;
    s.pressCell();
    s.dragCell(kRightEdge);
    s.tick(3);
    ASSERT_TRUE(s.surface.isEdgeScrollActiveForTest());
    EXPECT_TRUE(s.surface.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))) << "Esc is taken by the pan";
    EXPECT_FALSE(s.surface.isEdgeScrollActiveForTest()) << "Esc stops the pan";
    const float stopped = s.panX();
    s.dragCell(kRightEdge);
    s.tick(5);
    EXPECT_FLOAT_EQ(s.panX(), stopped) << "held at the edge, it stays stopped for this drag";
    s.releaseCell(kRightEdge);
    EXPECT_EQ(s.commits.size(), 1u) << "the move still commits once, where it was left";

    s.pressMarquee();
    s.dragMarquee(kRightEdge);
    EXPECT_TRUE(s.surface.isEdgeScrollActiveForTest()) << "the next gesture scrolls again";
    s.releaseMarquee(kRightEdge);
}
