// TimelineClipLaneRangeTests.cpp — the Range tool: the drag and its Shift-extension, the range's
// independence from the clip selection, its repaint gate, and the range verbs (split at edges,
// delete, delete and close the gap) through keys, the menu hook and applyRangeChoice.
#include "TimelineClipLaneTestFixture.h"

#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/RangeSelectionModel.h"

using synth::TrackId;
using synth::ui::EditTool;
using synth::ui::RangeSelectionModel;

namespace {
// The repaint-count seam plus a recording stand-in for the range's context menu (a real
// juce::PopupMenu must never be reached from a test).
class RangeLane : public TimelineClipLaneArea {
public:
    using TimelineClipLaneArea::TimelineClipLaneArea;
    int previewRepaints = 0;
    int rangeMenus = 0;
    int clipMenus = 0;

protected:
    void requestToolPreviewRepaint(juce::Rectangle<int> region) override {
        ++previewRepaints;
        TimelineClipLaneArea::requestToolPreviewRepaint(region);
    }
    void showRangeContextMenu(juce::Point<int>) override { ++rangeMenus; }
    void showClipContextMenu(synth::ClipId, juce::Point<int>) override { ++clipMenus; }
};

struct RangeFixture {
    TimelineDoc doc;
    TimelineViewState state;
    ClipSelectionModel selection;
    AppUndoManager undo;
    RangeLane lane{state, selection};
    TrackId t0, t1, t2;

    RangeFixture() {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Quarter;
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setSize(1200, 400);
        t0 = doc.addTrack(TrackKind::Midi, "0");
        t1 = doc.addTrack(TrackKind::Midi, "1");
        t2 = doc.addTrack(TrackKind::Midi, "2");
        lane.setActiveTool(EditTool::Range);
    }

    // The point at `beat` on the middle of row `row`.
    juce::Point<float> at(double beat, int row) const {
        return {(float)state.beatToX(beat), (float)(row * lane.getRowHeight() + lane.getRowHeight() / 2)};
    }

    void drag(double fromBeat, int fromRow, double toBeat, int toRow, int flags = 0) {
        const auto from = at(fromBeat, fromRow);
        lane.mouseDown(leftClick(lane, from, flags));
        lane.mouseDrag(leftDrag(lane, at(toBeat, toRow), from, flags));
        lane.mouseUp(leftDrag(lane, at(toBeat, toRow), from, flags));
    }
};
} // namespace

TEST(TimelineClipLaneRangeTest, RangeToolOwnsDigitTwo) {
    EXPECT_EQ(synth::ui::editToolKeyDigit(EditTool::Range), 2);
    EXPECT_EQ(synth::ui::editToolForKeyChar('2'), EditTool::Range);
    EXPECT_STREQ(synth::ui::editToolName(EditTool::Range), "Range");
    EXPECT_EQ(synth::ui::kAllEditTools[1], EditTool::Range) << "the strip reads 1, 2, 3, ...";
}

TEST(TimelineClipLaneRangeTest, ModelSpansRowsBetweenItsCorners) {
    TimelineDoc doc;
    const auto a = doc.addTrack(TrackKind::Midi, "a");
    const auto b = doc.addTrack(TrackKind::Midi, "b");
    const auto c = doc.addTrack(TrackKind::Midi, "c");
    RangeSelectionModel range;
    EXPECT_FALSE(range.isActive());

    range.begin(c, 8.0);
    range.extendTo(a, 2.0);
    EXPECT_DOUBLE_EQ(range.getStartBeat(), 2.0);
    EXPECT_DOUBLE_EQ(range.getEndBeat(), 8.0);
    EXPECT_EQ(range.coveredTracks(doc), (std::vector<TrackId>{a, b, c}));
    EXPECT_TRUE(range.contains(doc, 1, 2.0));
    EXPECT_FALSE(range.contains(doc, 1, 8.0)) << "half-open on the right";

    doc.removeTrack(a);
    EXPECT_TRUE(range.coveredTracks(doc).empty()) << "a corner's track is gone";
}

TEST(TimelineClipLaneRangeTest, DragMakesASnappedRangeAcrossRows) {
    RangeFixture f;
    f.drag(1.2, 0, 5.7, 1);

    const auto span = f.lane.getRangeSpan();
    ASSERT_TRUE(span.has_value());
    EXPECT_DOUBLE_EQ(span->startBeat, 1.0);
    EXPECT_DOUBLE_EQ(span->endBeat, 6.0);
    EXPECT_EQ(span->tracks, (std::vector<TrackId>{f.t0, f.t1}));
    EXPECT_FALSE(f.lane.isRangeDragActiveForTest());
    EXPECT_FALSE(f.undo.canUndo()) << "making a range edits nothing";

    const auto rect = f.lane.getRangeRectForTest();
    EXPECT_EQ(rect.getX(), 40);
    EXPECT_EQ(rect.getRight(), 240);
    EXPECT_EQ(rect.getY(), 0);
    EXPECT_EQ(rect.getHeight(), 2 * f.lane.getRowHeight());
}

