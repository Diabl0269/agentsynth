// TimelinePanelRulerTests.cpp
//
// synth::ui::TimelineRulerComponent: playhead/loop-zone press-drag interaction and the
// loop brace, driven with a raw synth::TransportService (never through MainComponent/
// AudioEngine); plus ruler-component paint smoke tests and the pure rulerTickPlanFor()
// density-band helper. Marker interaction lives in TimelinePanelMarkerTests.cpp.

#include "../../Source/AI/AIProvider.h"
#include "../../Source/AI/AIStateMapper/AIStateMapper.h"
#include "../../Source/AppUndoManager.h"
#include "../../Source/ProjectBundle.h"
#include "../../Source/Timeline/TimelineDoc.h"
#include "../../Source/Transport/TransportService.h"
#include "../../Source/UI/EdgeAutoScroll.h"
#include "../../Source/UI/Theme/AppLookAndFeel.h"
#include "../../Source/UI/Theme/BuiltInThemes.h"
#include "../../Source/UI/TimelinePanelComponent/TimelinePanelComponent.h"
#include "../../Source/UI/TrackColour.h"
#include "../../Source/UserSettings.h"
#include "MainComponent/MainComponent.h"
#include "TimelinePanelTestFixture.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// 3. Ruler/grid/zoom/scroll/snap/loop-brace.
// ============================================================================

// A press in the playhead zone seeks immediately — before mouseUp, so the cursor lands under the
// finger rather than waiting for the release.
TEST(TimelineRulerInteractionTest, PressInPlayheadZoneSeeksSnapped) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    // x = 180px -> beat 180/40 = 4.5 -> Beat-snapped (ties round up) to 5.0.
    const juce::Point<float> pos(180.0f, kPlayheadZoneY);
    ruler.mouseDown(makeClickEvent(ruler, pos));
    transport.tick(512); // drains the posted locateBeat() command

    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 5.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 1);

    // The release adds nothing new: same snapped beat, so the throttle suppresses a second post.
    ruler.mouseUp(makeClickEvent(ruler, pos));
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 5.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 1);
}

// Dragging in the playhead zone scrubs: the position follows the pointer, and a locateBeat() is
// posted only when the snapped beat actually changes.
TEST(TimelineRulerInteractionTest, DragInPlayheadZoneScrubsAndThrottlesPosts) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    const juce::Point<float> pressPos(100.0f, kPlayheadZoneY); // beat 2.5 -> snapped 3.0
    ruler.mouseDown(makeClickEvent(ruler, pressPos));
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 3.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 1);

    // Two moves that stay inside the same snapped beat (2.625 and 3.125 both snap to 3.0) post
    // nothing at all.
    ruler.mouseDrag(makeDragEvent(ruler, {105.0f, kPlayheadZoneY}, pressPos));
    ruler.mouseDrag(makeDragEvent(ruler, {125.0f, kPlayheadZoneY}, pressPos));
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 3.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 1);

    ruler.mouseDrag(makeDragEvent(ruler, {140.0f, kPlayheadZoneY}, pressPos)); // beat 3.5 -> 4.0
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 4.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 2);

    ruler.mouseDrag(makeDragEvent(ruler, {180.0f, kPlayheadZoneY}, pressPos)); // beat 4.5 -> 5.0
    ruler.mouseUp(makeDragEvent(ruler, {180.0f, kPlayheadZoneY}, pressPos));
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 5.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 3);

    // Scrubbing never touches the loop.
    EXPECT_FALSE(transport.getPositionSnapshot().looping);
}

// The zone is latched at mouseDown: a scrub that wanders up into the loop zone keeps scrubbing and
// never posts a loop.
TEST(TimelineRulerInteractionTest, GestureZoneIsStickyForTheWholeDrag) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
    transport.tick(512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    const juce::Point<float> pressPos(100.0f, kPlayheadZoneY);
    ruler.mouseDown(makeClickEvent(ruler, pressPos));
    EXPECT_EQ(ruler.getGestureZoneForTest(), synth::ui::TimelineRulerComponent::Zone::Playhead);

    // Same x travel, but the pointer has left the bottom band — and even the strip's top edge.
    ruler.mouseDrag(makeDragEvent(ruler, {180.0f, kLoopZoneY}, pressPos));
    ruler.mouseDrag(makeDragEvent(ruler, {180.0f, -20.0f}, pressPos));
    ruler.mouseUp(makeDragEvent(ruler, {180.0f, -20.0f}, pressPos));
    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_DOUBLE_EQ(snap.ppq, 5.0);
    EXPECT_EQ(ruler.getGestureZoneForTest(), synth::ui::TimelineRulerComponent::Zone::Playhead);
    // Loop untouched.
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);
}

