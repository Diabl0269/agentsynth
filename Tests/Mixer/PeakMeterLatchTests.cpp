// PeakMeterLatchTests.cpp -- FRO146: the lock-free "peak since I last looked" latch
// (Source/Mixer/PeakMeterLatch.h) that replaced ChannelStripModule/MasterModule's old
// plain-per-block-store meter, which silently dropped a hot block landing between two UI polls.
#include "Mixer/PeakMeterLatch.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

using synth::MeterReader;
using synth::PeakMeterLatch;

TEST(PeakMeterLatchTest, SilenceReadsZero) {
    PeakMeterLatch latch;
    EXPECT_EQ(latch.takePeak(MeterReader::Mixer), 0.0f);
}

TEST(PeakMeterLatchTest, MaxAcrossSeveralBlocksIsKeptUntilRead) {
    PeakMeterLatch latch;
    latch.storeBlockPeak(0.2f);
    latch.storeBlockPeak(0.9f); // the loudest block
    latch.storeBlockPeak(0.1f); // a quieter block afterwards must not overwrite the max
    EXPECT_FLOAT_EQ(latch.takePeak(MeterReader::Mixer), 0.9f);
}

TEST(PeakMeterLatchTest, ReadResetsOnlyThatCall) {
    PeakMeterLatch latch;
    latch.storeBlockPeak(0.5f);
    EXPECT_FLOAT_EQ(latch.takePeak(MeterReader::Mixer), 0.5f);
    EXPECT_EQ(latch.takePeak(MeterReader::Mixer), 0.0f) << "the same reader's very next read is silence";
}

TEST(PeakMeterLatchTest, TwoReaderSlotsAreIndependent) {
    PeakMeterLatch latch;
    latch.storeBlockPeak(0.7f);
    // The Mixer reader reads and resets its OWN slot only.
    EXPECT_FLOAT_EQ(latch.takePeak(MeterReader::Mixer), 0.7f);
    // A DIFFERENT reader that hasn't looked yet still sees the full peak -- the Mixer read above
    // must not have stolen it.
    EXPECT_FLOAT_EQ(latch.takePeak(MeterReader::TrackHeader), 0.7f);
    // Both are now drained.
    EXPECT_EQ(latch.takePeak(MeterReader::Mixer), 0.0f);
    EXPECT_EQ(latch.takePeak(MeterReader::TrackHeader), 0.0f);
}

TEST(PeakMeterLatchTest, ANewBlockAfterOneReaderDrainsStillReachesTheOtherReader) {
    PeakMeterLatch latch;
    latch.storeBlockPeak(0.3f);
    EXPECT_FLOAT_EQ(latch.takePeak(MeterReader::Mixer), 0.3f);
    // TrackHeader hasn't read yet -- a new, louder block raises its slot from its OWN un-consumed
    // 0.3f, independent of the Mixer reader having already drained its copy.
    latch.storeBlockPeak(0.6f);
    EXPECT_FLOAT_EQ(latch.takePeak(MeterReader::TrackHeader), 0.6f);
}

TEST(PeakMeterLatchTest, NaNInputIsDroppedNeverLatched) {
    PeakMeterLatch latch;
    latch.storeBlockPeak(0.4f);
    latch.storeBlockPeak(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(latch.takePeak(MeterReader::Mixer), 0.4f) << "a NaN block must not clobber the real peak";
}

TEST(PeakMeterLatchTest, NonPositiveInputIsANoOp) {
    PeakMeterLatch latch;
    latch.storeBlockPeak(0.4f);
    latch.storeBlockPeak(0.0f);
    latch.storeBlockPeak(-1.0f); // should never occur (a magnitude), but must never lower the slot
    EXPECT_FLOAT_EQ(latch.takePeak(MeterReader::Mixer), 0.4f);
}

TEST(PeakMeterLatchTest, ResetClearsEveryReadersSlot) {
    PeakMeterLatch latch;
    latch.storeBlockPeak(0.8f);
    latch.reset();
    EXPECT_EQ(latch.takePeak(MeterReader::Mixer), 0.0f);
    EXPECT_EQ(latch.takePeak(MeterReader::TrackHeader), 0.0f);
}
