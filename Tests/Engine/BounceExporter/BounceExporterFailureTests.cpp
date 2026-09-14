// Topic: an impossible destination or invalid options fail cleanly before anything renders.

#include "BounceExporterTestHelpers.h"

TEST(BounceExporterTest, UnwritablePathFailsCleanly) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    const auto missingDirectory =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_bounce_no_such_directory");
    ASSERT_FALSE(missingDirectory.exists()) << "this test needs a directory that genuinely isn't there";
    const auto target = missingDirectory.getChildFile("nope.wav");

    const auto result = BounceExporter::bounce(f.engine, target, defaultOptions());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.isNotEmpty()) << "a failure must say why";
    EXPECT_EQ(result.samplesWritten, (juce::int64)0);
    EXPECT_FALSE(target.existsAsFile());
    EXPECT_FALSE(missingDirectory.exists()) << "a failed bounce must not create directories on its way out";

    // The engine survived the failure: same prepare format, still renders.
    const auto after = f.engine.getTransport().getPositionSnapshot();
    EXPECT_FALSE(after.playing);
    EXPECT_EQ(after.sampleRate, kSampleRate);

    auto& transport = f.engine.getTransport();
    ASSERT_TRUE(transport.locateBeat(0.0));
    ASSERT_TRUE(transport.play());
    const auto live = f.driver->renderBlocks(kBeatSamples / kBlockSize);
    EXPECT_GT(TestAudioHelpers::computeRMS(live, 0), kEnergyThreshold);
}

TEST(BounceExporterTest, InvalidOptionsAreRejectedBeforeAnythingIsWritten) {
    Fixture f;
    ASSERT_TRUE(f.build());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_invalid.wav");

    auto backwards = defaultOptions();
    backwards.endBeat = backwards.startBeat;
    EXPECT_FALSE(BounceExporter::bounce(f.engine, out.file, backwards).ok);

    auto badDepth = defaultOptions();
    badDepth.bitDepth = 20;
    EXPECT_FALSE(BounceExporter::bounce(f.engine, out.file, badDepth).ok);

    auto badRate = defaultOptions();
    badRate.sampleRate = 0.0;
    EXPECT_FALSE(BounceExporter::bounce(f.engine, out.file, badRate).ok);

    EXPECT_FALSE(out.file.existsAsFile());
}

// ============================================================================
// 8. Progress is monotonic and finishes at 1.0
// ============================================================================