TEST(TimelineClipLaneRangeTest, DraggingPastTheLastRowClampsToIt) {
    RangeFixture f;
    const auto from = f.at(0.0, 0);
    f.lane.mouseDown(leftClick(f.lane, from));
    f.lane.mouseDrag(leftDrag(f.lane, {(float)f.state.beatToX(4.0), 390.0f}, from));
    f.lane.mouseUp(leftDrag(f.lane, {(float)f.state.beatToX(4.0), 390.0f}, from));
    const auto span = f.lane.getRangeSpan();
    ASSERT_TRUE(span.has_value());
    EXPECT_EQ(span->tracks.size(), 3u);
}

TEST(TimelineClipLaneRangeTest, AClickWithoutADragClearsTheRange) {
    RangeFixture f;
    f.drag(1.0, 0, 4.0, 0);
    ASSERT_TRUE(f.lane.getRangeSpan().has_value());

    const auto p = f.at(8.0, 1);
    f.lane.mouseDown(leftClick(f.lane, p));
    f.lane.mouseUp(leftClick(f.lane, p));
    EXPECT_FALSE(f.lane.getRangeSelection().isActive());
}

TEST(TimelineClipLaneRangeTest, ShiftExtendsFromTheAnchor) {
    RangeFixture f;
    f.drag(2.0, 0, 4.0, 0);
    const auto p = f.at(9.0, 2);
    f.lane.mouseDown(leftClick(f.lane, p, juce::ModifierKeys::shiftModifier));
    f.lane.mouseUp(leftClick(f.lane, p, juce::ModifierKeys::shiftModifier));

    const auto span = f.lane.getRangeSpan();
    ASSERT_TRUE(span.has_value());
    EXPECT_DOUBLE_EQ(span->startBeat, 2.0) << "the anchor corner stays put";
    EXPECT_DOUBLE_EQ(span->endBeat, 9.0);
    EXPECT_EQ(span->tracks.size(), 3u);
}

TEST(TimelineClipLaneRangeTest, MakingARangeClearsTheClipSelectionAndTouchesNoClip) {
    RangeFixture f;
    const auto clip = f.doc.addClip(f.t0, 0.0, 8.0, "c");
    f.selection.setSelection({clip});
    const auto revision = f.doc.getRevision();

    f.drag(2.0, 0, 4.0, 0); // starts ON the clip: a range still, never a move or trim
    EXPECT_TRUE(f.selection.isEmpty());
    EXPECT_EQ(f.doc.getRevision(), revision);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->startBeat, 0.0);
}

TEST(TimelineClipLaneRangeTest, DeleteKeyClearsTheRangeAsOneUndoStep) {
    RangeFixture f;
    const auto a = f.doc.addClip(f.t0, 0.0, 8.0, "a");
    const auto b = f.doc.addClip(f.t1, 3.0, 2.0, "b");
    f.lane.setRange(f.t0, 2.0, f.t1, 6.0);

    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.doc.getClip(b), nullptr);
    EXPECT_DOUBLE_EQ(f.doc.getClip(a)->lengthBeats, 2.0);
    ASSERT_EQ(f.doc.getTrack(f.t0)->clips.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(f.t0)->clips[1].startBeat, 6.0);
    EXPECT_TRUE(f.lane.getRangeSpan().has_value()) << "the range stays for the next verb";

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(f.t0)->clips.size(), 1u);
    EXPECT_EQ(f.doc.getTrack(f.t1)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo()) << "one gesture, one undo step";
}

TEST(TimelineClipLaneRangeTest, ShiftDeleteClosesTheGap) {
    RangeFixture f;
    const auto later = f.doc.addClip(f.t0, 10.0, 2.0, "later");
    const auto untouched = f.doc.addClip(f.t2, 10.0, 2.0, "other row");
    f.lane.setRange(f.t0, 2.0, f.t1, 6.0);

    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_DOUBLE_EQ(f.doc.getClip(later)->startBeat, 6.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(untouched)->startBeat, 10.0);
}

TEST(TimelineClipLaneRangeTest, SplitAtEdgesViaTheChoiceHook) {
    RangeFixture f;
    const auto clip = f.doc.addClip(f.t0, 0.0, 8.0, "c");
    f.lane.setRange(f.t0, 2.0, f.t0, 6.0);

    EXPECT_TRUE(f.lane.applyRangeChoice(TimelineClipLaneArea::RangeChoice::SplitAtEdges));
    EXPECT_EQ(f.doc.getTrack(f.t0)->clips.size(), 3u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->lengthBeats, 2.0);

    // A second split at the same edges finds nothing left to cut.
    EXPECT_FALSE(f.lane.applyRangeChoice(TimelineClipLaneArea::RangeChoice::SplitAtEdges));
}

