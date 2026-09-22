// Message thread: RemoteEngine::armLearn/cancelLearn/onLearned and the 300 ms settle window
// (RemoteEngineLearn.cpp, docs/control/midi-remote.md#learn-what-does-the-first-message-mean).
// Nothing here pumps a message loop -- every test drives handleMessage() directly (the MIDI-thread
// entry point) and calls RemoteEngine::drain() against a fake clock (setClock), same idiom as
// RemoteEngineApplyTests.cpp. Suite names contain "MidiRemote" per the ship-task --gtest_filter
// convention.

#include "MidiRemote/RemoteEngine/RemoteEngine.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr const char* kSource = "dev";

struct LearnHarness {
    RemoteEngine engine;
    double fakeNowMs = 0.0;
    LearnResult lastResult;
    int learnedCount = 0;

    LearnHarness() {
        engine.setClock([this] { return fakeNowMs; });
        engine.setSources({juce::String(kSource)}); // handleMessage requires a published source
        engine.onLearned = [this](const LearnResult& r) {
            lastResult = r;
            ++learnedCount;
        };
    }

    void arm(bool buttonLike, const juce::String& paramId = "target") {
        LearnRequest request;
        request.target.kind = Target::Kind::parameter;
        request.target.parameter.nodeUuid = "node";
        request.target.parameter.paramId = paramId;
        request.buttonLike = buttonLike;
        engine.armLearn(request);
    }

    bool send(const juce::MidiMessage& message) { return engine.handleMessage(kSource, message); }
    void advance(double ms) { fakeNowMs += ms; }
};

} // namespace

// ============================================================================
// Plain settle: highest message count wins, ties keep the first tally seen.
// ============================================================================

TEST(MidiRemoteEngineLearnTest, HighestCountWinsWhenNotButtonLike) {
    LearnHarness h;
    h.arm(/*buttonLike=*/false);

    h.send(juce::MidiMessage::controllerEvent(1, 10, 5)); // CC 10: one message
    h.advance(1.0);
    for (int i = 0; i < 5; ++i) // CC 20: five messages -- should win on count
        h.send(juce::MidiMessage::controllerEvent(1, 20, 64));
    h.engine.drain(); // tallies everything sent so far and stamps the first-event time
    h.advance(kLearnSettleMs + 1.0);
    h.engine.drain(); // now past the settle window -- resolves

    ASSERT_EQ(h.learnedCount, 1);
    EXPECT_EQ(h.lastResult.spec.number, 20);
}

TEST(MidiRemoteEngineLearnTest, TieKeepsFirstTallySeen) {
    LearnHarness h;
    h.arm(/*buttonLike=*/false);

    h.send(juce::MidiMessage::controllerEvent(1, 10, 5));  // seen first
    h.send(juce::MidiMessage::controllerEvent(1, 20, 64)); // same count, seen second
    h.engine.drain();
    h.advance(kLearnSettleMs + 1.0);
    h.engine.drain();

    ASSERT_EQ(h.learnedCount, 1);
    EXPECT_EQ(h.lastResult.spec.number, 10);
}

// ============================================================================
// buttonLike preference (FRO130 -- FRO127 left LearnRequest::buttonLike accepted but unread).
// ============================================================================

