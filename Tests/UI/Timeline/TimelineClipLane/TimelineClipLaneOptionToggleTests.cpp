// TimelineClipLaneOptionToggleTests.cpp -- Option pressed or released while a clip is being dragged switches the drag
// between copy and move at once (the ghost, the cursor, the section growth), with no pointer move needed, and the
// release commits the mode that is live then (docs/timeline/clips.md "Moving and copying"). The real mouse path drives
// the drag; modifier changes arrive through modifierKeysChanged as the OS sends them.
#include "TimelineClipLaneTestFixture.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"

namespace {

constexpr int kAlt = juce::ModifierKeys::altModifier;
const juce::ModifierKeys kOptionDown(kAlt);

// A clip [2, 6) in a section [0, 8), grabbed at its centre and dragged three beats right (it would end at 9).
struct ToggleRig : ClipLaneFixture {
    ClipId clip;
    synth::SectionId section;
    juce::Point<float> anchor;
    juce::Point<float> dragged;

    ToggleRig() {
        const auto track = doc.addTrack(TrackKind::Midi, "Track 1");
        clip = doc.addClip(track, 2.0, 4.0, "Clip");
        section = doc.makeSection(0.0, 8.0, "starter", 0xff00ff00, 0.5);
        lane.refreshFromDoc();
        anchor = centreOf(lane.getClipRect(clip));
        dragged = {anchor.x + 120.0f, anchor.y};
    }
    void startDrag(int flags = 0) {
        lane.mouseDown(leftClick(lane, anchor, flags));
        lane.mouseDrag(leftDrag(lane, {anchor.x + 20.0f, anchor.y}, anchor, flags));
        lane.mouseDrag(leftDrag(lane, dragged, anchor, flags));
    }
    void release(int flags = 0) { lane.mouseUp(leftDrag(lane, dragged, anchor, flags)); }
    std::size_t clipCount() const { return doc.getTrackForClip(clip)->clips.size(); }
};

} // namespace

// Regression test for FRO2441: Option only counted when it was already down at the press, so a drag could not be
// turned into a copy (or back into a move) once it was under way.
TEST(TimelineClipLaneOptionToggle, PressingOptionMidDragTurnsTheMoveIntoACopyWithGhostAndWholeLength) {
    ToggleRig r;
    r.startDrag();
    ASSERT_FALSE(r.lane.isCopyDragForTest());
    EXPECT_TRUE(r.lane.getDragGhostRectsForTest().empty());
    EXPECT_DOUBLE_EQ(r.lane.getEffectiveGeometryForTest(r.clip)->second, 3.0) << "a move is drawn trimmed";

    r.lane.modifierKeysChanged(kOptionDown); // no pointer move

    EXPECT_TRUE(r.lane.isCopyDragForTest());
    const auto ghosts = r.lane.getDragGhostRectsForTest();
    ASSERT_EQ(ghosts.size(), 1u);
    EXPECT_EQ(ghosts[0].getWidth(), r.lane.getClipRect(r.clip).getWidth()) << "the copy ghost is the whole clip";
    EXPECT_FLOAT_EQ(r.lane.getCopyGhostFadeForTest(), 1.0f);
    EXPECT_DOUBLE_EQ(r.lane.getEffectiveGeometryForTest(r.clip)->first, 2.0) << "the original stays home";
    EXPECT_TRUE(r.lane.getMouseCursor() == juce::MouseCursor::CopyingCursor);

    r.release(); // the release carries no Option here: the live mode (copy) is what commits
    EXPECT_EQ(r.clipCount(), 2u);
    EXPECT_DOUBLE_EQ(r.doc.getClip(r.clip)->startBeat, 2.0);
    EXPECT_DOUBLE_EQ(r.doc.getSection(r.section)->lengthBeats, 12.0);
    ASSERT_TRUE(r.undo.canUndo());
    r.undo.undo();
    EXPECT_EQ(r.clipCount(), 1u);
    EXPECT_FALSE(r.undo.canUndo()) << "one undo step";
}

