#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/PolySequencerModule.h"
#include "Modules/SequencerModule.h"
#include "PresetManager.h"
#include "SequencerTestSupport.h"
#include "SnippetManager.h"

using synth::AIStateMapper;
using synth::SequencerChord;

namespace {

juce::var parse(const juce::String& json) { return juce::JSON::parse(json); }

template <class Module>
Module* firstModule(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* module = dynamic_cast<Module*>(node->getProcessor()))
            return module;
    return nullptr;
}

const char* kLegacyMono = R"({"nodes": [{"id": 1, "type": "Sequencer", "params": {"run": true, "Pitch 1": 40,
    "Pitch 2": 0, "Pitch 3": 41, "Gate 3": 0.9, "F.Env 3": 0.25}}]})";

const char* kLegacyPoly = R"({"nodes": [{"id": 1, "type": "Poly Sequencer", "params": {"Step 1 Root": 50,
    "Step 1 Chord": "Minor", "Step 2 Chord": 4, "Gate 2": 0.7}}]})";

void expectLegacyMono(const synth::SequencerPattern& p) {
    EXPECT_EQ(p.steps[0].pitch, 40);
    EXPECT_FALSE(p.steps[1].on);
    EXPECT_EQ(p.steps[2].pitch, 41);
    EXPECT_FLOAT_EQ(p.steps[2].gate, 0.9f);
    EXPECT_FLOAT_EQ(p.steps[2].filter, 0.25f);
}

} // namespace

TEST(SequencerStateTest, LegacyMonoParamsLoadOnTheTrustedPath) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(parse(kLegacyMono), graph, true, /*trusted=*/true));
    auto* seq = firstModule<SequencerModule>(graph);
    ASSERT_NE(seq, nullptr);
    expectLegacyMono(seq->getPattern());
}

TEST(SequencerStateTest, LegacyMonoParamsLoadOnTheUntrustedPathAndPassValidation) {
    juce::AudioProcessorGraph graph;
    const auto patch = parse(kLegacyMono);
    const auto result = AIStateMapper::validatePatch(patch, graph, true, /*trusted=*/false);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patch, graph, true, /*trusted=*/false));
    expectLegacyMono(firstModule<SequencerModule>(graph)->getPattern());
}

TEST(SequencerStateTest, LegacyPolyParamsLoadByNameAndIndex) {
    for (bool trusted : {true, false}) {
        juce::AudioProcessorGraph graph;
        ASSERT_TRUE(AIStateMapper::applyJSONToGraph(parse(kLegacyPoly), graph, true, trusted));
        auto* seq = firstModule<PolySequencerModule>(graph);
        ASSERT_NE(seq, nullptr);
        const auto& p = seq->getPattern();
        EXPECT_EQ(p.steps[0].pitch, 50);
        EXPECT_EQ(p.steps[0].chord, SequencerChord::Minor);
        EXPECT_EQ(p.steps[1].chord, SequencerChord::Min7);
        EXPECT_FLOAT_EQ(p.steps[1].gate, 0.7f);
    }
}

TEST(SequencerStateTest, AnUnknownKeyIsStillRejectedOnTheUntrustedPath) {
    juce::AudioProcessorGraph graph;
    const auto patch = parse(R"({"nodes": [{"id": 1, "type": "Sequencer", "params": {"Pitch 9": 40}}]})");
    EXPECT_FALSE(AIStateMapper::validatePatch(patch, graph, true, false).ok);
    const auto poly = parse(R"({"nodes": [{"id": 1, "type": "Poly Sequencer", "params": {"Pitch 1": 40}}]})");
    EXPECT_FALSE(AIStateMapper::validatePatch(poly, graph, true, false).ok);
}

TEST(SequencerStateTest, StateIsAppliedAfterParamsSoItWins) {
    const auto patch = parse(R"({"nodes": [{"id": 1, "type": "Sequencer", "params": {"Pitch 1": 40},
        "state": {"steps": [{"pitch": 70}]}}]})");
    for (bool trusted : {true, false}) {
        juce::AudioProcessorGraph graph;
        ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patch, graph, true, trusted));
        EXPECT_EQ(firstModule<SequencerModule>(graph)->getPattern().steps[0].pitch, 70);
    }
}

