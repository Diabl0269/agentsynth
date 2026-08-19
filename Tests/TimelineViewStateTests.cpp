// TimelineViewStateTests.cpp
//
// synth::ui::TimelineViewState — the pure, headless beat<->pixel mapping shared by the
// timeline ruler and lanes grid. No JUCE component involved, so none of this is gated behind
// SYNTH_ENABLE_TIMELINE: the struct itself always compiles and is testable regardless of the flag
// (same reasoning TimelinePanelComponent's own isolation tests use).

#include "../Source/UI/TimelineViewState.h"
#include <gtest/gtest.h>
#include <random>

using synth::ui::TimelineViewState;
using Snap = TimelineViewState::Snap;

namespace {
// Relative-error helper for the round-trip precision assertions ("within 1e-9 relative").
void expectNearRelative(double actual, double expected, double relTol, const char* what) {
    const double scale = std::max(1.0, std::abs(expected));
    EXPECT_NEAR(actual, expected, relTol * scale) << what << ": actual=" << actual << " expected=" << expected;
}
} // namespace

// ---------------------------------------------------------------------------
// 1. MappingRoundTripsAtZoomExtremes
// ---------------------------------------------------------------------------
TEST(TimelineViewStateTest, MappingRoundTripsAtZoomExtremes) {
    for (double ppb : {TimelineViewState::kMinPixelsPerBeat, TimelineViewState::kMaxPixelsPerBeat}) {
        for (double firstVisible : {0.0, 1.0e6}) {
            TimelineViewState state;
            state.pixelsPerBeat = ppb;
            state.firstVisibleBeat = firstVisible;

            for (double beat : {firstVisible, firstVisible + 0.25, firstVisible + 3.5, firstVisible + 1000.0}) {
                const double x = state.beatToX(beat);
                const double roundTripped = state.xToBeat(x);
                expectNearRelative(roundTripped, beat, 1e-9, "xToBeat(beatToX(beat))");
            }

            for (double x : {0.0, 1.0, 500.0, 12345.0}) {
                const double beat = state.xToBeat(x);
                const double roundTripped = state.beatToX(beat);
                expectNearRelative(roundTripped, x, 1e-9, "beatToX(xToBeat(x))");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 2. ZoomAroundCursorKeepsAnchor
// ---------------------------------------------------------------------------
TEST(TimelineViewStateTest, ZoomAroundCursorKeepsAnchor) {
    std::mt19937 rng(12345); // seeded: deterministic across runs
    std::uniform_real_distribution<double> anchorDist(0.0, 1900.0);
    std::uniform_real_distribution<double> factorDist(0.5, 2.5);
    // pixelsPerBeat starts at the 24.0 default every iteration, so with factor >= 0.5 the zoomed
    // pixelsPerBeat never drops below 12.0: anchorX / pixelsPerBeat is at most 1900 / 12 ~= 158.3.
    // Starting firstVisibleBeat well above that keeps every iteration comfortably away from the
    // firstVisibleBeat >= 0 clamp, so the anchor invariant holds exactly (see the dedicated clamp
    // test below for the regime where it doesn't).
    std::uniform_real_distribution<double> startBeatDist(1000.0, 5000.0);

    for (int i = 0; i < 500; ++i) {
        TimelineViewState state;
        state.firstVisibleBeat = startBeatDist(rng);

        const double anchorX = anchorDist(rng);
        const double factor = factorDist(rng);
        const double anchorBeatBefore = state.xToBeat(anchorX);

        state.zoomAroundX(factor, anchorX);

        EXPECT_GE(state.pixelsPerBeat, TimelineViewState::kMinPixelsPerBeat);
        EXPECT_LE(state.pixelsPerBeat, TimelineViewState::kMaxPixelsPerBeat);
        EXPECT_GE(state.firstVisibleBeat, 0.0);

        const double anchorBeatAfter = state.xToBeat(anchorX);
        expectNearRelative(anchorBeatAfter, anchorBeatBefore, 1e-9, "anchor beat preserved across zoom");
    }
}

// Deep zoom-out at the left edge (firstVisibleBeat == 0) is the one regime where the
// firstVisibleBeat>=0 clamp can override the anchor invariant (see the comment on zoomAroundX) —
// checked separately so the random sweep above stays a clean "anchor always holds" assertion.
TEST(TimelineViewStateTest, ZoomAroundCursorClampsAtExtremesWithoutMisbehaving) {
    TimelineViewState state;
    state.pixelsPerBeat = TimelineViewState::kMaxPixelsPerBeat;
    state.firstVisibleBeat = 0.0;

    for (int i = 0; i < 50; ++i)
        state.zoomAroundX(0.1, 1800.0); // repeatedly zoom out hard, anchored near the right edge

    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, TimelineViewState::kMinPixelsPerBeat);
    EXPECT_GE(state.firstVisibleBeat, 0.0);

    for (int i = 0; i < 50; ++i)
        state.zoomAroundX(10.0, 100.0); // and back to max zoom-in

    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, TimelineViewState::kMaxPixelsPerBeat);
    EXPECT_GE(state.firstVisibleBeat, 0.0);
}

// ---------------------------------------------------------------------------
// 3. SnapTable
// ---------------------------------------------------------------------------
TEST(TimelineViewStateTest, SnapTableOffIsPassthrough) {
    TimelineViewState state;
    state.snap = Snap::Off;
    for (double beat : {0.0, 0.1, 3.14159, 1000.7}) {
        EXPECT_DOUBLE_EQ(state.snapBeat(beat, 4.0), beat);
        EXPECT_DOUBLE_EQ(state.snapBeat(beat, 3.0), beat);
    }
}

TEST(TimelineViewStateTest, SnapTableExactValuesAt4_4) {
    TimelineViewState state;
    const double beatsPerBar = 4.0;

    state.snap = Snap::Bar;
    EXPECT_DOUBLE_EQ(state.snapBeat(0.0, beatsPerBar), 0.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(1.9, beatsPerBar), 0.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(2.1, beatsPerBar), 4.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(7.9, beatsPerBar), 8.0);

    // Note values: a whole note is 4 beats (a beat is always a quarter note), so "1" spans a full
    // 4/4 bar — the DAW-conventional reading of the grid selector.
    state.snap = Snap::Whole;
    EXPECT_DOUBLE_EQ(state.snapBeat(1.9, beatsPerBar), 0.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(2.1, beatsPerBar), 4.0);

    state.snap = Snap::Half;
    EXPECT_DOUBLE_EQ(state.snapBeat(0.9, beatsPerBar), 0.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(1.1, beatsPerBar), 2.0);

    state.snap = Snap::Quarter;
    EXPECT_DOUBLE_EQ(state.snapBeat(1.4, beatsPerBar), 1.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(1.6, beatsPerBar), 2.0);

    // Eighth note = half a beat.
    state.snap = Snap::Eighth;
    EXPECT_DOUBLE_EQ(state.snapBeat(1.24, beatsPerBar), 1.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(1.26, beatsPerBar), 1.5);

    // Sixteenth note = 0.25 beat — the finest grid the selector offers.
    state.snap = Snap::Sixteenth;
    EXPECT_DOUBLE_EQ(state.snapBeat(1.1, beatsPerBar), 1.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(1.2, beatsPerBar), 1.25);
}

TEST(TimelineViewStateTest, RowHeightScaleClampsAndTrackScrollClamps) {
    TimelineViewState state;
    EXPECT_DOUBLE_EQ(state.rowHeightScale, 1.0);
    EXPECT_DOUBLE_EQ(state.trackScrollY, 0.0);

    state.scaleRowHeight(100.0);
    EXPECT_DOUBLE_EQ(state.rowHeightScale, TimelineViewState::kMaxRowHeightScale);
    state.scaleRowHeight(1.0e-6);
    EXPECT_DOUBLE_EQ(state.rowHeightScale, TimelineViewState::kMinRowHeightScale);
    state.scaleRowHeight(0.0); // non-positive factors are ignored, not clamped
    EXPECT_DOUBLE_EQ(state.rowHeightScale, TimelineViewState::kMinRowHeightScale);

    state.scrollTracksPx(500.0, 120.0);
    EXPECT_DOUBLE_EQ(state.trackScrollY, 120.0);
    state.scrollTracksPx(-500.0, 120.0);
    EXPECT_DOUBLE_EQ(state.trackScrollY, 0.0);
    state.scrollTracksPx(50.0, -10.0); // a shrunken content range clamps to 0, never negative
    EXPECT_DOUBLE_EQ(state.trackScrollY, 0.0);
}

TEST(TimelineViewStateTest, SnapEnabledFalseBehavesLikeOffButKeepsDivision) {
    TimelineViewState state;
    state.snap = Snap::Quarter;
    state.snapEnabled = false;

    // Effective division/snap: no grid, raw beats pass through.
    EXPECT_DOUBLE_EQ(state.divisionBeats(4.0), 0.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(1.37, 4.0), 1.37);

    // The chosen division survives underneath (what a one-shot quantise reads), and toggling back
    // on restores the magnetism.
    EXPECT_DOUBLE_EQ(state.divisionBeatsRaw(4.0), 1.0);
    state.snapEnabled = true;
    EXPECT_DOUBLE_EQ(state.snapBeat(1.37, 4.0), 1.0);
}

TEST(TimelineViewStateTest, SnapTableExactValuesAt3_4) {
    TimelineViewState state;
    const double beatsPerBar = 3.0;

    state.snap = Snap::Bar;
    EXPECT_DOUBLE_EQ(state.snapBeat(0.0, beatsPerBar), 0.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(1.4, beatsPerBar), 0.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(1.6, beatsPerBar), 3.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(4.4, beatsPerBar), 3.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(4.6, beatsPerBar), 6.0);
    EXPECT_DOUBLE_EQ(state.snapBeat(7.6, beatsPerBar), 9.0);

    // Whole/Half/Quarter/Eighth/Sixteenth are fixed note values (a beat is always a quarter
    // note), so beatsPerBar is irrelevant to them — only Bar consults it.
    state.snap = Snap::Quarter;
    EXPECT_DOUBLE_EQ(state.snapBeat(2.6, beatsPerBar), 3.0);
    state.snap = Snap::Sixteenth;
    EXPECT_DOUBLE_EQ(state.snapBeat(1.1, beatsPerBar), 1.0);
}

TEST(TimelineViewStateTest, SnapTableTiesRoundHalfUp) {
    TimelineViewState state;
    state.snap = Snap::Quarter;
    // Exactly halfway between 1 and 2 rounds up (toward +infinity, deterministic).
    EXPECT_DOUBLE_EQ(state.snapBeat(1.5, 4.0), 2.0);

    state.snap = Snap::Bar;
    EXPECT_DOUBLE_EQ(state.snapBeat(2.0, 4.0), 4.0); // exactly halfway between bar 0 and bar 1

    state.snap = Snap::Sixteenth;
    EXPECT_DOUBLE_EQ(state.snapBeat(1.0 + 0.25 / 2.0, 4.0), 1.25);
}