TEST(TimelineClipLaneOptionToggle, ReleasingOptionMidDragTurnsTheCopyBackIntoATrimmedMove) {
    ToggleRig r;
    r.startDrag(kAlt);
    ASSERT_TRUE(r.lane.isCopyDragForTest());

    r.lane.modifierKeysChanged(juce::ModifierKeys());

    EXPECT_FALSE(r.lane.isCopyDragForTest());
    EXPECT_TRUE(r.lane.getDragGhostRectsForTest().empty());
    EXPECT_FLOAT_EQ(r.lane.getCopyGhostFadeForTest(), 0.0f);
    const auto geometry = r.lane.getEffectiveGeometryForTest(r.clip);
    EXPECT_DOUBLE_EQ(geometry->first, 5.0);
    EXPECT_DOUBLE_EQ(geometry->second, 3.0);
    EXPECT_TRUE(r.lane.getMouseCursor() == juce::MouseCursor::DraggingHandCursor);

    r.release(kAlt); // a release that still reads Option held would be a copy; the live mode is move
    EXPECT_EQ(r.clipCount(), 1u);
    EXPECT_DOUBLE_EQ(r.doc.getClip(r.clip)->startBeat, 5.0);
    EXPECT_DOUBLE_EQ(r.doc.getClip(r.clip)->lengthBeats, 3.0) << "shortened to the section's end";
    EXPECT_DOUBLE_EQ(r.doc.getSection(r.section)->lengthBeats, 8.0) << "the section is not grown";
}

TEST(TimelineClipLaneOptionToggle, TogglingSeveralTimesCommitsTheModeLiveAtRelease) {
    ToggleRig r;
    r.startDrag();
    r.lane.modifierKeysChanged(kOptionDown);
    r.lane.modifierKeysChanged(juce::ModifierKeys());
    r.lane.modifierKeysChanged(kOptionDown);
    r.lane.modifierKeysChanged(kOptionDown); // a repeat of the same state changes nothing
    EXPECT_TRUE(r.lane.isCopyDragForTest());
    r.release();
    EXPECT_EQ(r.clipCount(), 2u);
    EXPECT_DOUBLE_EQ(r.doc.getSection(r.section)->lengthBeats, 12.0);
}

TEST(TimelineClipLaneOptionToggle, EscapeAfterAToggleCancelsAndLeavesTheDocumentAlone) {
    ToggleRig r;
    r.startDrag();
    r.lane.modifierKeysChanged(kOptionDown);
    ASSERT_TRUE(r.lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(r.lane.isCopyDragForTest());
    EXPECT_FLOAT_EQ(r.lane.getCopyGhostFadeForTest(), 0.0f);
    r.release(kAlt);
    EXPECT_EQ(r.clipCount(), 1u);
    EXPECT_DOUBLE_EQ(r.doc.getClip(r.clip)->startBeat, 2.0);
    EXPECT_DOUBLE_EQ(r.doc.getSection(r.section)->lengthBeats, 8.0);
    EXPECT_FALSE(r.undo.canUndo());
}

TEST(TimelineClipLaneOptionToggle, AModifierChangeOutsideADragDoesNothing) {
    ToggleRig r;
    r.lane.modifierKeysChanged(kOptionDown);
    EXPECT_FALSE(r.lane.isCopyDragForTest());
    EXPECT_FLOAT_EQ(r.lane.getCopyGhostFadeForTest(), 0.0f);
}

// The clip eases between its place and the drag's when Option flips, rather than jumping.
TEST(TimelineClipLaneOptionToggle, TheDraggedClipGlidesHomeWhenOptionIsPressedAndOutWhenItIsReleased) {
    ToggleRig r;
    r.lane.getClipGlideForTest().forceAnimateForTest(true);
    r.startDrag();
    r.lane.getClipGlideForTest().stepForTest(1000.0); // let the drag steps settle
    const int dragX = r.lane.getDrawnClipRectForTest(r.clip).getX();
    const int homeX = r.lane.getClipRect(r.clip).getX();
    ASSERT_GT(dragX, homeX + 40);

    r.lane.modifierKeysChanged(kOptionDown);
    EXPECT_EQ(r.lane.getDrawnClipRectForTest(r.clip).getX(), dragX) << "first frame still where it was drawn";
    r.lane.getClipGlideForTest().stepForTest(40.0);
    const int mid = r.lane.getDrawnClipRectForTest(r.clip).getX();
    EXPECT_LT(mid, dragX);
    EXPECT_GT(mid, homeX) << "midway home";
    r.lane.getClipGlideForTest().stepForTest(1000.0);
    EXPECT_EQ(r.lane.getDrawnClipRectForTest(r.clip).getX(), homeX);

    r.lane.modifierKeysChanged(juce::ModifierKeys());
    EXPECT_EQ(r.lane.getDrawnClipRectForTest(r.clip).getX(), homeX) << "first frame still at home";
    r.lane.getClipGlideForTest().stepForTest(1000.0);
    EXPECT_EQ(r.lane.getDrawnClipRectForTest(r.clip).getX(), dragX);
    r.release();
}
