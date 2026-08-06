#include "../Source/Modules/FX/ChorusModule.h"
#include "../Source/Modules/FX/CompressorModule.h"
#include "../Source/Modules/FX/DelayModule.h"
#include "../Source/Modules/FX/FlangerModule.h"
#include "../Source/Modules/FX/LimiterModule.h"
#include "../Source/Modules/FX/ParametricEQModule.h"
#include "../Source/Modules/FX/PhaserModule.h"
#include "../Source/Modules/FX/ReverbModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

class ModuleBypassTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear any test state before each test
    }

    void TearDown() override {
        // Clean up after each test
    }
};

/**
 * Test 1: DefaultNotBypassed
 * - Create a FilterModule, verify isBypassed() returns false by default
 */
TEST_F(ModuleBypassTest, DefaultNotBypassed) {
    FilterModule filter;
    EXPECT_FALSE(filter.isBypassed());
}

/**
 * Test 2: SetBypassedWorks
 * - Create a FilterModule, call setBypassed(true), verify isBypassed() returns true
 * - Call setBypassed(false), verify isBypassed() returns false
 */
TEST_F(ModuleBypassTest, SetBypassedWorks) {
    FilterModule filter;

    // Set to true
    filter.setBypassed(true);
    EXPECT_TRUE(filter.isBypassed());

    // Set to false
    filter.setBypassed(false);
    EXPECT_FALSE(filter.isBypassed());

    // Set to true again to ensure toggling works
    filter.setBypassed(true);
    EXPECT_TRUE(filter.isBypassed());
}

/**
 * Test 3: BypassedEffectPassesAudioThrough
 * - Create a FilterModule
 * - Call prepareToPlay(44100.0, 512)
 * - Create an AudioBuffer with the right channel count and fill channel 0 with known values (0.5f)
 * - Process once without bypass to get the filtered output (different from input)
 * - Create a fresh buffer with the same 0.5f values
 * - Set bypass to true
 * - Process — verify channel 0 output equals the input (0.5f for every sample)
 */
TEST_F(ModuleBypassTest, BypassedEffectPassesAudioThrough) {
    FilterModule filter;
    const double sampleRate = 44100.0;
    const int blockSize = 512;

    filter.prepareToPlay(sampleRate, blockSize);

    // Create a buffer with the right number of input channels
    int numChannels = filter.getTotalNumInputChannels();
    EXPECT_EQ(numChannels, 11);

    juce::AudioBuffer<float> buffer(numChannels, blockSize);
    juce::MidiBuffer midiBuffer;

    // Fill channel 0 with 0.5f values to verify passthrough
    const float testValue = 0.5f;
    for (int i = 0; i < blockSize; ++i) {
        buffer.setSample(0, i, testValue);
    }

    // Process without bypass to get the filtered output
    filter.setBypassed(false);
    filter.processBlock(buffer, midiBuffer);

    // Get the filtered output
    juce::AudioBuffer<float> filteredBuffer(numChannels, blockSize);
    juce::FloatVectorOperations::copy(filteredBuffer.getWritePointer(0), buffer.getReadPointer(0), blockSize);

    // Verify that the filter actually changed the signal (test is meaningful)
    float maxDifference = 0.0f;
    for (int i = 0; i < blockSize; ++i) {
        float diff = std::abs(filteredBuffer.getSample(0, i) - testValue);
        if (diff > maxDifference) {
            maxDifference = diff;
        }
    }
    // The filter should have modified the signal to some degree
    EXPECT_GT(maxDifference, 0.001f) << "Filter did not actually process the signal";

    // Now create a fresh buffer with the same test values
    juce::AudioBuffer<float> bypassBuffer(numChannels, blockSize);
    for (int i = 0; i < blockSize; ++i) {
        bypassBuffer.setSample(0, i, testValue);
    }

    // Set bypass to true and process
    filter.setBypassed(true);
    filter.processBlock(bypassBuffer, midiBuffer);

    // Verify that the output equals the input (passthrough)
    for (int i = 0; i < blockSize; ++i) {
        EXPECT_FLOAT_EQ(bypassBuffer.getSample(0, i), testValue)
            << "Bypassed filter did not pass audio through at sample " << i;
    }
}

