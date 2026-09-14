// Topic: file format -- WAV/AIFF, bit depth, exact sample length.

#include "BounceExporterTestHelpers.h"

// ============================================================================
// 1. The file exists, opens, and is exactly as long as the block arithmetic says
// ============================================================================

TEST(BounceExporterTest, BounceProducesParseableWavOfExactLength) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_length.wav");

    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions());
    ASSERT_TRUE(result.ok) << result.message;

    // The driver renders whole blocks and stops on the first one whose end position reaches the
    // target, so the length is ceil(sampleFromBeat(8) / blockSize) * blockSize. Eight beats divide
    // evenly here, so that is 375 * 512 with no overshoot.
    EXPECT_EQ(result.samplesWritten, (juce::int64)kEightBeatSamples);
    ASSERT_TRUE(out.file.existsAsFile());

    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok) << "the bounce must open with JUCE's own WAV reader";
    EXPECT_EQ(wav.lengthInSamples, (juce::int64)kEightBeatSamples);
    EXPECT_EQ(wav.sampleRate, kSampleRate);
    EXPECT_EQ(wav.bitsPerSample, 24);
    EXPECT_EQ(wav.numChannels, kNumChannels);
    EXPECT_FALSE(wav.usesFloatingPointData) << "24-bit must be integer PCM";
}

TEST(BounceExporterTest, FloatBitDepthWritesAFloatWav) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_float.wav");

    auto options = defaultOptions();
    options.bitDepth = 32;
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;

    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok);
    EXPECT_EQ(wav.bitsPerSample, 32);
    EXPECT_TRUE(wav.usesFloatingPointData) << "bitDepth 32 means an IEEE-float WAV, not 32-bit int";
    EXPECT_EQ(wav.lengthInSamples, (juce::int64)kEightBeatSamples);
}

TEST(BounceExporterTest, AiffFormatWritesAReadableAiffFile) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_format.aiff");

    auto options = defaultOptions();
    options.format = synth::BounceFormat::Aiff;
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;

    const auto aiff = readAiff(out.file);
    ASSERT_TRUE(aiff.ok) << "the bounce must open with JUCE's own AIFF reader";
    EXPECT_EQ(aiff.bitsPerSample, 24);
    EXPECT_EQ(aiff.numChannels, kNumChannels);
    EXPECT_EQ(aiff.lengthInSamples, (juce::int64)kEightBeatSamples);
}

TEST(BounceExporterTest, AiffRejects32BitFloatBeforeAnythingIsWritten) {
    Fixture f;
    ASSERT_TRUE(f.build());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_aiff_float.aiff");

    auto options = defaultOptions();
    options.format = synth::BounceFormat::Aiff;
    options.bitDepth = 32;
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("aiff")) << result.message;
    EXPECT_FALSE(out.file.existsAsFile());
}

// ============================================================================
// 2. Energy exactly where the notes are, silence exactly where they aren't
// ============================================================================
