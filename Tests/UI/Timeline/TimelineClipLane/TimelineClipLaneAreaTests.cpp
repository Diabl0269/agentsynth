// TimelineClipLaneAreaTests.cpp — core geometry + doc-change pruning + snapshot smoke test.
#include "TimelineClipLaneTestFixture.h"
#include <cmath>

TEST(TimelineClipLaneGeometryTest, GeometryMapsBeatsAndRows) {
    TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    constexpr int rowHeight = 56;

    const auto rect = TimelineClipLaneArea::computeClipRect(state, 2, 3.0, 2.5, rowHeight);

    EXPECT_EQ(rect.getX(), (int)std::llround(state.beatToX(3.0)));
    EXPECT_EQ(rect.getRight(), (int)std::llround(state.beatToX(5.5)));
    EXPECT_EQ(rect.getY(), 2 * rowHeight);
    EXPECT_EQ(rect.getHeight(), rowHeight);

    // Rows align with timelineTrackRowHeight: row N's y is exactly N * rowHeight, so a header row
    // and its clip row always share the same y regardless of scroll/zoom (both x-only concerns).
    const auto rowBelow = TimelineClipLaneArea::computeClipRect(state, 3, 3.0, 2.5, rowHeight);
    EXPECT_EQ(rowBelow.getY() - rect.getY(), rowHeight);

    // Horizontal scroll (firstVisibleBeat) shifts x, never y.
    state.firstVisibleBeat = 1.0;
    const auto scrolled = TimelineClipLaneArea::computeClipRect(state, 2, 3.0, 2.5, rowHeight);
    EXPECT_EQ(scrolled.getY(), rect.getY());
    EXPECT_LT(scrolled.getX(), rect.getX());

    // Vertical track scroll (trackScrollY) shifts y, never x.
    state.trackScrollY = 30.0;
    const auto vScrolled = TimelineClipLaneArea::computeClipRect(state, 2, 3.0, 2.5, rowHeight);
    EXPECT_EQ(vScrolled.getX(), scrolled.getX());
    EXPECT_EQ(vScrolled.getY(), 2 * rowHeight - 30);
}

TEST(TimelineClipLaneInteractionTest, DocChangeRefreshesAndPrunesSelection) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "A");
    f.selection.setSelection({clipId});
    ASSERT_TRUE(f.selection.contains(clipId));

    // A mutation straight on the doc (not through the lane area at all — e.g. the panel's own
    // TimelineDoc::Listener would call refreshFromDoc() on the next notification; this is that
    // call, exercised directly).
    ASSERT_TRUE(f.doc.removeClip(clipId));
    f.lane.refreshFromDoc();

    EXPECT_TRUE(f.selection.isEmpty());
}

TEST(TimelineClipLaneInteractionTest, SnapshotSmoke) {
    ClipLaneFixture f;
    const auto trackA = f.doc.addTrack(TrackKind::Midi, "Drums");
    const auto trackB = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto clip1 = f.doc.addClip(trackA, 0.0, 4.0, "Beat 1");
    f.doc.addClip(trackA, 4.0, 4.0, "Beat 2");
    f.doc.addClip(trackB, 2.0, 6.0, "Bassline");
    f.doc.addNote(clip1, makeNote(0.0, 36));
    f.doc.addNote(clip1, makeNote(1.0, 40, 0.5));
    f.selection.setSelection({clip1});

    f.lane.setSize(1000, 160);
    const juce::Image img = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getWidth(), 1000);
    EXPECT_EQ(img.getHeight(), 160);
}