// A click with no drag in the loop zone is inert: neither the loop nor the position moves. This is
// the whole point of the split — a stray click on the brace must not clear it.
TEST(TimelineRulerInteractionTest, ClickInLoopZoneChangesNothing) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
    ASSERT_TRUE(transport.locateBeat(1.0));
    transport.tick(512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    const juce::Point<float> pos(300.0f, kLoopZoneY);
    ruler.mouseDown(makeClickEvent(ruler, pos));
    ruler.mouseUp(makeClickEvent(ruler, pos));
    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_EQ(ruler.getGestureZoneForTest(), synth::ui::TimelineRulerComponent::Zone::Loop);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 0);
    EXPECT_DOUBLE_EQ(snap.ppq, 1.0);
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);
}

// Cmd+click (no drag) toggles looping off, keeping the prior bounds — from either zone.
TEST(TimelineRulerInteractionTest, CommandClickTogglesLoopOffKeepingBounds) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);
    for (const float y : {kLoopZoneY, kPlayheadZoneY}) {
        ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
        transport.tick(512);
        ASSERT_TRUE(transport.getPositionSnapshot().looping);

        const juce::Point<float> pos(100.0f, y);
        ruler.mouseDown(makeClickEvent(ruler, pos, cmd));
        ruler.mouseUp(makeClickEvent(ruler, pos, cmd));
        transport.tick(512);

        const auto snap = transport.getPositionSnapshot();
        EXPECT_FALSE(snap.looping) << "y = " << y;
        EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
        EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);
    }
    // Cmd never seeks, in either zone.
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 0);
}

// Press-drag-release in the loop zone sets the loop to the snapped [min,max] range; dragging
// leftwards (releasing before the press point) must still normalise start < end.
TEST(TimelineRulerInteractionTest, DragSetsLoopNormalisingReversedDrag) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 20.0;
    state.snap = synth::ui::TimelineViewState::Snap::Bar; // default 4/4 -> bars at beat 0,4,8,...

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    // Press at x=170 (beat 8.5 -> bar-snapped 8), drag/release at x=50 (beat 2.5 -> bar-snapped 4).
    const juce::Point<float> pressPos(170.0f, kLoopZoneY);
    const juce::Point<float> releasePos(50.0f, kLoopZoneY);

    ruler.mouseDown(makeClickEvent(ruler, pressPos));
    ruler.mouseDrag(makeDragEvent(ruler, releasePos, pressPos));
    ruler.mouseUp(makeDragEvent(ruler, releasePos, pressPos));

    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 8.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 0); // a loop drag never moves the playhead
}

// Hover indicator: tracks the pointer's half, clears on exit. The boundary row (y == height/2)
// belongs to the playhead zone.
TEST(TimelineRulerInteractionTest, HoveredZoneFollowsPointerHalfAndClearsOnExit) {
    using Zone = synth::ui::TimelineRulerComponent::Zone;

    synth::ui::TimelineViewState state;
    synth::ui::TimelineRulerComponent ruler(state); // no transport: hover is pure affordance
    ruler.setSize(800, 24);

    EXPECT_FALSE(ruler.getHoveredZoneForTest().has_value());

    ruler.mouseEnter(makeClickEvent(ruler, {100.0f, kLoopZoneY}));
    ASSERT_TRUE(ruler.getHoveredZoneForTest().has_value());
    EXPECT_EQ(*ruler.getHoveredZoneForTest(), Zone::Loop);

    ruler.mouseMove(makeClickEvent(ruler, {400.0f, kLoopZoneY})); // same zone, different x
    EXPECT_EQ(*ruler.getHoveredZoneForTest(), Zone::Loop);

    ruler.mouseMove(makeClickEvent(ruler, {400.0f, 12.0f})); // exactly on the split
    EXPECT_EQ(*ruler.getHoveredZoneForTest(), Zone::Playhead);

    ruler.mouseMove(makeClickEvent(ruler, {400.0f, kPlayheadZoneY}));
    EXPECT_EQ(*ruler.getHoveredZoneForTest(), Zone::Playhead);

    ruler.mouseExit(makeClickEvent(ruler, {400.0f, 40.0f}));
    EXPECT_FALSE(ruler.getHoveredZoneForTest().has_value());
}

