#include "AudioEngine.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/VCAModule.h"
#include "Modules/VisualBuffer.h"
#include <cmath>
#include <gtest/gtest.h>

// --- AttenuverterModule Peak/Mod Tracking Tests ---

class AttenuverterVisualTests : public ::testing::Test {
protected:
    std::unique_ptr<AttenuverterModule> module;

    void SetUp() override {
        module = std::make_unique<AttenuverterModule>();
        module->prepareToPlay(44100.0, 512);
    }
};

TEST_F(AttenuverterVisualTests, PeakTrackingWithSignal) {
    // Set amount to 1.0 (full pass-through)
    if (auto* amt = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1]))
        amt->setValueNotifyingHost(amt->convertTo0to1(1.0f));

    juce::MidiBuffer midi;
    // Process multiple blocks to let SmoothedValue ramp to target
    for (int block = 0; block < 5; ++block) {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        for (int i = 0; i < 512; ++i)
            buffer.setSample(0, i, 0.5f);
        module->processBlock(buffer, midi);
    }

    EXPECT_NEAR(module->getLastOutputPeak(), 0.5f, 0.05f);
}

TEST_F(AttenuverterVisualTests, ModValueTrackingReturnsMidBlockSample) {
    if (auto* amt = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1]))
        amt->setValueNotifyingHost(amt->convertTo0to1(1.0f));

    juce::MidiBuffer midi;
    // Process multiple blocks to let SmoothedValue ramp to target
    for (int block = 0; block < 5; ++block) {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        for (int i = 0; i < 512; ++i)
            buffer.setSample(0, i, 0.75f);
        module->processBlock(buffer, midi);
    }

    EXPECT_NEAR(module->getLastModValue(), 0.75f, 0.1f);
}

TEST_F(AttenuverterVisualTests, BypassedReturnsZeroPeakAndMod) {
    // Set bypass (parameter index 0 in ModuleBase)
    if (auto* bp = dynamic_cast<juce::AudioParameterBool*>(module->getParameters()[0]))
        bp->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> buffer(2, 128);
    for (int i = 0; i < 128; ++i)
        buffer.setSample(0, i, 1.0f);
    juce::MidiBuffer midi;

    module->processBlock(buffer, midi);

    EXPECT_FLOAT_EQ(module->getLastOutputPeak(), 0.0f);
    EXPECT_FLOAT_EQ(module->getLastModValue(), 0.0f);
}

TEST_F(AttenuverterVisualTests, ZeroAmountProducesZeroPeak) {
    // Default amount is 0.0
    juce::AudioBuffer<float> buffer(2, 128);
    buffer.clear();
    for (int i = 0; i < 128; ++i)
        buffer.setSample(0, i, 1.0f);
    juce::MidiBuffer midi;

    module->processBlock(buffer, midi);

    EXPECT_NEAR(module->getLastOutputPeak(), 0.0f, 0.05f);
}

// --- VisualBuffer RMS Computation Test ---

TEST(VisualBufferRMSTest, ComputeRMSFromBuffer) {
    VisualBuffer vb;
    // Push a known signal: 1024 samples of 0.5
    for (int i = 0; i < 1024; ++i)
        vb.pushSample(0.5f);

    std::vector<float> data(vb.getSize(), 0.0f);
    vb.copyTo(data);

    float sum = 0.0f;
    for (float s : data)
        sum += s * s;
    float rms = std::sqrt(sum / (float)data.size());

    EXPECT_NEAR(rms, 0.5f, 0.01f);
}

TEST(VisualBufferRMSTest, SilentBufferHasZeroRMS) {
    VisualBuffer vb;
    for (int i = 0; i < 1024; ++i)
        vb.pushSample(0.0f);

    std::vector<float> data(vb.getSize(), 0.0f);
    vb.copyTo(data);

    float sum = 0.0f;
    for (float s : data)
        sum += s * s;
    float rms = std::sqrt(sum / (float)data.size());

    EXPECT_FLOAT_EQ(rms, 0.0f);
}

// --- AudioEngine ModulationDisplayInfo Tests ---

class ModulationDisplayInfoTests : public ::testing::Test {
protected:
    std::unique_ptr<AudioEngine> engine;

    void SetUp() override { engine = std::make_unique<AudioEngine>(); }

    void TearDown() override {
        engine->shutdown();
        engine.reset();
    }
};

TEST_F(ModulationDisplayInfoTests, EmptyGraphReturnsEmptyInfo) {
    auto info = engine->getModulationDisplayInfo();
    EXPECT_TRUE(info.empty());
}

TEST_F(ModulationDisplayInfoTests, UnconnectedAttenuverterIsExcluded) {
    // Add an empty mod routing (attenuverter with no connections)
    engine->addEmptyModRouting();

    auto info = engine->getModulationDisplayInfo();
    EXPECT_TRUE(info.empty());
}

TEST_F(ModulationDisplayInfoTests, ConnectedRoutingAppearsInInfo) {
    auto& graph = engine->getGraph();

    // Use non-attenuverter modules as src/dst to avoid false matches
    auto srcNode = graph.addNode(std::make_unique<LFOModule>());
    auto dstNode = graph.addNode(std::make_unique<VCAModule>());
    ASSERT_NE(srcNode, nullptr);
    ASSERT_NE(dstNode, nullptr);

    // Add mod routing from src to dst channel 1 (VCA CV input)
    engine->addModRouting(srcNode->nodeID, 0, dstNode->nodeID, 1);

    auto info = engine->getModulationDisplayInfo();
    ASSERT_EQ(info.size(), 1u);
    EXPECT_EQ(info[0].destNodeID, dstNode->nodeID);
    EXPECT_EQ(info[0].destChannelIndex, 1);
}
