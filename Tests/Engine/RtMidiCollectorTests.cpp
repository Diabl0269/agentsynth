// synth::RtMidiCollector is the lock-free replacement for juce::MidiMessageCollector in ExternalMidiModule.
// It must place messages where the JUCE collector did.

#include <gtest/gtest.h>
#include <vector>

#include "AudioEngine/RtMidiCollector.h"

namespace {

juce::MidiMessage stamped(juce::MidiMessage m, double seconds) {
    m.setTimeStamp(seconds);
    return m;
}

std::vector<juce::MidiMessage> positions(const juce::MidiBuffer& buffer, std::vector<int>* at = nullptr) {
    std::vector<juce::MidiMessage> out;
    for (const auto meta : buffer) {
        out.push_back(meta.getMessage());
        if (at != nullptr)
            at->push_back(meta.samplePosition);
    }
    return out;
}

} // namespace

TEST(RtMidiCollectorTest, EmptyQueueAddsNothing) {
    synth::RtMidiCollector collector;
    collector.reset(48000.0);
    juce::MidiBuffer out;
    collector.removeNextBlockOfMessages(out, 256);
    EXPECT_TRUE(out.isEmpty());
}

TEST(RtMidiCollectorTest, MessageArrivingJustNowLandsAtTheEndOfTheBlock) {
    synth::RtMidiCollector collector;
    collector.reset(48000.0);
    // Timestamp 0 with a clock that was reset moments ago gives a position of about -0 samples: the short
    // time since the previous call puts it right at the end of the block, as juce::MidiMessageCollector did.
    ASSERT_TRUE(collector.push(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)));
    juce::MidiBuffer out;
    collector.removeNextBlockOfMessages(out, 512);
    std::vector<int> at;
    const auto msgs = positions(out, &at);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_TRUE(msgs[0].isNoteOn());
    EXPECT_GE(at[0], 0);
    EXPECT_LT(at[0], 512);
}

TEST(RtMidiCollectorTest, ChannelFilterKeepsOnlyThatChannel) {
    synth::RtMidiCollector collector;
    collector.reset(44100.0);
    collector.push(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    collector.push(juce::MidiMessage::noteOn(2, 61, (juce::uint8)100));
    collector.push(juce::MidiMessage::noteOn(3, 62, (juce::uint8)100));
    collector.push(juce::MidiMessage::midiClock()); // a system message has no channel

    juce::MidiBuffer out;
    collector.removeNextBlockOfMessages(out, 512, 3);
    const auto msgs = positions(out);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].getChannel(), 3);
}

TEST(RtMidiCollectorTest, NoFilterKeepsEveryMessageInArrivalOrder) {
    synth::RtMidiCollector collector;
    collector.reset(44100.0);
    for (int note = 60; note < 64; ++note)
        collector.push(juce::MidiMessage::noteOn(1, note, (juce::uint8)100));
    juce::MidiBuffer out;
    collector.removeNextBlockOfMessages(out, 512);
    const auto msgs = positions(out);
    ASSERT_EQ(msgs.size(), 4u);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(msgs[static_cast<std::size_t>(i)].getNoteNumber(), 60 + i);
}

TEST(RtMidiCollectorTest, QueueIsEmptyAfterABlock) {
    synth::RtMidiCollector collector;
    collector.reset(44100.0);
    collector.push(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    juce::MidiBuffer first;
    collector.removeNextBlockOfMessages(first, 512);
    juce::MidiBuffer second;
    collector.removeNextBlockOfMessages(second, 512);
    EXPECT_EQ(first.getNumEvents(), 1);
    EXPECT_TRUE(second.isEmpty());
}

TEST(RtMidiCollectorTest, SysExIsRefusedAndShortMessagesAreCarried) {
    synth::RtMidiCollector collector;
    collector.reset(44100.0);
    const juce::uint8 sysex[] = {0x01, 0x02, 0x03, 0x04};
    EXPECT_FALSE(collector.push(juce::MidiMessage::createSysExMessage(sysex, 4)));
    EXPECT_TRUE(collector.push(juce::MidiMessage::controllerEvent(1, 7, 99)));
    EXPECT_TRUE(collector.push(juce::MidiMessage::programChange(1, 5)));
    EXPECT_TRUE(collector.push(juce::MidiMessage::midiClock()));
    juce::MidiBuffer out;
    collector.removeNextBlockOfMessages(out, 512);
    EXPECT_EQ(out.getNumEvents(), 3);
}

TEST(RtMidiCollectorTest, ResetForgetsQueuedMessages) {
    synth::RtMidiCollector collector;
    collector.reset(44100.0);
    collector.push(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    collector.reset(44100.0);
    juce::MidiBuffer out;
    collector.removeNextBlockOfMessages(out, 512);
    EXPECT_TRUE(out.isEmpty());
}

TEST(RtMidiCollectorTest, DiscardedBlockDropsTheQueue) {
    synth::RtMidiCollector collector;
    collector.reset(44100.0);
    collector.push(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    collector.discardNextBlock();
    juce::MidiBuffer out;
    collector.removeNextBlockOfMessages(out, 512);
    EXPECT_TRUE(out.isEmpty());
}

TEST(RtMidiCollectorTest, AFullQueueDropsTheNewestAndNeverBlocks) {
    synth::RtMidiCollector collector;
    collector.reset(44100.0);
    int accepted = 0;
    for (int i = 0; i < synth::RtMidiCollector::kCapacity + 50; ++i)
        accepted += collector.push(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)) ? 1 : 0;
    EXPECT_EQ(accepted, synth::RtMidiCollector::kCapacity);
}

TEST(RtMidiCollectorTest, LongGapSqueezesOlderMessagesIntoTheBlock) {
    // Two messages 250 ms apart, drained 600 ms after the reset into a 512-sample block: the span is far longer
    // than the block, so both (inside the 32-block history, 16384 samples) are scaled into it, the later one after.
    synth::RtMidiCollector collector;
    collector.reset(44100.0);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    collector.push(stamped(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), now + 0.30));
    collector.push(stamped(juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), now + 0.55));
    juce::Thread::sleep(600);
    juce::MidiBuffer out;
    collector.removeNextBlockOfMessages(out, 512);
    std::vector<int> at;
    const auto msgs = positions(out, &at);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[0].getNoteNumber(), 60);
    EXPECT_EQ(msgs[1].getNoteNumber(), 61);
    EXPECT_LT(at[0], at[1]);
    EXPECT_LT(at[1], 512);
}