TEST(MidiRemoteEngineLearnTest, ButtonLikePrefersNoteOverHigherCountSweep) {
    LearnHarness h;
    h.arm(/*buttonLike=*/true);

    // A continuous CC sweep with a much higher count than the single note-on/off pair.
    for (int i = 0; i <= 10; ++i)
        h.send(juce::MidiMessage::controllerEvent(1, 30, (i * 127) / 10));
    h.send(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    h.send(juce::MidiMessage::noteOff(1, 60));
    h.engine.drain();
    h.advance(kLearnSettleMs + 1.0);
    h.engine.drain();

    ASSERT_EQ(h.learnedCount, 1);
    EXPECT_EQ(h.lastResult.spec.type, MessageType::note) << "the button-like note must win despite fewer messages";
    EXPECT_EQ(h.lastResult.spec.number, 60);
}

TEST(MidiRemoteEngineLearnTest, ButtonLikePrefersZeroOrOneTwentySevenCcOverSweep) {
    LearnHarness h;
    h.arm(/*buttonLike=*/true);

    // A continuous sweep (intermediate values) with a higher count than the button-shaped CC.
    for (int i = 0; i <= 10; ++i)
        h.send(juce::MidiMessage::controllerEvent(1, 30, (i * 127) / 10));
    h.send(juce::MidiMessage::controllerEvent(1, 40, 127)); // press
    h.send(juce::MidiMessage::controllerEvent(1, 40, 0));   // release -- only ever 0 or 127
    h.engine.drain();
    h.advance(kLearnSettleMs + 1.0);
    h.engine.drain();

    ASSERT_EQ(h.learnedCount, 1);
    EXPECT_EQ(h.lastResult.spec.number, 40) << "the 0/127-only CC must win over the sweep";
    EXPECT_EQ(h.lastResult.buttonMode, ButtonMode::momentary) << "sawRelease is still tracked independently";
}

TEST(MidiRemoteEngineLearnTest, ButtonLikeFallsBackToHighestCountWhenNothingLooksButtonLike) {
    LearnHarness h;
    h.arm(/*buttonLike=*/true);

    // Two CC tallies, both continuous sweeps (no button-like candidate at all) -- falls back to
    // the plain highest-count rule.
    for (int i = 0; i <= 3; ++i)
        h.send(juce::MidiMessage::controllerEvent(1, 10, (i * 127) / 3));
    for (int i = 0; i <= 8; ++i)
        h.send(juce::MidiMessage::controllerEvent(1, 20, (i * 127) / 8));
    h.engine.drain();
    h.advance(kLearnSettleMs + 1.0);
    h.engine.drain();

    ASSERT_EQ(h.learnedCount, 1);
    EXPECT_EQ(h.lastResult.spec.number, 20);
}

TEST(MidiRemoteEngineLearnTest, NotButtonLikeIgnoresButtonPreferenceEntirely) {
    LearnHarness h;
    h.arm(/*buttonLike=*/false);

    h.send(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    h.send(juce::MidiMessage::noteOff(1, 60));
    for (int i = 0; i <= 10; ++i) // higher count, and this is a plain (non-button-like) learn
        h.send(juce::MidiMessage::controllerEvent(1, 30, (i * 127) / 10));
    h.engine.drain();
    h.advance(kLearnSettleMs + 1.0);
    h.engine.drain();

    ASSERT_EQ(h.learnedCount, 1);
    EXPECT_EQ(h.lastResult.spec.type, MessageType::cc);
    EXPECT_EQ(h.lastResult.spec.number, 30);
}

// ============================================================================
// Cancel paths.
// ============================================================================

TEST(MidiRemoteEngineLearnTest, TimeoutWithNoEligibleMessageCancelsSilently) {
    LearnHarness h;
    h.arm(/*buttonLike=*/false);

    h.advance(kLearnTimeoutMs + 1.0);
    h.engine.drain();

    EXPECT_EQ(h.learnedCount, 0);
    EXPECT_FALSE(h.engine.isLearnArmed());
}

TEST(MidiRemoteEngineLearnTest, CancelLearnStopsTallying) {
    LearnHarness h;
    h.arm(/*buttonLike=*/false);
    h.send(juce::MidiMessage::controllerEvent(1, 10, 64));
    h.engine.cancelLearn();
    h.advance(kLearnSettleMs + 1.0);
    h.engine.drain();

    EXPECT_EQ(h.learnedCount, 0);
    EXPECT_FALSE(h.engine.isLearnArmed());
}

TEST(MidiRemoteEngineLearnTest, ArmingAgainReplacesThePendingLearn) {
    LearnHarness h;
    h.arm(/*buttonLike=*/false, "first");
    h.send(juce::MidiMessage::controllerEvent(1, 10, 64));
    h.engine.drain(); // tallies CC10 under "first" -- well under the 300 ms settle window

    h.arm(/*buttonLike=*/false, "second"); // replaces -- the first target's tally is discarded
    h.send(juce::MidiMessage::controllerEvent(1, 20, 64));
    h.engine.drain();
    h.advance(kLearnSettleMs + 1.0);
    h.engine.drain();

    ASSERT_EQ(h.learnedCount, 1);
    EXPECT_EQ(h.lastResult.target.parameter.paramId, "second");
    EXPECT_EQ(h.lastResult.spec.number, 20);
}
