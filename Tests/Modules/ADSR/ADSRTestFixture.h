// Shared fixture and small helpers for the ADSR test split (ADSRStageTests.cpp,
// ADSRGateTests.cpp, ADSRPolyTests.cpp, ADSRThresholdTests.cpp).

#pragma once

#include "Modules/ADSRModule.h"
#include "Modules/ModuleBase.h"
#include <gtest/gtest.h>
#include <iostream>

inline juce::AudioParameterFloat* floatParam(ADSRModule& m, const juce::String& id) {
    return dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&m, id));
}

inline juce::AudioParameterBool* boolParam(ADSRModule& m, const juce::String& id) {
    return dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&m, id));
}

inline void setFloat(ADSRModule& m, const juce::String& id, float actual) {
    auto* p = floatParam(m, id);
    ASSERT_NE(p, nullptr);
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

inline void setPoly(ADSRModule& m, bool on) {
    auto* p = boolParam(m, "poly");
    ASSERT_NE(p, nullptr);
    *p = on;
}

class ADSRTest : public ::testing::Test {
protected:
    ADSRModule adsr;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midiMessages;

    void SetUp() override {
        adsr.prepareToPlay(44100.0, 512);
        buffer.setSize(2, 512);
        buffer.clear();
    }
};
