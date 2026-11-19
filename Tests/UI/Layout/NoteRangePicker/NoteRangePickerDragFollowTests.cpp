// NoteRangePickerDragFollowTests.cpp -- an edge of the note range held at the end of the keyboard strip scrolls the
// strip, faster nearer (and past) its end, with the edge staying on the key under the pointer; moving back in and the
// release stop it, and Esc puts the range and the strip back with nothing committed, however long the button stays
// held.
#include "../ViewportDragFollow.h"
#include "UI/Layout/NoteRangePicker/NoteRangeGeometry.h"
#include "UI/Layout/NoteRangePicker/NoteRangePicker.h"
#include <gtest/gtest.h>

using synth::ui::NoteRangePicker;
using Part = NoteRangePicker::Part;

namespace {

juce::MouseEvent mouseAt(NoteRangePicker& comp, float x) {
    const juce::Point<float> p(x, 17.0f);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                            juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp,
                            juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
}

// One picker over C3..C4, its high edge pressed and lifted (past the click slop, clear of the strip's end).
struct HeldEdge {
    NoteRangePicker picker;
    int gestureEnds = 0;
    float width = 0.0f;
    float startScroll = 0.0f;

    HeldEdge() {
        picker.setSize(280, 34);
        picker.setRange(48, 60);
        picker.onGestureEnd = [this](int, int) { ++gestureEnds; };
        width = (float)picker.getWidth();
        startScroll = picker.getScroll();
        const float edge = highEdgeX();
        picker.mouseDown(mouseAt(picker, edge));
        picker.dragForTest(edge + 6.0f, 0.016, {});
    }
    float highEdgeX() { return picker.keyCentreX(picker.getHigh()) + picker.whiteKeyWidth() * 0.5f; }
    // One auto-repeat tick with the pointer resting at `x`.
    void tick(float x) { picker.dragForTest(x, 0.05, {}); }
};

} // namespace

TEST(NoteRangePickerDragFollow, AnEdgeHeldAtTheStripsEndScrollsTheStripAndStaysOnTheKeyUnderThePointer) {
    HeldEdge h;
    const float atEnd = h.width - 1.0f;
    for (int i = 0; i < 6; ++i) {
        const float before = h.picker.getScroll();
        const int highBefore = h.picker.getHigh();
        h.tick(atEnd);
        EXPECT_GT(h.picker.getScroll(), before) << "tick " << i << ": a pointer held at the end scrolls the strip";
        EXPECT_GE(h.picker.getHigh(), highBefore) << "tick " << i << ": the edge goes on with the strip";
        h.picker.finishMotionForTest();
        EXPECT_NEAR(h.highEdgeX(), atEnd, h.picker.whiteKeyWidth())
            << "tick " << i << ": the edge stays on the key under the pointer";
    }

    // Deeper is faster: the zone's first pixel crawls, the end itself runs faster, past it faster still.
    const float zone = h.picker.whiteKeyWidth() * synth::ui::noterange::kEdgeZoneKeys;
    const auto travel = [&h](float x) {
        const float before = h.picker.getScroll();
        h.tick(x);
        return h.picker.getScroll() - before;
    };
    const float shallow = travel(h.width - zone + 1.0f);
    const float deep = travel(h.width - 0.5f);
    const float past = travel(h.width + 40.0f);
    EXPECT_GT(shallow, 0.0f) << "inside the zone the strip moves";
    EXPECT_GT(deep, shallow) << "nearer the end is faster";
    EXPECT_GT(past, deep) << "past the end is faster still";

    // Back in the middle nothing scrolls, however long it rests; the release is one step and scrolls nothing more.
    h.tick(h.width * 0.5f);
    const float resting = h.picker.getScroll();
    for (int i = 0; i < 4; ++i)
        h.tick(h.width * 0.5f);
    EXPECT_EQ(h.picker.getScroll(), resting) << "moving back in stops the scroll";
    h.picker.mouseUp(mouseAt(h.picker, h.width * 0.5f));
    EXPECT_EQ(h.gestureEnds, 1) << "the whole drag, scroll included, is one step";
    h.tick(atEnd);
    EXPECT_EQ(h.picker.getScroll(), resting) << "after the release the strip stays put";

    // A press on the keys right at the strip's end stays put until the pointer moves toward that end.
    NoteRangePicker still;
    still.setSize(280, 34);
    still.setRange(48, 60);
    const float pressX = (float)still.getWidth() - 2.0f;
    viewport_drag_follow::expectPressInZoneHoldsStill(
        {[&] {
             still.mouseDown(mouseAt(still, pressX));
             still.dragForTest(pressX + 3.0f, 0.016, {});
             still.finishMotionForTest();
         },
         [&] { still.dragForTest(pressX + 3.0f, 0.05, {}); }, [&] { still.dragForTest(pressX + 6.0f, 0.016, {}); },
         [&] { return (double)still.getScroll(); }, [&] { still.mouseUp(mouseAt(still, pressX + 6.0f)); }});

    // Regression test for FRO2083: a real pointer dragged past the window keeps the strip scrolling, the edge in view.
    std::shared_ptr<HeldEdge> pastEdge;
    float pastX = 0.0f;
    viewport_drag_follow::expectPastWindowFollows(
        {[&] { pastEdge = std::make_shared<HeldEdge>(); },
         [&](float px) {
             pastX = pastEdge->width + px;
             pastEdge->tick(pastX);
         },
         [&] {
             pastEdge->tick(pastX);
             pastEdge->picker.finishMotionForTest();
         },
         [&] {
             return (double)pastEdge->picker.getScroll();
         }, // the edge sits on the key under the kept pointer, so its far side is at most one key past the end
         [&] { return pastEdge->highEdgeX() <= pastEdge->width + pastEdge->picker.whiteKeyWidth(); },
         [&] { pastEdge->picker.mouseUp(mouseAt(pastEdge->picker, pastX)); }});
}

TEST(NoteRangePickerDragFollow, EscWhileAnEdgeIsHeldAtTheEndPutsTheRangeAndTheStripBackAndCommitsNothing) {
    HeldEdge h;
    for (int i = 0; i < 4; ++i)
        h.tick(h.width - 1.0f);
    ASSERT_GT(h.picker.getScroll(), h.startScroll);
    ASSERT_GT(h.picker.getHigh(), 60);

    EXPECT_TRUE(h.picker.getStop(Part::HighEdge).keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)))
        << "the pressed edge has the focus, so Esc reaches the drag";
    h.picker.finishMotionForTest();
    EXPECT_EQ(h.picker.getLow(), 48);
    EXPECT_EQ(h.picker.getHigh(), 60) << "Esc puts the range back";
    EXPECT_EQ(h.picker.getScroll(), h.startScroll) << "and the strip";

    // The button is still down: the auto-repeat's events keep coming until the release, and scroll nothing.
    for (int i = 0; i < 4; ++i)
        h.tick(h.width - 1.0f);
    EXPECT_EQ(h.picker.getScroll(), h.startScroll) << "Esc stops the scroll";
    h.picker.mouseUp(mouseAt(h.picker, h.width - 1.0f));
    EXPECT_EQ(h.gestureEnds, 0) << "after Esc the release commits nothing";
    EXPECT_EQ(h.picker.getHigh(), 60);
}

TEST(NoteRangePickerDragFollow, EscWithNoDragIsLeftToThePopupAroundThePicker) {
    NoteRangePicker picker;
    picker.setSize(280, 34);
    picker.setRange(48, 60);
    EXPECT_FALSE(picker.getStop(Part::HighEdge).keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
}
