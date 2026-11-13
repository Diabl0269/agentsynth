// Concern: holding the cursor-glide keys. The pure motion model (tap threshold, ease-in ramp, cap,
// landing) is checked directly; the VBlank glue (TimelineCursorGlide) runs against a test clock and
// a recorded cursor; one panel-level case proves Cmd+Right is wired through the panel to the shared
// nudge state.

#include "ShortcutManager/ShortcutManager.h"
#include "Transport/CursorGlide.h"
#include "Transport/TransportNudge.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/CursorGlide/TimelineCursorGlide.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::GlideDirection;
namespace cg = synth::cursor_glide;

constexpr double kBar = 4.0;
constexpr double kCap = cg::kMaxBarsPerSecond * kBar; // beats per second

// ---- the model -----------------------------------------------------------------------------

TEST(CursorGlideModel, DoesNotMoveBeforeTheTapThreshold) {
    synth::CursorGlide glide;
    ASSERT_TRUE(glide.press(GlideDirection::Forward, 1000.0, 8.0, kBar));
    EXPECT_FALSE(glide.isGliding(1000.0 + cg::kTapMs - 1.0));
    EXPECT_DOUBLE_EQ(glide.positionAt(1000.0 + cg::kTapMs - 1.0), 8.0);
    EXPECT_TRUE(glide.isGliding(1000.0 + cg::kTapMs));
}

TEST(CursorGlideModel, VelocityStartsSlowEasesInAndIsCappedAfterTheRamp) {
    synth::CursorGlide glide;
    glide.press(GlideDirection::Forward, 0.0, 0.0, kBar);
    const double t0 = cg::kTapMs;
    EXPECT_NEAR(glide.velocityAt(t0), cg::kStartBeatsPerSecond, 1e-9);

    double previous = 0.0;
    for (double t = t0; t <= t0 + 4000.0; t += 7.0) {
        const double v = glide.velocityAt(t);
        EXPECT_GE(v, previous) << "never slows down while held, at " << t;
        EXPECT_LE(v, kCap + 1e-9) << "capped";
        previous = v;
    }
    EXPECT_NEAR(glide.velocityAt(t0 + cg::kRampMs), kCap, 1e-9);
    EXPECT_NEAR(glide.velocityAt(t0 + cg::kRampMs + 1500.0), kCap, 1e-9);
    // Ease-in: the first half of the ramp covers far less than half the speed gain.
    EXPECT_LT(glide.velocityAt(t0 + cg::kRampMs / 2.0), cg::kStartBeatsPerSecond + 0.2 * (kCap - 1.0));
}

TEST(CursorGlideModel, PositionIsTheIntegralOfVelocityWhateverTheFrameRate) {
    synth::CursorGlide glide;
    glide.press(GlideDirection::Forward, 0.0, 10.0, kBar);
    double integrated = 10.0;
    double previousT = cg::kTapMs;
    for (double t = cg::kTapMs + 1.0; t <= 3000.0; t += 1.0) {
        integrated += 0.5 * (glide.velocityAt(t) + glide.velocityAt(previousT)) * (t - previousT) / 1000.0;
        previousT = t;
    }
    EXPECT_NEAR(glide.positionAt(previousT), integrated, 0.01);
}

TEST(CursorGlideModel, GlidingBackClampsAtTheStartOfTheTimeline) {
    synth::CursorGlide glide;
    glide.press(GlideDirection::Back, 0.0, 1.0, kBar);
    EXPECT_DOUBLE_EQ(glide.positionAt(5000.0), 0.0);
}

TEST(CursorGlideModel, ARepeatedPressDoesNotRestartTheRamp) {
    synth::CursorGlide glide;
    ASSERT_TRUE(glide.press(GlideDirection::Forward, 0.0, 0.0, kBar));
    const double before = glide.velocityAt(2150.0);
    EXPECT_FALSE(glide.press(GlideDirection::Forward, 2000.0, 99.0, kBar));
    EXPECT_DOUBLE_EQ(glide.velocityAt(2150.0), before);
}

// ---- the glue ------------------------------------------------------------------------------

struct Rig {
    juce::Component owner; // never shown: the settle lands at once, which keeps tests off the VBlank
    double now = 1000.0;
    double cursor = 8.0;
    double grid = 0.0;
    bool keyHeld = true;
    std::vector<double> writes;
    std::vector<double> visible;
    synth::ui::TimelineCursorGlide glide{owner, makeHost()};