/**
 * Test 4: BypassedSourceOutputsSilence
 * - Create an OscillatorModule
 * - Call prepareToPlay(44100.0, 512)
 * - Create an AudioBuffer with appropriate channels, put some non-zero data in it
 * - Set bypass to true
 * - Process with a MidiBuffer
 * - Verify all samples in the buffer are 0.0f
 */
TEST_F(ModuleBypassTest, BypassedSourceOutputsSilence) {
    OscillatorModule oscillator;
    const double sampleRate = 44100.0;
    const int blockSize = 512;

    oscillator.prepareToPlay(sampleRate, blockSize);

    // Create a buffer with the right number of output channels.
    // OscillatorModule declares 14 output channels (8 audio + 6 silent pass-throughs)
    // so that JUCE correctly copies shared CV input buffers that fan out to multiple nodes.
    int numChannels = oscillator.getTotalNumOutputChannels();
    EXPECT_EQ(numChannels, 14);

    juce::AudioBuffer<float> buffer(numChannels, blockSize);
    juce::MidiBuffer midiBuffer;

    // Pre-fill the buffer with non-zero values to verify they get cleared
    for (int i = 0; i < blockSize; ++i) {
        buffer.setSample(0, i, 0.5f);
    }

    // Set bypass to true and process
    oscillator.setBypassed(true);
    oscillator.processBlock(buffer, midiBuffer);

    // Verify all samples are zero
    for (int i = 0; i < blockSize; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.0f) << "Bypassed oscillator did not clear buffer at sample " << i;
    }
}

/**
 * Test 5: BypassStateSerializedInState
 * - Create a FilterModule
 * - Set bypass to true
 * - Call getStateInformation() to get the state as MemoryBlock
 * - Create a NEW FilterModule
 * - Call setStateInformation() with the saved state
 * - Verify the new module's isBypassed() returns true
 */
TEST_F(ModuleBypassTest, BypassStateSerializedInState) {
    // Create first filter and set it to bypassed
    FilterModule filter1;
    filter1.setBypassed(true);
    EXPECT_TRUE(filter1.isBypassed());

    // Serialize the state
    juce::MemoryBlock stateData;
    filter1.getStateInformation(stateData);

    // Create a new filter and deserialize the state
    FilterModule filter2;
    EXPECT_FALSE(filter2.isBypassed()) << "New filter should start as not bypassed";

    filter2.setStateInformation(stateData.getData(), (int)stateData.getSize());

    // Verify the bypass state was restored
    EXPECT_TRUE(filter2.isBypassed()) << "Filter2 should have bypass state restored from serialized state";
}

// ---------------------------------------------------------------------------
// FX Module bypass dry-pass-through tests
// ---------------------------------------------------------------------------
// These tests verify that bypassed FX modules leave AUDIO channels intact
// (dry signal passes through) and only mute silences them.
// Modules with CV inputs (Chorus/Phaser/Flanger): 4 channels, audio on 0+1, CV on 2+3.
// Parametric EQ: 6 channels, audio on 0+1, CV on 2-5.
// Pure stereo modules (Reverb/Delay/Compressor/Limiter): 2 channels, all audio.
// ---------------------------------------------------------------------------

// --- Chorus ---

TEST(FXBypassTest, ChorusBypassPassesDryAudio) {
    ChorusModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 0.7f);
        buffer.setSample(1, i, 0.7f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    // Audio channels must be untouched
    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.7f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), 0.7f) << "Ch1 sample " << i;
    }
}

TEST(FXBypassTest, ChorusBypassClearsCVChannels) {
    ChorusModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(2, i, 0.5f);
        buffer.setSample(3, i, 0.5f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    // CV channels must be cleared to prevent leaking downstream
    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(2, i), 0.0f) << "CV ch2 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(3, i), 0.0f) << "CV ch3 sample " << i;
    }
}

TEST(FXBypassTest, ChorusMuteSilencesOutput) {
    ChorusModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 4; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.7f);

    module.setMuted(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 0; ch < 4; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "Ch" << ch << " sample " << i;
}

// --- Reverb ---

