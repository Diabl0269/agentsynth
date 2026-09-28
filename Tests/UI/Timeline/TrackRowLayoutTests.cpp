// TrackRowLayoutTests.cpp
//
// synth::ui::TrackRowLayout in isolation -- the one vertical mapping the header column and the clip
// lanes share (docs/timeline/track-automation.md#row-geometry): row list, y -> row, lane rows
// mapping to their parent track for drags, drop boundaries around whole track blocks, and
// Automation-kind tracks never expanding.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TrackRowLayout.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <set>

using synth::TimelineDoc;
using synth::TrackKind;
using synth::ui::TrackRowLayout;

namespace {

constexpr int kRow = 40; // lane rows are laneRowHeightFor(40) == 30

struct LayoutRig {
    TimelineDoc doc;
    synth::TrackId a, b, c;
    synth::LaneId a1, a2, c1;
    std::set<std::int64_t> expanded;
    TrackRowLayout layout;

    LayoutRig() {
        a = doc.addTrack(TrackKind::Midi, "A");
        b = doc.addTrack(TrackKind::Audio, "B");
        c = doc.addTrack(TrackKind::Midi, "C");
        synth::AutomationLane::RangeSnapshot range;
        a1 = doc.addLane(a, "node-a", "cutoff", range);
        a2 = doc.addLane(a, "node-a", "resonance", range);
        c1 = doc.addLane(c, "node-c", "gain", range);
    }
    void build() {
        layout.rebuild(doc, [this](synth::TrackId id) { return expanded.count(id.value) != 0; }, kRow);
    }
};

} // namespace

TEST(TrackRowLayoutTest, CollapsedIsUniformRows) {
    LayoutRig rig;
    rig.build();
    ASSERT_EQ(rig.layout.getRows().size(), 3u);
    EXPECT_EQ(rig.layout.getTotalHeight(), 3 * kRow);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(rig.layout.trackRowTop(i), i * kRow);
        EXPECT_EQ(rig.layout.trackIndexAt(i * kRow + 5), i);
    }
    EXPECT_EQ(rig.layout.getLaneRowCount(), 0);
    EXPECT_EQ(rig.layout.rowForLane(rig.a1), nullptr) << "a collapsed track shows no lane row";
}

TEST(TrackRowLayoutTest, ExpandedTrackInsertsLaneRowsUnderIt) {
    LayoutRig rig;
    rig.expanded.insert(rig.a.value);
    rig.build();

    const int lane = TrackRowLayout::laneRowHeightFor(kRow);
    EXPECT_EQ(lane, 30);
    ASSERT_EQ(rig.layout.getRows().size(), 5u);
    EXPECT_EQ(rig.layout.getLaneRowCount(), 2);
    EXPECT_EQ(rig.layout.trackRowTop(0), 0);
    EXPECT_EQ(rig.layout.trackRowTop(1), kRow + 2 * lane) << "B moves down by A's two lane rows";
    EXPECT_EQ(rig.layout.trackRowTop(2), 2 * kRow + 2 * lane);
    EXPECT_EQ(rig.layout.getTotalHeight(), 3 * kRow + 2 * lane);
    EXPECT_EQ(rig.layout.trackBlockBottom(0), kRow + 2 * lane);

    const auto* row = rig.layout.rowForLane(rig.a2);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->top, kRow + lane);
    EXPECT_EQ(row->height, lane);
    EXPECT_EQ(row->trackIndex, 0);
    EXPECT_EQ(row->laneIndex, 1);
    EXPECT_TRUE(row->isLaneRow());
}

TEST(TrackRowLayoutTest, HitTestingTreatsLaneRowsAsNoTrackButDragsAsTheParentTrack) {
    LayoutRig rig;
    rig.expanded.insert(rig.a.value);
    rig.build();
    const int laneRowY = kRow + 5; // inside A's first lane row

    EXPECT_FALSE(rig.layout.trackIndexAt(laneRowY).has_value()) << "no clip may be authored on a lane row";
    ASSERT_NE(rig.layout.rowAt(laneRowY), nullptr);
    EXPECT_EQ(rig.layout.rowAt(laneRowY)->lane, rig.a1);
    EXPECT_EQ(rig.layout.trackIndexForDrag(laneRowY), 0) << "a drag over A's lanes is still over A";
    EXPECT_EQ(rig.layout.trackIndexForDrag(rig.layout.trackRowTop(2) + 1), 2);
    EXPECT_EQ(rig.layout.trackIndexForDrag(rig.layout.getTotalHeight() + 1), 3) << "extrapolates below";
    EXPECT_EQ(rig.layout.trackIndexForDrag(-1), -1) << "extrapolates above";
    EXPECT_FALSE(rig.layout.trackIndexAt(rig.layout.getTotalHeight()).has_value());
    EXPECT_EQ(rig.layout.rowAt(-1), nullptr);
}

TEST(TrackRowLayoutTest, DropBoundariesAreTrackBlockEdges) {
    LayoutRig rig;
    rig.expanded.insert(rig.a.value);
    rig.build();
    EXPECT_EQ(rig.layout.boundaryY(0), 0);
    EXPECT_EQ(rig.layout.boundaryY(1), rig.layout.trackRowTop(1)) << "below A's lanes, never between A and them";
    EXPECT_EQ(rig.layout.boundaryY(3), rig.layout.getTotalHeight());
    EXPECT_EQ(rig.layout.dropBoundaryAt(kRow + 2), 0) << "just inside A's lanes is nearest the top edge";
    EXPECT_EQ(rig.layout.dropBoundaryAt(rig.layout.trackRowTop(1) - 2), 1);
    EXPECT_EQ(rig.layout.dropBoundaryAt(100000), 3);
    EXPECT_EQ(rig.layout.dropBoundaryAt(-100), 0);
}

TEST(TrackRowLayoutTest, UniformDropBoundaryMatchesTheOldRounding) {
    LayoutRig rig;
    rig.build();
    for (int y = -10; y < 3 * kRow + 10; ++y)
        EXPECT_EQ(rig.layout.dropBoundaryAt(y), std::clamp((y + kRow / 2) / kRow, 0, 3)) << "y=" << y;
}

TEST(TrackRowLayoutTest, AutomationKindTracksNeverExpand) {
    LayoutRig rig;
    const auto automation = rig.doc.addTrack(TrackKind::Automation, "Automation");
    synth::AutomationLane::RangeSnapshot range;
    const auto global = rig.doc.addLane(automation, "node-g", "level", range);
    rig.expanded.insert(automation.value);
    rig.expanded.insert(12345); // a stale id naming no track is ignored
    rig.build();
    EXPECT_EQ(rig.layout.getLaneRowCount(), 0);
    EXPECT_EQ(rig.layout.rowForLane(global), nullptr);
}

TEST(TrackRowLayoutTest, LaneRowHeightHasAFloor) {
    EXPECT_EQ(TrackRowLayout::laneRowHeightFor(56), 42);
    EXPECT_EQ(TrackRowLayout::laneRowHeightFor(8), 18);
}
