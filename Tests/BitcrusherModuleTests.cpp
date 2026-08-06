#include "../Source/Modules/FX/BitcrusherModule.h"
#include <gtest/gtest.h>

TEST(BitcrusherModuleTests, BasicProcessing) {
    BitcrusherModule module;
    module.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 512; ++i) {
            buffer.setSample(ch, i, 0.5f);
        }
    }

    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    // If depth is high and rate is 1, it should output exactly the input or very close
    EXPECT_NEAR(buffer.getSample(0, 0), 0.5f, 0.1f);
}

TEST(BitcrusherModuleTests, ParameterAccess) {
    BitcrusherModule module;
    auto params = module.getParameters();
    EXPECT_GT(params.size(), 0);
}
