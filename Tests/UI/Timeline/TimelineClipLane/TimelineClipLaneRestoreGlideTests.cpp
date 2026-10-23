// TimelineClipLaneRestoreGlideTests.cpp -- Cmd+Z and Cmd+Shift+Z of a clip trim glide the clip's edge back and forth
// instead of snapping it (docs/layout/animation.md "Undo and redo glide"). The real mouse path makes the trim and
// the real AppUndoManager restores it; the glide is stepped by hand.
#include "TimelineClipLaneTestFixture.h"
#include "UI/Layout/ReducedMotion.h"

namespace {

struct ReducedMotionGuard {
    ReducedMotionGuard() { synth::ui::setReducedMotionForTest(true); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

struct TrimRig : ClipLaneFixture {
    ClipId clip;

    TrimRig() {
        const auto track = doc.addTrack(TrackKind::Midi, "Track 1");
        clip = doc.addClip(track, 2.0, 4.0, "Clip");
        doc.addNote(clip, makeNote(0.0, 60, 1.0));
        doc.addNote(clip, makeNote(3.0, 64, 1.0));
        lane.refreshFromDoc();
        lane.getClipGlideForTest().forceAnimateForTest(true);
    }
    void trimLeftEdge() {
        const auto rect = lane.getClipRect(clip);
        const juce::Point<float> edge((float)rect.getX() + 3.0f, (float)rect.getCentreY());
        const juce::Point<float> dragged(edge.x + 52.0f, edge.y);
        lane.mouseDown(leftClick(lane, edge));
        lane.mouseDrag(leftDrag(lane, dragged, edge));
        lane.mouseUp(leftDrag(lane, dragged, edge));
        lane.refreshFromDoc();
    }
    int drawnX() const { return lane.getDrawnClipRectForTest(clip).getX(); }
};

} // namespace

// Regression test for FRO1065: the lane painted straight from the doc, so an undone trim snapped the clip's edge.
TEST(TimelineClipLaneRestoreGlide, UndoAndRedoOfALeftTrimGlideTheClipEdge) {
    TrimRig r;
    const int home = r.drawnX();
    r.trimLeftEdge();
    EXPECT_FALSE(r.lane.getClipGlideForTest().isAnimating()) << "the trim itself lands at once";
    const int trimmed = r.drawnX();
    ASSERT_GT(trimmed, home);

    ASSERT_TRUE(r.undo.undo());
    r.lane.refreshFromDoc();
    ASSERT_TRUE(r.lane.getClipGlideForTest().isAnimating());
    EXPECT_EQ(r.drawnX(), trimmed) << "drawn where the edge stood";
    EXPECT_EQ(r.lane.getClipRect(r.clip).getX(), home) << "the geometry is final at once";
    r.lane.getClipGlideForTest().stepForTest(40.0);
    EXPECT_GT(r.drawnX(), home);
    EXPECT_LT(r.drawnX(), trimmed);
    juce::Image frame(juce::Image::ARGB, r.lane.getWidth(), r.lane.getHeight(), true);
    juce::Graphics g(frame);
    r.lane.paintEntireComponent(g, false); // the notes are drawn against the gliding rectangle
    r.lane.getClipGlideForTest().stepForTest(400.0);
    EXPECT_EQ(r.drawnX(), home);

    ASSERT_TRUE(r.undo.redo());
    r.lane.refreshFromDoc();
    EXPECT_TRUE(r.lane.getClipGlideForTest().isAnimating()) << "redo glides too";
    r.lane.getClipGlideForTest().stepForTest(400.0);
    EXPECT_EQ(r.drawnX(), trimmed);
}

TEST(TimelineClipLaneRestoreGlide, UnderReduceMotionAnUndoneTrimLandsAtOnce) {
    TrimRig r;
    const int home = r.drawnX();
    r.trimLeftEdge();
    ReducedMotionGuard reduced;
    ASSERT_TRUE(r.undo.undo());
    r.lane.refreshFromDoc();
    EXPECT_FALSE(r.lane.getClipGlideForTest().isAnimating());
    EXPECT_EQ(r.drawnX(), home);
}

TEST(TimelineClipLaneRestoreGlide, AnEditAfterAnUndoDoesNotGlide) {
    TrimRig r;
    r.trimLeftEdge();
    ASSERT_TRUE(r.undo.undo());
    r.lane.refreshFromDoc();
    r.lane.getClipGlideForTest().stepForTest(400.0);
    r.trimLeftEdge();
    EXPECT_FALSE(r.lane.getClipGlideForTest().isAnimating()) << "only a restore glides";
}
