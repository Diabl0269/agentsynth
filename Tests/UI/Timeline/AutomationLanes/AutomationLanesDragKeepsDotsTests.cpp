// AutomationLanesDragKeepsDotsTests.cpp -- a dragged lane point passes other points freely and never deletes one:
// landing on another point's beat settles one tick beside it on the side the drag came from, by mouse and by a held
// key; what the last drag frame shows is what the drop writes; and two points one tick apart are drawn as a
// true vertical step. Real events on the real panel's editor.

#include "../../Layout/KeyGlide/KeyGlideTestHost.h"
#include "AutomationLanesTestFixture.h"
#include "UI/Layout/PointEditorDuplicate.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <algorithm>
#include <cmath>

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::LaneBreakpoint;

namespace {

const int kAlt = juce::ModifierKeys::altModifier;

// An open lane (snap a quarter note, one beat; 40 px per beat) with a Pointer editor whose held keys are the test's.
struct KeepLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;
    key_glide_test::FakeKeys keys;

    KeepLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        editor->getKeyHoldForTest().host = keys.host();
    }

    const std::vector<LaneBreakpoint>& points() const { return doc.getLane(lane)->points; }
    std::vector<double> beats() const {
        std::vector<double> out;
        for (const auto& p : points())
            out.push_back(p.beat);
        return out;
    }
    bool has(double beat) const {
        return std::any_of(points().begin(), points().end(), [&](const auto& p) { return p.beat == beat; });
    }
    juce::Point<float> at(double beat, double value) {
        return {(float)panel.getViewState().beatToX(beat), (float)editor->valueToY(value)};
    }

    // What the drag shows right now: the points it does not carry plus the moved ones, in beat order.
    std::vector<LaneBreakpoint> preview(const std::vector<double>& carried) const {
        std::vector<LaneBreakpoint> shown;
        for (const auto& p : points())
            if (std::find(carried.begin(), carried.end(), p.beat) == carried.end())
                shown.push_back(p);
        for (const auto& p : editor->getDragMovedPointsForTest())
            shown.push_back(p);
        std::sort(shown.begin(), shown.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
        return shown;
    }

    void holdAltRight() {
        keys.down = {juce::KeyPress::rightKey};
        keys.alt = true;
        EXPECT_TRUE(editor->keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAlt), 0)));
    }
    void frame(double glideMs) {
        keys.now = 1000.0 + synth::ui::lane_key::kTapMs + glideMs;
        editor->getKeyHoldForTest().tick();
    }
    void release() {
        keys.down.clear();
        editor->getKeyHoldForTest().checkKeyUp();
    }
};

bool sameBeats(const std::vector<LaneBreakpoint>& a, const std::vector<LaneBreakpoint>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].beat != b[i].beat || std::abs(a[i].value - b[i].value) > 1e-9)
            return false;
    return true;
}

// Five points at beats 1..5 and the one that moves at beat 0, below them all.
void addFiveAndTheMover(KeepLane& f) {
    f.doc.addBreakpoint(f.lane, 0.0, 10.0);
    for (int i = 1; i <= 5; ++i)
        f.doc.addBreakpoint(f.lane, (double)i, 20.0 + 10.0 * i);
}

} // namespace

