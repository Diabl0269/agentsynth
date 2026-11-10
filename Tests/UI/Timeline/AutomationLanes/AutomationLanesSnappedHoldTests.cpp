// AutomationLanesSnappedHoldTests.cpp -- holding Alt+Right with snap on: the selection keeps stepping on a coarse grid,
// no grid steps faster than the cap, the dot and its value bubble are drawn together during the hold, and the whole
// hold is one undo step.

#include "../../Layout/KeyGlide/KeyGlideTestHost.h"
#include "AutomationLanesTestFixture.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <vector>

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

namespace {

// Holds Alt+Right for five seconds at 60 frames a second with the ease running, and records each frame: the preview
// beat, where the preview rests on screen, where the grabbed dot is drawn and where the value bubble is drawn.
struct HoldFrame {
    double ms = 0.0;
    double previewBeat = 0.0;
    float restingX = 0.0f;
    float dotX = 0.0f;
    std::optional<float> bubbleX;
};

std::vector<HoldFrame> holdFiveSeconds(TimelineViewState::Snap snap) {
    SnapHold f;
    f.panel.getViewState().snap = snap;
    f.editor->forceGlideAnimateForTest(true);
    auto& tween = f.editor->getPointTweenForTest();
    std::vector<HoldFrame> frames;
    f.holdAltRight();
    // The ease's test clock counts from its own start, which every step that eases restarts.
    double easeMs = 0.0;
    double lastBeat = f.editor->getPreviewBeatForTest();
    for (int ms = 0; ms <= 5000; ms += 16) {
        f.frame((double)ms);
        if (f.editor->getPreviewBeatForTest() != lastBeat) {
            lastBeat = f.editor->getPreviewBeatForTest();
            easeMs = 0.0;
        }
        easeMs += 16.0;
        tween.stepForTest(easeMs);
        f.render(); // the bubble takes its place from what is drawn
        const double beat = f.editor->getPreviewBeatForTest();
        const float restingX = (float)f.panel.getViewState().beatToX(beat);
        const float dotX = restingX + tween.offsetFor(synth::ui::lane_key::tweenKey(2.0)).x;
        const auto anchor = f.editor->getPointBubbleForTest().getAnchor();
        frames.push_back({(double)ms, beat, restingX, dotX, anchor ? std::optional<float>(anchor->x) : std::nullopt});
    }
    f.release();
    f.editor->forceGlideAnimateForTest(false);
    return frames;
}

double beatAt(const std::vector<HoldFrame>& frames, double ms) {
    double beat = frames.front().previewBeat;
    for (const auto& frame : frames)
        if (frame.ms <= ms)
            beat = frame.previewBeat;
    return beat;
}

void expectSteadyHold(TimelineViewState::Snap snap, double stepBeats, const char* name) {
    const double pixelsPerBeat = 40.0; // the fixture's zoom
    const auto frames = holdFiveSeconds(snap);
    const float stepPx = (float)(stepBeats * pixelsPerBeat);
    int worstStepsInASecond = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        ASSERT_TRUE(frame.bubbleX.has_value()) << name << ": the bubble is shown during the hold";
        EXPECT_NEAR(*frame.bubbleX, frame.dotX, 1.0f)
            << name << ": the bubble is on the drawn dot at " << frame.ms << " ms";
        EXPECT_LE(std::abs(frame.restingX - frame.dotX), stepPx + 1.0f)
            << name << ": the drawn dot trails the preview by at most one step at " << frame.ms << " ms";
        int steps = 0;
        for (std::size_t j = i + 1; j < frames.size() && frames[j].ms < frame.ms + 1000.0; ++j)
            steps += (int)std::lround((frames[j].previewBeat - frames[j - 1].previewBeat) / stepBeats);
        worstStepsInASecond = std::max(worstStepsInASecond, steps);
    }
    EXPECT_LE(worstStepsInASecond, (int)synth::ui::lane_key::kSnappedMaxStepsPerSecond + 1)
        << name << ": no second of the hold steps faster than the cap";
    const double fourth = beatAt(frames, 4000.0) - beatAt(frames, 3000.0);
    const double fifth = beatAt(frames, 5000.0) - beatAt(frames, 4000.0);
    EXPECT_GT(fourth, 0.0) << name;
    // Each second's count can be one step off where its edge falls between steps.
    EXPECT_NEAR(fourth, fifth, 2.0 * stepBeats + 1e-9)
        << name << ": the pace after three seconds is the pace after five";
}

} // namespace

// Regression test for FRO1332: the cap grew with the grid (a bar grid could reach 256 beats a second), each step
// restarted the ease so the dot trailed further and further behind, and the bubble sat at the preview place ahead of
// the dot.
TEST(AutomationLanesSnappedHold, ABarHoldStepsAtAnEvenCappedPaceWithTheBubbleOnTheDot) {
    expectSteadyHold(TimelineViewState::Snap::Bar, 4.0, "bar");
}

TEST(AutomationLanesSnappedHold, AQuarterHoldStepsAtAnEvenCappedPaceWithTheBubbleOnTheDot) {
    expectSteadyHold(TimelineViewState::Snap::Quarter, 1.0, "1/4");
}

TEST(AutomationLanesSnappedHold, ASixteenthHoldStepsAtAnEvenCappedPaceWithTheBubbleOnTheDot) {
    expectSteadyHold(TimelineViewState::Snap::Sixteenth, 0.25, "1/16");
}

TEST(AutomationLanesSnappedHold, TheSnappedPaceIsCountedInGridStepsWhateverTheirSize) {
    namespace k = synth::ui::lane_key;
    for (const double step : {4.0, 1.0, 0.25}) {
        const auto pace = k::snappedHoldPace(step, k::kTimeStartBeatsPerSecond);
        EXPECT_DOUBLE_EQ(pace.max, k::kSnappedMaxStepsPerSecond * step) << step;
        EXPECT_LE(pace.start, pace.max) << step;
        EXPECT_GE(pace.start, std::min(pace.max, step / k::kSnappedStepSeconds)) << step;
    }
    EXPECT_DOUBLE_EQ(k::snappedHoldPace(0.01, 1.0).start, 0.12) << "the cap wins over a free start on a fine grid";
}
