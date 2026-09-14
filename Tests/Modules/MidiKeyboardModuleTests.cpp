#include "Modules/MidiKeyboardModule.h"
#include <gtest/gtest.h>

class MidiKeyboardModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<MidiKeyboardModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<MidiKeyboardModule> module;
};

TEST_F(MidiKeyboardModuleTest, NameIsCorrect) { EXPECT_EQ(module->getName(), "MIDI Keyboard"); }

TEST_F(MidiKeyboardModuleTest, KeyboardStateInitiallyEmpty) {
    juce::MidiBuffer buffer;
    juce::AudioBuffer<float> audioBuffer(2, 512);
    module->processBlock(audioBuffer, buffer);
    EXPECT_TRUE(buffer.isEmpty());
}

TEST_F(MidiKeyboardModuleTest, ProducesMidiWhenNoteOn) {
    auto& state = module->getKeyboardState();
    state.noteOn(1, 60, 0.8f);

    juce::MidiBuffer buffer;
    juce::AudioBuffer<float> audioBuffer(2, 512);
    module->processBlock(audioBuffer, buffer);

    EXPECT_FALSE(buffer.isEmpty());

    bool foundNoteOn = false;
    for (const auto metadata : buffer) {
        if (metadata.getMessage().isNoteOn()) {
            foundNoteOn = true;
            EXPECT_EQ(metadata.getMessage().getNoteNumber(), 60);
        }
    }
    EXPECT_TRUE(foundNoteOn);
}

TEST_F(MidiKeyboardModuleTest, OctaveShiftWorks) {
    auto& state = module->getKeyboardState();
    state.noteOn(1, 60, 0.8f);

    // Set octave to +1
    auto* octaveParam = dynamic_cast<juce::AudioParameterInt*>(module->getParameters()[1]);
    ASSERT_NE(octaveParam, nullptr);
    *octaveParam = 1;

    juce::MidiBuffer buffer;
    juce::AudioBuffer<float> audioBuffer(2, 512);
    module->processBlock(audioBuffer, buffer);

    bool foundShiftedNote = false;
    for (const auto metadata : buffer) {
        if (metadata.getMessage().isNoteOn()) {
            foundShiftedNote = true;
            EXPECT_EQ(metadata.getMessage().getNoteNumber(), 72); // 60 + 12
        }
    }
    EXPECT_TRUE(foundShiftedNote);
}
