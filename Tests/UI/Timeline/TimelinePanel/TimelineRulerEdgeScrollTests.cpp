// TimelineRulerEdgeScrollTests.cpp -- a drag on the ruler follows the view: a marker, the loop range being drawn and
// the playhead being scrubbed, held at the ruler's edge, scroll the shared timeline view (faster nearer the edge) with
// the dragged thing staying under the still pointer; release, Esc and moving back in stop it; a marker move is still
// one undo step, and Esc puts a marker or the loop back.
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanelTestEvents.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/TimelineRulerComponent.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {

constexpr double kPixelsPerBeat = 40.0;
constexpr int kWidth = 800;

struct RulerRig {
    synth::ui::TimelineViewState state;
    synth::TransportService transport;
    synth::TimelineDoc doc;
    AppUndoManager undo;
    synth::ui::TimelineRulerComponent ruler{state};
    synth::MarkerId marker;
    int scrolled = 0;

    RulerRig() {
        state.pixelsPerBeat = kPixelsPerBeat;
        state.firstVisibleBeat = 0.0;
        state.snap = synth::ui::TimelineViewState::Snap::Quarter;
        transport.prepare(48000.0, 512);
        ruler.setTransport(&transport);
        ruler.setTimelineDoc(&doc);
        ruler.setUndoManager(&undo);
        ruler.setSize(kWidth, 24);
        ruler.onViewScrolledByDrag = [this] { ++scrolled; };
        marker = doc.addMarker(2.0, "Intro", 0xff000000);
        undo.beginNewTransaction();
    }

    juce::Point<float> flagCentre() const {
        for (const auto& flag : ruler.buildMarkerFlags())
            if (flag.id == marker)
                return flag.bounds.getCentre();
        return {-1.0f, -1.0f};
    }

    // Each tick, then the audio thread's drain of what it posted (a seek or a loop is a transport command).
    void ticks(int n) {
        for (int i = 0; i < n; ++i) {
            ruler.tickEdgeScrollForTest();
            transport.tick(512);
        }
    }
    void drain() { transport.tick(512); }
};

// The beat under x, as the ruler snaps it (a quarter note).
double snappedBeatAt(const RulerRig& rig, double x) { return std::round(rig.state.xToBeat(x)); }

} // namespace

TEST(TimelineRulerEdgeScroll, AMarkerHeldAtTheRightEdgeScrollsTheViewAndStaysUnderThePointer) {
    RulerRig rig;
    const auto press = rig.flagCentre();
    const juce::Point<float> edge{(float)kWidth - 4.0f, press.y};
    rig.ruler.mouseDown(makeClickEvent(rig.ruler, press));
    rig.ruler.mouseDrag(makeDragEvent(rig.ruler, edge, press));
    ASSERT_TRUE(rig.ruler.isEdgeScrollRunningForTest());
    const double grabOffset = rig.state.xToBeat(press.x) - 2.0;
    const double before = rig.ruler.getMarkerDragBeatForTest();

    rig.ticks(20);
    EXPECT_GT(rig.state.firstVisibleBeat, 0.0) << "the view scrolled right";
    EXPECT_EQ(rig.scrolled, 20) << "the panel hears each scroll (follow-playhead off, the strip repainted)";
    EXPECT_GT(rig.ruler.getMarkerDragBeatForTest(), before) << "the flag rides along";
    EXPECT_DOUBLE_EQ(rig.ruler.getMarkerDragBeatForTest(), std::round(rig.state.xToBeat(edge.x) - grabOffset))
        << "it is re-derived from the still pointer against the scrolled view";

    const double shown = rig.ruler.getMarkerDragBeatForTest();
    rig.ruler.mouseUp(makeDragEvent(rig.ruler, edge, press));
    EXPECT_FALSE(rig.ruler.isEdgeScrollRunningForTest()) << "release stops it";
    EXPECT_DOUBLE_EQ(rig.doc.getMarker(rig.marker)->beat, shown) << "the drop is what the drag showed";
    rig.undo.undo();
    EXPECT_DOUBLE_EQ(rig.doc.getMarker(rig.marker)->beat, 2.0) << "one undo puts it back";
    EXPECT_FALSE(rig.undo.canUndo()) << "the whole move is one undo step";
}

TEST(TimelineRulerEdgeScroll, ItIsFasterNearerTheEdgeAndStopsWhenThePointerMovesBackIn) {
    RulerRig shallow, deep;
    for (auto* rig : {&shallow, &deep}) {
        const auto press = rig->flagCentre();
        const float x = rig == &shallow ? (float)kWidth - 20.0f : (float)kWidth - 2.0f;
        rig->ruler.mouseDown(makeClickEvent(rig->ruler, press));
        rig->ruler.mouseDrag(makeDragEvent(rig->ruler, {x, press.y}, press));
        rig->ticks(5);
    }
    EXPECT_GT(deep.state.firstVisibleBeat, shallow.state.firstVisibleBeat);

    const auto press = deep.flagCentre();
    deep.ruler.mouseDrag(makeDragEvent(deep.ruler, {400.0f, press.y}, press));
    EXPECT_FALSE(deep.ruler.isEdgeScrollRunningForTest()) << "back in the middle: no scroll";
    deep.ruler.mouseUp(makeDragEvent(deep.ruler, {400.0f, press.y}, press));
}

