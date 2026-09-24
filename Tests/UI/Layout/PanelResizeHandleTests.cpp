// PanelResizeHandleTests.cpp -- FRO231: synth::ui::PanelResizeHandle in isolation (a stand-in owner
// component, no MainComponent). The dock-level behaviour on every tab lives in
// Tests/UI/Mixer/MixerDockResizeTests.cpp.

#include "../Timeline/TimelinePanel/TimelinePanelTestFixture.h" // makeClickEvent / makeDragEvent
#include "UI/Layout/PanelResizeHandle.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

using synth::ui::PanelResizeHandle;

// A bottom-anchored "panel" with the handle laid along its top edge, the way every owner uses it.
struct HandleFixture {
    juce::Component root;
    juce::Component owner;
    PanelResizeHandle handle{owner};
    std::vector<int> live, committed;

    HandleFixture() {
        root.setSize(800, 600);
        root.addAndMakeVisible(owner);
        owner.setBounds(0, 380, 800, 220); // bottom edge pinned at 600
        owner.addAndMakeVisible(handle);
        handle.setBounds(0, 0, 800, PanelResizeHandle::kHeight);
        handle.onResize = [this](int h) { live.push_back(h); };
        handle.onResizeCommitted = [this](int h) { committed.push_back(h); };
    }
};

} // namespace

TEST(PanelResizeHandleTest, ShowsTheUpDownResizeCursor) {
    HandleFixture f;
    EXPECT_TRUE(f.handle.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor);
    EXPECT_EQ(PanelResizeHandle::kHeight, 5);
}

TEST(PanelResizeHandleTest, DragReportsTheHeightFromTheOwnersFixedBottomEdge) {
    HandleFixture f;
    f.handle.mouseDown(makeClickEvent(f.handle, {10.0f, 2.0f}));
    // Dragged 62 px above the grab point against a pinned bottom edge: 220 + 62.
    f.handle.mouseDrag(makeDragEvent(f.handle, {10.0f, -60.0f}, {10.0f, 2.0f}));
    ASSERT_EQ(f.live.size(), 1u);
    EXPECT_EQ(f.live.front(), 282);
    EXPECT_TRUE(f.committed.empty()) << "nothing is committed mid-drag";

    f.handle.mouseUp(makeClickEvent(f.handle, {10.0f, -60.0f}));
    ASSERT_EQ(f.committed.size(), 1u);
    EXPECT_EQ(f.committed.front(), 282);
    EXPECT_EQ(f.live.size(), 1u) << "mouse-up adds no extra resize step";
}

TEST(PanelResizeHandleTest, ReportedHeightIsUnclampedAndShrinksOnADownwardDrag) {
    HandleFixture f;
    f.handle.mouseDown(makeClickEvent(f.handle, {10.0f, 2.0f}));
    f.handle.mouseDrag(makeDragEvent(f.handle, {10.0f, 90.0f}, {10.0f, 2.0f}));
    f.handle.mouseDrag(makeDragEvent(f.handle, {10.0f, 400.0f}, {10.0f, 2.0f}));
    ASSERT_EQ(f.live.size(), 2u) << "every drag step fires the live callback";
    EXPECT_EQ(f.live[0], 132); // 220 - 88
    EXPECT_LT(f.live[1], 0) << "clamping belongs to the owner, not the handle";
}

// The owner moves its own top edge under the cursor on every callback; only the pinned bottom
// edge is a stable reference, so the same cursor position must keep reporting the same height.
TEST(PanelResizeHandleTest, TheOwnerMovingItsTopEdgeDoesNotMakeTheGestureChaseItself) {
    HandleFixture f;
    f.handle.mouseDown(makeClickEvent(f.handle, {10.0f, 2.0f}));
    f.handle.mouseDrag(makeDragEvent(f.handle, {10.0f, -60.0f}, {10.0f, 2.0f}));
    ASSERT_EQ(f.live.size(), 1u);
    EXPECT_EQ(f.live.back(), 282);

    // The owner grows to the requested height with its bottom edge pinned; the handle rides with
    // its top edge, so the same on-screen cursor is now +62 in handle-local coordinates -> y == 2.
    f.owner.setBounds(0, 600 - 282, 800, 282);
    f.handle.mouseDrag(makeDragEvent(f.handle, {10.0f, 2.0f}, {10.0f, 2.0f}));
    EXPECT_EQ(f.live.back(), 282) << "an unmoved cursor reports an unchanged height";
}

TEST(PanelResizeHandleTest, ACommitFiresOnlyForAGestureThatMoved) {
    HandleFixture f;

    // A drag that never began on the strip reports nothing.
    f.handle.mouseDrag(makeDragEvent(f.handle, {10.0f, -300.0f}, {10.0f, 2.0f}));
    f.handle.mouseUp(makeClickEvent(f.handle, {10.0f, -300.0f}));
    EXPECT_TRUE(f.live.empty());
    EXPECT_TRUE(f.committed.empty());

    // A stray click that never dragged commits nothing -- no settings write on a click.
    f.handle.mouseDown(makeClickEvent(f.handle, {10.0f, 2.0f}));
    f.handle.mouseUp(makeClickEvent(f.handle, {10.0f, 2.0f}));
    EXPECT_TRUE(f.live.empty());
    EXPECT_TRUE(f.committed.empty());

    // ...and it does not poison the next real gesture.
    f.handle.mouseDown(makeClickEvent(f.handle, {10.0f, 2.0f}));
    f.handle.mouseDrag(makeDragEvent(f.handle, {10.0f, -20.0f}, {10.0f, 2.0f}));
    f.handle.mouseUp(makeClickEvent(f.handle, {10.0f, -20.0f}));
    EXPECT_EQ(f.committed.size(), 1u);
}

TEST(PanelResizeHandleTest, HoverStateFlipsOnlyOnEnterAndExit) {
    HandleFixture f;
    EXPECT_FALSE(f.handle.isHovered());
    f.handle.mouseEnter(makeClickEvent(f.handle, {10.0f, 2.0f}));
    EXPECT_TRUE(f.handle.isHovered());
    f.handle.mouseEnter(makeClickEvent(f.handle, {40.0f, 3.0f})); // a second enter is not a change
    EXPECT_TRUE(f.handle.isHovered());
    f.handle.mouseExit(makeClickEvent(f.handle, {40.0f, 3.0f}));
    EXPECT_FALSE(f.handle.isHovered());
}

TEST(PanelResizeHandleTest, HoverBrightensTheHairlineAndAddsAWash) {
    HandleFixture f;
    auto render = [&f] { return f.handle.createComponentSnapshot(f.handle.getLocalBounds()); };

    const juce::Image idle = render();
    ASSERT_FALSE(idle.isNull());
    f.handle.mouseEnter(makeClickEvent(f.handle, {10.0f, 2.0f}));
    const juce::Image hovered = render();

    EXPECT_NE(idle.getPixelAt(10, 0), hovered.getPixelAt(10, 0)) << "the hairline changes colour";
    EXPECT_EQ(idle.getPixelAt(10, 3).getAlpha(), 0) << "idle draws nothing but the hairline";
    EXPECT_GT(hovered.getPixelAt(10, 3).getAlpha(), 0) << "hovered picks up a faint wash below it";
}
