// TimelineClipLaneSelectionTests.cpp — ClipSelectionModel, clipHitTestMarquee, and click/marquee/
// keyboard selection interactions on TimelineClipLaneArea.
#include "TimelineClipLaneTestFixture.h"

TEST(ClipSelectionModel, StartsEmpty) {
    ClipSelectionModel sel;
    EXPECT_TRUE(sel.isEmpty());
    EXPECT_EQ(sel.size(), 0);
    EXPECT_FALSE(sel.contains(cid(1)));
}

TEST(ClipSelectionModel, AddIsIdempotent) {
    ClipSelectionModel sel;
    EXPECT_TRUE(sel.add(cid(7)));
    EXPECT_FALSE(sel.add(cid(7))) << "adding an already-selected id must report no change";
    EXPECT_EQ(sel.size(), 1);
    EXPECT_TRUE(sel.contains(cid(7)));
}

TEST(ClipSelectionModel, RejectsInvalidClipIdZero) {
    // value == 0 is TimelineDoc's "not found" sentinel — selecting it would let a rejected/absent
    // clip reach a batched move or delete.
    ClipSelectionModel sel;
    EXPECT_FALSE(sel.add(cid(0)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(ClipSelectionModel, RemoveReportsWhetherAnythingWasRemoved) {
    ClipSelectionModel sel;
    sel.add(cid(3));
    EXPECT_TRUE(sel.remove(cid(3)));
    EXPECT_FALSE(sel.remove(cid(3)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(ClipSelectionModel, ToggleReturnsStateAfterToggling) {
    ClipSelectionModel sel;
    EXPECT_TRUE(sel.toggle(cid(2))) << "toggling an unselected id selects it";
    EXPECT_TRUE(sel.contains(cid(2)));
    EXPECT_FALSE(sel.toggle(cid(2))) << "toggling a selected id deselects it";
    EXPECT_FALSE(sel.contains(cid(2)));
}

TEST(ClipSelectionModel, SetSelectionReplacesAndDeduplicates) {
    ClipSelectionModel sel;
    sel.add(cid(99));
    sel.setSelection({cid(1), cid(2), cid(2), cid(0)});

    EXPECT_EQ(sel.size(), 2) << "duplicates collapse and the invalid id is dropped";
    EXPECT_TRUE(sel.contains(cid(1)));
    EXPECT_TRUE(sel.contains(cid(2)));
    EXPECT_FALSE(sel.contains(cid(99))) << "setSelection replaces rather than merges";
}

TEST(ClipSelectionModel, GetSelectedIsOrderedByValueRegardlessOfInsertionOrder) {
    ClipSelectionModel a;
    a.setSelection({cid(30), cid(10), cid(20)});
    ClipSelectionModel b;
    b.setSelection({cid(10), cid(20), cid(30)});

    auto expected = std::vector<ClipId>{cid(10), cid(20), cid(30)};
    EXPECT_EQ(a.getSelected(), expected);
    EXPECT_EQ(b.getSelected(), expected);
}

TEST(ClipSelectionModel, ClearEmptiesEverything) {
    ClipSelectionModel sel;
    sel.setSelection({cid(1), cid(2)});
    sel.clear();
    EXPECT_TRUE(sel.isEmpty());
}

TEST(ClipSelectionModel, RetainOnlyDropsIdsThatNoLongerExist) {
    ClipSelectionModel sel;
    sel.setSelection({cid(1), cid(2), cid(3)});

    EXPECT_TRUE(sel.retainOnly({cid(1), cid(3)}));
    EXPECT_EQ(sel.size(), 2);
    EXPECT_TRUE(sel.contains(cid(1)));
    EXPECT_FALSE(sel.contains(cid(2)));
    EXPECT_TRUE(sel.contains(cid(3)));
}

TEST(ClipSelectionModel, RetainOnlyReportsNoChangeWhenEverythingSurvives) {
    ClipSelectionModel sel;
    sel.setSelection({cid(1), cid(2)});
    EXPECT_FALSE(sel.retainOnly({cid(1), cid(2), cid(5)}));
    EXPECT_EQ(sel.size(), 2);
}

TEST(ClipSelectionModel, RetainOnlyWithNothingAliveClearsSelection) {
    ClipSelectionModel sel;
    sel.setSelection({cid(1), cid(2)});
    EXPECT_TRUE(sel.retainOnly({}));
    EXPECT_TRUE(sel.isEmpty());
}

namespace {
std::vector<std::pair<ClipId, juce::Rectangle<int>>> threeClipRects() {
    return {
        {cid(1), juce::Rectangle<int>(0, 0, 100, 100)},
        {cid(2), juce::Rectangle<int>(200, 0, 100, 100)},
        {cid(3), juce::Rectangle<int>(400, 400, 100, 100)},
    };
}
} // namespace

TEST(ClipMarqueeHitTest, SelectsFullyEnclosedClips) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(-10, -10, 330, 130), threeClipRects());
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0], cid(1));
    EXPECT_EQ(hits[1], cid(2));
}

TEST(ClipMarqueeHitTest, SelectsPartiallyTouchedClips) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(90, 90, 20, 20), threeClipRects());
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], cid(1));
}

