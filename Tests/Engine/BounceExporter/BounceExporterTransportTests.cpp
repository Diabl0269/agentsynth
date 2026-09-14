// Topic: the transport/engine round trip through a bounce, and the external-MIDI interlock for the render's duration.

#include "BounceExporterTestHelpers.h"

TEST(BounceExporterTest, TransportRestoredAfterBounce) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    auto& transport = f.engine.getTransport();
    ASSERT_TRUE(transport.locateBeat(3.0));
    ASSERT_TRUE(transport.play());
    f.driver->renderBlocks(1); // one tick is what makes the queued locate/play real

    const auto beforeBounce = transport.getPositionSnapshot();
    ASSERT_NEAR(beforeBounce.ppq, 3.0, 1e-12);
    ASSERT_TRUE(beforeBounce.playing);

    ScopedTempFile out("agentsynth_bounce_restore.wav");
    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions());
    ASSERT_TRUE(result.ok) << result.message;

    const auto afterBounce = transport.getPositionSnapshot();
    EXPECT_NEAR(afterBounce.ppq, 3.0, 1e-12) << "a bounce leaves the playhead where it found it";
    EXPECT_FALSE(afterBounce.playing) << "…and leaves it stopped, the way every DAW does";
    EXPECT_EQ(afterBounce.sampleRate, kSampleRate) << "the engine must be back on its previous render format";

    // The graph, the published snapshot and the Track In binding all survived the round trip:
    // playing the clip again still makes the note at beat 0 audible.
    ASSERT_TRUE(transport.locateBeat(0.0));
    ASSERT_TRUE(transport.play());
    const auto live = f.driver->renderBlocks(kBeatSamples / kBlockSize);
    EXPECT_GT(TestAudioHelpers::computeRMS(live, 0), kEnergyThreshold)
        << "Track In must still emit after the engine has been round-tripped through a bounce";
}

// ============================================================================
// 5b. External MIDI is suspended for exactly the render — see
//     AudioEngine::suspendExternalMidi()/isExternalMidiSuspended() and BounceSession's
//     ExternalMidiGuard. isExternalMidiSuspended()'s lifetime IS the contract: a real
//     juce::MidiInput can't be constructed in a headless test, so there is no way to drive
//     handleIncomingMidiMessage's ExternalMidiModule-name-match branch through a fake device here
//     — asserting on that lifetime is what a future edit breaking the interlock would turn red.
//     The null-source call below pins the one thing that IS reachable without a real device: the
//     suspended check has to run before handleIncomingMidiMessage ever touches `source` — reorder
//     it below the node loop and this crashes the whole test binary on a null deref (an
//     ExternalMidiModule is in the graph precisely so that loop is non-trivial) rather than merely
//     going red, since ordinarily calling this with a null source and an ExternalMidiModule
//     present is exactly what Tests/Engine/DeviceChangeTests.cpp avoids doing.
// ============================================================================

TEST(BounceExporterTest, ExternalMidiSuspendedOnlyForTheDurationOfARender) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ASSERT_NE(f.engine.getGraph().addNode(std::make_unique<ExternalMidiModule>()).get(), nullptr);

    ASSERT_FALSE(f.engine.isExternalMidiSuspended()) << "nothing should be suspended before a bounce starts";

    ScopedTempFile out("agentsynth_bounce_external_midi.wav");
    bool observedSuspended = false;
    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions(), [&](double) {
        f.engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
        observedSuspended = observedSuspended || f.engine.isExternalMidiSuspended();
        return true;
    });

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(observedSuspended) << "external MIDI must be suspended while a bounce is rendering";
    EXPECT_FALSE(f.engine.isExternalMidiSuspended()) << "and resumed again once bounce() has returned";
}

TEST(BounceExporterTest, ExternalMidiResumedAfterCancellation) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_external_midi_cancel.wav");
    bool observedSuspended = false;
    int calls = 0;
    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions(), [&](double) {
        ++calls;
        observedSuspended = observedSuspended || f.engine.isExternalMidiSuspended();
        return calls < 5; // cancel a handful of blocks in, mirroring CancellationDeletesPartialFile below
    });

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(observedSuspended) << "the render never actually suspended anything - this test proved nothing";
    EXPECT_FALSE(f.engine.isExternalMidiSuspended()) << "a cancelled bounce must resume MIDI delivery too";
}

TEST(BounceExporterTest, ExternalMidiResumedAfterSetupFailure) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    // Fails while opening the output stream, AFTER suspendDeviceCallback/suspendExternalMidi have
    // already run in BounceSession's constructor — unlike an invalid-options failure, which
    // returns before either is called. Mirrors UnwritablePathFailsCleanly below.
    const auto missingDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("agentsynth_bounce_external_midi_no_such_directory");
    ASSERT_FALSE(missingDirectory.exists()) << "this test needs a directory that genuinely isn't there";
    const auto target = missingDirectory.getChildFile("nope.wav");

    const auto result = BounceExporter::bounce(f.engine, target, defaultOptions());

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(f.engine.isExternalMidiSuspended()) << "a failed-after-suspend bounce must resume MIDI delivery too";
}

// ============================================================================
// 6. Cancelling leaves nothing behind
// ============================================================================
