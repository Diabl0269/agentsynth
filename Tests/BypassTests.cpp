#include "Modules/AttenuverterModule.h"
#include <gtest/gtest.h>

TEST(AttenuverterBypassTest, BypassClearsBuffer) {
    auto module = std::make_unique<AttenuverterModule>();
    module->prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(1, 128);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 1.0f); // Set to 1.0
    }
    juce::MidiBuffer midi;

    // Enable bypass
    module->setBypassed(true);

    module->processBlock(buffer, midi);

    // Verify buffer is cleared
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        EXPECT_EQ(buffer.getSample(0, i), 0.0f) << "Sample " << i << " was not cleared by bypass";
    }
}
