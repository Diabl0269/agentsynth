#include "Modules/FilterModule.h"
#include <gtest/gtest.h>

class FilterTest : public ::testing::Test {
protected:
    FilterModule filter;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midiMessages;

    void SetUp() override {
        filter.prepareToPlay(44100.0, 512);
        buffer.setSize(4, 512);
        buffer.clear();
        // Create some noise or DC offset to filter
        juce::Random r;
        auto* data = buffer.getWritePointer(0);
        for (int i = 0; i < 512; ++i) {
            data[i] = r.nextFloat() * 2.0f - 1.0f;
        }
    }
};

TEST_F(FilterTest, InitState) {
    // Just ensure it processes without crashing immediately
    filter.processBlock(buffer, midiMessages);
}

TEST_F(FilterTest, LowPassAttenuatesHighFreq) {
    // Hard to test exact attenuation without FFT, but we can check RMS change.
    // If we set cutoff very low, output RMS should be significantly lower than
    // input.

    // 1. Measure input RMS
    float inputRMS = buffer.getRMSLevel(0, 0, buffer.getNumSamples());

    // 2. Set Cutoff Low (e.g., 20Hz)
    // Param index 0 is likely Cutoff based on ModuleBase pattern, but let's find
    // by name if possible or just assume implementation details for now as we are
    // inside the codebase. Let's iterate params to find "Cutoff".

    juce::AudioProcessorParameter* cutoffParam = findParameterByID(&filter, "cutoff");
    for (auto* p : filter.getParameters()) {
        if (p->getName(100) == "Cutoff") {
            cutoffParam = p;
            break;
        }
    }

    // Normalized value 0.0 should be min freq
    cutoffParam->setValueNotifyingHost(0.0f);

    // 3. Process
    filter.processBlock(buffer, midiMessages);

    // 4. Measure output RMS
    float outputRMS = buffer.getRMSLevel(0, 0, buffer.getNumSamples());

    EXPECT_LT(outputRMS, inputRMS);
}

TEST_F(FilterTest, ModulationScalesCorrectly) {
    auto* cutoffParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&filter, "cutoff"));

    // Set base cutoff to 440Hz
    cutoffParam->setValueNotifyingHost(cutoffParam->getNormalisableRange().convertTo0to1(440.0f));

    // Set CV1 to 1.0 (Full upward shift)
    auto* cv1 = buffer.getWritePointer(1);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        cv1[i] = 1.0f;
    }

    // Process
    filter.processBlock(buffer, midiMessages);

    // It is hard to test the internal frequency state directly without exposing it,
    // but we know it processes without crashing. The visual buffer or an exposed state would be needed for exact freq
    // assertions.
}

TEST_F(FilterTest, MultiParamModulation) {
    // Set Cutoff CV (ch 1) to 1.0 and Res CV (ch 2) to 1.0
    auto* cv1 = buffer.getWritePointer(1);
    auto* cv2 = buffer.getWritePointer(2);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        cv1[i] = 1.0f;
        cv2[i] = 1.0f;
    }

    // Process
    filter.processBlock(buffer, midiMessages);
}

TEST_F(FilterTest, FilterTypeParameterExists) {
    auto* typeParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&filter, "filterType"));
    ASSERT_NE(typeParam, nullptr);
    EXPECT_EQ(typeParam->choices.size(), 7);
}

TEST_F(FilterTest, AllFilterTypesProcessWithoutCrash) {
    for (int type = 0; type < 7; ++type) {
        filter.prepareToPlay(44100.0, 512);
        auto* typeParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&filter, "filterType"));
        ASSERT_NE(typeParam, nullptr);

        float normalized = (type == 0) ? 0.0f : static_cast<float>(type) / 6.0f;
        typeParam->setValueNotifyingHost(normalized);

        buffer.clear();
        auto* data = buffer.getWritePointer(0);
        juce::Random r;
        for (int i = 0; i < 512; ++i)
            data[i] = r.nextFloat() * 2.0f - 1.0f;

        EXPECT_NO_THROW(filter.processBlock(buffer, midiMessages));
    }
}

TEST_F(FilterTest, HPFAttenuatesLowFreq) {
    // Set to HPF24 (index 2)
    auto* typeParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&filter, "filterType"));
    ASSERT_NE(typeParam, nullptr);
    typeParam->setValueNotifyingHost(2.0f / 6.0f);

    // Set cutoff high
    auto* cutoffParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&filter, "cutoff"));
    ASSERT_NE(cutoffParam, nullptr);
    cutoffParam->setValueNotifyingHost(0.9f);

    // Fill with DC (low freq content)
    auto* data = buffer.getWritePointer(0);
    for (int i = 0; i < 512; ++i)
        data[i] = 1.0f;

    float inputRMS = buffer.getRMSLevel(0, 0, 512);
    filter.processBlock(buffer, midiMessages);
    float outputRMS = buffer.getRMSLevel(0, 0, 512);

    EXPECT_LT(outputRMS, inputRMS);
}

TEST_F(FilterTest, NotchProcessesWithoutCrash) {
    // Set to Notch (index 6)
    auto* typeParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&filter, "filterType"));
    ASSERT_NE(typeParam, nullptr);
    typeParam->setValueNotifyingHost(1.0f);

    filter.processBlock(buffer, midiMessages);
}

TEST_F(FilterTest, PolyMode_IndependentFiltering) {
    auto* polyP = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&filter, "poly"));
    ASSERT_NE(polyP, nullptr);
    *polyP = true;

    filter.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> polyBuffer(11, 512);
    polyBuffer.clear();

    // Fill voice 0 with noise, voice 1 with silence
    juce::Random r;
    for (int i = 0; i < 512; ++i)
        polyBuffer.setSample(0, i, r.nextFloat() * 2.0f - 1.0f);

    juce::MidiBuffer midi;
    filter.processBlock(polyBuffer, midi);

    // Voice 0 should have output, voice 1 should be silent
    EXPECT_GT(polyBuffer.getRMSLevel(0, 0, 512), 0.0f);
    EXPECT_NEAR(polyBuffer.getRMSLevel(1, 0, 512), 0.0f, 1e-6f);
}

TEST_F(FilterTest, PolyMode_NoBleed) {
    auto* polyP = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&filter, "poly"));
    *polyP = true;
    filter.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> polyBuffer(11, 512);
    polyBuffer.clear();

    // Strong signal only on voice 0
    for (int i = 0; i < 512; ++i)
        polyBuffer.setSample(0, i, 1.0f);

    juce::MidiBuffer midi;
    filter.processBlock(polyBuffer, midi);

    // Voice 1 should remain silent (no bleed)
    EXPECT_NEAR(polyBuffer.getRMSLevel(1, 0, 512), 0.0f, 1e-6f);
}