TEST(FXBypassTest, ReverbBypassPassesDryAudio) {
    ReverbModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 0.6f);
        buffer.setSample(1, i, 0.6f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.6f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), 0.6f) << "Ch1 sample " << i;
    }
}

TEST(FXBypassTest, ReverbMuteSilencesOutput) {
    ReverbModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.6f);

    module.setMuted(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "Ch" << ch << " sample " << i;
}

// --- Delay ---

TEST(FXBypassTest, DelayBypassPassesDryAudio) {
    DelayModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 0.5f);
        buffer.setSample(1, i, 0.5f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.5f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), 0.5f) << "Ch1 sample " << i;
    }
}

TEST(FXBypassTest, DelayMuteSilencesOutput) {
    DelayModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    module.setMuted(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "Ch" << ch << " sample " << i;
}

// --- Phaser ---

TEST(FXBypassTest, PhaserBypassPassesDryAudio) {
    PhaserModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 0.4f);
        buffer.setSample(1, i, 0.4f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.4f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), 0.4f) << "Ch1 sample " << i;
    }
}

TEST(FXBypassTest, PhaserBypassClearsCVChannels) {
    PhaserModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(2, i, 0.3f);
        buffer.setSample(3, i, 0.3f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(2, i), 0.0f) << "CV ch2 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(3, i), 0.0f) << "CV ch3 sample " << i;
    }
}

TEST(FXBypassTest, PhaserMuteSilencesOutput) {
    PhaserModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 4; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.4f);

    module.setMuted(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 0; ch < 4; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "Ch" << ch << " sample " << i;
}

// --- Flanger ---

TEST(FXBypassTest, FlangerBypassPassesDryAudio) {
    FlangerModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 0.8f);
        buffer.setSample(1, i, 0.8f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.8f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), 0.8f) << "Ch1 sample " << i;
    }
}

TEST(FXBypassTest, FlangerBypassClearsCVChannels) {
    FlangerModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(2, i, 0.5f);
        buffer.setSample(3, i, 0.5f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(2, i), 0.0f) << "CV ch2 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(3, i), 0.0f) << "CV ch3 sample " << i;
    }
}

TEST(FXBypassTest, FlangerMuteSilencesOutput) {
    FlangerModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(4, 512);
    buffer.clear();
    for (int ch = 0; ch < 4; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.8f);

    module.setMuted(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 0; ch < 4; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "Ch" << ch << " sample " << i;
}

// --- Compressor ---

TEST(FXBypassTest, CompressorBypassPassesDryAudio) {
    CompressorModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 0.3f);
        buffer.setSample(1, i, 0.3f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.3f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), 0.3f) << "Ch1 sample " << i;
    }
}

TEST(FXBypassTest, CompressorMuteSilencesOutput) {
    CompressorModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    module.setMuted(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "Ch" << ch << " sample " << i;
}

// --- Limiter ---

TEST(FXBypassTest, LimiterBypassPassesDryAudio) {
    LimiterModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 0.9f);
        buffer.setSample(1, i, 0.9f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.9f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), 0.9f) << "Ch1 sample " << i;
    }
}

TEST(FXBypassTest, LimiterMuteSilencesOutput) {
    LimiterModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.9f);

    module.setMuted(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "Ch" << ch << " sample " << i;
}

// --- Parametric EQ --- (6 channels: audio on 0+1, CV on 2-5)

TEST(FXBypassTest, ParametricEQBypassPassesDryAudio) {
    ParametricEQModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(6, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 0.7f);
        buffer.setSample(1, i, 0.7f);
    }

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.7f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), 0.7f) << "Ch1 sample " << i;
    }
}

TEST(FXBypassTest, ParametricEQBypassClearsCVChannels) {
    ParametricEQModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(6, 512);
    buffer.clear();
    for (int ch = 2; ch < 6; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    module.setBypassed(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 2; ch < 6; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "CV ch" << ch << " sample " << i;
}

TEST(FXBypassTest, ParametricEQMuteSilencesOutput) {
    ParametricEQModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(6, 512);
    for (int ch = 0; ch < 6; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.7f);

    module.setMuted(true);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 0; ch < 6; ++ch)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "Ch" << ch << " sample " << i;
}
