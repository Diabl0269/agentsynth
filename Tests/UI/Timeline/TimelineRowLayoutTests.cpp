// TimelineRowLayoutTests.cpp — the timeline's one row-geometry model: pure maths against the old
// `index * rowHeight` arithmetic, then the real panel / clip-lane path with a track that has extra
// room under its clip row (the shape automation sub-lanes will give it).

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanel/TimelinePanelTestEvents.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineRowLayout.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::TimelineRowLayout;

TEST(TimelineRowLayoutTest, UniformRowsMatchTheOldIndexTimesHeightMath) {
    for (const int rowHeight : {8, 28, 56, 168}) {
        const TimelineRowLayout layout(5, rowHeight);
        EXPECT_EQ(layout.totalHeight(), 5 * rowHeight);
        EXPECT_EQ(layout.trackRowHeight(), rowHeight);
        for (int i = 0; i < 5; ++i) {
            EXPECT_EQ(layout.trackTop(i), i * rowHeight);
            EXPECT_EQ(layout.trackSpan(i), juce::Range<int>(i * rowHeight, (i + 1) * rowHeight));
        }
        for (int y = 0; y < 5 * rowHeight; ++y) {
            EXPECT_EQ(layout.trackIndexAtY(y), y / rowHeight) << "y=" << y;
            EXPECT_TRUE(layout.isInTrackRow(y));
        }
    }
}

TEST(TimelineRowLayoutTest, OutOfRangeYAndEmptyLayoutsHitNothing) {
    const TimelineRowLayout layout(3, 50);
    EXPECT_EQ(layout.trackIndexAtY(-1), -1);
    EXPECT_EQ(layout.trackIndexAtY(150), -1) << "beyond the end";
    EXPECT_FALSE(layout.isInTrackRow(150));
    const TimelineRowLayout empty;
    EXPECT_EQ(empty.totalHeight(), 0);
    EXPECT_EQ(empty.trackIndexAtY(0), -1);
    EXPECT_EQ(TimelineRowLayout(0, 50).trackTop(2), 100) << "indices past the end keep the uniform pitch";
}

TEST(TimelineRowLayoutTest, ExtrasShiftLaterTracksAndAreHitAsTheirOwnTrack) {
    const TimelineRowLayout layout(3, 50, {30, 0, 10});
    EXPECT_EQ(layout.trackTop(0), 0);
    EXPECT_EQ(layout.trackTop(1), 80);
    EXPECT_EQ(layout.trackTop(2), 130);
    EXPECT_EQ(layout.trackSpan(0), juce::Range<int>(0, 80));
    EXPECT_EQ(layout.trackSpan(2), juce::Range<int>(130, 190));
    EXPECT_EQ(layout.trackExtraHeight(0), 30);
    EXPECT_EQ(layout.totalHeight(), 190);

    EXPECT_EQ(layout.hitAtY(49).trackIndex, 0);
    EXPECT_TRUE(layout.hitAtY(49).inClipRow);
    EXPECT_EQ(layout.hitAtY(50).trackIndex, 0) << "first pixel of the extra area is still track 0";
    EXPECT_FALSE(layout.hitAtY(50).inClipRow);
    EXPECT_EQ(layout.hitAtY(79).trackIndex, 0);
    EXPECT_EQ(layout.hitAtY(80).trackIndex, 1);
    EXPECT_TRUE(layout.hitAtY(80).inClipRow);
    EXPECT_EQ(layout.hitAtY(189).trackIndex, 2);
    EXPECT_FALSE(layout.hitAtY(189).inClipRow);
    EXPECT_EQ(layout.hitAtY(190).trackIndex, -1);
}

TEST(TimelineRowLayoutTest, MissingAndNegativeExtrasCountAsZero) {
    const TimelineRowLayout layout(3, 40, {-5});
    EXPECT_EQ(layout.totalHeight(), 120);
    EXPECT_EQ(layout.trackExtraHeight(7), 0);
}

TEST(TimelineRowLayoutTest, NearestTopMatchesTheOldRoundedRowDeltaWhenUniform) {
    const TimelineRowLayout layout(4, 56);
    for (int from = 0; from < 4; ++from)
        for (int dy = -300; dy <= 300; ++dy) {
            const int expected = from + (int)std::llround((double)dy / 56.0);
            const int actual = layout.trackIndexNearestTop(from, dy);
            EXPECT_EQ(actual, juce::isPositiveAndBelow(expected, 4) ? expected : -1) << from << "/" << dy;
        }
}

TEST(TimelineRowLayoutTest, NearestTopWalksThroughExtras) {
    const TimelineRowLayout layout(3, 50, {30, 0, 0}); // tops 0, 80, 130
    EXPECT_EQ(layout.trackIndexNearestTop(0, 70), 1);
    EXPECT_EQ(layout.trackIndexNearestTop(0, 30), 0) << "nearer track 0's top than track 1's";
    EXPECT_EQ(layout.trackIndexNearestTop(2, -40), 1);
}

// ---- Through the real panel: header column and clip lane read the same extras ----
// Track A gets one automation lane and is folded open, so its lane row is the extra area.

namespace {
struct ExtrasPanel {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    synth::TrackId t0, t1;
    int extra = 0;

