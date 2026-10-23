// TakeCaptureWriter: the shared audio-thread -> WAV + peaks capture core behind the Rec Tap and the input
// recorder. Exact samples for mono and stereo, silence for a missing channel, the frame-0 anchor, the rate halt,
// and a writer driven from a second thread while the message thread reads its live peaks.

#include "TakeFileTestHelpers.h"
#include "Timeline/PeaksFile.h"
#include "Timeline/TakeCaptureWriter.h"
#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace {

using synth::TakeCaptureWriter;
using synth::test::readTakeWav;
using synth::test::ScopedTakeFile;

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

// Exactly representable in float, so the 32-bit float WAV round-trips it bit for bit.
float sampleAt(juce::int64 frame, int channel) {
    return (float)(frame % 1024) / 2048.0f * (channel == 0 ? 1.0f : -1.0f);
}

void pushBlocks(TakeCaptureWriter& writer, int blocks, int channels) {
    std::vector<float> left((std::size_t)kBlock), right((std::size_t)kBlock);
    for (int block = 0; block < blocks; ++block) {
        for (int i = 0; i < kBlock; ++i) {
            left[(std::size_t)i] = sampleAt((juce::int64)block * kBlock + i, 0);
            right[(std::size_t)i] = sampleAt((juce::int64)block * kBlock + i, 1);
        }
        const float* pointers[] = {left.data(), right.data()};
        writer.push(pointers, channels, kBlock, 1000 + (juce::int64)block * kBlock, true);
    }
}

} // namespace

TEST(TakeCaptureWriterTest, StereoTakeWritesExactSamplesAndPeaks) {
    juce::TimeSliceThread thread("test writer");
    ScopedTakeFile wav("agentsynth_capture_stereo.wav");
    ScopedTakeFile peaks("agentsynth_capture_stereo.agpk");
    TakeCaptureWriter writer(thread);
    ASSERT_TRUE(writer.start(wav.file, peaks.file, kRate, 2));
    EXPECT_FALSE(writer.start(wav.file, peaks.file, kRate, 2)) << "one take at a time";

    pushBlocks(writer, 8, 2);
    const auto result = writer.stop();
    thread.stopThread(2000);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.lengthSamples, 8 * kBlock);
    EXPECT_FALSE(result.overran);
    ASSERT_TRUE(result.captureStartValid);
    EXPECT_EQ(result.captureStartTimelineSample, 1000) << "frame 0 is the first pushed block's start";

    const auto contents = readTakeWav(wav.file);
    ASSERT_TRUE(contents.ok);
    ASSERT_EQ(contents.numChannels, 2);
    ASSERT_EQ(contents.lengthInSamples, 8 * kBlock);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 8 * kBlock; ++i)
            ASSERT_EQ(contents.audio.getSample(channel, i), sampleAt(i, channel)) << channel << ":" << i;

    synth::PeaksFile::Data data;
    ASSERT_TRUE(synth::PeaksFile::read(peaks.file, data));
    EXPECT_EQ(data.numChannels, 2);
    EXPECT_EQ(data.bucketSize, TakeCaptureWriter::kPeakBucketSize);
    ASSERT_EQ(data.buckets.size(), 8u * 2u) << "one bucket per 256 frames, per channel";
    EXPECT_FLOAT_EQ(data.buckets[0].second, sampleAt(255, 0)) << "left max of the first bucket";
    EXPECT_FLOAT_EQ(data.buckets[1].first, sampleAt(255, 1)) << "right min of the first bucket";
}

