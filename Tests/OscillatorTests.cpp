#include "Modules/OscillatorModule.h"
#include <gtest/gtest.h>

class OscillatorTest : public ::testing::Test {
protected:
    OscillatorModule oscillator;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midiMessages;

    void SetUp() override {
        oscillator.prepareToPlay(44100.0, 512);
        buffer.setSize(2, 512);
        buffer.clear();
    }
};

TEST_F(OscillatorTest, ProducesSilenceWhenNoChannels) {
    juce::AudioBuffer<float> emptyBuffer;
    oscillator.processBlock(emptyBuffer, midiMessages);
    // Should not crash
}

TEST_F(OscillatorTest, GeneratesSignal) {
    // Oscillator is running by default (A4 = 440Hz)
    oscillator.processBlock(buffer, midiMessages);

    float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    EXPECT_GT(rms, 0.0f);
}

TEST_F(OscillatorTest, RespondsToMidiNote) {
    // Note On 69 (A4 = 440Hz)
    auto noteOn = juce::MidiMessage::noteOn(1, 69, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);
    oscillator.processBlock(buffer, midiMessages);

    // We can't easily check frequency without FFT, but we can verify it doesn't crash
    // and produces signal.
    EXPECT_GT(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.0f);

    // Note On 60 (C4 ~ 261.63Hz)
    midiMessages.clear();
    auto noteOn2 = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    midiMessages.addEvent(noteOn2, 0);
    oscillator.processBlock(buffer, midiMessages);
    EXPECT_GT(buffer.getRMSLevel(0, 0, buffer.getNumSamples()), 0.0f);
}

TEST_F(OscillatorTest, TuningParametersExist) {
    const auto& params = oscillator.getParameters();
    bool foundOctave = false, foundCoarse = false, foundFine = false;

    for (auto* p : params) {
        juce::String name = p->getName(100);
        if (name == "Octave")
            foundOctave = true;
        if (name == "Coarse")
            foundCoarse = true;
        if (name == "Fine")
            foundFine = true;
    }

    EXPECT_TRUE(foundOctave);
    EXPECT_TRUE(foundCoarse);
    EXPECT_TRUE(foundFine);
}

TEST_F(OscillatorTest, VerifiesTuningLogic) {
    // Default: Midi 69, Oct 0, Coarse 0, Fine 0 -> 440Hz
    EXPECT_NEAR(oscillator.getTargetFrequency(), 440.0f, 0.01f);

    // Octave Shift
    const auto& params = oscillator.getParameters();
    juce::RangedAudioParameter *octParam = nullptr, *coarseParam = nullptr, *fineParam = nullptr;
    for (auto* p : params) {
        if (p->getName(100) == "Octave")
            octParam = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (p->getName(100) == "Coarse")
            coarseParam = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (p->getName(100) == "Fine")
            fineParam = dynamic_cast<juce::RangedAudioParameter*>(p);
    }
    ASSERT_NE(octParam, nullptr);
    ASSERT_NE(coarseParam, nullptr);
    ASSERT_NE(fineParam, nullptr);

    // +1 Octave -> 880Hz
    octParam->setValueNotifyingHost(octParam->getNormalisableRange().convertTo0to1(1.0f));
    EXPECT_NEAR(oscillator.getTargetFrequency(), 880.0f, 0.01f);

    // -1 Octave -> 220Hz (relative to original 440)
    octParam->setValueNotifyingHost(octParam->getNormalisableRange().convertTo0to1(-1.0f));
    EXPECT_NEAR(oscillator.getTargetFrequency(), 220.0f, 0.01f);

    // Reset Octave
    octParam->setValueNotifyingHost(octParam->getNormalisableRange().convertTo0to1(0.0f));

    // Coarse Tune +7 semitones (Perfect 5th) -> 440 * 2^(7/12) ~ 659.25Hz
    coarseParam->setValueNotifyingHost(coarseParam->getNormalisableRange().convertTo0to1(7.0f));
    EXPECT_NEAR(oscillator.getTargetFrequency(), 659.25f, 0.05f);

    // Fine Tune +100 cents (1 semitone) -> 440 * 2^(8/12) ~ 698.46Hz
    fineParam->setValueNotifyingHost(fineParam->getNormalisableRange().convertTo0to1(100.0f));
    EXPECT_NEAR(oscillator.getTargetFrequency(), 698.46f, 0.05f);

    // MIDI Note Change (Note 60 = C4 ~ 261.63Hz, with coarse 7 and fine 100 -> Note 68 ~ 415.30Hz)
    midiMessages.clear();
    midiMessages.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    oscillator.processBlock(buffer, midiMessages);
    EXPECT_NEAR(oscillator.getTargetFrequency(), 415.30f, 0.05f);
}

