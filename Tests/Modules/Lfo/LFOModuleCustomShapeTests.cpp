// LFOModuleCustomShapeTests.cpp -- LFOModule's Custom shape (index 5): the appended
// choice, the publish/adopt table crossing to the audio thread, bipolar/unipolar output, the
// getExtraState/setExtraState round-trip (trusted path only -- docs/ai/patch-safety.md), retrig,
// and the generation counter the card's reverse-sync polls.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/LFOModule.h"
#include <gtest/gtest.h>

using synth::LfoCustomWave;

namespace {
juce::AudioProcessorParameter* findParam(LFOModule& lfo, const juce::String& paramId) {
    for (auto* p : lfo.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (withId->paramID == paramId)
                return p;
    return nullptr;
}

void selectShape(LFOModule& lfo, int index) {
    *dynamic_cast<juce::AudioParameterChoice*>(findParam(lfo, "shape")) = index;
}

juce::AudioBuffer<float> renderOneBlock(LFOModule& lfo, int numSamples = 512) {
    juce::AudioBuffer<float> buffer(3, numSamples);
    buffer.clear();
    juce::MidiBuffer midi;
    lfo.processBlock(buffer, midi);
    return buffer;
}
} // namespace

TEST(LFOModuleCustomShapeTest, ShapeChoiceListAppendsCustomAtIndexFive) {
    LFOModule lfo;
    auto* shape = dynamic_cast<juce::AudioParameterChoice*>(findParam(lfo, "shape"));
    ASSERT_NE(shape, nullptr);
    ASSERT_EQ(shape->choices.size(), 6);
    EXPECT_EQ(shape->choices[5], "Custom");
    EXPECT_EQ(LFOModule::kCustomShapeIndex, 5);
}

TEST(LFOModuleCustomShapeTest, ParamOrderUnchanged) {
    LFOModule lfo;
    const char* expected[] = {"shape", "mode",  "bipolar", "rateHz", "rateSync", "retrig",
                              "level", "glide", "phase",   "fadeIn", "muted"};
    auto params = lfo.getParameters();
    // Index 0 is the inherited "bypassed" param; the rest follow the constructor's own order.
    ASSERT_GE(params.size(), 1 + (int)std::size(expected));
    for (size_t i = 0; i < std::size(expected); ++i) {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(params[(int)i + 1]);
        ASSERT_NE(withId, nullptr);
        EXPECT_EQ(withId->paramID, juce::String(expected[i])) << "index " << i;
    }
}

TEST(LFOModuleCustomShapeTest, DefaultCustomWaveIsTriangleBeforeAnyPublish) {
    LFOModule lfo;
    selectShape(lfo, LFOModule::kCustomShapeIndex);
    // Never called setCustomWave -- the ctor renders defaultWave() into BOTH table slots, so the
    // very first block already plays it (no one-block silence/garbage gap).
    const auto buffer = renderOneBlock(lfo);
    bool sawNonZero = false;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        sawNonZero |= (buffer.getSample(0, i) != 0.0f);
    EXPECT_TRUE(sawNonZero);
}

TEST(LFOModuleCustomShapeTest, CustomBipolarOutputMatchesRenderedTable) {
    LFOModule lfo;
    *dynamic_cast<juce::AudioParameterBool*>(findParam(lfo, "bipolar")) = true;
    *dynamic_cast<juce::AudioParameterBool*>(findParam(lfo, "mode")) = false; // Hz, not Sync
    selectShape(lfo, LFOModule::kCustomShapeIndex);

    const auto ramp = LfoCustomWave::preset(LfoCustomWave::Preset::RampUp);
    lfo.setCustomWave(ramp);
    LfoCustomWave::Table table{};
    ramp.renderTable(table);

    // A rate of ~0 Hz keeps phase pinned near 0 across one block, so sample 0 reads table[0].
    auto* rateHz = dynamic_cast<juce::AudioParameterFloat*>(findParam(lfo, "rateHz"));
    rateHz->setValueNotifyingHost(rateHz->getNormalisableRange().convertTo0to1(0.01f));
    renderOneBlock(lfo, 1); // adopt the pending table (processBlock's own adoptPendingCustomTable)
    const auto buffer = renderOneBlock(lfo, 1);
    EXPECT_NEAR(buffer.getSample(0, 0), table[0] * 2.0f - 1.0f, 1.0e-3f);
}

TEST(LFOModuleCustomShapeTest, CustomUnipolarOutputEqualsCurveY) {
    LFOModule lfo;
    *dynamic_cast<juce::AudioParameterBool*>(findParam(lfo, "bipolar")) = false;
    *dynamic_cast<juce::AudioParameterBool*>(findParam(lfo, "mode")) = false;
    selectShape(lfo, LFOModule::kCustomShapeIndex);

    const auto ramp = LfoCustomWave::preset(LfoCustomWave::Preset::RampUp);
    lfo.setCustomWave(ramp);
    auto* rateHz = dynamic_cast<juce::AudioParameterFloat*>(findParam(lfo, "rateHz"));
    rateHz->setValueNotifyingHost(rateHz->getNormalisableRange().convertTo0to1(0.01f));
    renderOneBlock(lfo, 1);
    const auto buffer = renderOneBlock(lfo, 1);
    EXPECT_NEAR(buffer.getSample(0, 0), ramp.evaluate(0.0f), 1.0e-2f);
}

TEST(LFOModuleCustomShapeTest, SetCustomWaveIsAdoptedNextBlock) {
    LFOModule lfo;
    selectShape(lfo, LFOModule::kCustomShapeIndex);
    renderOneBlock(lfo); // consume the ctor's already-adopted default table

    lfo.setCustomWave(LfoCustomWave::preset(LfoCustomWave::Preset::Square));
    // Not yet visible to a block already "in flight" conceptually -- but the very next
    // processBlock call both adopts and plays it, since adopt runs before the sample loop.
    const auto buffer = renderOneBlock(lfo);
    bool sawNonZero = false;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        sawNonZero |= (buffer.getSample(0, i) != 0.0f);
    EXPECT_TRUE(sawNonZero);
}

TEST(LFOModuleCustomShapeTest, RetrigResetsCustomPhaseToWaveStart) {
    LFOModule lfo;
    *dynamic_cast<juce::AudioParameterBool*>(findParam(lfo, "retrig")) = true;
    *dynamic_cast<juce::AudioParameterBool*>(findParam(lfo, "bipolar")) = false;
    *dynamic_cast<juce::AudioParameterBool*>(findParam(lfo, "mode")) = false;
    selectShape(lfo, LFOModule::kCustomShapeIndex);
    lfo.setCustomWave(LfoCustomWave::preset(LfoCustomWave::Preset::RampUp));

    auto* rateHz = dynamic_cast<juce::AudioParameterFloat*>(findParam(lfo, "rateHz"));
    rateHz->setValueNotifyingHost(rateHz->getNormalisableRange().convertTo0to1(20.0f)); // advance phase fast
    renderOneBlock(lfo, 1);
    renderOneBlock(lfo, 256); // phase now well away from 0

    juce::AudioBuffer<float> buffer(3, 4);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.5f), 0);
    lfo.processBlock(buffer, midi);
    EXPECT_NEAR(buffer.getSample(0, 0), 0.0f, 0.05f) << "retrig resets phase to the wave's own start (RampUp: y=0)";
}

