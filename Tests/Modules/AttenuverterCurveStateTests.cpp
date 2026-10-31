// AttenuverterCurveStateTests.cpp -- the curve as module state: it rides the node's "state" through
// AIStateMapper's graph JSON (trusted apply), a linear attenuverter writes nothing, and undo restores
// the previous curve on the live node.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModCurve.h"
#include "Modules/VCAModule.h"
#include "SnippetManager.h"
#include <gtest/gtest.h>

using synth::ModCurve;

namespace {
AttenuverterModule* addCurveAtten(juce::AudioProcessorGraph& g, juce::AudioProcessorGraph::NodeID* id = nullptr) {
    auto node = g.addNode(std::make_unique<AttenuverterModule>());
    if (id != nullptr)
        *id = node->nodeID;
    return dynamic_cast<AttenuverterModule*>(node->getProcessor());
}

AttenuverterModule* onlyCurveAtten(juce::AudioProcessorGraph& g) {
    for (auto* node : g.getNodes())
        if (auto* a = dynamic_cast<AttenuverterModule*>(node->getProcessor()))
            return a;
    return nullptr;
}
} // namespace

TEST(AttenuverterCurveStateTest, CurveSurvivesTheTrustedGraphRoundTrip) {
    juce::AudioProcessorGraph source;
    auto* a = addCurveAtten(source);
    const auto exp = ModCurve::preset(ModCurve::Preset::Exp);
    a->setCurve(exp);

    const auto json = synth::AIStateMapper::graphToJSON(source);
    juce::AudioProcessorGraph target;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, target, true, /*trusted=*/true));
    auto* restored = onlyCurveAtten(target);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->getCurve(), exp);
}

TEST(AttenuverterCurveStateTest, NoStateMeansLinearAndLinearWritesNoState) {
    juce::AudioProcessorGraph source;
    addCurveAtten(source);
    const auto json = synth::AIStateMapper::graphToJSON(source);
    const auto* nodes = json.getProperty("nodes", {}).getArray();
    ASSERT_NE(nodes, nullptr);
    ASSERT_EQ(nodes->size(), 1);
    EXPECT_FALSE(nodes->getReference(0).getDynamicObject()->hasProperty("state"));

    juce::AudioProcessorGraph target;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, target, true, true));
    ASSERT_NE(onlyCurveAtten(target), nullptr);
    EXPECT_TRUE(onlyCurveAtten(target)->getCurve().isLinear());
}

TEST(AttenuverterCurveStateTest, UntrustedStateIsIgnored) {
    juce::AudioProcessorGraph source;
    addCurveAtten(source)->setCurve(ModCurve::preset(ModCurve::Preset::Log));
    const auto json = synth::AIStateMapper::graphToJSON(source);

    juce::AudioProcessorGraph target;
    // validatePatch may refuse a bare attenuverter node outright; either way the curve must not arrive.
    synth::AIStateMapper::applyJSONToGraph(json, target, true, /*trusted=*/false);
    if (auto* a = onlyCurveAtten(target))
        EXPECT_TRUE(a->getCurve().isLinear());
}

TEST(AttenuverterCurveStateTest, UndoRestoresThePreviousCurveOnTheLiveNode) {
    juce::AudioProcessorGraph graph;
    auto* atten = addCurveAtten(graph);
    const auto exp = ModCurve::preset(ModCurve::Preset::Exp);
    atten->setCurve(exp);

    AppUndoManager undo;
    undo.captureBeforeState(graph); // what GraphEditor::beginModAmountGesture does
    atten->setCurve(ModCurve::preset(ModCurve::Preset::Log));
    undo.pushSnapshotFromCapture(graph);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(onlyCurveAtten(graph), atten) << "restored in place, not rebuilt";
    EXPECT_EQ(atten->getCurve(), exp);

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(atten->getCurve(), ModCurve::preset(ModCurve::Preset::Log));
}

TEST(AttenuverterCurveStateTest, UndoBackToLinearClearsTheCurve) {
    juce::AudioProcessorGraph graph;
    auto* atten = addCurveAtten(graph);
    AppUndoManager undo;
    undo.captureBeforeState(graph);
    atten->setCurve(ModCurve::preset(ModCurve::Preset::SCurve));
    undo.pushSnapshotFromCapture(graph);
    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(atten->getCurve().isLinear());
}

// Copy and paste of two modules joined by a bent modulation keeps the curve; the same payload through an untrusted
// apply resolves to a linear curve.
TEST(AttenuverterCurveStateTest, SnippetPasteKeepsTheBentCurveAndUntrustedApplyDoesNot) {
    juce::AudioProcessorGraph source;
    const auto lfo = source.addNode(std::make_unique<LFOModule>())->nodeID;
    const auto vca = source.addNode(std::make_unique<VCAModule>())->nodeID;
    juce::AudioProcessorGraph::NodeID attenId;
    auto* atten = addCurveAtten(source, &attenId);
    ASSERT_TRUE(source.addConnection({{lfo, 0}, {attenId, 0}}));
    ASSERT_TRUE(source.addConnection({{attenId, 0}, {vca, 0}}));
    const auto exp = ModCurve::preset(ModCurve::Preset::Exp);
    atten->setCurve(exp);

    const auto snippet = synth::SnippetManager::extractSnippet(source, {lfo, vca}, "pair", true);
    juce::AudioProcessorGraph target;
    ASSERT_FALSE(synth::SnippetManager::insertSnippet(snippet, target, {0, 0}, true).empty());
    ASSERT_NE(onlyCurveAtten(target), nullptr);
    EXPECT_EQ(onlyCurveAtten(target)->getCurve(), exp);

    juce::AudioProcessorGraph untrusted;
    // The same snippet entry through the model-output path: it may be refused outright, but a curve must never arrive.
    synth::AIStateMapper::applyJSONToGraph(snippet, untrusted, true, /*trusted=*/false);
    if (auto* a = onlyCurveAtten(untrusted))
        EXPECT_TRUE(a->getCurve().isLinear()) << "model output does not author curves";
}
