// EdgeScrollPump: the one display-rate clock behind every edge-follow. The pan's speed is per second, eased in and
// out, carried below a pixel and stepped in whole quanta, so a 120 Hz display draws twice the frames at half the step
// and the view covers the same distance a second; and no edge follower runs a timer of its own.

#include "../SourceScan.h"
#include "UI/Layout/EdgeScrollPump.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/EdgeScrollTimer.h"
#include "UI/Timeline/TimelineViewState.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <regex>
#include <vector>

using synth::ui::EdgeScrollPump;

namespace {

constexpr double kMax = synth::ui::kEdgeAutoScrollMaxPxPerSec;

// Every step a full-speed pan makes over `seconds` at `hz`, stepped in `quantum`s.
std::vector<double> fullSpeedSteps(double hz, double seconds, double quantum) {
    juce::Component host;
    EdgeScrollPump pump(host);
    std::vector<double> steps;
    pump.onFrame = [&] { steps.push_back(pump.step(0, 1.0, kMax, quantum)); };
    pump.start();
    const int frames = (int)std::lround(seconds * hz);
    for (int i = 0; i < frames; ++i)
        pump.frameForTest(1.0 / hz);
    return steps;
}

double sum(const std::vector<double>& v, size_t from = 0) {
    double total = 0.0;
    for (size_t i = from; i < v.size(); ++i)
        total += v[i];
    return total;
}

} // namespace

TEST(EdgeScrollPump, AtFullSpeedEveryFrameStepsTheSameAndA120HzStepIsHalfA60HzOne) {
    const auto at60 = fullSpeedSteps(60.0, 1.0, 0.5);
    const auto at120 = fullSpeedSteps(120.0, 1.0, 0.5);
    ASSERT_EQ(at60.size(), 60u);
    ASSERT_EQ(at120.size(), 120u);
    // Past the 0.1 s ease in, every frame moves the same: no beat between a timer and the display.
    for (size_t i = 10; i < at60.size(); ++i)
        EXPECT_DOUBLE_EQ(at60[i], kMax / 60.0) << "60 Hz frame " << i;
    for (size_t i = 14; i < at120.size(); ++i)
        EXPECT_DOUBLE_EQ(at120[i], kMax / 120.0) << "120 Hz frame " << i;
    EXPECT_DOUBLE_EQ(at120.back() * 2.0, at60.back()) << "twice the frames, half the step";
    // The same distance a second at either rate (within one quantum and the ease in).
    EXPECT_NEAR(sum(at60), sum(at120), 2.0);
    EXPECT_NEAR(sum(at60), kMax - 0.5 * kMax * EdgeScrollPump::kRampSeconds, 6.0);
}

TEST(EdgeScrollPump, StepsAreWholeQuantaAndTheFractionCarries) {
    // 540 px/s at 120 Hz is 4.5 px a frame: whole logical pixels alternate 4 and 5, never drifting.
    const auto steps = fullSpeedSteps(120.0, 1.0, 1.0);
    for (const double s : steps)
        EXPECT_EQ(s, std::floor(s)) << "a step is whole pixels";
    for (size_t i = 20; i < steps.size(); ++i)
        EXPECT_TRUE(steps[i] == 4.0 || steps[i] == 5.0) << "frame " << i << " stepped " << steps[i];
    EXPECT_NEAR(sum(steps, 20), (double)(steps.size() - 20) * 4.5, 1.0);
}

TEST(EdgeScrollPump, TheSpeedEasesInAndOutWithinTheRamp) {
    juce::Component host;
    EdgeScrollPump pump(host);
    double target = 1.0;
    std::vector<double> steps;
    pump.onFrame = [&] { steps.push_back(pump.step(0, target, kMax, 0.0)); };
    pump.start();
    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 12; ++i)
        pump.frameForTest(dt);
    // Ease in: never a jump from rest, rising every frame, full speed by the end of the ramp.
    EXPECT_LE(steps.front(), kMax * dt * (dt / EdgeScrollPump::kRampSeconds) + 1e-9);
    for (size_t i = 1; i < steps.size(); ++i)
        EXPECT_GE(steps[i], steps[i - 1]);
    const int rampFrames = (int)std::ceil(EdgeScrollPump::kRampSeconds / dt);
    EXPECT_DOUBLE_EQ(steps[(size_t)rampFrames], kMax * dt);
    // Ease out: the pointer leaves the zone; the pan slows every frame and rests within the ramp.
    target = 0.0;
    steps.clear();
    for (int i = 0; i < rampFrames + 1; ++i)
        pump.frameForTest(dt);
    EXPECT_GT(steps.front(), 0.0) << "no dead stop";
    for (size_t i = 1; i < steps.size(); ++i)
        EXPECT_LE(steps[i], steps[i - 1]);
    EXPECT_DOUBLE_EQ(steps.back(), 0.0);
    EXPECT_TRUE(pump.settled());
}