TEST(TimelineRulerEdgeScroll, EscapeAtTheEdgeStopsTheScrollAndPutsTheMarkerBack) {
    RulerRig rig;
    const auto press = rig.flagCentre();
    const juce::Point<float> edge{(float)kWidth - 4.0f, press.y};
    rig.ruler.mouseDown(makeClickEvent(rig.ruler, press));
    rig.ruler.mouseDrag(makeDragEvent(rig.ruler, edge, press));
    rig.ticks(10);
    EXPECT_TRUE(rig.ruler.sendEscapeForTest());
    EXPECT_FALSE(rig.ruler.isEdgeScrollRunningForTest());
    rig.ruler.mouseDrag(makeDragEvent(rig.ruler, edge, press));
    rig.ruler.mouseUp(makeDragEvent(rig.ruler, edge, press));
    EXPECT_DOUBLE_EQ(rig.doc.getMarker(rig.marker)->beat, 2.0);
    EXPECT_FALSE(rig.undo.canUndo()) << "a cancelled drag writes nothing";
    EXPECT_FALSE(rig.ruler.sendEscapeForTest()) << "the listener went with the drag";
}

TEST(TimelineRulerEdgeScroll, ALoopDrawnToTheEdgeKeepsGrowingAndEscapePutsTheOldLoopBack) {
    RulerRig rig;
    rig.transport.setLoop(1.0, 3.0, false);
    rig.drain();
    const juce::Point<float> press{80.0f, 4.0f}; // the loop (top) half, beat 2
    const juce::Point<float> edge{(float)kWidth - 4.0f, 4.0f};
    rig.ruler.mouseDown(makeClickEvent(rig.ruler, press));
    rig.ruler.mouseDrag(makeDragEvent(rig.ruler, edge, press));
    rig.drain();
    ASSERT_TRUE(rig.ruler.isEdgeScrollRunningForTest());
    const double endBefore = rig.transport.getPositionSnapshot().loopEndPpq;
    rig.ticks(20);
    const auto snap = rig.transport.getPositionSnapshot();
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0) << "the start stays where the drag began";
    EXPECT_GT(snap.loopEndPpq, endBefore) << "the end follows the scrolled view";
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, snappedBeatAt(rig, edge.x)) << "and stays under the pointer";

    EXPECT_TRUE(rig.ruler.sendEscapeForTest());
    rig.drain();
    const auto back = rig.transport.getPositionSnapshot();
    EXPECT_DOUBLE_EQ(back.loopStartPpq, 1.0);
    EXPECT_DOUBLE_EQ(back.loopEndPpq, 3.0);
    EXPECT_FALSE(back.looping);
    rig.ruler.mouseUp(makeDragEvent(rig.ruler, edge, press));
    rig.drain();
    EXPECT_DOUBLE_EQ(rig.transport.getPositionSnapshot().loopEndPpq, 3.0) << "the release after Esc changes nothing";
}

TEST(TimelineRulerEdgeScroll, AScrubHeldAtTheLeftEdgeScrollsBackAndStopsAtBeatZero) {
    RulerRig rig;
    rig.state.firstVisibleBeat = 4.0;
    const juce::Point<float> press{400.0f, 18.0f}; // the playhead (bottom) half
    const juce::Point<float> edge{2.0f, 18.0f};
    rig.ruler.mouseDown(makeClickEvent(rig.ruler, press));
    rig.ruler.mouseDrag(makeDragEvent(rig.ruler, edge, press));
    rig.drain();
    ASSERT_GT(rig.transport.getPositionSnapshot().ppq, 3.5) << "the scrub reached the view's first beat";
    rig.ticks(60);
    EXPECT_DOUBLE_EQ(rig.state.firstVisibleBeat, 0.0) << "the view stops at beat 0";
    EXPECT_DOUBLE_EQ(rig.transport.getPositionSnapshot().ppq, 0.0) << "the playhead followed to the start";
    rig.ruler.mouseUp(makeDragEvent(rig.ruler, edge, press));
    EXPECT_FALSE(rig.ruler.isEdgeScrollRunningForTest());
}

TEST(TimelineRulerEdgeScroll, NothingScrollsWhileThePianoRollLendsTheRulerItsMapping) {
    RulerRig rig;
    synth::ui::TimelineViewState roll;
    roll.pixelsPerBeat = 80.0;
    rig.ruler.setMappingOverride(&roll, 44);
    const auto press = rig.flagCentre();
    rig.ruler.mouseDown(makeClickEvent(rig.ruler, press));
    rig.ruler.mouseDrag(makeDragEvent(rig.ruler, {(float)kWidth - 4.0f, press.y}, press));
    EXPECT_FALSE(rig.ruler.isEdgeScrollRunningForTest());
    rig.ruler.mouseUp(makeDragEvent(rig.ruler, {(float)kWidth - 4.0f, press.y}, press));
}