TEST(ClipMarqueeHitTest, MissesClipsOutsideTheBand) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(120, 0, 50, 50), threeClipRects());
    EXPECT_TRUE(hits.empty());
}

TEST(ClipMarqueeHitTest, DegenerateMarqueeSelectsNothing) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(50, 50, 0, 0), threeClipRects());
    EXPECT_TRUE(hits.empty());
}

TEST(ClipMarqueeHitTest, IgnoresInvalidClipIds) {
    std::vector<std::pair<ClipId, juce::Rectangle<int>>> rects{{cid(0), juce::Rectangle<int>(0, 0, 100, 100)}};
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(0, 0, 200, 200), rects);
    EXPECT_TRUE(hits.empty());
}

TEST(ClipMarqueeHitTest, EmptyListYieldsNoHits) {
    auto hits = synth::ui::clipHitTestMarquee(juce::Rectangle<int>(0, 0, 500, 500), {});
    EXPECT_TRUE(hits.empty());
}

TEST(TimelineClipLaneInteractionTest, ClickSelectsDeferredDeselect) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "Clip A");
    ASSERT_TRUE(clipId.isValid());

    const auto rect = f.lane.getClipRect(clipId);
    const auto clipCentre = centreOf(rect);

    // Click on the clip selects it — a plain mouseDown+mouseUp at the same position with no drag.
    f.lane.mouseDown(leftClick(f.lane, clipCentre));
    f.lane.mouseUp(leftClick(f.lane, clipCentre));
    ASSERT_TRUE(f.selection.contains(clipId));

    // A press on EMPTY space defers: it must not clear on mouseDown alone.
    const juce::Point<float> emptyA(900.0f, 300.0f);
    f.lane.mouseDown(leftClick(f.lane, emptyA));
    EXPECT_TRUE(f.selection.contains(clipId)) << "mouseDown alone must not clear — see the deferred-click contract";

    // ...but a press that turns into a DRAG promotes to a marquee (cancelling the deferred click).
    const juce::Point<float> emptyB(950.0f, 300.0f);
    f.lane.mouseDrag(leftDrag(f.lane, emptyB, emptyA));
    EXPECT_TRUE(f.lane.isMarqueeActiveForTest());
    f.lane.mouseUp(leftDrag(f.lane, emptyB, emptyA));
    EXPECT_FALSE(f.lane.isMarqueeActiveForTest());

    // A plain click-release (no drag) on empty space clears.
    f.selection.setSelection({clipId});
    f.lane.mouseDown(leftClick(f.lane, emptyA));
    f.lane.mouseUp(leftClick(f.lane, emptyA));
    EXPECT_TRUE(f.selection.isEmpty());

    // Right-click preserves the selection (the GraphEditor rule) — whether on a clip or on empty
    // lane space.
    f.selection.setSelection({clipId});
    f.lane.mouseDown(rightClick(f.lane, emptyA));
    f.lane.mouseUp(rightClick(f.lane, emptyA));
    EXPECT_TRUE(f.selection.contains(clipId));
}

TEST(TimelineClipLaneInteractionTest, MarqueeSelectsIntersecting) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto trackId2 = f.doc.addTrack(TrackKind::Midi, "Track 2");
    const auto clipA = f.doc.addClip(trackId, 0.0, 4.0, "A");   // row 0, x in [0, 160)
    const auto clipB = f.doc.addClip(trackId2, 20.0, 4.0, "B"); // row 1, x in [800, 960) — far away
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());

    const auto rectA = f.lane.getClipRect(clipA);

    // Anchor sits just BELOW clipA's row (empty space — mouseDown there must start a marquee, not
    // select/drag the clip), and the drag reaches up and to the right so the resulting rect clips
    // clipA's bottom-right corner only.
    const juce::Point<float> anchor((float)rectA.getRight() - 10.0f, (float)rectA.getBottom() + 4.0f);
    const juce::Point<float> current((float)rectA.getRight() + 40.0f, (float)rectA.getY() - 4.0f);

    f.lane.mouseDown(leftClick(f.lane, anchor));
    f.lane.mouseDrag(leftDrag(f.lane, current, anchor));
    ASSERT_TRUE(f.lane.isMarqueeActiveForTest());
    EXPECT_TRUE(f.selection.contains(clipA));
    EXPECT_FALSE(f.selection.contains(clipB)) << "clipB sits far outside the marquee band";
    f.lane.mouseUp(leftDrag(f.lane, current, anchor));

    // Additive (Shift-held) marquee starting from clipB's own selection preserves it.
    f.selection.setSelection({clipB});
    f.lane.mouseDown(leftClick(f.lane, anchor, juce::ModifierKeys::shiftModifier));
    f.lane.mouseDrag(leftDrag(f.lane, current, anchor, juce::ModifierKeys::shiftModifier));
    EXPECT_TRUE(f.selection.contains(clipA));
    EXPECT_TRUE(f.selection.contains(clipB)) << "additive marquee must keep the base selection";
    f.lane.mouseUp(leftDrag(f.lane, current, anchor, juce::ModifierKeys::shiftModifier));
}

