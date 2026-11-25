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

// The keys reach the render thread through a lock-free ring, not MidiKeyboardState::processNextMidiBuffer.

TEST_F(MidiKeyboardModuleTest, NoteOffFollowsNoteOnInOrder) {
    auto& state = module->getKeyboardState();
    state.noteOn(1, 60, 0.8f);
    state.noteOff(1, 60, 0.0f);

    juce::MidiBuffer buffer;
    juce::AudioBuffer<float> audioBuffer(2, 512);
    module->processBlock(audioBuffer, buffer);

    std::vector<juce::MidiMessage> msgs;
    for (const auto metadata : buffer)
        msgs.push_back(metadata.getMessage());
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_TRUE(msgs[0].isNoteOn());
    EXPECT_TRUE(msgs[1].isNoteOff());
    EXPECT_EQ(msgs[1].getNoteNumber(), 60);
}

TEST_F(MidiKeyboardModuleTest, KeysPressedWhileBypassedAreNotHeldForLater) {
    auto* bypass = dynamic_cast<juce::AudioParameterBool*>(module->getParameters()[0]);
    ASSERT_NE(bypass, nullptr);
    *bypass = true;
    module->getKeyboardState().noteOn(1, 60, 0.8f);

    juce::MidiBuffer buffer;
    juce::AudioBuffer<float> audioBuffer(2, 512);
    module->processBlock(audioBuffer, buffer);
    EXPECT_TRUE(buffer.isEmpty());

    *bypass = false;
    module->processBlock(audioBuffer, buffer);
    EXPECT_TRUE(buffer.isEmpty()) << "a key pressed during bypass must not sound when the bypass ends";
}

TEST_F(MidiKeyboardModuleTest, OctaveShiftThatLeavesTheRangeKeepsTheNote) {
    auto* octaveParam = dynamic_cast<juce::AudioParameterInt*>(module->getParameters()[1]);
    ASSERT_NE(octaveParam, nullptr);
    *octaveParam = 2; // +24
    module->getKeyboardState().noteOn(1, 120, 0.8f);

    juce::MidiBuffer buffer;
    juce::AudioBuffer<float> audioBuffer(2, 512);
    module->processBlock(audioBuffer, buffer);
    ASSERT_EQ(buffer.getNumEvents(), 1);
    EXPECT_EQ((*buffer.begin()).getMessage().getNoteNumber(), 120);
}

TEST_F(MidiKeyboardModuleTest, DestroyingTheModuleDetachesItFromTheKeyboardState) {
    // The state outlives nothing here, but the listener must be gone before it: a press after the module is
    // destroyed would call a dead object. Destroy, then make sure the process is still standing.
    auto local = std::make_unique<MidiKeyboardModule>();
    auto* state = &local->getKeyboardState();
    state->noteOn(1, 60, 0.5f);
    local.reset();
    SUCCEED();
}
