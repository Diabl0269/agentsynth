#include "Modules/ComparatorModule.h"
#include "Modules/ModuleBase.h"
#include <gtest/gtest.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

void setThreshold(ComparatorModule& m, float actual) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&m, "trigThreshold"));
    ASSERT_NE(p, nullptr);
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

} // namespace

class ComparatorModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<ComparatorModule>();
        module->prepareToPlay(kSampleRate, kBlockSize);
        buffer.setSize(2, kBlockSize);
        buffer.clear();
    }

    std::unique_ptr<ComparatorModule> module;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
};

TEST_F(ComparatorModuleTest, NameAndTypeAreCorrect) {
    EXPECT_EQ(module->getName(), "Comparator");
    EXPECT_EQ(module->getModuleType(), ModuleType::Comparator);
}

TEST_F(ComparatorModuleTest, ChannelLayoutIsSignalAndThreshold) {
    EXPECT_EQ(module->getTotalNumInputChannels(), 2);
    EXPECT_EQ(module->getTotalNumOutputChannels(), 2);
    EXPECT_EQ(module->getVisibleInputPortCount(), 2);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 2);
    EXPECT_EQ(module->getInputPortLabel(0), "Signal");
    EXPECT_EQ(module->getInputPortLabel(1), "Threshold");
    EXPECT_EQ(module->getOutputPortLabel(0), "Gate");
    EXPECT_EQ(module->getOutputPortLabel(1), "Inverse");
}

TEST_F(ComparatorModuleTest, PortRolesAreCorrect) {
    EXPECT_EQ(module->mapInputChannel(0).role, PortRole::Audio);
    EXPECT_EQ(module->mapInputChannel(1).role, PortRole::ModCV);
    EXPECT_EQ(module->mapOutputChannel(0).role, PortRole::Gate);
    EXPECT_EQ(module->mapOutputChannel(1).role, PortRole::Gate);
}

TEST_F(ComparatorModuleTest, ModulationTargetsOnlyThreshold) {
    const auto targets = module->getModulationTargets();
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0].name, "Threshold");
    EXPECT_EQ(targets[0].channelIndex, 1);
}

TEST_F(ComparatorModuleTest, GateGoesHighAboveThreshold) {
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, 0.8f);
    module->processBlock(buffer, midi);
    EXPECT_NEAR(buffer.getRMSLevel(0, 0, kBlockSize), 1.0f, 1e-4);
    EXPECT_NEAR(buffer.getRMSLevel(1, 0, kBlockSize), 0.0f, 1e-4);
    EXPECT_TRUE(module->isOverThreshold());
}

TEST_F(ComparatorModuleTest, GateStaysLowBelowThreshold) {
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, 0.2f);
    module->processBlock(buffer, midi);
    EXPECT_NEAR(buffer.getRMSLevel(0, 0, kBlockSize), 0.0f, 1e-4);
    EXPECT_NEAR(buffer.getRMSLevel(1, 0, kBlockSize), 1.0f, 1e-4);
    EXPECT_FALSE(module->isOverThreshold());
}

TEST_F(ComparatorModuleTest, InverseIsComplementOfGate) {
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, i < kBlockSize / 2 ? 0.0f : 1.0f);
    module->processBlock(buffer, midi);
    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_NEAR(buffer.getSample(0, i) + buffer.getSample(1, i), 1.0f, 1e-5);
}

TEST_F(ComparatorModuleTest, HysteresisRejectsDither) {
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, 0.8f);
    module->processBlock(buffer, midi);
    EXPECT_TRUE(module->isOverThreshold());

    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, 0.48f);
    module->processBlock(buffer, midi);
    EXPECT_TRUE(module->isOverThreshold());
}

TEST_F(ComparatorModuleTest, ThresholdCVShiftsTheSlice) {
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(0, i, 0.2f);
        buffer.setSample(1, i, -0.4f); // 0.5 - 0.4 = 0.1
    }
    module->processBlock(buffer, midi);
    EXPECT_NEAR(buffer.getSample(0, kBlockSize - 1), 1.0f, 1e-4);
    EXPECT_NEAR(module->getEffectiveThreshold(), 0.1f, 1e-4);
}

TEST_F(ComparatorModuleTest, LoweredKnobFiresOnSmallSignal) {
    setThreshold(*module, 0.1f);
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, 0.2f);
    module->processBlock(buffer, midi);
    EXPECT_NEAR(buffer.getSample(0, kBlockSize - 1), 1.0f, 1e-4);
}

TEST_F(ComparatorModuleTest, BypassClearsBothOutputs) {
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, 1.0f);
    module->processBlock(buffer, midi);
    EXPECT_GT(buffer.getRMSLevel(0, 0, kBlockSize), 0.5f);

    auto* bypassed = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(module.get(), "bypassed"));
    ASSERT_NE(bypassed, nullptr);
    *bypassed = true;
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, 1.0f);
    module->processBlock(buffer, midi);
    EXPECT_NEAR(buffer.getRMSLevel(0, 0, kBlockSize), 0.0f, 1e-4);
    EXPECT_NEAR(buffer.getRMSLevel(1, 0, kBlockSize), 0.0f, 1e-4);
}

TEST_F(ComparatorModuleTest, MuteClearsBothOutputs) {
    auto* muted = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(module.get(), "muted"));
    ASSERT_NE(muted, nullptr);
    *muted = true;
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f);
    }
    module->processBlock(buffer, midi);
    EXPECT_NEAR(buffer.getRMSLevel(0, 0, kBlockSize), 0.0f, 1e-4);
    EXPECT_NEAR(buffer.getRMSLevel(1, 0, kBlockSize), 0.0f, 1e-4);
}

TEST_F(ComparatorModuleTest, MeterReportsSignedPeak) {
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, -0.7f);
    module->processBlock(buffer, midi);
    EXPECT_NEAR(module->getMeterLevel(), -0.7f, 1e-4);
}

TEST_F(ComparatorModuleTest, TriggerCountAdvancesOnRisingEdge) {
    const int before = module->getTriggerCount();
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(0, i, 1.0f);
    module->processBlock(buffer, midi);
    EXPECT_GT(module->getTriggerCount(), before);
}

TEST_F(ComparatorModuleTest, ZeroLengthBufferDoesNotCrash) {
    juce::AudioBuffer<float> empty(2, 0);
    EXPECT_NO_THROW(module->processBlock(empty, midi));
}

TEST_F(ComparatorModuleTest, DefaultThresholdIsHalf) { EXPECT_NEAR(module->getEffectiveThreshold(), 0.5f, 1e-5f); }

TEST_F(ComparatorModuleTest, ThresholdScaleIsBipolar) {
    EXPECT_EQ(module->getThresholdScale(), ThresholdScale::Bipolar);
    EXPECT_EQ(module->getThresholdParamID(), "trigThreshold");
}