TEST(LFOModuleCustomShapeTest, ExtraStateOmittedWhenDefaultAndCarriedRegardlessOfShape) {
    LFOModule lfo;
    EXPECT_TRUE(lfo.getExtraState().isVoid()) << "default Triangle wave emits nothing";

    lfo.setCustomWave(LfoCustomWave::preset(LfoCustomWave::Preset::Square));
    EXPECT_FALSE(lfo.getExtraState().isVoid());

    // Still emitted while shape is something else entirely -- a sculpted wave must survive
    // switching away from Custom and back (spec section 4's "Card wiring" undo note).
    selectShape(lfo, 0); // Sine
    EXPECT_FALSE(lfo.getExtraState().isVoid());
}

TEST(LFOModuleCustomShapeTest, SetExtraStateSanitisesHostileJson) {
    LFOModule lfo;
    auto* hostile = new juce::DynamicObject();
    hostile->setProperty("version", 999);
    hostile->setProperty("points", "not an array");
    lfo.setExtraState(juce::var(hostile));
    EXPECT_TRUE(lfo.getCustomWave().isDefault()) << "an unresolvable state falls back to the default wave";
}

TEST(LFOModuleCustomShapeTest, GenerationBumpsOnEverySetCustomWave) {
    LFOModule lfo;
    const int g0 = lfo.getCustomWaveGeneration();
    lfo.setCustomWave(LfoCustomWave::preset(LfoCustomWave::Preset::Square));
    const int g1 = lfo.getCustomWaveGeneration();
    lfo.setCustomWave(LfoCustomWave::preset(LfoCustomWave::Preset::Pulse));
    const int g2 = lfo.getCustomWaveGeneration();
    EXPECT_GT(g1, g0);
    EXPECT_GT(g2, g1);
}

// A factory-preset-shaped patch names its shape by STRING ("Triangle"), never an index, so
// appending "Custom" at index 5 must not shift what an old preset resolves to.
TEST(LFOModuleCustomShapeTest, FactoryPresetWithOldShapeNameStillResolves) {
    juce::AudioProcessorGraph graph;

    auto* node = new juce::DynamicObject();
    node->setProperty("id", 1);
    node->setProperty("type", "LFO");
    node->setProperty("x", 0);
    node->setProperty("y", 0);
    auto* params = new juce::DynamicObject();
    params->setProperty("shape", "Triangle");
    node->setProperty("params", juce::var(params));
    juce::Array<juce::var> nodes;
    nodes.add(juce::var(node));

    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", 1);
    root->setProperty("nodes", juce::var(nodes));
    root->setProperty("connections", juce::var(juce::Array<juce::var>()));
    juce::var patch(root);

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, graph, true, /*trusted=*/true));
    auto* lfoNode = graph.getNodeForId(juce::AudioProcessorGraph::NodeID(1));
    ASSERT_NE(lfoNode, nullptr);
    auto* shape = dynamic_cast<juce::AudioParameterChoice*>(
        findParam(*dynamic_cast<LFOModule*>(lfoNode->getProcessor()), "shape"));
    ASSERT_NE(shape, nullptr);
    EXPECT_EQ(shape->getIndex(), 1) << "\"Triangle\" still resolves to index 1";
}
