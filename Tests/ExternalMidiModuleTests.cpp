#include "../Source/Modules/ExternalMidiModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <thread>
#include <vector>

class ExternalMidiModuleTest : public ::testing::Test {
protected:
    void SetUp() override { module = std::make_unique<ExternalMidiModule>(); }

    std::unique_ptr<ExternalMidiModule> module;
};

TEST_F(ExternalMidiModuleTest, AcceptsAndProducesMidi) {
    EXPECT_FALSE(module->acceptsMidi());
    EXPECT_TRUE(module->producesMidi());
}

TEST_F(ExternalMidiModuleTest, ProcessesIncomingMidi) {
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    module->pushMidiMessage(noteOn);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midiMessages;

    module->processBlock(buffer, midiMessages);

    EXPECT_EQ(midiMessages.getNumEvents(), 1);
    auto it = midiMessages.begin();
    EXPECT_EQ((*it).getMessage().getRawDataSize(), noteOn.getRawDataSize());
    EXPECT_TRUE(0 == std::memcmp((*it).getMessage().getRawData(), noteOn.getRawData(), noteOn.getRawDataSize()));
}

TEST_F(ExternalMidiModuleTest, FiltersByChannel) {
    // Set channel to 2
    auto* channelParam = (juce::AudioParameterInt*)module->getParameters()[2];
    *channelParam = 3;

    auto msg1 = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    auto msg2 = juce::MidiMessage::noteOn(2, 60, (juce::uint8)100);
    auto msg3 = juce::MidiMessage::noteOn(3, 60, (juce::uint8)100);

    module->pushMidiMessage(msg1);
    module->pushMidiMessage(msg2);
    module->pushMidiMessage(msg3);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midiMessages;

    module->processBlock(buffer, midiMessages);

    int count = 0;
    for (auto it = midiMessages.begin(); it != midiMessages.end(); ++it) {
        count++;
        EXPECT_EQ((*it).getMessage().getChannel(), 3);
    }

    EXPECT_EQ(count, 1);
}

// ===========================================================================
// MidiMessageCollector-based timestamping
// ===========================================================================

TEST_F(ExternalMidiModuleTest, UntimestampedMessagesLandAtSampleZero) {
    // Synthetic/test messages with no explicit timestamp (timestamp == 0) must still land at
    // sample 0 of the very next block, preserving backward compatibility with existing callers
    // (e.g. AudioRenderingTests.cpp) that never call setTimeStamp().
    auto noteOn = juce::MidiMessage::noteOn(1, 60, 0.8f);
    module->pushMidiMessage(noteOn);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midiMessages;
    module->processBlock(buffer, midiMessages);

    ASSERT_EQ(midiMessages.getNumEvents(), 1);
    auto it = midiMessages.begin();
    EXPECT_EQ((*it).samplePosition, 0);
    EXPECT_TRUE((*it).getMessage().isNoteOn());
}

namespace {
// Busy-wait/sleep until the given absolute wall-clock time (in the same units as
// juce::Time::getMillisecondCounterHiRes()*0.001, i.e. seconds) is reached. Scheduling against
// an absolute deadline (rather than accumulating per-iteration sleeps) avoids drift across a
// long loop.
void waitUntilSeconds(double targetSeconds) {
    for (;;) {
        double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        double remaining = targetSeconds - now;
        if (remaining <= 0.0)
            return;
        if (remaining > 0.002)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        else
            std::this_thread::yield();
    }
}
} // namespace

// NOTE on test design: juce::MidiMessageCollector::removeNextBlockOfMessages() unconditionally
// drains and clears its ENTIRE queue on every call -- it does not hold back events whose
// timestamp lies beyond the current block. A message queued long before it's due gets flushed
// (position-clamped) on the very next drain, regardless of how far "in the future" its
// timestamp claims to be. This matches real usage: a juce::MidiInput callback delivers a
// message when it physically arrives, stamped at (approximately) that same moment -- it never
// hands you a message minutes ahead of time. These tests therefore call pushMidiMessage() only
// once wall-clock time actually reaches the message's timestamp, exactly mirroring what a real
// MIDI input thread does, so the collector's sample-accurate windowing has a chance to operate.

