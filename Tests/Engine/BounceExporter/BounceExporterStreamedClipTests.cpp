// Topic: streamed audio clips survive a faster-than-realtime render, from their first frame and starting mid-clip.

#include "BounceExporterTestHelpers.h"

TEST(BounceExporterTest, BouncedAudioClipIsWholeFromItsFirstFrame) {
    AudioClipFixture f;
    // Prefetch paused: nothing but the bounce's own priming call can fill a ring, so a bounce that
    // does not handshake with the streamer renders pure silence here.
    ASSERT_TRUE(f.build(/*pausePrefetch=*/true, /*clipStartBeat=*/0.0, /*clipLengthBeats=*/2.0,
                        /*sourceFrames=*/4 * (juce::int64)kBeatSamples));

    ScopedTempFile out("agentsynth_bounce_clip_head.wav");

    auto options = defaultOptions();
    options.endBeat = 2.0;
    options.bitDepth = 32; // float in, float out: the file is a bit-exact copy of the asset

    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.streamDropouts, 0) << result.message;

    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok);

    constexpr int kClipSamples = 2 * kBeatSamples;
    ASSERT_GE(wav.lengthInSamples, (juce::int64)kClipSamples);

    for (int channel = 0; channel < kNumChannels; ++channel) {
        int firstBad = -1;
        const int bad = countClipMismatches(wav.audio, channel, /*sourceOffset=*/0, kClipSamples, &firstBad);
        EXPECT_EQ(bad, 0) << "channel " << channel << ": first mismatch at sample " << firstBad
                          << " — the head of a bounced clip must not be silence";
    }
}

TEST(BounceExporterTest, BounceStartingMidClipWaitsForTheRealPrefetchThread) {
    AudioClipFixture f;
    // The shipping path: the real background thread, and a range that starts two beats INTO the
    // clip, so the frame the first block needs is one no seed could have guessed.
    ASSERT_TRUE(f.build(/*pausePrefetch=*/false, /*clipStartBeat=*/0.0, /*clipLengthBeats=*/4.0,
                        /*sourceFrames=*/8 * (juce::int64)kBeatSamples));

    ScopedTempFile out("agentsynth_bounce_clip_midway.wav");

    auto options = defaultOptions();
    options.startBeat = 2.0;
    options.endBeat = 3.0;
    options.bitDepth = 32;

    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.streamDropouts, 0) << result.message;

    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok);
    ASSERT_GE(wav.lengthInSamples, (juce::int64)kBeatSamples);

    for (int channel = 0; channel < kNumChannels; ++channel) {
        int firstBad = -1;
        const int bad = countClipMismatches(wav.audio, channel, /*sourceOffset=*/2 * (juce::int64)kBeatSamples,
                                            kBeatSamples, &firstBad);
        EXPECT_EQ(bad, 0) << "channel " << channel << ": first mismatch at sample " << firstBad;
    }
}