    synth::ui::TimelineCursorGlide::Host makeHost() {
        synth::ui::TimelineCursorGlide::Host host;
        host.nowMs = [this] { return now; };
        host.cursorBeat = [this] { return cursor; };
        host.beatsPerBar = [] { return kBar; };
        host.gridBeats = [this] { return grid; };
        host.moveCursor = [this](double beat) {
            cursor = beat;
            writes.push_back(beat);
        };
        host.ensureVisible = [this](double beat) { visible.push_back(beat); };
        host.isKeyHeld = [this](const juce::KeyPress&) { return keyHeld; };
        return host;
    }

    juce::KeyPress key() const {
        return juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier, 0);
    }
    void press(GlideDirection direction = GlideDirection::Forward) { glide.press(direction, key()); }
    /** Holds for `ms`, ticking every 16 ms like a 60 Hz VBlank. */
    void holdFor(double ms) {
        for (double elapsed = 0.0; elapsed < ms; elapsed += 16.0) {
            now += 16.0;
            glide.tick();
        }
    }
};

TEST(TimelineCursorGlide, ATapMovesOneBeatWhenSnapIsOff) {
    Rig rig;
    rig.cursor = 8.3;
    rig.press();
    rig.now += 100.0;
    rig.glide.tick();
    EXPECT_TRUE(rig.writes.empty()) << "nothing moves during a tap";
    rig.glide.release();
    EXPECT_DOUBLE_EQ(rig.cursor, 9.3);

    Rig back;
    back.cursor = 8.3;
    back.press(GlideDirection::Back);
    back.now += 100.0;
    back.glide.release();
    EXPECT_DOUBLE_EQ(back.cursor, 7.3);
}

TEST(TimelineCursorGlide, ATapMovesOneGridStepWhenSnapIsOn) {
    Rig rig;
    rig.grid = 0.25;
    rig.cursor = 8.1;
    rig.press();
    rig.now += 100.0;
    rig.glide.release();
    EXPECT_DOUBLE_EQ(rig.cursor, 8.25) << "to the next grid line";

    rig.cursor = 8.0; // already on a line: a full step
    rig.now += 500.0;
    rig.press();
    rig.now += 100.0;
    rig.glide.release();
    EXPECT_DOUBLE_EQ(rig.cursor, 8.25);

    rig.cursor = 8.0;
    rig.now += 500.0;
    rig.press(GlideDirection::Back);
    rig.now += 100.0;
    rig.glide.release();
    EXPECT_DOUBLE_EQ(rig.cursor, 7.75);
}

TEST(TimelineCursorGlide, HoldingAcceleratesFromSlowUpToTheCap) {
    Rig rig;
    rig.cursor = 0.0;
    rig.press();
    rig.holdFor(cg::kTapMs - 20.0);
    EXPECT_TRUE(rig.writes.empty()) << "still inside the tap window";

    rig.holdFor(4000.0);
    ASSERT_GT(rig.writes.size(), 100u);
    std::vector<double> speeds; // beats per second, frame to frame
    for (size_t i = 1; i < rig.writes.size(); ++i)
        speeds.push_back((rig.writes[i] - rig.writes[i - 1]) / 0.016);
    EXPECT_NEAR(speeds.front(), cg::kStartBeatsPerSecond, 0.3) << "starts at about one beat per second";
    for (size_t i = 1; i < speeds.size(); ++i)
        EXPECT_GE(speeds[i], speeds[i - 1] - 1e-6) << "velocity is monotonic, frame " << i;
    EXPECT_NEAR(speeds.back(), kCap, 0.5) << "capped at about eight bars per second";
    EXPECT_LE(*std::max_element(speeds.begin(), speeds.end()), kCap + 0.5);
    EXPECT_EQ(rig.visible.size(), rig.writes.size()) << "every move keeps the cursor in view";
}

TEST(TimelineCursorGlide, ReleaseWithSnapOnLandsOnTheNearestGridLine) {
    Rig rig;
    rig.grid = 0.5;
    rig.cursor = 0.0;
    rig.press();
    rig.holdFor(1300.0);
    ASSERT_FALSE(rig.writes.empty());
    const double free = rig.writes.back();
    ASSERT_GT(std::fabs(free / 0.5 - std::round(free / 0.5)), 0.01) << "the test needs an off-grid stop";

    rig.glide.release();
    EXPECT_DOUBLE_EQ(rig.cursor, std::floor(free / 0.5 + 0.5) * 0.5);
    EXPECT_NEAR(std::fmod(rig.cursor, 0.5), 0.0, 1e-9);
}