TEST(SequencerStateTest, UntrustedStateIsClampedAndReachesThePattern) {
    const auto patch = parse(R"({"nodes": [{"id": 1, "type": "Poly Sequencer",
        "state": {"seed": 5, "steps": [{"pitch": 9999, "velocity": 0, "chord": "Scale Triad"}]}}]})");
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::validatePatch(patch, graph, true, false).ok);
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patch, graph, true, false));
    const auto& p = firstModule<PolySequencerModule>(graph)->getPattern();
    EXPECT_EQ(p.seed, 5u);
    EXPECT_EQ(p.steps[0].pitch, 127);
    EXPECT_EQ(p.steps[0].velocity, 1);
    EXPECT_EQ(p.steps[0].chord, SequencerChord::ScaleTriad);
}

TEST(SequencerStateTest, SavedGraphCarriesStateAndNoLegacyKeysAndRoundTrips) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(parse(kLegacyMono), graph, true, true));
    auto* seq = firstModule<SequencerModule>(graph);
    // An edit made after the adoption is part of what gets saved.
    auto edited = seq->getPattern();
    edited.steps[0].pitch = 44;
    seq->setPattern(edited);

    const auto saved = AIStateMapper::graphToJSON(graph);
    const auto text = juce::JSON::toString(saved);
    EXPECT_TRUE(text.contains("\"state\""));
    EXPECT_FALSE(text.contains("Pitch 1"));
    EXPECT_FALSE(text.contains("F.Env"));

    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(saved, reloaded, true, true));
    auto* again = firstModule<SequencerModule>(reloaded);
    EXPECT_EQ(again->getPattern().steps, edited.steps);
    EXPECT_EQ(again->getPattern().seed, seq->getPattern().seed);
}

TEST(SequencerStateTest, SnapshotRestoreBringsBackAnEditedPattern) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(parse(kLegacyMono), graph, true, true));
    auto* seq = firstModule<SequencerModule>(graph);
    const auto before = AIStateMapper::graphToJSON(graph);
    const auto original = seq->getPattern();

    auto edited = original;
    edited.steps[5].pitch = 99;
    seq->setPattern(edited);
    ASSERT_TRUE(AIStateMapper::applySnapshotPreservingNodes(before, graph));
    EXPECT_EQ(firstModule<SequencerModule>(graph)->getPattern().steps, original.steps);
}

TEST(SequencerStateTest, EachNewModuleGetsItsOwnSeedAndThePatternGenerationMoves) {
    SequencerModule a;
    SequencerModule b;
    EXPECT_NE(a.getPattern().seed, b.getPattern().seed);
    const int generation = a.patternGeneration();
    a.setPattern(a.getPattern());
    EXPECT_GT(a.patternGeneration(), generation);
}

TEST(SequencerStateTest, StepSequencePresetKeepsItsNotes) {
    const auto names = synth::PresetManager::getPresetNames();
    const int index = names.indexOf("Step Sequence");
    ASSERT_GE(index, 0);
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(synth::PresetManager::loadPreset(index, graph));
    auto* seq = firstModule<SequencerModule>(graph);
    ASSERT_NE(seq, nullptr);
    const int pitches[8] = {48, 51, 55, 58, 60, 55, 51, 50};
    const float gates[8] = {0.5f, 0.5f, 0.5f, 0.5f, 0.8f, 0.3f, 0.3f, 0.5f};
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(seq->getPattern().steps[(size_t)i].pitch, pitches[i]) << i;
        EXPECT_FLOAT_EQ(seq->getPattern().steps[(size_t)i].gate, gates[i]) << i;
    }
}

// A snippet file drops every module's "state" unless asked for; the sequencers' pattern is pure data and stays.
TEST(SequencerStateTest, SnippetFilesKeepTheSequencerPattern) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(parse(kLegacyMono), graph, true, true));
    auto* seq = firstModule<SequencerModule>(graph);
    const auto original = seq->getPattern();

    auto node = graph.getNodes().getFirst();
    const auto snippet = synth::SnippetManager::extractSnippet(graph, {node->nodeID}, "Seq");
    const auto prepared = synth::SnippetManager::prepareForInsert(snippet, {0, 0}, 100);

    juce::AudioProcessorGraph target;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(prepared, target, false, true));
    EXPECT_EQ(firstModule<SequencerModule>(target)->getPattern().steps, original.steps);
}
