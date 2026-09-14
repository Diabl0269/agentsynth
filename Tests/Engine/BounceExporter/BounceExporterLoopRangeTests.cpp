// Topic: P8-17 -- a disengaged loop region is a valid bounce range, and the loop arm state must not change what lands
// in the file.

#include "BounceExporterTestHelpers.h"

namespace {
constexpr double kLoopStartBeatTest = 2.0;
constexpr double kLoopEndBeatTest = 6.0;
constexpr int kLoopSpanBlocks = 188;                           // ceil((6-2)*24000 / 512) == ceil(96000 / 512)
constexpr int kLoopSpanSamples = kLoopSpanBlocks * kBlockSize; // 96128
static_assert(kLoopSpanSamples == kLoopSpanBlocks * kBlockSize, "span block math");
} // namespace

// With looping DISABLED, a bounce of the loop region renders exactly that span, and the region is
// handed back unchanged (a bounce never arms the loop it merely borrows as a range source).
TEST(BounceExporterTest, LoopRangeWithLoopDisabledBouncesTheLoopSpan) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    auto& transport = f.engine.getTransport();
    // Locators set, loop disengaged - exactly what "loop off but locators present" is.
    ASSERT_TRUE(transport.setLoop(kLoopStartBeatTest, kLoopEndBeatTest, false));
    f.driver->renderBlocks(1); // make the queued setLoop real in the snapshot
    const auto snap = transport.getPositionSnapshot();
    EXPECT_FALSE(snap.looping);
    ASSERT_NEAR(snap.loopStartPpq, kLoopStartBeatTest, 1e-12);
    ASSERT_NEAR(snap.loopEndPpq, kLoopEndBeatTest, 1e-12);

    ScopedTempFile out("agentsynth_bounce_loopoff.wav");
    auto options = defaultOptions();
    options.startBeat = snap.loopStartPpq; // the loop region, handed in as the range
    options.endBeat = snap.loopEndPpq;
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;

    // Exactly the 4-beat span: 96000 samples -> 188 whole blocks, no overshoot.
    EXPECT_EQ(result.samplesWritten, (juce::int64)kLoopSpanSamples);
    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok);
    EXPECT_EQ(wav.lengthInSamples, (juce::int64)kLoopSpanSamples);

    // Inside the span, energy sits where the standard notes are (beats 2 and 4) and is silent in
    // the gap between them (beat 3). Beat 6's note starts exactly at the range end and is off the
    // half-open [2,6) range, so it is correctly absent. Window offsets are from the loop start.
    auto at = [](double beat) { return (int)std::llround((beat - kLoopStartBeatTest) * kSamplesPerBeat); };
    for (double beat : {2.0, 4.0}) {
        const int start = at(beat);
        const float rms =
            TestAudioHelpers::computeRMSInRange(wav.audio, start + kNoteGuard, start + kBeatSamples - kNoteGuard, 0);
        EXPECT_GT(rms, kEnergyThreshold) << "bounced note at beat " << beat << " measured rms=" << rms;
    }
    {
        const int start = at(3.0);
        const float rms = TestAudioHelpers::computeRMSInRange(wav.audio, start + kGapStartGuard,
                                                              start + kBeatSamples - kGapEndGuard, 0);
        EXPECT_LT(rms, kSilenceThreshold) << "bounced gap at beat 3 measured rms=" << rms;
    }

    // A disengaged region stays disengaged across the bounce: it renders the span linearly but
    // hands back exactly the loop state it found (stopped, not looped), the same round-trip that
    // the armed case asserts in reverse.
    const auto after = f.engine.getTransport().getPositionSnapshot();
    EXPECT_FALSE(after.looping);
    EXPECT_NEAR(after.loopStartPpq, kLoopStartBeatTest, 1e-12);
    EXPECT_NEAR(after.loopEndPpq, kLoopEndBeatTest, 1e-12);
}

// The loop ARM state must not change what lands in the file: an armed region and a disengaged region
// at the same locators bounce byte-identically (the exporter unloops for the duration in both
// cases). Two fresh fixtures, not one bounced twice - "the same project" means a fresh start, since
// a second bounce off a live engine carries the first's DSP state over.
TEST(BounceExporterTest, ArmedLoopBouncesByteIdenticallyToADisengagedOne) {
    ScopedTempFile armedFile("agentsynth_bounce_looparmed.wav");
    ScopedTempFile disarmedFile("agentsynth_bounce_loopdisarmed.wav");

    auto bounceLoopSpan = [](AudioEngine& engine, const juce::File& file) {
        auto options = defaultOptions();
        options.startBeat = kLoopStartBeatTest;
        options.endBeat = kLoopEndBeatTest;
        options.tailSeconds = 0.0;
        options.sampleRate = kSampleRate;
        options.blockSize = kBlockSize;
        options.bitDepth = 32; // float in, float out: any leak would show in the samples, not just length
        options.numChannels = kNumChannels;
        return BounceExporter::bounce(engine, file, options);
    };

    // Armed region.
    {
        Fixture f;
        ASSERT_TRUE(f.build());
        ASSERT_TRUE(f.addStandardNotes());
        f.publish();

        auto& transport = f.engine.getTransport();
        ASSERT_TRUE(transport.setLoop(kLoopStartBeatTest, kLoopEndBeatTest, true));
        f.driver->renderBlocks(1);
        ASSERT_TRUE(transport.getPositionSnapshot().looping) << "the armed setup did not take";

        const auto result = bounceLoopSpan(f.engine, armedFile.file);
        ASSERT_TRUE(result.ok) << result.message;

        // A bounce unloops for the render but hands the region back exactly as found - still
        // armed here - only stopped. That stop-without-unlooping on exit is why the two
        // files are identical: the arm flag touched nothing on the render path.
        const auto after = transport.getPositionSnapshot();
        EXPECT_NEAR(after.loopStartPpq, kLoopStartBeatTest, 1e-12);
        EXPECT_NEAR(after.loopEndPpq, kLoopEndBeatTest, 1e-12);
        EXPECT_FALSE(after.playing);
        EXPECT_TRUE(after.looping) << "the bounce stops the transport but restores the loop state it found";
    }
    // Disengaged region, same locators, fresh engine.
    {
        Fixture f;
        ASSERT_TRUE(f.build());
        ASSERT_TRUE(f.addStandardNotes());
        f.publish();

        auto& transport = f.engine.getTransport();
        ASSERT_TRUE(transport.setLoop(kLoopStartBeatTest, kLoopEndBeatTest, false));
        f.driver->renderBlocks(1);
        ASSERT_FALSE(transport.getPositionSnapshot().looping);

        const auto result = bounceLoopSpan(f.engine, disarmedFile.file);
        ASSERT_TRUE(result.ok) << result.message;
    }

    juce::MemoryBlock armedBytes, disarmedBytes;
    ASSERT_TRUE(armedFile.file.loadFileAsData(armedBytes));
    ASSERT_TRUE(disarmedFile.file.loadFileAsData(disarmedBytes));
    EXPECT_GT(armedBytes.getSize(), (size_t)0);
    EXPECT_EQ(armedBytes.getSize(), disarmedBytes.getSize()) << "the two spans must render the same length";
    EXPECT_TRUE(armedBytes == disarmedBytes) << "the loop arm state must not change what lands in the file";
}
