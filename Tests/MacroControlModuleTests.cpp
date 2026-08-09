#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/MacroControlModule.h"
#include <gtest/gtest.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

// Enough blocks for the 20 ms output smoother to settle on its target.
constexpr int kSettleBlocks = 4;

juce::AudioParameterInt* countParam(MacroControlModule& m) {
    for (auto* p : m.getParameters())
        if (auto* i = dynamic_cast<juce::AudioParameterInt*>(p))
            if (i->paramID == "macroCount")
                return i;
    return nullptr;
}

juce::AudioParameterFloat* macroParam(MacroControlModule& m, int index) {
    const juce::String id = "macro" + juce::String(index + 1);
    for (auto* p : m.getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p))
            if (f->paramID == id)
                return f;
    return nullptr;
}

juce::AudioParameterBool* boolParam(MacroControlModule& m, const juce::String& id) {
    for (auto* p : m.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p))
            if (b->paramID == id)
                return b;
    return nullptr;
}

void setFloat(juce::RangedAudioParameter* p, float denormalised) {
    p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(denormalised));
}

// Runs the module until its smoothers settle and returns the last block.
juce::AudioBuffer<float> renderSettled(MacroControlModule& m, int blocks = kSettleBlocks) {
    juce::AudioBuffer<float> buffer(MacroControlModule::kMaxMacros, kBlockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < blocks; ++i) {
        buffer.clear();
        m.processBlock(buffer, midi);
    }
    return buffer;
}

class MacroControlModuleTest : public ::testing::Test {
protected:
    void SetUp() override { module.prepareToPlay(kSampleRate, kBlockSize); }
    MacroControlModule module;
};

//==============================================================================
// Identity / port model
//==============================================================================

TEST_F(MacroControlModuleTest, DefaultsToEightVisibleMacrosOfSixteenChannels) {
    EXPECT_EQ(module.getName(), "Macros");
    EXPECT_EQ(module.getModuleType(), ModuleType::MacroControl);
    EXPECT_EQ(module.getTotalNumOutputChannels(), MacroControlModule::kMaxMacros);
    EXPECT_EQ(module.getMacroCount(), MacroControlModule::kDefaultMacros);
    EXPECT_EQ(module.getVisibleOutputPortCount(), MacroControlModule::kDefaultMacros);
    EXPECT_EQ(module.getVisibleInputPortCount(), 0);
}

TEST_F(MacroControlModuleTest, DeclaresNoMidiPorts) {
    EXPECT_FALSE(module.acceptsMidi());
    EXPECT_FALSE(module.producesMidi());
}

TEST_F(MacroControlModuleTest, OutputPortsAreLabelledAndTaggedAsModCV) {
    EXPECT_EQ(module.getOutputPortLabel(0), "M1");
    EXPECT_EQ(module.getOutputPortLabel(MacroControlModule::kMaxMacros - 1), "M16");

    for (int ch = 0; ch < module.getMacroCount(); ++ch) {
        auto port = module.mapOutputChannel(ch);
        EXPECT_EQ(port.role, PortRole::ModCV);
        EXPECT_EQ(port.visibleJackIndex, ch);
        EXPECT_EQ(port.polyVoiceSpan, 1);
    }
}

//==============================================================================
// CV generation
//==============================================================================

TEST_F(MacroControlModuleTest, EachMacroEmitsItsKnobValueOnItsOwnChannel) {
    setFloat(macroParam(module, 0), 0.25f);
    setFloat(macroParam(module, 1), 0.75f);
    setFloat(macroParam(module, 7), 1.0f);

    auto buffer = renderSettled(module);

    EXPECT_NEAR(buffer.getSample(0, kBlockSize - 1), 0.25f, 1e-4f);
    EXPECT_NEAR(buffer.getSample(1, kBlockSize - 1), 0.75f, 1e-4f);
    EXPECT_NEAR(buffer.getSample(7, kBlockSize - 1), 1.0f, 1e-4f);
    EXPECT_NEAR(buffer.getSample(2, kBlockSize - 1), 0.0f, 1e-4f);
}

TEST_F(MacroControlModuleTest, ChannelsAboveTheVisibleCountAreSilent) {
    for (int i = 0; i < MacroControlModule::kMaxMacros; ++i)
        setFloat(macroParam(module, i), 1.0f);

    countParam(module)->setValueNotifyingHost(countParam(module)->convertTo0to1(4));
    auto buffer = renderSettled(module);

    for (int ch = 0; ch < 4; ++ch)
        EXPECT_NEAR(buffer.getSample(ch, kBlockSize - 1), 1.0f, 1e-4f) << "channel " << ch;

    for (int ch = 4; ch < MacroControlModule::kMaxMacros; ++ch)
        EXPECT_FLOAT_EQ(buffer.getSample(ch, kBlockSize - 1), 0.0f) << "channel " << ch;
}

TEST_F(MacroControlModuleTest, GrowingTheBankRevealsTheHiddenMacrosAtTheirStoredValues) {
    setFloat(macroParam(module, 11), 0.6f);
    countParam(module)->setValueNotifyingHost(countParam(module)->convertTo0to1(4));
    renderSettled(module);
    EXPECT_EQ(module.getVisibleOutputPortCount(), 4);

    countParam(module)->setValueNotifyingHost(countParam(module)->convertTo0to1(16));
    auto buffer = renderSettled(module);

    EXPECT_EQ(module.getVisibleOutputPortCount(), 16);
    EXPECT_NEAR(buffer.getSample(11, kBlockSize - 1), 0.6f, 1e-4f);
}