TEST_F(OscillatorTest, ChangesWaveform) {
    const auto& params = oscillator.getParameters();
    juce::AudioProcessorParameter* waveParam = nullptr;
    for (auto* p : params) {
        if (p->getName(100) == "Waveform") {
            waveParam = p;
            break;
        }
    }
    ASSERT_NE(waveParam, nullptr);

    float values[] = {0.0f, 0.33f, 0.66f, 1.0f}; // Map to index 0, 1, 2, 3

    for (float v : values) {
        waveParam->setValueNotifyingHost(v);
        buffer.clear();
        oscillator.processBlock(buffer, midiMessages);
        float rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
        EXPECT_GT(rms, 0.0f) << "Failed for waveform value " << v;
    }
}

TEST_F(OscillatorTest, ModulatesWaveform) {
    // Provide CV input on channel 1 (Waveform Mod)
    juce::AudioBuffer<float> cvBuffer(5, 512);
    cvBuffer.clear();
    auto* cvWrite = cvBuffer.getWritePointer(1);
    for (int i = 0; i < 512; ++i) {
        cvWrite[i] = 1.0f; // Max modulation -> shifts waveform by 3
    }

    oscillator.processBlock(cvBuffer, midiMessages);
    float rms = cvBuffer.getRMSLevel(0, 0, cvBuffer.getNumSamples());
    EXPECT_GT(rms, 0.0f);
}

TEST_F(OscillatorTest, ModulatesTuning) {
    // Provide CV input on channels 2, 3, 4
    juce::AudioBuffer<float> cvBuffer(5, 512);
    cvBuffer.clear();
    auto* octWrite = cvBuffer.getWritePointer(2);
    auto* coarseWrite = cvBuffer.getWritePointer(3);
    auto* fineWrite = cvBuffer.getWritePointer(4);

    for (int i = 0; i < 512; ++i) {
        octWrite[i] = 0.5f;    // +2 octaves
        coarseWrite[i] = 0.5f; // +6 semitones
        fineWrite[i] = 0.5f;   // +50 cents
    }

    oscillator.processBlock(cvBuffer, midiMessages);
    float rms = cvBuffer.getRMSLevel(0, 0, cvBuffer.getNumSamples());
    EXPECT_GT(rms, 0.0f);
}

TEST_F(OscillatorTest, PolyMode_IndependentVoices) {
    // Enable poly mode
    auto* polyP = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&oscillator, "poly"));
    ASSERT_NE(polyP, nullptr);
    *polyP = true;

    // Create poly buffer: 14 inputs, but we need at least 14 channels for the graph
    juce::AudioBuffer<float> polyBuffer(14, 512);
    polyBuffer.clear();

    // Set different pitch CVs: voice 0 = 440Hz, voice 1 = 880Hz
    for (int i = 0; i < 512; ++i) {
        polyBuffer.setSample(0, i, 440.0f); // Voice 0: A4
        polyBuffer.setSample(1, i, 880.0f); // Voice 1: A5
    }

    oscillator.processBlock(polyBuffer, midiMessages);

    // Both voices should produce non-zero output
    EXPECT_GT(polyBuffer.getRMSLevel(0, 0, 512), 0.0f);
    EXPECT_GT(polyBuffer.getRMSLevel(1, 0, 512), 0.0f);
}

TEST_F(OscillatorTest, MonoMode_BackwardsCompatible) {
    // Default: poly should be off
    auto noteOn = juce::MidiMessage::noteOn(1, 69, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);
    oscillator.processBlock(buffer, midiMessages);
    EXPECT_GT(buffer.getRMSLevel(0, 0, 512), 0.0f);
}

TEST_F(OscillatorTest, Unison_MonoMode) {
    // Find unison and detune params
    auto* unisonP = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(&oscillator, "unison"));
    auto* detuneP = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&oscillator, "detune"));
    ASSERT_NE(unisonP, nullptr);
    ASSERT_NE(detuneP, nullptr);

    *unisonP = 4;
    detuneP->setValueNotifyingHost(detuneP->getNormalisableRange().convertTo0to1(20.0f));

    auto noteOn = juce::MidiMessage::noteOn(1, 69, (juce::uint8)100);
    midiMessages.addEvent(noteOn, 0);
    oscillator.processBlock(buffer, midiMessages);

    EXPECT_GT(buffer.getRMSLevel(0, 0, 512), 0.0f);
}