TEST(EdgeScrollPump, ALongFrameNeverLeaps) {
    juce::Component host;
    EdgeScrollPump pump(host);
    double moved = 0.0;
    pump.onFrame = [&] { moved = pump.step(0, 1.0, kMax, 0.0); };
    pump.start();
    for (int i = 0; i < 10; ++i)
        pump.frameForTest(1.0 / 30.0);
    pump.frameForTest(2.0); // a two-second stall (a modal loop, a rebuild)
    EXPECT_LE(moved, kMax * EdgeScrollPump::kMaxFrameSeconds + 1e-9);
}

TEST(EdgeScrollPump, StopFromInsideAFrameEndsTheFrames) {
    juce::Component host;
    auto pump = std::make_unique<EdgeScrollPump>(host);
    int frames = 0;
    pump->onFrame = [&] {
        ++frames;
        pump->stop();
    };
    pump->start();
    pump->frameForTest();
    EXPECT_FALSE(pump->isRunning());
    pump->frameForTest();
    EXPECT_EQ(frames, 1);
    pump.reset(); // the deferred release of the display link must not outlive it
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

// Reduce Motion and Animations Off keep the edge pan: it is the drag's own feedback, like the playhead, not a
// decorative motion (docs/layout/animation.md "Edge scroll").
TEST(EdgeScrollPump, ReduceMotionAndAnimationsOffKeepThePan) {
    for (const auto mode : {synth::ui::AnimationMode::reduced, synth::ui::AnimationMode::off}) {
        synth::ui::setAnimationMode(mode);
        juce::Component host;
        host.setSize(400, 40);
        synth::ui::EdgeScrollTimer follow(host);
        synth::ui::TimelineViewState view;
        follow.onTick = [&] { follow.scrollView(view); };
        follow.track(true, {200, 20}, host.getLocalBounds()); // pressed in the middle
        follow.track(true, {399, 20}, host.getLocalBounds()); // then held at the right edge
        ASSERT_TRUE(follow.isRunning());
        for (int i = 0; i < 10; ++i)
            follow.frameForTest();
        EXPECT_GT(view.firstVisibleBeat, 0.0) << synth::ui::animationModeToString(mode);
        follow.stop();
    }
    synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
}

// Every edge follower runs on the pump: none starts a timer at the old edge rate or owns a juce::Timer of its own.
TEST(EdgeScrollPump, NoEdgeFollowerRunsItsOwnTimer) {
    namespace scan = synth::test::source_scan;
    const std::regex oldRate(R"(kEdgeScrollHz)");
    const std::regex ownTimer(R"(juce::Timer\b|startTimer(Hz)?\s*\()");
    int followers = 0;
    for (const auto& [path, text] : scan::sourceTexts()) {
        EXPECT_FALSE(std::regex_search(text, oldRate)) << path << " still paces an edge scroll at a fixed rate";
        const bool follower = path.find("EdgeScroll") != std::string::npos;
        if (!follower || path.find("EdgeScrollPump") != std::string::npos)
            continue;
        ++followers;
        EXPECT_FALSE(std::regex_search(text, ownTimer))
            << path << " drives an edge scroll from its own timer; step it from EdgeScrollPump instead";
    }
    EXPECT_GE(followers, 10) << "the scan no longer finds the edge followers";
    // A reorder list follows its view on the same pump: no source that follows a view through the shared follower or a
    // ReorderDragSession keeps the old drag auto-repeat going to feed it events while the pointer rests.
    const std::regex autoRepeat(R"(beginDragAutoRepeat\s*\()");
    int reorderOwners = 0;
    for (const auto& [path, text] : scan::sourceTexts()) {
        const bool owner = text.find("ViewportDragFollower") != std::string::npos ||
                           text.find("ReorderDragSession") != std::string::npos;
        if (!owner)
            continue;
        ++reorderOwners;
        EXPECT_FALSE(std::regex_search(text, autoRepeat))
            << path << " feeds an edge scroll from the drag auto-repeat; the follower's pump runs the frames";
    }
    EXPECT_GE(reorderOwners, 10) << "the scan no longer finds the reorder lists";
    for (const auto& [path, text] : scan::sourceTexts())
        EXPECT_FALSE(std::regex_search(text, std::regex(R"(struct\s+AutoScrollTimer\b)")))
            << path << " has a private edge-scroll timer again";
}
