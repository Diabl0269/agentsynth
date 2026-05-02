#include "../Source/Modules/ExternalMidiModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

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