TEST(TimelineCursorGlide, TheMotionBeforeReleaseIsNeverSnapped) {
    Rig rig;
    rig.grid = 1.0;
    rig.cursor = 0.0;
    rig.press();
    rig.holdFor(1000.0);
    bool anyOffGrid = false;
    for (double w : rig.writes)
        anyOffGrid = anyOffGrid || std::fabs(w - std::round(w)) > 0.01;
    EXPECT_TRUE(anyOffGrid) << "snap applies to the final landing only";
}

TEST(TimelineCursorGlide, ReleaseWithSnapOffStopsWhereItIs) {
    Rig rig;
    rig.cursor = 0.0;
    rig.press();
    rig.holdFor(1300.0);
    const double stopped = rig.writes.back();
    rig.glide.release();
    EXPECT_DOUBLE_EQ(rig.cursor, stopped);
}

TEST(TimelineCursorGlide, KeyRepeatDoesNotRestartTheAcceleration) {
    Rig rig;
    rig.cursor = 0.0;
    rig.press();
    for (int i = 0; i < 140; ++i) { // 2.24 s of hold, with the OS repeating the key down all the while
        rig.now += 16.0;
        rig.glide.press(GlideDirection::Forward, rig.key());
        rig.glide.tick();
    }
    const size_t n = rig.writes.size();
    ASSERT_GT(n, 10u);
    const double speed = (rig.writes[n - 1] - rig.writes[n - 2]) / 0.016;
    EXPECT_NEAR(speed, kCap, 0.5) << "a restarted ramp would still be crawling";
}

TEST(TimelineCursorGlide, NothingRunsWhenIdle) {
    Rig rig;
    EXPECT_FALSE(rig.glide.isAnimating());
    rig.press();
    EXPECT_TRUE(rig.glide.isAnimating());
    rig.holdFor(1000.0);
    rig.glide.release();
    EXPECT_FALSE(rig.glide.isAnimating()) << "frames stop with the key";
    EXPECT_FALSE(rig.glide.isHeld());

    const size_t writes = rig.writes.size();
    rig.holdFor(500.0);
    EXPECT_EQ(rig.writes.size(), writes) << "no frame moves the cursor once released";
}

TEST(TimelineCursorGlide, TheKeyBeingUpEndsTheHold) {
    Rig rig;
    rig.press();
    rig.holdFor(500.0);
    rig.keyHeld = false;
    rig.glide.keyStateMayHaveChanged();
    EXPECT_FALSE(rig.glide.isHeld());
    EXPECT_FALSE(rig.glide.isAnimating());
}

TEST(TimelineCursorGlide, GlidingBackStopsAtTheStart) {
    Rig rig;
    rig.cursor = 0.5;
    rig.press(GlideDirection::Back);
    rig.holdFor(1000.0);
    EXPECT_DOUBLE_EQ(rig.cursor, 0.0);
}

// ---- the start speed follows the grid ---------------------------------------------------------

/** Beats per second of the first moving frame of a hold on `grid`. */
double firstFrameSpeed(double grid) {
    Rig rig;
    rig.grid = grid;
    rig.cursor = 0.0;
    rig.press();
    rig.holdFor(cg::kTapMs + 40.0);
    EXPECT_GE(rig.writes.size(), 2u);
    return (rig.writes[1] - rig.writes[0]) / 0.016;
}

TEST(TimelineCursorGlide, TheStartSpeedIsProportionalToTheGridStep) {
    const double whole = firstFrameSpeed(4.0);          // 1/1
    const double sixteenth = firstFrameSpeed(0.25);     // 1/16
    const double thirtySecond = firstFrameSpeed(0.125); // 1/32
    EXPECT_NEAR(whole, 4.0 * cg::kStartGridStepsPerSecond, 1.0);
    EXPECT_NEAR(thirtySecond, 0.125 * cg::kStartGridStepsPerSecond, 0.1);
    EXPECT_GT(whole, 10.0 * thirtySecond) << "a coarse grid starts far faster than a fine one";
    EXPECT_NEAR(sixteenth, cg::kStartBeatsPerSecond, 0.3) << "a sixteenth grid starts at the old fixed speed";
}

