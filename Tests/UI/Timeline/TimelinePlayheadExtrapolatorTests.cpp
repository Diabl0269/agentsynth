// TimelinePlayheadExtrapolatorTests.cpp
//
// The playhead's position between audio blocks: the pure PlayheadExtrapolator on fake blocks of 256 and 512 frames, and
// a Smooth follow driven at 60 and 120 Hz on an injected clock.

#include "Transport/TransportService.h"
#include "UI/Timeline/PlayheadExtrapolator.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {

using synth::ui::PlayheadExtrapolator;
using Snapshot = synth::TransportService::PositionSnapshot;

constexpr double kRate = 48000.0;
constexpr double kBpm = 120.0;
constexpr double kBeatsPerSecond = kBpm / 60.0;
constexpr double kClockZeroMs = 5000.0; // the fake wall clock's reading at virtual time 0

Snapshot blockSnapshot(int block, int blockFrames) {
    const double blockMs = blockFrames / kRate * 1000.0;
    Snapshot snap;
    snap.playing = true;
    snap.bpm = kBpm;
    snap.sampleRate = kRate;
    snap.ppq = block * blockMs / 1000.0 * kBeatsPerSecond;
    snap.publishedAtMs = kClockZeroMs + block * blockMs;
    return snap;
}

// Display frames at `hz` over one second of virtual time, each reading the latest block published by then.
void runFrames(int blockFrames, double hz,
               const std::function<void(double ppq, double rawPpq, double trueBeat)>& onFrame) {
    PlayheadExtrapolator extrapolator;
    const double blockMs = blockFrames / kRate * 1000.0;
    for (int frame = 1; frame <= (int)hz; ++frame) {
        const double virtualMs = frame * 1000.0 / hz;
        const int block = (int)std::floor(virtualMs / blockMs);
        const auto snap = blockSnapshot(block, blockFrames);
        const double out = extrapolator.extrapolate(snap, kClockZeroMs + virtualMs);
        onFrame(out, snap.ppq, virtualMs / 1000.0 * kBeatsPerSecond);
    }
}

constexpr double kMaxAheadBeats = PlayheadExtrapolator::kMaxAheadMs / 1000.0 * kBeatsPerSecond;

} // namespace

TEST(PlayheadExtrapolatorTest, IsMonotonicAndWithinTheClampAcrossBlocksOf256And512Frames) {
    for (const int blockFrames : {256, 512}) {
        for (const double hz : {60.0, 120.0}) {
            double previous = -1.0;
            runFrames(blockFrames, hz, [&](double out, double raw, double) {
                EXPECT_GE(out, previous) << blockFrames << " frames at " << hz << " Hz: never goes back";
                EXPECT_GE(out, raw) << "never behind the published position";
                EXPECT_LE(out, raw + kMaxAheadBeats + 1e-9) << "never more than 25 ms of beats ahead";
                previous = out;
            });
        }
    }
}

TEST(PlayheadExtrapolatorTest, TracksTheTrueTransportPositionWhereTheRawOneStepsPerBlock) {
    double worstExtrapolated = 0.0;
    double worstRaw = 0.0;
    runFrames(512, 120.0, [&](double out, double raw, double trueBeat) {
        worstExtrapolated = std::max(worstExtrapolated, std::abs(out - trueBeat));
        worstRaw = std::max(worstRaw, std::abs(raw - trueBeat));
    });
    EXPECT_LT(worstExtrapolated, 1e-9);
    EXPECT_GT(worstRaw, 0.01) << "without it the position lags by up to a block";
}

TEST(PlayheadExtrapolatorTest, ALocateResetsAtOnceEvenWhenItLandsBelowTheHeldPosition) {
    PlayheadExtrapolator extrapolator;
    auto snap = blockSnapshot(0, 512);
    snap.ppq = 10.0;
    snap.publishedAtMs = kClockZeroMs;
    EXPECT_GT(extrapolator.extrapolate(snap, kClockZeroMs + 20.0), 10.02);

    snap.ppq = 10.01;
    snap.locateCount = 1;
    EXPECT_DOUBLE_EQ(extrapolator.extrapolate(snap, kClockZeroMs), 10.01);
}

