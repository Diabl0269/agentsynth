// TimelineDocAutomationSpanTests.cpp
//
// TimelineDoc::transferAutomationSpans -- the batched primitive behind "automation follows events"
// (docs/timeline/track-automation.md#automation-follows-events): move/copy/remove a clip span's
// points on the source track's OWN lanes, all in one mutation.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <vector>

using synth::AutomationLane;
using synth::TimelineDoc;
using synth::TrackKind;
using Edit = TimelineDoc::AutomationSpanEdit;

namespace {

struct CountingListener : TimelineDoc::Listener {
    int calls = 0;
    void timelineChanged(const TimelineDoc&) override { ++calls; }
};

struct SpanRig {
    TimelineDoc doc;
    synth::TrackId a, b, c;
    synth::LaneId aCutoff, aLevel, bCutoff;

    SpanRig() {
        a = doc.addTrack(TrackKind::Midi, "A");
        b = doc.addTrack(TrackKind::Midi, "B");
        c = doc.addTrack(TrackKind::Midi, "C");
        AutomationLane::RangeSnapshot range;
        range.minValue = 0.0f;
        range.maxValue = 100.0f;
        aCutoff = doc.addLane(a, "synth-a", "cutoff", range);
        aLevel = doc.addLane(a, "strip-a", "level", range);
        bCutoff = doc.addLane(b, "synth-b", "cutoff", range);
        for (double beat : {0.0, 1.0, 2.0, 3.5, 4.0, 6.0})
            doc.addBreakpoint(aCutoff, beat, beat * 10.0);
        doc.addBreakpoint(aLevel, 1.0, 50.0);
        doc.addBreakpoint(bCutoff, 9.0, 90.0);
    }
    std::vector<double> beats(synth::LaneId lane) const {
        std::vector<double> out;
        for (const auto& p : doc.getLane(lane)->points)
            out.push_back(p.beat);
        return out;
    }
    static Edit move(synth::TrackId src, double start, double end, synth::TrackId dst, double destStart) {
        Edit e;
        e.kind = Edit::Kind::Move;
        e.sourceTrack = src;
        e.startBeat = start;
        e.endBeat = end;
        e.destTrack = dst;
        e.destStartBeat = destStart;
        return e;
    }
};

} // namespace

TEST(TimelineDocAutomationSpanTest, MoveOnTheSameTrackShiftsTheSpanAndReplacesTheDestination) {
    SpanRig rig;
    CountingListener listener;
    rig.doc.addListener(&listener);
    const auto revision = rig.doc.getRevision();

    // Clip [0, 4) moves to [4, 8): points 0,1,2,3.5 travel to 4,5,6,7.5; the old 4 and 6 in the
    // destination span are replaced.
    ASSERT_TRUE(rig.doc.transferAutomationSpans({SpanRig::move(rig.a, 0.0, 4.0, rig.a, 4.0)}));
    EXPECT_EQ(rig.beats(rig.aCutoff), (std::vector<double>{4.0, 5.0, 6.0, 7.5}));
    EXPECT_DOUBLE_EQ(rig.doc.getLane(rig.aCutoff)->points[1].value, 10.0) << "values travel with their beats";
    EXPECT_EQ(rig.beats(rig.aLevel), (std::vector<double>{5.0})) << "every lane on the track moves";
    EXPECT_EQ(rig.doc.getRevision(), revision + 1) << "one mutation for the whole batch";
    EXPECT_EQ(listener.calls, 1);
    rig.doc.removeListener(&listener);
}

TEST(TimelineDocAutomationSpanTest, CopyKeepsTheSourceSpan) {
    SpanRig rig;
    Edit copy = SpanRig::move(rig.a, 0.0, 2.0, rig.a, 10.0);
    copy.kind = Edit::Kind::Copy;
    ASSERT_TRUE(rig.doc.transferAutomationSpans({copy}));
    EXPECT_EQ(rig.beats(rig.aCutoff), (std::vector<double>{0.0, 1.0, 2.0, 3.5, 4.0, 6.0, 10.0, 11.0}));
}

