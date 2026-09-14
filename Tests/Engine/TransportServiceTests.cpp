#include "Transport/TransportService.h"
#include <atomic>
#include <cmath>
#include <gtest/gtest.h>
#include <thread>

using synth::BlockTimeInfo;
using synth::ConstantTempoMap;
using synth::TransportService;

namespace {
constexpr double kSampleRate = 48000.0; // 120 BPM -> exactly 24000 samples per beat
constexpr int kBlockSize = 512;
constexpr double kSamplesPerBeat = 24000.0;
} // namespace

// ---------------------------------------------------------------- TempoMap ---

TEST(TempoMapTest, KnownConversionsAt120Bpm48k) {
    ConstantTempoMap map(120.0, 48000.0);
    EXPECT_DOUBLE_EQ(map.beatFromSample(24000), 1.0);
    EXPECT_DOUBLE_EQ(map.beatFromSample(0), 0.0);
    EXPECT_EQ(map.sampleFromBeat(2.0), 48000);
    EXPECT_EQ(map.sampleFromBeat(0.5), 12000);
}

TEST(TempoMapTest, RoundTripIsStable) {
    ConstantTempoMap map(133.7, 44100.0);
    // sampleFromBeat rounds to whole samples, so a round trip may be off by up to
    // half a sample's worth of beats — never more.
    const double halfSampleInBeats = 0.5 * 133.7 / (60.0 * 44100.0);
    for (double beat : {0.0, 0.25, 1.0, 3.75, 16.0, 999.5}) {
        const auto sample = map.sampleFromBeat(beat);
        EXPECT_NEAR(map.beatFromSample(sample), beat, halfSampleInBeats) << "beat " << beat;
    }
}

TEST(TempoMapTest, BpmChangeChangesConversion) {
    ConstantTempoMap map(120.0, 48000.0);
    map.setBpm(240.0);
    EXPECT_EQ(map.sampleFromBeat(1.0), 12000);
    EXPECT_DOUBLE_EQ(map.beatFromSample(12000), 1.0);
}

// -------------------------------------------------------- TransportService ---

class TransportServiceTest : public ::testing::Test {
protected:
    TransportService transport;

    void SetUp() override { transport.prepare(kSampleRate, kBlockSize); }

    // Post-and-tick helper so tests read like a user gesture followed by a block.
    const BlockTimeInfo& tick(int numSamples = kBlockSize) { return transport.tick(numSamples); }
};

TEST_F(TransportServiceTest, InitialStateIsStoppedAtZero) {
    const auto snap = transport.getPositionSnapshot();
    EXPECT_FALSE(snap.playing);
    EXPECT_DOUBLE_EQ(snap.ppq, 0.0);
    EXPECT_EQ(snap.samplePosition, 0);
    EXPECT_DOUBLE_EQ(snap.bpm, 120.0);
    EXPECT_EQ(snap.timeSigNumerator, 4);
    EXPECT_EQ(snap.timeSigDenominator, 4);
    EXPECT_FALSE(snap.looping);
}

TEST_F(TransportServiceTest, TickWhileStoppedDoesNotAdvance) {
    const auto& info = tick();
    EXPECT_FALSE(info.playing);
    EXPECT_DOUBLE_EQ(info.startPpq, 0.0);
    EXPECT_DOUBLE_EQ(info.endPpq, 0.0);
    EXPECT_EQ(info.numSamples, kBlockSize);
    EXPECT_EQ(transport.getPositionSnapshot().samplePosition, 0);
}

TEST_F(TransportServiceTest, PlayAdvancesBySamples) {
    ASSERT_TRUE(transport.play());
    const auto& info = tick();
    EXPECT_TRUE(info.playing);
    EXPECT_EQ(info.blockStartSample, 0);
    EXPECT_NEAR(info.endPpq, kBlockSize / kSamplesPerBeat, 1e-9);
    EXPECT_EQ(transport.getPositionSnapshot().samplePosition, 0); // snapshot is block start

    tick();
    EXPECT_EQ(transport.getPositionSnapshot().samplePosition, kBlockSize);
}

TEST_F(TransportServiceTest, EndPpqIsContinuousAcrossBlocks) {
    transport.play();
    double lastEnd = tick().endPpq;
    for (int i = 0; i < 20; ++i) {
        const auto& info = tick();
        EXPECT_DOUBLE_EQ(info.startPpq, lastEnd);
        lastEnd = info.endPpq;
    }
}