TEST_F(ExternalMidiModuleTest, RelativeTimingBetweenMessagesIsPreserved) {
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    const double blockDuration = blockSize / sampleRate;

    module->prepareToPlay(sampleRate, blockSize);

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double t1 = now + 0.10;
    const double t2 = now + 0.10 + blockDuration;

    auto msg1 = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    msg1.setTimeStamp(t1);
    auto msg2 = juce::MidiMessage::noteOn(1, 61, (juce::uint8)100);
    msg2.setTimeStamp(t2);

    bool pushed1 = false;
    bool pushed2 = false;
    std::vector<int> hitAbsoluteSamples;

    // 30 * 512 / 44100 ~= 0.35s, comfortably past t2.
    constexpr int numBlocksToRun = 30;
    for (int blockIndex = 0; blockIndex < numBlocksToRun; ++blockIndex) {
        waitUntilSeconds(now + blockIndex * blockDuration);

        double nowInLoop = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (!pushed1 && nowInLoop >= t1) {
            module->pushMidiMessage(msg1);
            pushed1 = true;
        }
        if (!pushed2 && nowInLoop >= t2) {
            module->pushMidiMessage(msg2);
            pushed2 = true;
        }

        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midiMessages;
        module->processBlock(buffer, midiMessages);

        for (const auto metadata : midiMessages) {
            if (metadata.getMessage().isNoteOn())
                hitAbsoluteSamples.push_back(blockIndex * blockSize + metadata.samplePosition);
        }
    }

    ASSERT_TRUE(pushed1);
    ASSERT_TRUE(pushed2);
    ASSERT_EQ(hitAbsoluteSamples.size(), 2u);
    // Absolute placement has wall-clock jitter from the reset() call, and even the RELATIVE
    // spacing drifts when the drive loop runs late under CI/parallel-build load (the collector's
    // window advances by exactly numSamples per call while wall time slips underneath it). The
    // regression this test guards is the old collapse-to-sample-0 behaviour, where the spacing
    // was 0 — so the tolerance only needs to separate ~512 from 0, not pin scheduler jitter.
    EXPECT_NEAR(hitAbsoluteSamples[1] - hitAbsoluteSamples[0], blockSize, 256);
}

TEST_F(ExternalMidiModuleTest, FutureTimestampedMessageArrivesInALaterBlock) {
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    const double blockDuration = blockSize / sampleRate;

    module->prepareToPlay(sampleRate, blockSize);

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double targetTime = now + 0.5;

    auto msg = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    msg.setTimeStamp(targetTime);

    bool pushed = false;
    int numArrivals = 0;
    int arrivalAbsoluteSample = -1;

    constexpr int numBlocksToRun = 60; // 60 * 512 / 44100 ~= 0.7s
    for (int blockIndex = 0; blockIndex < numBlocksToRun; ++blockIndex) {
        waitUntilSeconds(now + blockIndex * blockDuration);

        double nowInLoop = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (!pushed && nowInLoop >= targetTime) {
            module->pushMidiMessage(msg);
            pushed = true;
        }

        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midiMessages;
        module->processBlock(buffer, midiMessages);

        for (const auto metadata : midiMessages) {
            if (metadata.getMessage().isNoteOn()) {
                numArrivals++;
                arrivalAbsoluteSample = blockIndex * blockSize + metadata.samplePosition;
            }
        }
    }

    ASSERT_TRUE(pushed);
    EXPECT_EQ(numArrivals, 1);
    // 0.5s ahead == 22050 samples at 44.1kHz; +-100ms (+-4410 samples) tolerance for scheduler
    // jitter across the real-time-paced loop.
    EXPECT_NEAR(arrivalAbsoluteSample, 22050, 4410);
}

TEST_F(ExternalMidiModuleTest, BypassedModuleDiscardsQueuedMessages) {
    auto* bypassParam = (juce::AudioParameterBool*)module->getParameters()[0];
    ASSERT_NE(bypassParam, nullptr);
    *bypassParam = true;

    // Timestamp 0 lands at sample 0 regardless of real wall-clock time (see
    // UntimestampedMessagesLandAtSampleZero), so it's queued and available immediately.
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    module->pushMidiMessage(noteOn);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.setSample(0, 0, 1.0f);
    juce::MidiBuffer midiMessages;

    module->processBlock(buffer, midiMessages);

    // Bypass branch is a dry no-op that returns early without touching the graph-supplied
    // MIDI buffer, per the existing bypass contract -- it stays empty as passed in.
    EXPECT_EQ(midiMessages.getNumEvents(), 0);
    EXPECT_EQ(buffer.getSample(0, 0), 0.0f);

    // Un-bypass and process another block: the message queued while bypassed must have been
    // discarded (drained-and-dropped), not merely delayed -- it must NOT reappear here.
    *bypassParam = false;

    juce::AudioBuffer<float> buffer2(2, 512);
    juce::MidiBuffer midiMessages2;
    module->processBlock(buffer2, midiMessages2);

    EXPECT_EQ(midiMessages2.getNumEvents(), 0);
}