TEST(TimelineClipLaneInteractionTest, DeleteKeyDeletesSelectionOneStep) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 4.0, "A");
    const auto clipB = f.doc.addClip(trackId, 10.0, 2.0, "B");
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());
    f.selection.setSelection({clipA, clipB});

    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.doc.getTrack(trackId)->clips.size(), 0u);
    EXPECT_TRUE(f.selection.isEmpty());

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(trackId)->clips.size(), 2u) << "both clips came back in ONE undo";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneInteractionTest, DeleteKeyWithEmptySelectionReturnsFalse) {
    ClipLaneFixture f;
    f.doc.addTrack(TrackKind::Midi, "Track 1");
    ASSERT_TRUE(f.selection.isEmpty());
    EXPECT_FALSE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
}

TEST(TimelineClipLaneInteractionTest, EscapeKeyClearsSelection) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 4.0, "A");
    f.selection.setSelection({clipId});

    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(f.selection.isEmpty());
    EXPECT_FALSE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))) << "nothing left to clear";
}

// ---- Panel-scoped "P" = loop the selection ----

TEST(TimelineClipLaneInteractionTest, LoopSelectionKeyReportsTheSelectionSpan) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 4.0, "A");
    ASSERT_TRUE(clipId.isValid());

    double reportedStart = -1.0, reportedEnd = -1.0;
    int calls = 0;
    f.lane.onLoopRangeRequested = [&](double start, double end) {
        reportedStart = start;
        reportedEnd = end;
        ++calls;
    };

    f.selection.setSelection({clipId});
    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress('p')));
    EXPECT_EQ(calls, 1);
    EXPECT_DOUBLE_EQ(reportedStart, 4.0);
    EXPECT_DOUBLE_EQ(reportedEnd, 8.0) << "the span is [startBeat, startBeat + lengthBeats]";

    // The doc is untouched: this reports a range outwards, it never edits anything.
    EXPECT_EQ(f.doc.getTrack(trackId)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneInteractionTest, LoopSelectionKeySpansEverySelectedClip) {
    ClipLaneFixture f;
    const auto trackA = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto trackB = f.doc.addTrack(TrackKind::Midi, "Track 2");
    const auto early = f.doc.addClip(trackB, 2.0, 1.0, "early"); // earliest start, on the LATER row
    const auto late = f.doc.addClip(trackA, 16.0, 4.0, "late");  // latest end
    const auto middle = f.doc.addClip(trackA, 8.0, 2.0, "middle");
    const auto unselected = f.doc.addClip(trackB, 40.0, 4.0, "unselected");
    ASSERT_TRUE(unselected.isValid());

    double reportedStart = -1.0, reportedEnd = -1.0;
    f.lane.onLoopRangeRequested = [&](double start, double end) {
        reportedStart = start;
        reportedEnd = end;
    };

    f.selection.setSelection({late, early, middle});
    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress('p')));
    EXPECT_DOUBLE_EQ(reportedStart, 2.0) << "min startBeat across the whole selection, any row";
    EXPECT_DOUBLE_EQ(reportedEnd, 20.0) << "max endBeat — a clip outside the selection is ignored";
}

TEST(TimelineClipLaneInteractionTest, LoopSelectionKeyWithEmptySelectionReturnsFalse) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    f.doc.addClip(trackId, 0.0, 4.0, "A");

    int calls = 0;
    f.lane.onLoopRangeRequested = [&](double, double) { ++calls; };

    ASSERT_TRUE(f.selection.isEmpty());
    EXPECT_FALSE(f.lane.keyPressed(juce::KeyPress('p'))) << "nothing selected: the key must bubble";
    EXPECT_EQ(calls, 0);
    EXPECT_FALSE(f.lane.getSelectedClipSpan().has_value());
}