// Regression test for FRO1331: the copy's first dot sits one tick after the last original, so a small left drag with
// snap on landed on the original's beat and the drop deleted it.
TEST(AutomationLanesDragKeepsDots, DuplicatedRightAfterThenDraggedLeftKeepsBothDotsWithSnapOnAndOff) {
    for (const bool snap : {true, false}) {
        SCOPED_TRACE(snap ? "snap on" : "snap off");
        KeepLane f;
        f.panel.getViewState().snapEnabled = snap;
        f.doc.addBreakpoint(f.lane, 0.0, 20.0);
        f.doc.addBreakpoint(f.lane, 3.0, 90.0);
        f.editor->getPointSelection().setSelection({0.0, 3.0});
        ASSERT_TRUE(f.editor->duplicateSelectedPoints());
        const double t = synth::ui::tickBeats();
        ASSERT_TRUE(f.has(3.0 + t)) << "Right after: the first copy is one tick past the last original";
        f.undo.beginNewTransaction();
        const auto rev = f.doc.getRevision();

        f.editor->getPointSelection().setSelection({3.0 + t});
        dragAcross(*f.editor, f.at(3.0 + t, 20.0), f.at(2.8 + t, 20.0), 6);
        EXPECT_EQ(f.points().size(), 4u) << "no dot is deleted";
        EXPECT_TRUE(f.has(3.0)) << "the upper dot is still there";
        const auto order = f.beats();
        EXPECT_TRUE(std::is_sorted(order.begin(), order.end()));
        if (snap) {
            EXPECT_TRUE(f.has(3.0 + t)) << "the snapped landing is the upper dot's beat, so it settles back beside it";
        } else {
            EXPECT_GT(order[1], 2.7) << "with snap off it passes the upper dot freely";
            EXPECT_LT(order[1], 2.9);
            EXPECT_EQ(order[2], 3.0) << "and the upper dot now comes after it";
            EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one edit";
            f.undo.undo();
            EXPECT_TRUE(f.has(3.0 + t)) << "one undo puts it back";
            EXPECT_EQ(f.points().size(), 4u);
        }
    }
}

TEST(AutomationLanesDragKeepsDots, AMouseDragAcrossFiveDotsKeepsThemAllAndLandsOneTickBesideTheOneItIsDroppedOn) {
    KeepLane f;
    addFiveAndTheMover(f);
    const auto before = f.points();
    const auto rev = f.doc.getRevision();
    const auto from = f.at(0.0, 10.0), to = f.at(4.0, 10.0);

    f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
    for (int i = 1; i <= 16; ++i) {
        f.editor->mouseDrag(makeDragEvent(*f.editor, from + (to - from) * ((float)i / 16.0f), from, leftButton()));
        const auto shown = f.preview({0.0});
        EXPECT_EQ(shown.size(), 6u) << "frame " << i << ": the preview shows every dot";
        for (int k = 1; k <= 5; ++k)
            EXPECT_TRUE(std::any_of(shown.begin(), shown.end(), [&](const auto& p) { return p.beat == k; }))
                << "frame " << i << ": the dot at beat " << k << " is passed, not taken";
    }
    f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));

    EXPECT_EQ(f.points().size(), 6u);
    for (int k = 1; k <= 5; ++k)
        EXPECT_TRUE(f.has((double)k)) << "beat " << k;
    EXPECT_TRUE(f.has(4.0 - synth::ui::tickBeats())) << "dropped on beat 4: one tick before it, the side it came from";
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one edit";
    f.undo.undo();
    EXPECT_TRUE(sameBeats(f.points(), before)) << "one undo step";
}

TEST(AutomationLanesDragKeepsDots, AKeyHoldAcrossFiveDotsKeepsThemAllAndIsOneUndoStep) {
    KeepLane f;
    addFiveAndTheMover(f);
    const auto before = f.points();
    const auto rev = f.doc.getRevision();
    f.editor->getPointSelection().setSelection({0.0});

    f.holdAltRight();
    double ms = 0.0;
    while (f.editor->getPreviewBeatForTest() < 4.5 && ms < 10000.0) {
        f.frame(ms += 16.0);
        const double shownAt = f.editor->getPreviewBeatForTest();
        EXPECT_NE(shownAt - std::floor(shownAt), 0.0) << "at " << ms << " ms the snapped step sits beside a dot";
        EXPECT_EQ(f.preview({0.0}).size(), 6u);
    }
    ASSERT_GE(f.editor->getPreviewBeatForTest(), 4.5) << "the hold crossed the five dots";
    const auto shown = f.preview({0.0});
    f.release();

    EXPECT_EQ(f.points().size(), 6u);
    for (int k = 1; k <= 5; ++k)
        EXPECT_TRUE(f.has((double)k)) << "beat " << k;
    EXPECT_TRUE(sameBeats(f.points(), shown)) << "the release writes what the hold showed";
    EXPECT_EQ(f.doc.getRevision(), rev + 1) << "one edit";
    f.undo.undo();
    EXPECT_TRUE(sameBeats(f.points(), before)) << "one undo step";
}