TEST(TimelineClipLaneRangeTest, AVerbOverEmptyTimeRecordsNothing) {
    RangeFixture f;
    f.lane.setRange(f.t0, 2.0, f.t1, 6.0);
    EXPECT_FALSE(f.lane.applyRangeChoice(TimelineClipLaneArea::RangeChoice::Delete));
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneRangeTest, EscapeAndSwitchingToolsDropTheRange) {
    RangeFixture f;
    f.lane.setRange(f.t0, 2.0, f.t0, 6.0);
    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.lane.getRangeSelection().isActive());

    f.lane.setRange(f.t0, 2.0, f.t0, 6.0);
    f.lane.setActiveTool(EditTool::Select);
    EXPECT_FALSE(f.lane.getRangeSelection().isActive());
}

TEST(TimelineClipLaneRangeTest, RemovingACornerTrackDropsTheRange) {
    RangeFixture f;
    f.lane.setRange(f.t0, 2.0, f.t1, 6.0);
    f.doc.removeTrack(f.t1);
    f.lane.refreshFromDoc();
    EXPECT_FALSE(f.lane.getRangeSelection().isActive());
}

TEST(TimelineClipLaneRangeTest, RightClickInsideTheRangeOpensTheRangeMenu) {
    RangeFixture f;
    f.doc.addClip(f.t0, 0.0, 8.0, "c");
    f.lane.setRange(f.t0, 2.0, f.t0, 6.0);

    f.lane.mouseDown(rightClick(f.lane, f.at(3.0, 0)));
    EXPECT_EQ(f.lane.rangeMenus, 1);
    EXPECT_EQ(f.lane.clipMenus, 0);

    f.lane.mouseDown(rightClick(f.lane, f.at(7.0, 0))); // on the clip, outside the range
    EXPECT_EQ(f.lane.rangeMenus, 1);
    EXPECT_EQ(f.lane.clipMenus, 1);
}

TEST(TimelineClipLaneRangeTest, DragRepaintsOnlyWhenTheSnappedExtentChanges) {
    RangeFixture f;
    const auto from = f.at(1.0, 0);
    f.lane.mouseDown(leftClick(f.lane, from));
    f.lane.previewRepaints = 0;

    f.lane.mouseDrag(leftDrag(f.lane, f.at(3.0, 0), from));
    EXPECT_EQ(f.lane.previewRepaints, 1);
    f.lane.mouseDrag(leftDrag(f.lane, f.at(3.2, 0), from)); // still snaps to 3
    f.lane.mouseDrag(leftDrag(f.lane, f.at(2.8, 0), from));
    EXPECT_EQ(f.lane.previewRepaints, 1) << "movement inside one snap cell and row costs nothing";
    f.lane.mouseDrag(leftDrag(f.lane, f.at(3.0, 1), from)); // new row
    EXPECT_EQ(f.lane.previewRepaints, 2);
    f.lane.mouseUp(leftDrag(f.lane, f.at(3.0, 1), from));
}

TEST(TimelineClipLaneRangeTest, LoopKeyLoopsTheRange) {
    RangeFixture f;
    f.lane.setRange(f.t0, 2.0, f.t0, 6.0);
    double loopStart = -1.0, loopEnd = -1.0;
    f.lane.onLoopRangeRequested = [&](double s, double e) {
        loopStart = s;
        loopEnd = e;
    };
    EXPECT_TRUE(f.lane.keyPressed(juce::KeyPress('p', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_DOUBLE_EQ(loopStart, 2.0);
    EXPECT_DOUBLE_EQ(loopEnd, 6.0);
}

TEST(TimelineClipLaneRangeTest, LeftCutAudioUsesTheTransportTempo) {
    RangeFixture f;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    transport.setBpm(60.0); // one second per beat
    transport.tick(512);
    f.lane.setTransport(&transport);
    EXPECT_DOUBLE_EQ(f.lane.secondsPerBeat(), 1.0);

    const auto audioTrack = f.doc.addTrack(TrackKind::Audio, "A");
    const auto clip = f.doc.addClip(audioTrack, 0.0, 8.0, "take");
    ASSERT_TRUE(f.doc.setClipAsset(clip, "Audio/take.wav", 0.0));
    f.lane.setRange(audioTrack, 0.0, audioTrack, 3.0);
    ASSERT_TRUE(f.lane.applyRangeChoice(TimelineClipLaneArea::RangeChoice::Delete));

    const auto& clips = f.doc.getTrack(audioTrack)->clips;
    ASSERT_EQ(clips.size(), 1u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 3.0);
    EXPECT_DOUBLE_EQ(clips[0].sourceStartSeconds, 3.0);
    f.lane.setTransport(nullptr);
}
