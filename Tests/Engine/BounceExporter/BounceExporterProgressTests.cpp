// Topic: progress reporting is monotonic and finishes at 1.0.

#include "BounceExporterTestHelpers.h"

TEST(BounceExporterTest, ProgressReachesOne) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_progress.wav");

    auto options = defaultOptions();
    options.tailSeconds = 0.5; // so the fraction has to account for both phases

    std::vector<double> reported;
    const auto result = BounceExporter::bounce(f.engine, out.file, options, [&reported](double fraction) {
        reported.push_back(fraction);
        return true;
    });
    ASSERT_TRUE(result.ok) << result.message;

    ASSERT_FALSE(reported.empty());
    EXPECT_GT(reported.front(), 0.0);
    for (std::size_t i = 1; i < reported.size(); ++i)
        EXPECT_GE(reported[i], reported[i - 1]) << "progress went backwards at report " << i;
    for (double fraction : reported)
        EXPECT_LE(fraction, 1.0);
    EXPECT_NEAR(reported.back(), 1.0, 1e-9);

    // 8 beats (375 blocks) + 0.5 s of tail (ceil(24000/512) == 47 blocks).
    EXPECT_EQ(result.samplesWritten, (juce::int64)(kEightBeatBlocks + 47) * kBlockSize);
}

// ============================================================================
// 9. Streamed audio clips survive a faster-than-realtime render
// ============================================================================