TEST_F(TransportServiceTest, CommandsApplyInPostingOrder) {
    ASSERT_TRUE(transport.locateBeat(4.0));
    ASSERT_TRUE(transport.setBpm(140.0));
    ASSERT_TRUE(transport.play());
    const auto& info = tick();
    EXPECT_TRUE(info.playing);
    EXPECT_DOUBLE_EQ(info.startPpq, 4.0);
    EXPECT_DOUBLE_EQ(info.bpm, 140.0);
    // Sample position was re-derived under the 140 BPM map after the locate.
    const auto expected = (std::int64_t)std::llround(4.0 * 60.0 * kSampleRate / 140.0);
    EXPECT_EQ(info.blockStartSample, expected);
}

TEST_F(TransportServiceTest, StopHoldsPosition) {
    transport.play();
    tick();
    transport.stop();
    tick();
    const auto pos = transport.getPositionSnapshot().samplePosition;
    EXPECT_EQ(pos, kBlockSize);
    tick();
    EXPECT_EQ(transport.getPositionSnapshot().samplePosition, pos);
}

TEST_F(TransportServiceTest, LocateWhilePlayingJumps) {
    transport.play();
    tick();
    transport.locateBeat(10.0);
    const auto& info = tick();
    EXPECT_DOUBLE_EQ(info.startPpq, 10.0);
}

TEST_F(TransportServiceTest, LocateClampsNegativeToZero) {
    transport.locateBeat(-3.0);
    EXPECT_DOUBLE_EQ(tick().startPpq, 0.0);
}

TEST_F(TransportServiceTest, SetBpmPreservesMusicalPosition) {
    transport.play();
    double ppqBefore = 0.0;
    for (int i = 0; i < 100; ++i)
        ppqBefore = tick().endPpq; // position after the last rendered block
    transport.setBpm(240.0);
    const auto& info = tick();
    EXPECT_DOUBLE_EQ(info.startPpq, ppqBefore);
    EXPECT_DOUBLE_EQ(info.bpm, 240.0);
    EXPECT_EQ(info.blockStartSample, (std::int64_t)std::llround(ppqBefore * 60.0 * kSampleRate / 240.0));
}

TEST_F(TransportServiceTest, LoopWrapInsideBlockReportsWrapSample) {
    transport.setLoop(0.0, 1.0, true); // one beat = 24000 samples
    transport.play();
    // Advance to sample 23552 (46 blocks); the next block crosses 24000 at offset 448.
    for (int i = 0; i < 46; ++i)
        tick();
    {
        const auto& info = tick();
        EXPECT_EQ(info.blockStartSample, 46 * kBlockSize);
        EXPECT_EQ(info.loopWrapSample, 24000 - 46 * kBlockSize);
    }
    // The next block starts where the wrap left off: 64 samples into the loop.
    const auto& next = tick();
    EXPECT_EQ(next.blockStartSample, (46 + 1) * kBlockSize - 24000);
    EXPECT_NEAR(next.startPpq, (double)next.blockStartSample / kSamplesPerBeat, 1e-9);
}

TEST_F(TransportServiceTest, LoopWrapExactlyAtBlockBoundaryIsSeamless) {
    transport.setLoop(0.0, 1.0, true);
    transport.locateBeat((24000.0 - kBlockSize) / kSamplesPerBeat);
    transport.play();
    EXPECT_EQ(tick().loopWrapSample, -1); // wrap lands on the boundary: nothing to split
    EXPECT_EQ(tick().blockStartSample, 0);
}

TEST_F(TransportServiceTest, NoWrapWhenLocatedPastLoopEnd) {
    transport.setLoop(0.0, 1.0, true);
    transport.locateBeat(2.0);
    transport.play();
    EXPECT_EQ(tick().loopWrapSample, -1);
    EXPECT_EQ(tick().blockStartSample, 48000 + kBlockSize);
}

TEST_F(TransportServiceTest, TinyLoopWrapsByModuloWithinOneBlock) {
    transport.setLoop(0.0, 1.0 / 16.0, true); // 1500 samples
    transport.play();
    EXPECT_EQ(tick(4800).loopWrapSample, 1500);
    // 4800 = 3 * 1500 + 300: the position lands 300 samples into the loop.
    EXPECT_EQ(tick().blockStartSample, 300);
}

TEST_F(TransportServiceTest, LoopDisabledDoesNotWrap) {
    transport.setLoop(0.0, 1.0, false);
    transport.locateBeat(0.9);
    transport.play();
    tick(kBlockSize * 10);
    EXPECT_GT(tick().startPpq, 1.0);
}

TEST_F(TransportServiceTest, TooShortLoopIsRefused) {
    transport.setLoop(0.0, 1.0 / 64.0, true);
    tick();
    EXPECT_FALSE(transport.getPositionSnapshot().looping);
}