// The locators stay VISIBLE when looping is off: a range that exists is always braced, just greyed
// out. Three states — range+armed, range+disarmed, no range at all.
TEST(TimelineRulerInteractionTest, LoopBraceStaysVisibleWhileDisarmed) {
    using Ruler = synth::ui::TimelineRulerComponent;
    using BraceState = Ruler::BraceState;

    // The pure rule first: only the range's existence decides whether a brace is drawn at all.
    EXPECT_EQ(Ruler::braceStateFor(true, 2.0, 6.0), BraceState::Active);
    EXPECT_EQ(Ruler::braceStateFor(false, 2.0, 6.0), BraceState::Inactive);
    EXPECT_EQ(Ruler::braceStateFor(false, 2.0, 2.0), BraceState::None);
    EXPECT_EQ(Ruler::braceStateFor(true, 6.0, 2.0), BraceState::None) << "a reversed range is not a range";

    // ...and that a disarmed brace is drawn in a visibly DIFFERENT colour from an armed one (the
    // muted text token, not a faded accent — the hover band is already accent at 10%).
    const juce::Colour accent(0xff00D1FF), textMuted(0xff8A93A0);
    EXPECT_EQ(Ruler::braceColourFor(BraceState::Active, accent, textMuted), accent);
    EXPECT_NE(Ruler::braceColourFor(BraceState::Inactive, accent, textMuted), accent);

    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    Ruler ruler(state);
    ruler.setSize(800, 24);
    EXPECT_EQ(ruler.getBraceStateForTest(), BraceState::None) << "no transport: nothing to brace";

    ruler.setTransport(&transport);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
    transport.tick(512);
    EXPECT_EQ(ruler.getBraceStateForTest(), BraceState::Active);

    // Cmd+click disarms it — the brace must survive, dimmed, rather than vanishing.
    const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);
    ruler.mouseDown(makeClickEvent(ruler, {100.0f, kLoopZoneY}, cmd));
    transport.tick(512);
    ASSERT_FALSE(transport.getPositionSnapshot().looping);
    EXPECT_EQ(ruler.getBraceStateForTest(), BraceState::Inactive);

    // A collapsed range has no locators to show at all.
    ASSERT_TRUE(transport.setLoop(3.0, 3.0, true));
    transport.tick(512);
    EXPECT_EQ(ruler.getBraceStateForTest(), BraceState::None);

    EXPECT_FALSE(ruler.createComponentSnapshot(ruler.getLocalBounds()).isNull());
}

// A plain click on the dimmed brace re-arms the existing range (the inverse of the Cmd+click that
// disarmed it). Outside the brace's span, a no-drag loop-zone click stays inert.
TEST(TimelineRulerInteractionTest, ClickOnDimmedBraceReArmsLooping) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, false)); // a range exists, looping off -> dimmed brace
    transport.tick(512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);
    ASSERT_EQ(ruler.getBraceStateForTest(), synth::ui::TimelineRulerComponent::BraceState::Inactive);

    // x = 400px -> beat 10: past the brace's [80, 240] px span, so nothing happens.
    const juce::Point<float> outside(400.0f, kLoopZoneY);
    ruler.mouseDown(makeClickEvent(ruler, outside));
    ruler.mouseUp(makeClickEvent(ruler, outside));
    transport.tick(512);
    auto snap = transport.getPositionSnapshot();
    EXPECT_FALSE(snap.looping) << "a click off the brace must not arm anything";
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);

    // x = 160px -> beat 4, inside [2, 6): re-arms, bounds untouched.
    const juce::Point<float> onBrace(160.0f, kLoopZoneY);
    ruler.mouseDown(makeClickEvent(ruler, onBrace));
    ruler.mouseUp(makeClickEvent(ruler, onBrace));
    transport.tick(512);
    snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 0) << "a loop-zone click never moves the playhead";

    // Clicking an ALREADY-armed brace stays inert (it is not a toggle — Cmd+click is).
    ruler.mouseDown(makeClickEvent(ruler, onBrace));
    ruler.mouseUp(makeClickEvent(ruler, onBrace));
    transport.tick(512);
    EXPECT_TRUE(transport.getPositionSnapshot().looping);
}

