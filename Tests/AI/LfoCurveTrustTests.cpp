// LfoCurveTrustTests.cpp
//
// Model-authored patches may set exactly one kind of node "state": an LFO's drawn Custom curve, which
// LfoCustomWave::fromVar clamps into pure points. Every other module's state (a Sampler's file path)
// stays trusted-only. See docs/ai/patch-format.md#per-node-state and docs/ai/patch-safety.md.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/SamplerModule.h"
#include <gtest/gtest.h>

using synth::AIStateMapper;
using synth::LfoCustomWave;

namespace {

juce::var point(double x, double y, double bend = 0.0) {
    auto* p = new juce::DynamicObject();
    p->setProperty("x", x);
    p->setProperty("y", y);
    p->setProperty("bend", bend);
    return juce::var(p);
}

juce::var curve(const juce::Array<juce::var>& points) {
    auto* c = new juce::DynamicObject();
    c->setProperty("version", 1);
    c->setProperty("points", juce::var(points));
    return juce::var(c);
}

// A falling swoop: starts high, drops fast, settles low.
juce::var swoop() { return curve({point(0.0, 1.0), point(0.3, 0.2, -0.5), point(1.0, 0.0)}); }

juce::var patchWith(const juce::String& type, const juce::var& state, int id = 1) {
    auto* node = new juce::DynamicObject();
    node->setProperty("id", id);
    node->setProperty("type", type);
    if (!state.isVoid())
        node->setProperty("state", state);
    auto* root = new juce::DynamicObject();
    root->setProperty("nodes", juce::Array<juce::var>{juce::var(node)});
    root->setProperty("connections", juce::Array<juce::var>{});
    return juce::var(root);
}

template <typename Module>
Module* only(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* m = dynamic_cast<Module*>(node->getProcessor()))
            return m;
    return nullptr;
}

} // namespace

TEST(LfoCurveTrustTest, AnUntrustedPatchSetsTheLfoCurve) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(
        AIStateMapper::applyJSONToGraph(patchWith("LFO", swoop()), graph, /*clearExisting=*/true, /*trusted=*/false));
    auto* lfo = only<LFOModule>(graph);
    ASSERT_NE(lfo, nullptr);
    EXPECT_EQ(lfo->getCustomWave(), LfoCustomWave::fromVar(swoop()));
    EXPECT_FALSE(lfo->getCustomWave().isDefault());
    EXPECT_NEAR(lfo->getCustomWave().evaluate(0.0f), 1.0f, 1e-4f);
}

TEST(LfoCurveTrustTest, AnUntrustedCurveIsClampedAndCapped) {
    juce::Array<juce::var> points;
    for (int i = 0; i < 100; ++i)
        points.add(point(i / 99.0, i % 2 == 0 ? 7.0 : -3.0, 9.0)); // y and bend far out of range
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("LFO", curve(points)), graph, true, false));
    const auto& wave = only<LFOModule>(graph)->getCustomWave();
    ASSERT_LE((int)wave.points.size(), LfoCustomWave::kMaxPoints);
    ASSERT_GE((int)wave.points.size(), LfoCustomWave::kMinPoints);
    for (const auto& p : wave.points) {
        EXPECT_GE(p.x, 0.0f);
        EXPECT_LE(p.x, 1.0f);
        EXPECT_GE(p.y, 0.0f);
        EXPECT_LE(p.y, 1.0f);
        EXPECT_GE(p.bend, -1.0f);
        EXPECT_LE(p.bend, 1.0f);
    }
    EXPECT_EQ(wave.points.front().x, 0.0f);
    EXPECT_EQ(wave.points.back().x, 1.0f);
}

TEST(LfoCurveTrustTest, AMalformedUntrustedCurveLeavesTheDefaultWave) {
    for (const auto& garbage : {juce::var("not a curve"), juce::var(42), curve({point(0.5, 0.5)}), juce::var()}) {
        juce::AudioProcessorGraph graph;
        ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("LFO", garbage), graph, true, false));
        EXPECT_TRUE(only<LFOModule>(graph)->getCustomWave().isDefault()) << juce::JSON::toString(garbage);
    }
}