TEST(TimelineDocAutomationSpanTest, RemoveDropsOnlyTheHalfOpenSpan) {
    SpanRig rig;
    Edit remove;
    remove.kind = Edit::Kind::Remove;
    remove.sourceTrack = rig.a;
    remove.startBeat = 1.0;
    remove.endBeat = 4.0;
    ASSERT_TRUE(rig.doc.transferAutomationSpans({remove}));
    EXPECT_EQ(rig.beats(rig.aCutoff), (std::vector<double>{0.0, 4.0, 6.0})) << "4.0 is the next clip's, kept";
    EXPECT_TRUE(rig.doc.getLane(rig.aLevel)->points.empty());
    EXPECT_EQ(rig.beats(rig.bCutoff), (std::vector<double>{9.0})) << "other tracks untouched";
}

TEST(TimelineDocAutomationSpanTest, CrossTrackMoveTargetsTheSameParameterOnlyWhenItExists) {
    SpanRig rig;
    ASSERT_TRUE(rig.doc.transferAutomationSpans({SpanRig::move(rig.a, 0.0, 2.0, rig.b, 20.0)}));
    // cutoff has a unique counterpart on B: carried over.
    EXPECT_EQ(rig.beats(rig.bCutoff), (std::vector<double>{9.0, 20.0, 21.0}));
    EXPECT_EQ(rig.beats(rig.aCutoff), (std::vector<double>{2.0, 3.5, 4.0, 6.0}));
    // level has none on B: its points stay put on A.
    EXPECT_EQ(rig.beats(rig.aLevel), (std::vector<double>{1.0}));
}

TEST(TimelineDocAutomationSpanTest, ACarryWithNoPointsNeverClearsTheDestination) {
    SpanRig rig;
    ASSERT_TRUE(rig.doc.transferAutomationSpans({SpanRig::move(rig.a, 7.0, 9.0, rig.a, 0.0)}));
    EXPECT_EQ(rig.beats(rig.aCutoff), (std::vector<double>{0.0, 1.0, 2.0, 3.5, 4.0, 6.0}));
}

TEST(TimelineDocAutomationSpanTest, OverlappingBatchReadsOriginalPoints) {
    SpanRig rig;
    // Two clips [0,2) and [2,4) both shift right by 2 in one batch: the second clip's source span is
    // the first clip's destination, and must still be read from the ORIGINAL points.
    ASSERT_TRUE(rig.doc.transferAutomationSpans(
        {SpanRig::move(rig.a, 0.0, 2.0, rig.a, 2.0), SpanRig::move(rig.a, 2.0, 4.0, rig.a, 4.0)}));
    EXPECT_EQ(rig.beats(rig.aCutoff), (std::vector<double>{2.0, 3.0, 4.0, 5.5, 6.0}))
        << "6.0 sits outside every half-open destination span, so it stays";
}

TEST(TimelineDocAutomationSpanTest, NothingUnderAnySpanIsANoOp) {
    SpanRig rig;
    const auto revision = rig.doc.getRevision();
    EXPECT_TRUE(rig.doc.transferAutomationSpans({SpanRig::move(rig.c, 0.0, 4.0, rig.c, 8.0)}));
    EXPECT_TRUE(rig.doc.transferAutomationSpans({}));
    EXPECT_EQ(rig.doc.getRevision(), revision);
}

TEST(TimelineDocAutomationSpanTest, MalformedEditsAreRejectedWholesale) {
    SpanRig rig;
    const auto revision = rig.doc.getRevision();
    EXPECT_FALSE(rig.doc.transferAutomationSpans({SpanRig::move(rig.a, 4.0, 2.0, rig.a, 0.0)})) << "end <= start";
    EXPECT_FALSE(rig.doc.transferAutomationSpans({SpanRig::move(rig.a, 0.0, 2.0, rig.a, -1.0)})) << "negative dest";
    EXPECT_FALSE(rig.doc.transferAutomationSpans(
        {SpanRig::move(rig.a, 0.0, 2.0, rig.a, 8.0), SpanRig::move(synth::TrackId{999}, 0.0, 1.0, rig.a, 0.0)}))
        << "one unresolved track rejects the whole batch";
    EXPECT_EQ(rig.doc.getRevision(), revision);
    EXPECT_EQ(rig.beats(rig.aCutoff), (std::vector<double>{0.0, 1.0, 2.0, 3.5, 4.0, 6.0}));
}