TEST(CursorGlideModel, EveryGridAcceleratesTheSameWayFromItsOwnStartToTheCap) {
    for (const double grid : {4.0, 1.0, 0.125}) {
        synth::CursorGlide glide;
        glide.press(GlideDirection::Forward, 0.0, 0.0, kBar, grid);
        const double v0 = grid * cg::kStartGridStepsPerSecond;
        const double t0 = cg::kTapMs;
        EXPECT_NEAR(glide.velocityAt(t0), v0, 1e-9);
        for (const double u : {0.25, 0.5, 0.75})
            EXPECT_NEAR(glide.velocityAt(t0 + u * cg::kRampMs), v0 + (kCap - v0) * u * u * u, 1e-9) << grid;
        EXPECT_NEAR(glide.velocityAt(t0 + cg::kRampMs), kCap, 1e-9);
    }
}

TEST(CursorGlideModel, AGridTooCoarseForTheCapStartsAtTheCap) {
    synth::CursorGlide glide;
    glide.press(GlideDirection::Forward, 0.0, 0.0, kBar, 16.0); // 4 bars: 64 beats/s would exceed the cap
    EXPECT_NEAR(glide.velocityAt(cg::kTapMs), glide.velocityAt(cg::kTapMs + cg::kRampMs), 1e-9);
}

TEST(TimelineCursorGlide, ReleasingResetsTheSpeedToTheStartOfTheGrid) {
    Rig rig;
    rig.grid = 4.0;
    rig.cursor = 0.0;
    rig.press();
    rig.holdFor(2500.0); // up to the cap
    rig.glide.release();

    rig.grid = 0.125; // a new hold on a fine grid starts gently, not where the last one ended
    rig.writes.clear();
    rig.now += 500.0;
    rig.press();
    rig.holdFor(cg::kTapMs + 40.0);
    ASSERT_GE(rig.writes.size(), 2u);
    EXPECT_NEAR((rig.writes[1] - rig.writes[0]) / 0.016, 0.125 * cg::kStartGridStepsPerSecond, 0.1);
}

TEST(TimelineCursorGlide, ATapMovesExactlyOneGridStepOnAnyGrid) {
    for (const double grid : {4.0, 0.125}) {
        Rig rig;
        rig.grid = grid;
        rig.cursor = 8.0;
        rig.press();
        rig.now += 100.0;
        rig.glide.release();
        EXPECT_DOUBLE_EQ(rig.cursor, 8.0 + grid);
    }
}

// ---- the panel ------------------------------------------------------------------------------

TEST(TimelinePanelCursorGlide, CmdRightIsConsumedAndATapNudgesTheSharedState) {
    synth::TransportService transport;
    synth::TransportNudgeState nudge;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    panel.setTransport(&transport);
    panel.setTransportNudgeState(&nudge);
    panel.setGlideClockForTest([] { return 1000.0; }); // frozen: press and release are one tap apart

    const juce::KeyPress cmdRight(juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier, 0);
    EXPECT_TRUE(panel.keyPressed(cmdRight));
    EXPECT_TRUE(panel.getCursorGlide().isHeld());
    EXPECT_TRUE(panel.keyPressed(cmdRight)) << "OS key repeat is swallowed too";

    // No physical key is down in a test run, so the next key-state change reads as the release.
    panel.keyStateChanged(false);
    EXPECT_FALSE(panel.getCursorGlide().isHeld());
    EXPECT_TRUE(nudge.pending);
    EXPECT_DOUBLE_EQ(nudge.target, 1.0) << "a tap moves one grid step (the default quarter-note grid)";
}

TEST(TimelinePanelCursorGlide, WithoutATransportTheKeysAreLeftAlone) {
    synth::ui::TimelinePanelComponent panel;
    EXPECT_FALSE(panel.keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier, 0)));
}

TEST(TimelinePanelCursorGlide, TheGlideActionsAreRebindableTimelineActions) {
    ShortcutManager manager;
    EXPECT_EQ(manager.getBinding("timelineGlideBack"),
              juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier, 0));
    EXPECT_EQ(manager.getBinding("timelineGlideForward"),
              juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier, 0));
    EXPECT_EQ(ShortcutManager::getCategory("timelineGlideBack"), ShortcutCategory::Timeline);
    EXPECT_NE(ShortcutManager::getActionDescription("timelineGlideForward"), "timelineGlideForward");

    // Nothing else in the Timeline category already holds either chord.
    EXPECT_TRUE(manager.getConflictingAction("timelineGlideBack", manager.getBinding("timelineGlideBack")).isEmpty());
    for (const auto& id : manager.getActionIds())
        if (id != "timelineGlideBack" && ShortcutManager::getCategory(id) == ShortcutCategory::Timeline)
            EXPECT_NE(manager.getBinding(id), manager.getBinding("timelineGlideBack")) << id;
}

} // namespace