TEST(LfoCurveTrustTest, AnUntrustedMergeSetsTheCurveOnAnExistingLfo) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("LFO", {}), graph, true, true));
    ASSERT_TRUE(only<LFOModule>(graph)->getCustomWave().isDefault());
    const auto json = AIStateMapper::graphToJSON(graph);
    const int id = (int)json.getDynamicObject()->getProperty("nodes")[0].getDynamicObject()->getProperty("id");
    auto merge = patchWith("LFO", swoop(), id);
    merge.getDynamicObject()->setProperty("mode", "merge");
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(merge, graph, /*clearExisting=*/false, /*trusted=*/false));
    EXPECT_EQ(only<LFOModule>(graph)->getCustomWave(), LfoCustomWave::fromVar(swoop()));
}

TEST(LfoCurveTrustTest, AnUntrustedSamplerStateStillLoadsNoFile) {
    // A real, decodable WAV, so a load would succeed if the state were honoured.
    const auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("agentsynth-lfo-trust-" + juce::Uuid().toString() + ".wav");
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(new juce::FileOutputStream(wav), 44100.0, 1, 16, {}, 0));
        ASSERT_NE(writer, nullptr);
        juce::AudioBuffer<float> buffer(1, 4410);
        buffer.clear();
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }
    // Held in a var for the whole test: both patches share it, and a bare DynamicObject* would be freed
    // with the first patch.
    const juce::var state(new juce::DynamicObject());
    state.getDynamicObject()->setProperty(synth::module_file_keys::kSampleFile, wav.getFullPathName());

    juce::AudioProcessorGraph untrusted;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("Sampler", state), untrusted, true, false));
    ASSERT_NE(only<SamplerModule>(untrusted), nullptr);
    EXPECT_TRUE(only<SamplerModule>(untrusted)->getSampleFilePath().isEmpty());

    juce::AudioProcessorGraph trusted;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("Sampler", state), trusted, true, true));
    EXPECT_EQ(only<SamplerModule>(trusted)->getSampleFilePath(), wav.getFullPathName())
        << "control: the same state does load on the trusted path";
    wav.deleteFile();
}

TEST(LfoCurveTrustTest, TheTrustedPathRoundTripsTheCurveAsBefore) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWith("LFO", swoop()), graph, true, true));
    const auto json = AIStateMapper::graphToJSON(graph);
    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, reloaded, true, true));
    EXPECT_EQ(only<LFOModule>(reloaded)->getCustomWave(), LfoCustomWave::fromVar(swoop()));
}

namespace {
juce::var lfoParams(std::initializer_list<std::pair<const char*, juce::var>> params) {
    auto* p = new juce::DynamicObject();
    for (const auto& [id, value] : params)
        p->setProperty(id, value);
    auto* node = new juce::DynamicObject();
    node->setProperty("id", 1);
    node->setProperty("type", "LFO");
    node->setProperty("params", juce::var(p));
    auto* root = new juce::DynamicObject();
    root->setProperty("nodes", juce::Array<juce::var>{juce::var(node)});
    root->setProperty("connections", juce::Array<juce::var>{});
    return juce::var(root);
}

bool isSynced(LFOModule& lfo) {
    for (auto* param : lfo.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(param); b != nullptr && b->paramID == "mode")
            return b->get();
    return true;
}
} // namespace

TEST(LfoRateModeTest, AModelLfoWithOnlyARateInHzRunsInHzMode) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(lfoParams({{"rateHz", 10.0}}), graph, true, false));
    EXPECT_FALSE(isSynced(*only<LFOModule>(graph))) << "the 10 Hz the model asked for must not be ignored";
}

TEST(LfoRateModeTest, AnExplicitModeOrSyncRateIsKept) {
    juce::AudioProcessorGraph synced;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(lfoParams({{"rateHz", 3.0}, {"mode", true}}), synced, true, false));
    EXPECT_TRUE(isSynced(*only<LFOModule>(synced)));
    juce::AudioProcessorGraph division;
    ASSERT_TRUE(
        AIStateMapper::applyJSONToGraph(lfoParams({{"rateHz", 3.0}, {"rateSync", "1/8"}}), division, true, false));
    EXPECT_TRUE(isSynced(*only<LFOModule>(division)));
}

TEST(LfoRateModeTest, TheTrustedPathIsUnchanged) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(lfoParams({{"rateHz", 3.0}}), graph, true, true));
    EXPECT_TRUE(isSynced(*only<LFOModule>(graph)));
}