TEST(TakeCaptureWriterTest, MonoTakeWritesOneChannelAndAMissingChannelIsSilence) {
    juce::TimeSliceThread thread("test writer");
    ScopedTakeFile wav("agentsynth_capture_mono.wav");
    ScopedTakeFile peaks("agentsynth_capture_mono.agpk");
    ScopedTakeFile silentWav("agentsynth_capture_silent.wav");
    ScopedTakeFile silentPeaks("agentsynth_capture_silent.agpk");
    TakeCaptureWriter writer(thread, TakeCaptureWriter::kDefaultRingCapacityFrames, /*allocateRingNow*/ false);
    TakeCaptureWriter silent(thread, TakeCaptureWriter::kDefaultRingCapacityFrames, false);
    ASSERT_TRUE(writer.start(wav.file, peaks.file, kRate, 1));
    ASSERT_TRUE(silent.start(silentWav.file, silentPeaks.file, kRate, 1));

    pushBlocks(writer, 4, 1);
    const float* none[] = {nullptr, nullptr};
    for (int block = 0; block < 4; ++block)
        silent.push(none, 2, kBlock, 0, true);

    const auto result = writer.stop();
    const auto silentResult = silent.stop();
    thread.stopThread(2000);

    ASSERT_TRUE(result.ok);
    const auto contents = readTakeWav(wav.file);
    ASSERT_EQ(contents.numChannels, 1);
    ASSERT_EQ(contents.lengthInSamples, 4 * kBlock);
    for (int i = 0; i < 4 * kBlock; ++i)
        ASSERT_EQ(contents.audio.getSample(0, i), sampleAt(i, 0)) << i;

    ASSERT_TRUE(silentResult.ok);
    const auto silence = readTakeWav(silentWav.file);
    ASSERT_EQ(silence.lengthInSamples, 4 * kBlock) << "a missing channel still advances the take";
    EXPECT_EQ(silence.audio.getMagnitude(0, 0, 4 * kBlock), 0.0f);

    synth::PeaksFile::Data data;
    ASSERT_TRUE(synth::PeaksFile::read(peaks.file, data));
    EXPECT_EQ(data.numChannels, 1);
    EXPECT_EQ(data.buckets.size(), 4u);
}

TEST(TakeCaptureWriterTest, ARateChangeHaltsThePushButTheTakeStillCommits) {
    juce::TimeSliceThread thread("test writer");
    ScopedTakeFile wav("agentsynth_capture_halt.wav");
    ScopedTakeFile peaks("agentsynth_capture_halt.agpk");
    TakeCaptureWriter writer(thread);
    ASSERT_TRUE(writer.start(wav.file, peaks.file, kRate, 2));
    pushBlocks(writer, 2, 2);
    writer.latchHaltIfRateDiffers(kRate);
    EXPECT_FALSE(writer.wasHaltedByFormatChange()) << "the same rate is no boundary";
    writer.latchHaltIfRateDiffers(44100.0);
    EXPECT_TRUE(writer.wasHaltedByFormatChange());
    EXPECT_TRUE(writer.isCapturing()) << "the message thread still owns the commit";
    pushBlocks(writer, 2, 2);
    const auto result = writer.stop();
    thread.stopThread(2000);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.lengthSamples, 2 * kBlock) << "nothing after the boundary";
    EXPECT_FALSE(writer.stop().ok) << "a second stop is a no-op";
}

// The audio thread pushes while the message thread starts, reads live peaks and stops, again and again: the
// handoff a sanitizer build checks. Each take must hold exactly what was pushed while it was armed.
TEST(TakeCaptureWriterTest, PushFromAnotherThreadWhileTheMessageThreadReadsAndStops) {
    juce::TimeSliceThread thread("test writer");
    ScopedTakeFile wav("agentsynth_capture_race.wav");
    ScopedTakeFile peaks("agentsynth_capture_race.agpk");
    TakeCaptureWriter writer(thread);
    std::atomic<bool> running{true};
    std::thread audio([&] {
        std::vector<float> block((std::size_t)kBlock, 0.25f);
        const float* pointers[] = {block.data(), block.data()};
        while (running.load()) {
            writer.push(pointers, 2, kBlock, 0, true);
            std::this_thread::yield();
        }
    });
    for (int take = 0; take < 5; ++take) {
        ASSERT_TRUE(writer.start(wav.file, peaks.file, kRate, 2));
        std::vector<std::pair<float, float>> live;
        for (int i = 0; i < 20; ++i)
            writer.copyLivePeaks(live);
        const auto result = writer.stop();
        EXPECT_TRUE(result.ok);
        const auto contents = readTakeWav(wav.file);
        ASSERT_TRUE(contents.ok);
        EXPECT_EQ(contents.lengthInSamples, result.lengthSamples);
        if (contents.lengthInSamples > 0)
            EXPECT_FLOAT_EQ(contents.audio.getSample(1, 0), 0.25f);
    }
    running.store(false);
    audio.join();
    thread.stopThread(2000);
}
