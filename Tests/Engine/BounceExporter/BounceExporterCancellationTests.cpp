// Topic: cancelling a bounce mid-render leaves nothing behind, whether during the range or during its tail.

#include "BounceExporterTestHelpers.h"

TEST(BounceExporterTest, CancellationDeletesPartialFile) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_cancel.wav");

    int calls = 0;
    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions(), [&calls](double) {
        ++calls;
        return calls < 5; // cancel a handful of blocks in, long before the range ends
    });

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("cancel")) << result.message;
    EXPECT_FALSE(out.file.existsAsFile()) << "a cancelled bounce must not leave a truncated file";
    EXPECT_LT(result.samplesWritten, (juce::int64)kEightBeatSamples);

    // …and no temp file either: the sibling juce::TemporaryFile goes with the exporter's scope.
    const auto strays = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .findChildFiles(juce::File::findFiles, false, "agentsynth_bounce_cancel_temp*");
    EXPECT_TRUE(strays.isEmpty()) << "a cancelled bounce left " << strays.size() << " temp file(s) behind";

    // The engine is still usable and the transport is back where it started.
    const auto after = f.engine.getTransport().getPositionSnapshot();
    EXPECT_NEAR(after.ppq, 0.0, 1e-12);
    EXPECT_FALSE(after.playing);
}

// ============================================================================
// 7. An impossible destination fails before anything is rendered, and restores
// ============================================================================

TEST(BounceExporterTest, CancellingDuringTheTailStopsTheRender) {
    Fixture f;
    ASSERT_TRUE(f.build(/*withDelayTail=*/true));
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_cancel_tail.wav");

    auto options = defaultOptions();
    options.tailSeconds = 4.0; // 375 blocks of tail behind the 375-block range

    // Cancel on the FIRST tail block: the range is written in full, the tail is abandoned.
    int written = 0;
    const auto result = BounceExporter::bounce(f.engine, out.file, options, [&written](double) {
        ++written;
        return written <= kEightBeatBlocks;
    });

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("cancel")) << result.message;
    EXPECT_EQ(written, kEightBeatBlocks + 1) << "the cancelling block is the last one written";
    EXPECT_EQ(result.samplesWritten, (juce::int64)(kEightBeatBlocks + 1) * kBlockSize);
    EXPECT_FALSE(out.file.existsAsFile());

    const auto after = f.engine.getTransport().getPositionSnapshot();
    EXPECT_FALSE(after.playing);
}

// ============================================================================
// 10. P8-17 - a disengaged loop region is a valid bounce range
// ============================================================================
// "Current loop range" was wired (P8-5) to seed a bounce's [start, end), but the option was offered
// only while the loop was ARMED. P8-17 decouples it: the loop LOCATORS (loopStartPpq/loopEndPpq)
// name a span whether or not looping is live, and that span is a first-class bounce range. These
// tests pin that a disengaged region bounces the same audio a live one would, that the region is the
// range source independent of the arm flag, and that nothing in the armed/disengaged state leaks
// into the file. The render is [start, end) written from sample 0, so a window at a beat b sits at
// (b - startBeat) samples-per-beat, never at b absolute.
