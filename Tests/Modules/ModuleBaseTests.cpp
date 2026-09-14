#include "Modules/ModuleBase.h"
#include <gtest/gtest.h>

// Concrete implementation for testing
class TestModule : public ModuleBase {
public:
    TestModule()
        : ModuleBase("TestModule", 1, 1) {
        addParameter(new juce::AudioParameterFloat("gain", "Gain", 0.0f, 1.0f, 0.5f));
    }

    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Oscillator; }
};

class ModuleBaseTest : public ::testing::Test {
protected:
    TestModule module;
};

TEST_F(ModuleBaseTest, BoilerplateGetters) {
    EXPECT_EQ(module.getName(), "TestModule");
    EXPECT_TRUE(module.acceptsMidi());
    EXPECT_TRUE(module.producesMidi());
    EXPECT_EQ(module.getTailLengthSeconds(), 0.0);
    EXPECT_EQ(module.getNumPrograms(), 1);
    EXPECT_EQ(module.getCurrentProgram(), 0);
}

TEST_F(ModuleBaseTest, VisualBufferManagement) {
    // Enabled by default? No, base doesn't enable it, sub classes do.
    // Let's check initial state.
    EXPECT_EQ(module.getVisualBuffer(), nullptr);

    module.enableVisualBuffer(true);
    EXPECT_NE(module.getVisualBuffer(), nullptr);

    module.enableVisualBuffer(false);
    EXPECT_EQ(module.getVisualBuffer(), nullptr);
}

TEST_F(ModuleBaseTest, StateSerialization) {
    // 1. Modify parameter
    auto* param = module.getParameters()[1]; // Gain
    param->setValueNotifyingHost(0.8f);
    EXPECT_FLOAT_EQ(param->getValue(), 0.8f);

    // 2. Save State
    juce::MemoryBlock savedData;
    module.getStateInformation(savedData);

    // 3. Reset Parameter
    param->setValueNotifyingHost(0.0f);
    EXPECT_FLOAT_EQ(param->getValue(), 0.0f);

    // 4. Load State
    module.setStateInformation(savedData.getData(), (int)savedData.getSize());

    // 5. Verify Restore
    EXPECT_FLOAT_EQ(param->getValue(), 0.8f);

    // 6. Test Invalid Data
    // Empty data
    module.setStateInformation(nullptr, 0);
    // Should not crash, param should stay same
    EXPECT_FLOAT_EQ(param->getValue(), 0.8f);

    // Garbage data
    const char* garbage = "Not XML";
    module.setStateInformation(garbage, (int)strlen(garbage));
    // Should not crash
    EXPECT_FLOAT_EQ(param->getValue(), 0.8f);

    // Valid XML but wrong tag
    juce::ValueTree wrongTag("WrongTag");
    juce::MemoryBlock wrongData;
    juce::AudioProcessor::copyXmlToBinary(*wrongTag.createXml(), wrongData);
    module.setStateInformation(wrongData.getData(), (int)wrongData.getSize());
    // Should not crash
}

TEST_F(ModuleBaseTest, ProgramMethods) {
    // These are boilerplate but we should cover them
    module.setCurrentProgram(10); // Should ignore
    EXPECT_EQ(module.getProgramName(0), "");
    module.changeProgramName(0, "NewName"); // Should ignore
}