// The guard for lane point drags: at the last drag frame the preview (the points not carried plus the moved ones) is
// exactly what the drop commits, mouse and held keys, including a drop on another dot's beat.
TEST(AutomationLanesDragKeepsDots, ThePreviewAtTheLastDragFrameIsWhatTheDropCommits) {
    for (const bool snap : {true, false}) {
        SCOPED_TRACE(snap ? "snap on" : "snap off");
        KeepLane f;
        f.panel.getViewState().snapEnabled = snap;
        addFiveAndTheMover(f);
        const auto from = f.at(0.0, 10.0), to = f.at(3.0, 55.0);
        f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
        for (int i = 1; i <= 6; ++i)
            f.editor->mouseDrag(makeDragEvent(*f.editor, from + (to - from) * ((float)i / 6.0f), from, leftButton()));
        const auto shown = f.preview({0.0});
        f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));
        EXPECT_TRUE(sameBeats(f.points(), shown)) << "mouse";
        EXPECT_EQ(f.points().size(), 6u);
    }

    KeepLane keys;
    addFiveAndTheMover(keys);
    keys.editor->getPointSelection().setSelection({0.0});
    keys.holdAltRight();
    keys.frame(700.0);
    const auto shown = keys.preview({0.0});
    keys.release();
    EXPECT_TRUE(sameBeats(keys.points(), shown)) << "held keys";
}

// Regression test for FRO1331: the curve was sampled every 2 px, so two points one tick apart (a duplicate's join)
// were drawn as a 2 px ramp that leaned. The step is now drawn straight down at the points' x.
TEST(AutomationLanesDragKeepsDots, TwoPointsOneTickApartAreDrawnAsAVerticalStep) {
    KeepLane f;
    const double joint = 2.37; // between grid lines, at a fractional x
    const double t = synth::ui::tickBeats();
    f.doc.addBreakpoint(f.lane, 0.0, 90.0);
    f.doc.addBreakpoint(f.lane, joint, 90.0);
    f.doc.addBreakpoint(f.lane, joint + t, 10.0);
    f.doc.addBreakpoint(f.lane, 6.0, 10.0);

    // A tall lane, so the bands below sit well clear of the two dots drawn at the ends of the drop.
    f.editor->setSize(f.editor->getWidth(), 200);
    juce::Image image(juce::Image::ARGB, f.editor->getWidth(), f.editor->getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g(image);
        f.editor->paintEntireComponent(g, true);
    }
    const float x = f.at(joint, 0.0).x;
    // The centre of the line along a band of rows, inside a window around the joint: each pixel weighs by how much
    // brighter it is than the same row well right of the joint (the background and any grid line there).
    const auto brightness = [&](int px, int y) { return image.getPixelAt(px, y).getPerceivedBrightness(); };
    const auto centre = [&](double fromValue, double toValue) {
        double sum = 0.0, weight = 0.0;
        const int y0 = (int)std::ceil(f.editor->valueToY(fromValue)), y1 = (int)f.editor->valueToY(toValue);
        for (int y = y0; y <= y1; ++y)
            for (int px = (int)x - 5; px <= (int)x + 6; ++px) {
                const double w = std::max(0.0, (double)(brightness(px, y) - brightness((int)x + 30, y)) - 0.06);
                sum += w * px;
                weight += w;
            }
        return weight > 0.0 ? sum / weight : -1.0;
    };
    const double upper = centre(75.0, 60.0), lower = centre(40.0, 25.0);
    ASSERT_GT(upper, 0.0) << "the drop is drawn in the upper band";
    ASSERT_GT(lower, 0.0) << "and in the lower band";
    EXPECT_LT(std::abs(upper - lower), 0.25) << "the step does not lean: upper " << upper << ", lower " << lower;
    EXPECT_NEAR(upper, x - 0.5, 1.0) << "and it falls at the points' x";
}