TEST(PlayheadExtrapolatorTest, TheLoopWrapLoopChangeAndStopResetToTheRawPosition) {
    PlayheadExtrapolator extrapolator;
    Snapshot snap;
    snap.playing = true;
    snap.bpm = kBpm;
    snap.looping = true;
    snap.loopStartPpq = 0.0;
    snap.loopEndPpq = 4.0;
    snap.publishedAtMs = kClockZeroMs;

    snap.ppq = 3.99;
    const double nearEnd = extrapolator.extrapolate(snap, kClockZeroMs + 25.0);
    EXPECT_LE(nearEnd, 4.0) << "never past the loop end";

    snap.ppq = 0.02; // the wrap: the position went backwards
    EXPECT_DOUBLE_EQ(extrapolator.extrapolate(snap, kClockZeroMs), 0.02);

    snap.loopEndPpq = 8.0; // a loop setting changed
    snap.ppq = 0.5;
    EXPECT_DOUBLE_EQ(extrapolator.extrapolate(snap, kClockZeroMs), 0.5);

    snap.playing = false;
    snap.ppq = 0.7;
    EXPECT_DOUBLE_EQ(extrapolator.extrapolate(snap, kClockZeroMs + 25.0), 0.7);
}

TEST(PlayheadExtrapolatorTest, ASnapshotWithoutAPublishTimeIsPassedThrough) {
    PlayheadExtrapolator extrapolator;
    Snapshot snap;
    snap.playing = true;
    snap.ppq = 3.0;
    EXPECT_DOUBLE_EQ(extrapolator.extrapolate(snap, 9999.0), 3.0);
}

// Smooth follow at a constant tempo, with the transport publishing a block of 512 frames every 10.7 ms and the display
// drawing at 60 and at 120 Hz: the view origin's step in pixels may vary by one pixel from frame to frame, no more (the
// raw position moves in block-sized steps, which at this zoom is five pixels of difference).
TEST(PlayheadFrameRateFollowTest, SmoothFollowStepsVaryByAtMostOnePixelAt60And120Hz) {
    constexpr int kBlockFrames = 512;
    constexpr double kPixelsPerBeat = 200.0;
    const double blockMs = kBlockFrames / kRate * 1000.0;

    for (const double hz : {60.0, 120.0}) {
        synth::ui::TimelinePanelComponent panel;
        panel.setSize(1200, 320);
        panel.getViewState().pixelsPerBeat = kPixelsPerBeat;
        panel.getPlayhead().setVisible(false);
        panel.setFollowPlayheadEnabled(true);
        panel.setFollowMode(synth::ui::FollowMode::Smooth);

        synth::TransportService transport;
        transport.prepare(kRate, kBlockFrames);
        panel.setTransport(&transport);
        ASSERT_TRUE(transport.locateBeat(100.0));
        ASSERT_TRUE(transport.play());

        int blocks = 0;
        double lastPublishedMs = 0.0;
        double lastPublishedVirtualMs = 0.0;
        double clock = 0.0;
        panel.getPlayhead().setClockForTest([&clock] { return clock; });
        const auto advanceTo = [&](double virtualMs) {
            while (blocks * blockMs <= virtualMs) {
                transport.tick(kBlockFrames);
                lastPublishedMs = transport.getPositionSnapshot().publishedAtMs;
                lastPublishedVirtualMs = blocks * blockMs;
                ++blocks;
            }
            clock = lastPublishedMs + (virtualMs - lastPublishedVirtualMs);
        };

        advanceTo(0.0);
        panel.updateFromTransport(transport.getPositionSnapshot(), 0.0); // the play transition starts the frames
        ASSERT_TRUE(panel.getPlayhead().isPlayheadFrameRunning());

        std::vector<int> steps;
        double previousOriginPx = panel.getViewState().firstVisibleBeat * kPixelsPerBeat;
        for (int frame = 1; frame <= (int)hz; ++frame) {
            advanceTo(frame * 1000.0 / hz);
            panel.getPlayhead().runFrameForTest();
            const double originPx = panel.getViewState().firstVisibleBeat * kPixelsPerBeat;
            if (frame > 4)
                steps.push_back((int)std::llround(originPx - previousOriginPx));
            previousOriginPx = originPx;
        }
        ASSERT_GT(steps.size(), 20u);
        const auto [low, high] = std::minmax_element(steps.begin(), steps.end());
        EXPECT_GT(*low, 0) << hz << " Hz: the view moves on every frame";
        EXPECT_LE(*high - *low, 1) << hz << " Hz: steps " << *low << ".." << *high;

        panel.getPlayhead().updateFromTransport({}, 0.0); // stopped: drops the frames before the clock goes
    }
}
