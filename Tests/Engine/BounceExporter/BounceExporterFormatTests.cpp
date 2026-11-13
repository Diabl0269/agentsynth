// Topic: file format -- WAV/AIFF/FLAC/MP3, bit depth, exact sample length.

#include "BounceExporterTestHelpers.h"
#include "Transport/LameLocator.h"

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

TEST(BounceExporterTest, FlacFormatWritesAReadableNonSilentFlacFile) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_format.flac");

    auto options = defaultOptions();
    options.format = synth::BounceFormat::Flac;
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(out.file));
    ASSERT_NE(reader, nullptr) << "the bounce must open with JUCE's own FLAC reader";
    EXPECT_EQ(reader->getFormatName(), "FLAC file");
    EXPECT_EQ((int)reader->bitsPerSample, 24);
    EXPECT_EQ((int)reader->numChannels, kNumChannels);
    EXPECT_EQ(reader->sampleRate, kSampleRate);
    EXPECT_EQ(reader->lengthInSamples, (juce::int64)kEightBeatSamples);

    juce::AudioBuffer<float> audio((int)reader->numChannels, (int)reader->lengthInSamples);
    reader->read(&audio, 0, (int)reader->lengthInSamples, 0, true, true);
    EXPECT_GT(audio.getMagnitude(0, audio.getNumSamples()), 0.01f) << "the FLAC bounce must carry the notes";
}

TEST(BounceExporterTest, FlacRejects32BitFloatBeforeAnythingIsWritten) {
    Fixture f;
    ASSERT_TRUE(f.build());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_flac_float.flac");

    auto options = defaultOptions();
    options.format = synth::BounceFormat::Flac;
    options.bitDepth = 32;
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("flac")) << result.message;
    EXPECT_FALSE(out.file.existsAsFile());
}

TEST(BounceExporterTest, Mp3FormatWritesANonEmptyMp3WithAnInstalledLame) {
    const auto lame = synth::findLameExecutable();
    if (!lame.existsAsFile())
        GTEST_SKIP() << "lame is not installed; MP3 export cannot be exercised here";

    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_format.mp3");
    auto options = defaultOptions();
    options.format = synth::BounceFormat::Mp3;
    options.lameExecutable = lame;
    options.mp3BitrateKbps = 128;
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;

    ASSERT_TRUE(out.file.existsAsFile());
    // 8 beats at 120 bpm is 4 s; CBR 128 kbps is 16000 bytes/s, so the size pins the bitrate.
    const double seconds = (double)result.samplesWritten / kSampleRate;
    const double expectedBytes = seconds * 128000.0 / 8.0;
    EXPECT_NEAR((double)out.file.getSize(), expectedBytes, expectedBytes * 0.1);

    // An MP3 frame header: 11 sync bits set.
    juce::MemoryBlock head;
    ASSERT_TRUE(out.file.loadFileAsData(head));
    const auto* bytes = static_cast<const juce::uint8*>(head.getData());
    const size_t start = (head.getSize() > 3 && bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3') ? 10 : 0;
    ASSERT_GT(head.getSize(), start + 2);
    EXPECT_EQ(bytes[start], 0xFF);
    EXPECT_EQ(bytes[start + 1] & 0xE0, 0xE0);
}

TEST(BounceExporterTest, Mp3WithoutLameFailsClearlyBeforeAnythingIsWritten) {
    Fixture f;
    ASSERT_TRUE(f.build());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_nolame.mp3");
    auto options = defaultOptions();
    options.format = synth::BounceFormat::Mp3;
    options.lameExecutable = juce::File("/nonexistent/lame");
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("lame")) << result.message;
    EXPECT_FALSE(out.file.existsAsFile());
}

TEST(BounceExporterTest, Mp3EncoderThatProducesNothingSurfacesAnErrorAndNoFile) {
#if JUCE_WINDOWS
    GTEST_SKIP() << "needs a POSIX shell script as the stand-in encoder";
#else
    // An "encoder" that exits successfully without writing anything - what a broken lame looks like.
    ScopedTempFile fake("agentsynth_fake_lame.sh");
    ASSERT_TRUE(fake.file.replaceWithText("#!/bin/sh\nexit 0\n"));
    ASSERT_TRUE(fake.file.setExecutePermission(true));

    Fixture f;
    ASSERT_TRUE(f.build());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_brokenlame.mp3");
    auto options = defaultOptions();
    options.format = synth::BounceFormat::Mp3;
    options.lameExecutable = fake.file;
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("mp3")) << result.message;
    EXPECT_FALSE(out.file.existsAsFile());
#endif
}