// paint() must not crash with a null transport (default-constructed ruler never had setTransport()
// called), at both a very zoomed-out and a very zoomed-in pixelsPerBeat.
TEST(TimelineRulerComponentTest, SnapshotSmokeAtTwoZoomsWithNullTransport) {
    synth::ui::TimelineViewState state;
    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setSize(800, 24);
    ASSERT_EQ(ruler.getTransport(), nullptr);

    state.pixelsPerBeat = synth::ui::TimelineViewState::kMinPixelsPerBeat;
    const juce::Image zoomedOut = ruler.createComponentSnapshot(ruler.getLocalBounds());
    EXPECT_FALSE(zoomedOut.isNull());
    EXPECT_EQ(zoomedOut.getWidth(), 800);
    EXPECT_EQ(zoomedOut.getHeight(), 24);

    // Also the widest band: at kMaxPixelsPerBeat the strip draws beat ticks AND their "bar.beat"
    // sub-labels, so this snapshot covers that paint path too.
    state.pixelsPerBeat = synth::ui::TimelineViewState::kMaxPixelsPerBeat;
    ASSERT_TRUE(synth::ui::rulerTickPlanFor(state.pixelsPerBeat, 4.0).drawBeatLabels);
    const juce::Image zoomedIn = ruler.createComponentSnapshot(ruler.getLocalBounds());
    EXPECT_FALSE(zoomedIn.isNull());
    EXPECT_EQ(zoomedIn.getWidth(), 800);
    EXPECT_EQ(zoomedIn.getHeight(), 24);
}

// The ruler's three density bands, asserted through the pure helper paint() itself calls — no
// painting, no font measurement, so the thresholds mean the same thing on every platform. The SNAP
// division is deliberately absent from all of this: the strip is a bars/beats reference, not a
// picture of the current grid.
TEST(TimelineRulerTickPlanTest, TooDenseForBeatTicksDrawsNeitherTicksNorLabels) {
    const auto plan = synth::ui::rulerTickPlanFor(synth::ui::kMinBeatTickSpacingPx - 0.5, 4.0);
    EXPECT_FALSE(plan.drawBeatTicks);
    EXPECT_FALSE(plan.drawBeatLabels);
    // The threshold itself is inclusive — and is comfortably above the ~6 px the ticks need to read
    // as separate marks.
    EXPECT_GE(synth::ui::kMinBeatTickSpacingPx, 6.0);
    EXPECT_TRUE(synth::ui::rulerTickPlanFor(synth::ui::kMinBeatTickSpacingPx, 4.0).drawBeatTicks);
}

TEST(TimelineRulerTickPlanTest, MiddleBandDrawsTicksWithoutLabels) {
    for (const double pixelsPerBeat :
         {synth::ui::kMinBeatTickSpacingPx, 24.0, synth::ui::kMinBeatLabelSpacingPx - 0.5}) {
        const auto plan = synth::ui::rulerTickPlanFor(pixelsPerBeat, 4.0);
        EXPECT_TRUE(plan.drawBeatTicks) << "pixelsPerBeat " << pixelsPerBeat;
        EXPECT_FALSE(plan.drawBeatLabels) << "pixelsPerBeat " << pixelsPerBeat;
    }
}

TEST(TimelineRulerTickPlanTest, WideBandDrawsTicksAndBarDotBeatLabels) {
    for (const double pixelsPerBeat : {synth::ui::kMinBeatLabelSpacingPx, 200.0}) {
        const auto plan = synth::ui::rulerTickPlanFor(pixelsPerBeat, 4.0);
        EXPECT_TRUE(plan.drawBeatTicks) << "pixelsPerBeat " << pixelsPerBeat;
        EXPECT_TRUE(plan.drawBeatLabels) << "a label always sits against a tick";
    }
}

TEST(TimelineRulerTickPlanTest, ABarOfOneBeatOrLessHasNoBeatsToMark) {
    // Every "beat" would land on a bar line, so a tick there would only thicken it.
    EXPECT_FALSE(synth::ui::rulerTickPlanFor(200.0, 1.0).drawBeatTicks);
    EXPECT_FALSE(synth::ui::rulerTickPlanFor(200.0, 0.0).drawBeatLabels);
    // And a degenerate zoom is inert rather than undefined.
    EXPECT_FALSE(synth::ui::rulerTickPlanFor(0.0, 4.0).drawBeatTicks);
}
