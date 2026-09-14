// Topic: rendered-content correctness -- energy exactly where notes are, FX tail ring-out, and byte-identical
// determinism across repeated bounces.

#include "BounceExporterTestHelpers.h"

TEST(BounceExporterTest, EnergyWhereNotesAre) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_energy.wav");

    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions());
    ASSERT_TRUE(result.ok) << result.message;

    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok);
    ASSERT_EQ(wav.lengthInSamples, (juce::int64)kEightBeatSamples);

    for (double beat : kNoteBeats) {
        const int start = sampleAt(beat);
        const int end = start + kBeatSamples;
        const float rms = TestAudioHelpers::computeRMSInRange(wav.audio, start + kNoteGuard, end - kNoteGuard, 0);
        EXPECT_GT(rms, kEnergyThreshold) << "bounced note at beat " << beat << " measured rms=" << rms;
    }
    for (double beat : kGapBeats) {
        const int start = sampleAt(beat);
        const int end = start + kBeatSamples;
        const float rms = TestAudioHelpers::computeRMSInRange(wav.audio, start + kGapStartGuard, end - kGapEndGuard, 0);
        EXPECT_LT(rms, kSilenceThreshold) << "bounced gap at beat " << beat << " measured rms=" << rms;
    }
}

// ============================================================================
// 3. The tail: FX ring out past the range, and only when asked for
// ============================================================================

TEST(BounceExporterTest, TailRingsOut) {
    Fixture f;
    ASSERT_TRUE(f.build(/*withDelayTail=*/true));
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile tailed("agentsynth_bounce_tail.wav");

    auto options = defaultOptions();
    options.tailSeconds = 1.0;
    const auto result = BounceExporter::bounce(f.engine, tailed.file, options);
    ASSERT_TRUE(result.ok) << result.message;

    // One second of tail is 48000 samples == 93.75 blocks, rounded up to 94 whole blocks.
    constexpr int kTailBlocks = 94;
    constexpr int kTailSamples = kTailBlocks * kBlockSize;
    ASSERT_EQ((int)std::ceil(1.0 * kSampleRate / (double)kBlockSize), kTailBlocks);

    const auto wav = readWav(tailed.file);
    ASSERT_TRUE(wav.ok);
    ASSERT_EQ(wav.lengthInSamples, (juce::int64)(kEightBeatSamples + kTailSamples));

    constexpr int kWindow = 4800; // 100 ms
    const float tailStartRms =
        TestAudioHelpers::computeRMSInRange(wav.audio, kEightBeatSamples, kEightBeatSamples + kWindow, 0);
    const float tailEndRms = TestAudioHelpers::computeRMSInRange(wav.audio, kEightBeatSamples + kTailSamples - kWindow,
                                                                 kEightBeatSamples + kTailSamples, 0);

    EXPECT_GT(tailStartRms, kSilenceThreshold)
        << "the tail must still be ringing where the range ended, rms=" << tailStartRms;
    EXPECT_GT(tailStartRms, tailEndRms) << "a tail decays: start rms=" << tailStartRms << ", end rms=" << tailEndRms;

    // …and with no tail asked for, the file stops dead at the range even though the same Delay is
    // still ringing at that instant.
    ScopedTempFile untailed("agentsynth_bounce_no_tail.wav");
    const auto noTail = BounceExporter::bounce(f.engine, untailed.file, defaultOptions());
    ASSERT_TRUE(noTail.ok) << noTail.message;
    EXPECT_EQ(noTail.samplesWritten, (juce::int64)kEightBeatSamples);

    const auto untailedWav = readWav(untailed.file);
    ASSERT_TRUE(untailedWav.ok);
    EXPECT_EQ(untailedWav.lengthInSamples, (juce::int64)kEightBeatSamples);
}

// ============================================================================
// 4. Two bounces of the same project are byte-identical
// ============================================================================
// Two fixtures, one bounce each: "the same project" means the same starting state, and a second
// bounce from a live engine inherits whatever DSP state the first left behind (free-running
// oscillator phase, an un-decayed delay line) because prepareToPlay resets rates and ramps, not
// phase. Nothing on the render path reads the wall clock, so same input in, same bytes out.

TEST(BounceExporterTest, DeterministicByteIdentical) {
    ScopedTempFile first("agentsynth_bounce_det_a.wav");
    ScopedTempFile second("agentsynth_bounce_det_b.wav");

    {
        Fixture f;
        ASSERT_TRUE(f.build());
        ASSERT_TRUE(f.addStandardNotes());
        f.publish();
        const auto result = BounceExporter::bounce(f.engine, first.file, defaultOptions());
        ASSERT_TRUE(result.ok) << result.message;
    }
    {
        Fixture f;
        ASSERT_TRUE(f.build());
        ASSERT_TRUE(f.addStandardNotes());
        f.publish();
        const auto result = BounceExporter::bounce(f.engine, second.file, defaultOptions());
        ASSERT_TRUE(result.ok) << result.message;
    }

    juce::MemoryBlock firstBytes, secondBytes;
    ASSERT_TRUE(first.file.loadFileAsData(firstBytes));
    ASSERT_TRUE(second.file.loadFileAsData(secondBytes));
    EXPECT_GT(firstBytes.getSize(), (size_t)0);
    EXPECT_TRUE(firstBytes == secondBytes) << "two bounces of the same project must be byte-identical";
}

// ============================================================================
// 5. The transport (and the engine) are handed back exactly as they were found
// ============================================================================