    ExtrasPanel() {
        panel.setTimelineDoc(&doc);
        panel.setSize(1200, 400);
        panel.getViewState().pixelsPerBeat = 40.0;
        panel.getViewState().firstVisibleBeat = 0.0;
        panel.getViewState().snap = synth::ui::TimelineViewState::Snap::Bar;
        t0 = doc.addTrack(synth::TrackKind::Midi, "A");
        t1 = doc.addTrack(synth::TrackKind::Midi, "B");
        doc.addLane(t0, "node-uuid", "cutoff", {});
        panel.setTrackAutomationExpanded(t0, true);
        extra = panel.getClipLaneArea().getRowLayout().trackExtraHeight(0);
    }
};
} // namespace

TEST(TimelineRowLayoutIntegrationTest, HeadersAndClipRowsShiftTogetherPastAnExtraArea) {
    ExtrasPanel f;
    auto& lane = f.panel.getClipLaneArea();
    const int rowHeight = lane.getRowHeight();
    EXPECT_EQ(f.extra, 40) << "one lane row at the default height, unzoomed; the add button sits in its gutter";
    EXPECT_EQ(f.panel.getTrackHeaderAt(0)->getY(), 0);
    EXPECT_EQ(f.panel.getTrackHeaderAt(1)->getY(), rowHeight + f.extra);
    EXPECT_EQ(f.panel.getTrackHeaderAt(1)->getHeight(), rowHeight) << "a header is only the clip-row part";

    const auto layout = lane.getRowLayout();
    EXPECT_EQ(layout.trackTop(1), rowHeight + f.extra);
    EXPECT_EQ(layout.totalHeight(), 2 * rowHeight + f.extra);
}

// A real mouse-down at the shifted y lands on track 1's clip; the y track 1 WOULD have had under the
// old uniform math is track 0's extra area, which is no clip row at all.
TEST(TimelineRowLayoutIntegrationTest, ClickAtTheShiftedYHitsTrackOne) {
    ExtrasPanel f;
    auto& lane = f.panel.getClipLaneArea();
    const int rowHeight = lane.getRowHeight();
    const auto clipId = f.doc.addClip(f.t1, 4.0, 4.0, "B clip");
    ASSERT_TRUE(clipId.isValid());

    const auto rect = lane.getClipRect(clipId);
    EXPECT_EQ(rect.getY(), rowHeight + f.extra);

    const float x = (float)rect.getCentreX();
    const float shiftedY = (float)(rowHeight + f.extra + rowHeight / 2);
    EXPECT_EQ(lane.getRowLayout().trackIndexAtY((int)shiftedY), 1);
    EXPECT_FALSE(lane.getRowLayout().isInTrackRow(rowHeight + 10)) << "inside track 0's extra area";

    f.panel.getClipSelection().clear();
    lane.mouseDown(makeClickEvent(lane, {x, shiftedY}, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    EXPECT_TRUE(f.panel.getClipSelection().contains(clipId));
    lane.mouseUp(makeClickEvent(lane, {x, shiftedY}));

    // The old index*height y for track 1 now falls in track 0's extra area: no hit.
    f.panel.getClipSelection().clear();
    lane.mouseDown(makeClickEvent(lane, {x, (float)(rowHeight + rowHeight / 2) - 20.0f},
                                  juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    EXPECT_FALSE(f.panel.getClipSelection().contains(clipId));
    lane.mouseUp(makeClickEvent(lane, {x, (float)(rowHeight + rowHeight / 2) - 20.0f}));
}

TEST(TimelineRowLayoutIntegrationTest, TotalHeightIncludesTheExtraArea) {
    ExtrasPanel f;
    f.panel.setSize(1200, 400);
    const int total = f.panel.getClipLaneArea().getRowLayout().totalHeight();
    EXPECT_EQ(total, 2 * f.panel.getClipLaneArea().getRowHeight() + f.extra);
}

// ---- Per-track row heights and the zoom anchor ----

TEST(TimelineRowLayoutTest, RowHeightOverrideShortensOneTracksClipRow) {
    const TimelineRowLayout layout(3, 50, {0, 0, 40}, {0, 0, 26});
    EXPECT_EQ(layout.trackRowHeight(), 50) << "the default stays the default";
    EXPECT_EQ(layout.trackRowHeight(2), 26);
    EXPECT_EQ(layout.trackSpan(2), juce::Range<int>(100, 166));
    EXPECT_TRUE(layout.hitAtY(125).inClipRow);
    EXPECT_FALSE(layout.hitAtY(126).inClipRow) << "past the 26 px section row: its extra area";
    EXPECT_EQ(layout.totalHeight(), 166);
}

TEST(TimelineRowLayoutTest, MapContentYKeepsTheSamePlaceInATrackAcrossAZoom) {
    // Clip rows and lane rows double; the fixed 26 px row does not.
    const TimelineRowLayout before(3, 50, {40, 0, 40}, {0, 0, 26});
    const TimelineRowLayout after(3, 100, {80, 0, 80}, {0, 0, 26});
    EXPECT_DOUBLE_EQ(TimelineRowLayout::mapContentY(before, after, 25.0), 50.0) << "middle of track 0's clip row";
    EXPECT_DOUBLE_EQ(TimelineRowLayout::mapContentY(before, after, 70.0), 140.0) << "middle of track 0's lane";
    EXPECT_DOUBLE_EQ(TimelineRowLayout::mapContentY(before, after, 115.0), 230.0) << "middle of track 1";
    // Track 2 starts at 140 before and 280 after; its section row is fixed, its lane doubles.
    EXPECT_DOUBLE_EQ(TimelineRowLayout::mapContentY(before, after, 153.0), 293.0) << "in the fixed row";
    EXPECT_DOUBLE_EQ(TimelineRowLayout::mapContentY(before, after, 186.0), 280.0 + 26.0 + 40.0) << "mid lane";
}
