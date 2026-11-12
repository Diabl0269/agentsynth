// TimelineClipLaneDragGlideTests.cpp -- dragging a clip's edge (or the clip) with snap on glides the drawn clip from
// one grid line to the next instead of popping, the notes inside following by the sizing rule, the preview and the drop
// final at once (docs/timeline/clips.md "Edge drag", docs/layout/undo-glide.md). The real mouse path drives the drag;
// the tween clock is stepped by hand.
#include "TimelineClipLaneTestFixture.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include <array>
#include <cmath>

namespace {

using synth::ClipSizingMode;

struct ReducedMotionGuard {
    ReducedMotionGuard() { synth::ui::setReducedMotionForTest(true); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// A clip at beats 2..6 (x 80..240 at 40 px per beat) with two notes on one pitch, at beats 3 and 5.
struct DragRig : ClipLaneFixture {
    ClipId clip;
    juce::Point<float> grab;

    explicit DragRig(ClipSizingMode mode = ClipSizingMode::Trim) {
        const auto track = doc.addTrack(TrackKind::Midi, "Track 1");
        clip = doc.addClip(track, 2.0, 4.0, "Clip");
        doc.addNote(clip, makeNote(1.0, 60, 1.0));
        doc.addNote(clip, makeNote(3.0, 60, 0.5));
        lane.setClipSizingMode(mode);
        lane.refreshFromDoc();
        lane.getClipGlideForTest().forceAnimateForTest(true);
    }
    juce::Point<float> edgePoint(bool left) const {
        const auto rect = lane.getClipRect(clip);
        return {left ? (float)rect.getX() + 3.0f : (float)rect.getRight() - 3.0f, (float)rect.getCentreY()};
    }
    void press(bool left) {
        grab = edgePoint(left);
        lane.mouseDown(leftClick(lane, grab));
    }
    void dragBy(float px) { lane.mouseDrag(leftDrag(lane, {grab.x + px, grab.y}, grab)); }
    void release(float px) {
        lane.mouseUp(leftDrag(lane, {grab.x + px, grab.y}, grab));
        lane.refreshFromDoc();
    }
    juce::Rectangle<int> drawn() const { return lane.getDrawnClipRectForTest(clip); }
    double noteX(int index) const { return lane.getDrawnNoteXForTest(clip, index); }
    void step(double ms) { lane.getClipGlideForTest().stepForTest(ms); }
    bool gliding() { return lane.getClipGlideForTest().isAnimating(); }
    juce::Image paint() {
        juce::Image image(juce::Image::ARGB, lane.getWidth(), lane.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        lane.paintEntireComponent(g, false);
        return image;
    }
};

} // namespace

// Regression test for FRO1330: the snapped preview painted at once, so every grid crossing was one frame jump.
TEST(TimelineClipLaneDragGlide, ARightEdgeStepAcrossTwoGridLinesGlidesWhileThePreviewIsFinalAtOnce) {
    DragRig r;
    ASSERT_EQ(r.drawn().getRight(), 240);
    r.press(false);
    r.dragBy(80.0f); // two grid lines to the right
    ASSERT_TRUE(r.gliding());
    const auto preview = r.lane.getEffectiveGeometryForTest(r.clip);
    ASSERT_TRUE(preview.has_value());
    EXPECT_DOUBLE_EQ(preview->second, 6.0) << "the preview is on the new line at once";
    EXPECT_EQ(r.drawn().getRight(), 240) << "the first frame is still at the old line";
    r.step(40.0);
    EXPECT_GT(r.drawn().getRight(), 240);
    EXPECT_LT(r.drawn().getRight(), 320) << "midway between the two lines";
    r.step(400.0);
    EXPECT_EQ(r.drawn().getRight(), 320);
    EXPECT_FALSE(r.gliding());
    r.release(80.0f);
}

// The second step starts from where the first is drawn, never from the line it was heading for.
TEST(TimelineClipLaneDragGlide, ASecondStepMidGlideContinuesFromWhereTheEdgeIsDrawn) {
    DragRig r;
    r.press(false);
    r.dragBy(40.0f);
    r.step(60.0);
    const int midway = r.drawn().getRight();
    ASSERT_GT(midway, 240);
    ASSERT_LT(midway, 280);
    r.dragBy(80.0f);
    EXPECT_EQ(r.drawn().getRight(), midway) << "no jump at the new step";
    r.step(400.0);
    EXPECT_EQ(r.drawn().getRight(), 320);
    r.release(80.0f);
}

// The left edge, in each sizing mode: the edge glides and the notes follow the rule the drop uses (Trim keeps them on
// their beat under the sliding edge, Carry takes them along, Stretch scales them), landing where the drop leaves them.
TEST(TimelineClipLaneDragGlide, ALeftEdgeStepGlidesTheEdgeAndTheNotesBySizingMode) {
    struct Expect {
        ClipSizingMode mode;
        double firstNoteEnd; // the first note's drawn x once settled (after the clip start moved to beat 3)
    };
    // Note 0 starts at beat 3 (x 120). Left edge +1 beat: Trim keeps x 120; Carry keeps it 1 beat into the clip, so
    // beat 4 (x 160); Stretch scales the offsets by 3/4, so 3 + 0.75 = beat 3.75 (x 150).
    for (const auto& e : {Expect{ClipSizingMode::Trim, 120.0}, Expect{ClipSizingMode::CarryContents, 160.0},
                          Expect{ClipSizingMode::Stretch, 150.0}}) {
        SCOPED_TRACE(synth::clipSizingModeKey(e.mode));
        DragRig r(e.mode);
        ASSERT_NEAR(r.noteX(0), 120.0, 0.01);
        r.press(true);
        r.dragBy(40.0f);
        EXPECT_EQ(r.drawn().getX(), 80) << "the first frame is still at the old edge";
        EXPECT_NEAR(r.noteX(0), 120.0, 1.0) << "and the notes at their old place";
        r.step(40.0);
        EXPECT_GT(r.drawn().getX(), 80);
        EXPECT_LT(r.drawn().getX(), 120);
        if (e.firstNoteEnd != 120.0) {
            EXPECT_GT(r.noteX(0), 120.0) << "the notes are on their way";
            EXPECT_LT(r.noteX(0), e.firstNoteEnd);
        } else {
            EXPECT_NEAR(r.noteX(0), 120.0, 0.01) << "Trim: the edge slides over notes that keep their beat";
        }
        r.step(400.0);
        EXPECT_EQ(r.drawn().getX(), 120);
        EXPECT_NEAR(r.noteX(0), e.firstNoteEnd, 0.01);
        r.release(40.0f);
    }
}

// A whole clip dragged sideways across the grid glides the same way (nothing else moves).
TEST(TimelineClipLaneDragGlide, ABodyDragStepGlidesTheClipAndItsNotes) {
    DragRig r;
    const auto rect = r.lane.getClipRect(r.clip);
    r.grab = {(float)rect.getCentreX(), (float)rect.getCentreY()};
    r.lane.mouseDown(leftClick(r.lane, r.grab));
    r.dragBy(40.0f);
    EXPECT_EQ(r.drawn().getX(), 80);
    EXPECT_NEAR(r.noteX(0), 120.0, 1.0);
    r.step(40.0);
    EXPECT_GT(r.drawn().getX(), 80);
    EXPECT_LT(r.drawn().getX(), 120);
    r.step(400.0);
    EXPECT_EQ(r.drawn().getX(), 120);
    EXPECT_NEAR(r.noteX(0), 160.0, 0.01);
    r.release(40.0f);
}

// Under Reduce Motion nothing eases: the drawn clip is on the line at once.
TEST(TimelineClipLaneDragGlide, UnderReduceMotionAStepLandsAtOnce) {
    DragRig r;
    ReducedMotionGuard reduced;
    r.press(false);
    r.dragBy(80.0f);
    EXPECT_FALSE(r.gliding());
    EXPECT_EQ(r.drawn().getRight(), 320);
    r.release(80.0f);
}

// With snap off the preview moves under a pixel per pointer event: easing each would only lag the clip behind the
// pointer, so those steps stay plain; a snapped step of the same drag glides.
TEST(TimelineClipLaneDragGlide, SubPixelStepsWithSnapOffDoNotGlide) {
    DragRig r;
    r.state.snap = TimelineViewState::Snap::Off;
    r.press(false);
    for (const float px : {0.1f, 0.2f, 0.3f, 0.4f}) {
        r.dragBy(px);
        EXPECT_FALSE(r.gliding()) << px << " px is no jump";
    }
    r.dragBy(30.0f);
    EXPECT_TRUE(r.gliding()) << "a real jump still eases";
    r.release(30.0f);
}

// The drop is not a second jump: the glide still running at mouse-up carries on across the commit.
TEST(TimelineClipLaneDragGlide, TheGlideCarriesAcrossTheDrop) {
    DragRig r;
    r.press(false);
    r.dragBy(80.0f);
    r.step(50.0);
    const int before = r.drawn().getRight();
    ASSERT_GT(before, 240);
    ASSERT_LT(before, 320);
    r.release(80.0f);
    EXPECT_DOUBLE_EQ(r.doc.getClip(r.clip)->lengthBeats, 6.0) << "committed at the line";
    EXPECT_EQ(r.drawn().getRight(), before) << "no jump at the drop";
    r.step(400.0);
    EXPECT_EQ(r.drawn().getRight(), 320);
    EXPECT_FALSE(r.gliding());
    // And Cmd+Z still glides back.
    ASSERT_TRUE(r.undo.undo());
    r.lane.refreshFromDoc();
    EXPECT_TRUE(r.gliding());
    EXPECT_EQ(r.drawn().getRight(), 320);
    r.step(400.0);
    EXPECT_EQ(r.drawn().getRight(), 240);
}

// Hit tests and the drop read the preview, never the drawn rectangle: mid-glide the committed geometry is the line.
TEST(TimelineClipLaneDragGlide, TheDropReadsThePreviewNotTheDrawnRect) {
    DragRig r;
    r.press(false);
    r.dragBy(80.0f);
    r.step(20.0);
    ASSERT_LT(r.drawn().getRight(), 300);
    r.release(80.0f);
    EXPECT_DOUBLE_EQ(r.doc.getClip(r.clip)->lengthBeats, 6.0);
    EXPECT_EQ(r.lane.getClipRect(r.clip).getRight(), 320);
}

// The note lines are painted against the gliding rectangle: mid-glide the Carry notes are between their two places,
// and the first frame has them at the old one.
TEST(TimelineClipLaneDragGlide, TheNoteLinesArePaintedMidGlide) {
    DragRig r(ClipSizingMode::CarryContents);
    r.press(true);
    r.dragBy(40.0f);
    const auto rect = r.drawn();
    const float pitchFrac = 1.0f - 60.0f / 127.0f;
    const int y = rect.getY() + 2 + (int)std::lround(pitchFrac * (float)(rect.getHeight() - 4));
    const auto lineStart = [&](const juce::Image& image) {
        for (int x = rect.getX() + 6; x < rect.getRight() - 4; ++x) {
            int best = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const auto on = image.getPixelAt(x, y + dy);
                const auto off = image.getPixelAt(x, y + 8);
                best = std::max(best, std::abs((int)on.getRed() - (int)off.getRed()) +
                                          std::abs((int)on.getGreen() - (int)off.getGreen()) +
                                          std::abs((int)on.getBlue() - (int)off.getBlue()));
            }
            if (best > 40)
                return x;
        }
        return -1;
    };
    const int first = lineStart(r.paint());
    EXPECT_NEAR(first, 120, 2) << "first frame: the note line at its old place";
    r.step(50.0);
    const int mid = lineStart(r.paint());
    EXPECT_GT(mid, 122);
    EXPECT_LT(mid, 158);
    EXPECT_NEAR((double)mid, r.noteX(0), 2.5) << "painted where the glide says";
    r.step(400.0);
    EXPECT_NEAR(lineStart(r.paint()), 160, 2);
    r.release(40.0f);
}
