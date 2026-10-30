// AutomationLanesSnappedHoldTests.cpp -- holding Alt+Right on a coarse grid: the selection keeps stepping at a pace
// scaled to the grid, the dot is drawn travelling during the hold, and the whole hold is one undo step.

#include "../../Layout/KeyGlide/KeyGlideTestHost.h"
#include "AutomationLanesTestFixture.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <algorithm>
#include <set>

using namespace automation_lanes_test;
using synth::ui::AutomationLaneEditor;
using synth::ui::TimelineViewState;

namespace {

struct SnapHold : LanesPanel {
    synth::TrackId track;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;
    key_glide_test::FakeKeys keys;

    SnapHold() {
        track = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = addLane(track, "cutoff");
        panel.setTrackAutomationExpanded(track, true);
        editor = panel.laneEditorForTest(lane);
        editor->setTool(AutomationLaneEditor::Tool::Pointer);
        doc.addBreakpoint(lane, 2.0, 60.0);
        panel.getViewState().snapEnabled = true;
        panel.getViewState().snap = TimelineViewState::Snap::Bar;
        editor->getKeyHoldForTest().host = keys.host();
        editor->getPointSelection().setSelection({2.0});
    }
    void holdAltRight() {
        keys.down = {juce::KeyPress::rightKey};
        keys.alt = true;
        const juce::KeyPress press(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0);
        EXPECT_TRUE(editor->keyPressed(press));
    }
    void frame(double glideMs) {
        keys.now = 1000.0 + synth::ui::lane_key::kTapMs + glideMs;
        editor->getKeyHoldForTest().tick();
    }
    void release() {
        keys.down.clear();
        editor->getKeyHoldForTest().checkKeyUp();
    }
    juce::Image render() {
        juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        editor->paintEntireComponent(g, true);
        return image;
    }
    // The held point is drawn in the fixture's fallback accent (yellow) at its preview place.
    bool dotDrawnAt(const juce::Image& image, double beat) {
        const int x = juce::roundToInt(panel.getViewState().beatToX(beat));
        const int y = juce::roundToInt(editor->valueToY(60.0));
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -2; dy <= 2; ++dy) {
                const auto c = image.getPixelAt(x + dx, y + dy);
                if (c.getRed() > 200 && c.getGreen() > 200 && c.getBlue() < 90 && c.getAlpha() > 200)
                    return true;
            }
        return false;
    }
};

} // namespace

// Regression test for FRO1199: the hold started at one beat a second, so a one-bar grid stepped once in about two
// seconds and looked like a single step.
TEST(AutomationLanesSnappedHold, AOneBarGridKeepsSteppingDuringAThreeSecondHoldAndIsOneUndoStep) {
    SnapHold f;
    const auto rev = f.doc.getRevision();
    f.holdAltRight();
    const double bar = 4.0;
    EXPECT_DOUBLE_EQ(f.editor->getPreviewBeatForTest(), 2.0 + bar) << "the tap is one bar";

    std::set<double> landed{f.editor->getPreviewBeatForTest()};
    int stepsAtOneSecond = 0;
    for (int ms = 0; ms <= 3000; ms += 50) {
        f.frame((double)ms);
        landed.insert(f.editor->getPreviewBeatForTest());
        if (ms == 1000)
            stepsAtOneSecond = (int)landed.size() - 1;
    }
    EXPECT_GE(stepsAtOneSecond, 3) << "at least three steps in the first second of the hold";
    EXPECT_GE((int)landed.size() - 1, 12) << "a steady, accelerating pace over three seconds";
    EXPECT_EQ(f.doc.getRevision(), rev) << "nothing is committed while the key is down";
    f.release();
    EXPECT_EQ(f.doc.getRevision(), rev + 1);
    f.undo.undo();
    EXPECT_EQ(f.doc.getLane(f.lane)->points.front().beat, 2.0) << "one undo takes the whole hold back";
}

TEST(AutomationLanesSnappedHold, AFineGridIsNotSlowerThanBefore) {
    SnapHold f;
    f.panel.getViewState().snap = TimelineViewState::Snap::Sixteenth;
    f.holdAltRight();
    f.frame(1000.0);
    // One beat a second is the old start speed; the ease only adds to it.
    EXPECT_GE(f.editor->getPreviewBeatForTest() - 2.0, 1.0 - 0.25);
    f.release();
}

TEST(AutomationLanesSnappedHold, TheDotIsDrawnAtItsPreviewPlaceWhileTheKeyIsDown) {
    synth::ui::setReducedMotionForTest(true);
    SnapHold f;
    f.holdAltRight();
    std::vector<double> seen;
    for (const double glideMs : {0.0, 300.0, 600.0}) {
        f.frame(glideMs);
        const double beat = f.editor->getPreviewBeatForTest();
        if (std::find(seen.begin(), seen.end(), beat) == seen.end()) {
            seen.push_back(beat);
            EXPECT_TRUE(f.dotDrawnAt(f.render(), beat)) << "the dot is drawn at beat " << beat;
        }
    }
    EXPECT_GE(seen.size(), 2u) << "the drawn dot travelled during the hold";
    EXPECT_FALSE(f.dotDrawnAt(f.render(), 2.0)) << "and it left its old place";
    f.release();
    synth::ui::setReducedMotionForTest(std::nullopt);
}