TEST_F(MacroControlModuleTest, BipolarMapsTheKnobToMinusOnePlusOne) {
    auto* bipolar = boolParam(module, "macroBipolar");
    bipolar->setValueNotifyingHost(1.0f);

    setFloat(macroParam(module, 0), 0.0f);
    setFloat(macroParam(module, 1), 0.5f);
    setFloat(macroParam(module, 2), 1.0f);

    auto buffer = renderSettled(module);

    EXPECT_TRUE(module.isBipolar());
    EXPECT_NEAR(buffer.getSample(0, kBlockSize - 1), -1.0f, 1e-4f);
    EXPECT_NEAR(buffer.getSample(1, kBlockSize - 1), 0.0f, 1e-4f);
    EXPECT_NEAR(buffer.getSample(2, kBlockSize - 1), 1.0f, 1e-4f);
}

TEST_F(MacroControlModuleTest, KnobMovesAreSmoothedRatherThanStepped) {
    setFloat(macroParam(module, 0), 0.0f);
    renderSettled(module);

    setFloat(macroParam(module, 0), 1.0f);

    juce::AudioBuffer<float> buffer(MacroControlModule::kMaxMacros, kBlockSize);
    juce::MidiBuffer midi;
    buffer.clear();
    module.processBlock(buffer, midi);

    // 20 ms of smoothing at 44.1 kHz is ~882 samples, so a single 512-sample block must not
    // arrive at the target and the first sample must not jump the whole way.
    EXPECT_LT(buffer.getSample(0, 0), 0.1f);
    EXPECT_LT(buffer.getSample(0, kBlockSize - 1), 1.0f);
    EXPECT_GT(buffer.getSample(0, kBlockSize - 1), buffer.getSample(0, 0));
}

//==============================================================================
// Bypass / mute — pure source module, so both silence the output
//==============================================================================

TEST_F(MacroControlModuleTest, BypassSilencesEveryChannel) {
    setFloat(macroParam(module, 0), 1.0f);
    renderSettled(module);

    module.setBypassed(true);
    auto buffer = renderSettled(module, 1);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        EXPECT_FLOAT_EQ(buffer.getSample(ch, 0), 0.0f) << "channel " << ch;
}

TEST_F(MacroControlModuleTest, MuteSilencesEveryChannel) {
    setFloat(macroParam(module, 0), 1.0f);
    renderSettled(module);

    module.setMuted(true);
    auto buffer = renderSettled(module, 1);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        EXPECT_FLOAT_EQ(buffer.getSample(ch, 0), 0.0f) << "channel " << ch;
}

TEST_F(MacroControlModuleTest, ProcessingAZeroChannelBufferIsSafe) {
    juce::AudioBuffer<float> empty(0, 0);
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module.processBlock(empty, midi));
}

//==============================================================================
// Graph / preset integration
//==============================================================================

TEST(MacroControlModuleFactoryTest, FactoryCreatesMacrosByName) {
    auto module = synth::AIStateMapper::createModule("Macros");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->getName(), "Macros");
}

TEST(MacroControlModuleFactoryTest, CountAndKnobValuesSurviveAJSONRoundTrip) {
    juce::AudioProcessorGraph source;
    auto node = source.addNode(std::make_unique<MacroControlModule>());
    ASSERT_NE(node, nullptr);

    auto* macros = dynamic_cast<MacroControlModule*>(node->getProcessor());
    ASSERT_NE(macros, nullptr);
    countParam(*macros)->setValueNotifyingHost(countParam(*macros)->convertTo0to1(12));
    setFloat(macroParam(*macros, 3), 0.42f);

    auto json = synth::AIStateMapper::graphToJSON(source);

    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, true, /*trusted=*/true));

    MacroControlModule* roundTripped = nullptr;
    for (auto* n : restored.getNodes())
        if (auto* m = dynamic_cast<MacroControlModule*>(n->getProcessor()))
            roundTripped = m;

    ASSERT_NE(roundTripped, nullptr);
    EXPECT_EQ(roundTripped->getMacroCount(), 12);
    EXPECT_NEAR(roundTripped->getMacroKnob(3), 0.42f, 1e-3f);
}

TEST(MacroControlModuleFactoryTest, OneMacroCanDriveSeveralDestinationsAtOnce) {
    // The macro idea itself: a single output jack fanned out to many CV destinations. The graph
    // permits one source channel to feed any number of destinations, which is what makes a
    // 16-macro bank useful without needing 16 separate routing matrices.
    juce::AudioProcessorGraph graph;
    auto macrosNode = graph.addNode(std::make_unique<MacroControlModule>());
    auto filterA = graph.addNode(synth::AIStateMapper::createModule("Filter"));
    auto filterB = graph.addNode(synth::AIStateMapper::createModule("Distortion"));
    ASSERT_NE(macrosNode, nullptr);
    ASSERT_NE(filterA, nullptr);
    ASSERT_NE(filterB, nullptr);

    EXPECT_TRUE(graph.addConnection({{macrosNode->nodeID, 0}, {filterA->nodeID, 8}})); // Filter cutoff CV
    EXPECT_TRUE(graph.addConnection({{macrosNode->nodeID, 0}, {filterB->nodeID, 2}})); // Distortion drive CV

    int fanOut = 0;
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == macrosNode->nodeID && c.source.channelIndex == 0)
            ++fanOut;

    EXPECT_EQ(fanOut, 2);
}

} // namespace