TEST_F(TransportServiceTest, TimeSignatureValidation) {
    EXPECT_FALSE(transport.setTimeSignature(4, 5));
    EXPECT_FALSE(transport.setTimeSignature(0, 4));
    EXPECT_TRUE(transport.setTimeSignature(3, 4));
    tick();
    const auto snap = transport.getPositionSnapshot();
    EXPECT_EQ(snap.timeSigNumerator, 3);
    EXPECT_EQ(snap.timeSigDenominator, 4);
}

TEST_F(TransportServiceTest, FifoOverflowDropsInsteadOfBlocking) {
    int successes = 0;
    for (int i = 0; i < 400; ++i)
        if (transport.play())
            ++successes;
    EXPECT_GE(successes, 250);
    EXPECT_LT(successes, 400); // the tail was dropped, not queued unboundedly
    tick();
    EXPECT_TRUE(transport.getPositionSnapshot().playing);
    // The FIFO drained: there is room again.
    EXPECT_TRUE(transport.stop());
}

TEST_F(TransportServiceTest, PrepareKeepsMusicalPosition) {
    transport.locateBeat(2.0);
    tick();
    ASSERT_EQ(transport.getPositionSnapshot().samplePosition, 48000);
    transport.prepare(96000.0, kBlockSize);
    const auto snap = transport.getPositionSnapshot();
    EXPECT_DOUBLE_EQ(snap.ppq, 2.0);
    EXPECT_EQ(snap.samplePosition, 96000);
    EXPECT_DOUBLE_EQ(snap.sampleRate, 96000.0);
}

TEST_F(TransportServiceTest, GetPositionExposesJucePlayHeadInfo) {
    transport.setBpm(100.0);
    transport.setTimeSignature(3, 4);
    transport.setLoop(1.0, 5.0, true);
    transport.locateBeat(7.0);
    transport.play();
    tick();

    juce::AudioPlayHead& playHead = transport;
    const auto pos = playHead.getPosition();
    ASSERT_TRUE(pos.hasValue());
    EXPECT_DOUBLE_EQ(*pos->getBpm(), 100.0);
    EXPECT_DOUBLE_EQ(*pos->getPpqPosition(), 7.0);
    EXPECT_TRUE(pos->getIsPlaying());
    EXPECT_TRUE(pos->getIsLooping());
    ASSERT_TRUE(pos->getLoopPoints().hasValue());
    EXPECT_DOUBLE_EQ(pos->getLoopPoints()->ppqStart, 1.0);
    EXPECT_DOUBLE_EQ(pos->getLoopPoints()->ppqEnd, 5.0);
    ASSERT_TRUE(pos->getTimeSignature().hasValue());
    EXPECT_EQ(pos->getTimeSignature()->numerator, 3);
    EXPECT_EQ(pos->getTimeSignature()->denominator, 4);
    // 3/4: bars are 3 quarter-note beats long; ppq 7 is 1 beat into bar 2.
    EXPECT_DOUBLE_EQ(*pos->getPpqPositionOfLastBarStart(), 6.0);
    EXPECT_EQ(*pos->getBarCount(), 2);
    const auto expectedSamples = (std::int64_t)std::llround(7.0 * 60.0 * kSampleRate / 100.0);
    EXPECT_EQ(*pos->getTimeInSamples(), expectedSamples);
    EXPECT_NEAR(*pos->getTimeInSeconds(), (double)expectedSamples / kSampleRate, 1e-9);
}

TEST_F(TransportServiceTest, GetCurrentBlockInfoMatchesTickResult) {
    transport.play();
    const auto& info = tick();
    EXPECT_EQ(&info, &transport.getCurrentBlockInfo());
}

// A concurrent reader never observes a torn snapshot: with the BPM fixed, ppq and
// samplePosition must always agree under the tempo map.
TEST_F(TransportServiceTest, ConcurrentSnapshotReadsAreConsistent) {
    transport.play();
    std::atomic<bool> done{false};
    std::atomic<int> inconsistencies{0};

    std::thread reader([&] {
        while (!done.load()) {
            const auto snap = transport.getPositionSnapshot();
            const double expectedPpq = (double)snap.samplePosition / kSamplesPerBeat;
            if (std::abs(snap.ppq - expectedPpq) > 1e-6)
                inconsistencies.fetch_add(1);
        }
    });

    for (int i = 0; i < 20000; ++i)
        tick(64);
    done.store(true);
    reader.join();
    EXPECT_EQ(inconsistencies.load(), 0);
}
