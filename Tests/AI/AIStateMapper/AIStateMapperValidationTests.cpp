// ---------------------------------------------------------------------------------------------
// Strict/untrusted patch validation (default mode) — hardening against a patch source that
// isn't just a trusted local Ollama: a remote server, or a local model emitting garbage under
// load. Every test below relies on applyJSONToGraph's default trusted=false.
// ---------------------------------------------------------------------------------------------
#include "AIStateMapperTestHelpers.h"
#include "PresetManager.h"
#include <gtest/gtest.h>
#include <limits>

TEST(AIStateMapperTest, RejectsPatchExceedingNodeCap) {
    juce::AudioProcessorGraph graph;

    juce::Array<juce::var> nodes;
    for (int i = 0; i < synth::AIStateMapper::kMaxNodes + 1; ++i) {
        juce::DynamicObject::Ptr n = new juce::DynamicObject();
        n->setProperty("id", i + 1);
        n->setProperty("type", "Oscillator");
        nodes.add(juce::var(n.get()));
    }
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", nodes);
    root->setProperty("connections", juce::Array<juce::var>());

    bool success = synth::AIStateMapper::applyJSONToGraph(juce::var(root.get()), graph, true);
    EXPECT_FALSE(success);
    EXPECT_EQ(graph.getNumNodes(), 0);
}

TEST(AIStateMapperTest, RejectsUnknownModuleType) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(R"({"nodes":[{"id":1,"type":"TotallyBogusModule"}],"connections":[]})");

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, true);
    EXPECT_FALSE(success);
    EXPECT_EQ(graph.getNumNodes(), 0);
}

TEST(AIStateMapperTest, RejectsDuplicateNodeIds) {
    juce::AudioProcessorGraph graph;
    juce::var json =
        juce::JSON::parse(R"({"nodes":[{"id":5,"type":"Oscillator"},{"id":5,"type":"Filter"}],"connections":[]})");

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, true);
    EXPECT_FALSE(success);
    EXPECT_EQ(graph.getNumNodes(), 0);
}

TEST(AIStateMapperTest, RejectsNaNAndInfinityParameterValues) {
    // NaN/Infinity can't be spelled in strict JSON text, so build the var tree directly.
    juce::DynamicObject::Ptr params = new juce::DynamicObject();
    params->setProperty("fine", std::numeric_limits<double>::quiet_NaN());

    juce::DynamicObject::Ptr node = new juce::DynamicObject();
    node->setProperty("id", 1);
    node->setProperty("type", "Oscillator");
    node->setProperty("params", juce::var(params.get()));

    juce::Array<juce::var> nodesArr;
    nodesArr.add(juce::var(node.get()));

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", nodesArr);
    root->setProperty("connections", juce::Array<juce::var>());

    {
        juce::AudioProcessorGraph graph;
        bool success = synth::AIStateMapper::applyJSONToGraph(juce::var(root.get()), graph, true);
        EXPECT_FALSE(success);
        EXPECT_EQ(graph.getNumNodes(), 0);
    }

    params->setProperty("fine", std::numeric_limits<double>::infinity());
    {
        juce::AudioProcessorGraph graph;
        bool success = synth::AIStateMapper::applyJSONToGraph(juce::var(root.get()), graph, true);
        EXPECT_FALSE(success);
        EXPECT_EQ(graph.getNumNodes(), 0);
    }
}

// Regression for a real Ollama grammar-decoder failure: the model's structured-output decoder
// corrupted the "waveform" key into the literal string `waveform": "Saw",` and mapped it to
// itself, so the real "waveform" key never appeared in the JSON at all. Before this test's fix,
// applyParamsToProcessor only ever looked up known paramIDs BY NAME, so the corrupted key was
// silently ignored, the Oscillator stayed at its constructed default, and the patch was reported
// as applied successfully. validateNodeParams must now catch the unmatched key and fail closed so
// the existing bounded-retry loop (AIIntegrationService::applyPatchWithRetry) engages instead.
TEST(AIStateMapperTest, RejectsUnknownParameterKeyFromGarbledOllamaOutput) {
    juce::AudioProcessorGraph graph;
    juce::DynamicObject::Ptr params = new juce::DynamicObject();
    params->setProperty("bypassed", false);
    params->setProperty(juce::Identifier("waveform\": \"Saw\","), juce::String("waveform\": \"Saw\","));
    params->setProperty("octave", 0.0);
    params->setProperty("coarse", 0.0);
    params->setProperty("fine", 0.0);
    params->setProperty("unison", 8.0);
    params->setProperty("detune", 20.0);

    juce::DynamicObject::Ptr node = new juce::DynamicObject();
    node->setProperty("id", 1);
    node->setProperty("type", "Oscillator");
    node->setProperty("params", juce::var(params.get()));

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", juce::Array<juce::var>({juce::var(node.get())}));
    root->setProperty("connections", juce::Array<juce::var>());

    auto validation =
        synth::AIStateMapper::validatePatch(juce::var(root.get()), graph, /*clearExisting=*/true, /*trusted=*/false);
    EXPECT_FALSE(validation.ok);
    EXPECT_EQ(validation.error, synth::PatchValidationError::UnknownParameterKey);

    EXPECT_FALSE(synth::AIStateMapper::applyJSONToGraph(juce::var(root.get()), graph, /*clearExisting=*/true,
                                                        /*trusted=*/false));
    EXPECT_EQ(graph.getNumNodes(), 0);
}

// Simpler unit case for the same rule: any params key that doesn't match a real paramID on the
// module — not just the specific corrupted shape above — must be rejected.
TEST(AIStateMapperTest, RejectsGenericUnknownParameterKey) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(
        R"({"nodes":[{"id":1,"type":"Oscillator","params":{"waveform":"Saw","bogusKey":1.0}}],"connections":[]})");

    auto validation = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/true, /*trusted=*/false);
    EXPECT_FALSE(validation.ok);
    EXPECT_EQ(validation.error, synth::PatchValidationError::UnknownParameterKey);
    // The message must name a real parameter ID, not just report that one was wrong — pinned so a
    // future edit can't quietly drop the list the retry loop needs to converge.
    EXPECT_TRUE(validation.message.contains("detune")) << validation.message;

    EXPECT_FALSE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/false));
    EXPECT_EQ(graph.getNumNodes(), 0);
}

// Negative control: a params object containing only real paramIDs must still validate OK, so the
// new check doesn't false-positive on legitimate patches.
TEST(AIStateMapperTest, ValidParamsObjectStillPassesValidation) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(
        R"({"nodes":[{"id":1,"type":"Oscillator","params":{"waveform":"Saw","octave":0.0,"coarse":0.0,)"
        R"("fine":0.0,"unison":8.0,"detune":20.0,"bypassed":false}}],"connections":[]})");

    auto validation = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/true, /*trusted=*/false);
    EXPECT_TRUE(validation.ok) << validation.message;

    EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/false));
    EXPECT_EQ(graph.getNumNodes(), 1);
}

TEST(AIStateMapperTest, RejectsNegativeAndOutOfRangePortIndices) {
    {
        juce::AudioProcessorGraph graph;
        juce::var json = juce::JSON::parse(R"({
            "nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Filter"}],
            "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":-5}]
        })");
        bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, true);
        EXPECT_FALSE(success);
        EXPECT_EQ(graph.getNumNodes(), 0);
    }
    {
        juce::AudioProcessorGraph graph;
        juce::var json = juce::JSON::parse(R"({
            "nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Filter"}],
            "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":99999}]
        })");
        bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, true);
        EXPECT_FALSE(success);
        EXPECT_EQ(graph.getNumNodes(), 0);
    }
}

TEST(AIStateMapperTest, RejectsConnectionToUnknownNodeId) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(R"({
        "nodes":[{"id":1,"type":"Oscillator"}],
        "connections":[{"src":1,"srcPort":0,"dst":9999,"dstPort":0}]
    })");

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, true);
    EXPECT_FALSE(success);
    EXPECT_EQ(graph.getNumNodes(), 0);
}

TEST(AIStateMapperTest, RejectsSelfConnectionCycle) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(R"({
        "nodes":[{"id":1,"type":"Filter"}],
        "connections":[{"src":1,"srcPort":0,"dst":1,"dstPort":0}]
    })");

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, true);
    EXPECT_FALSE(success);
    EXPECT_EQ(graph.getNumNodes(), 0);
}

TEST(AIStateMapperTest, ClampsOutOfRangeParameterValues) {
    juce::AudioProcessorGraph graph;
    juce::var json =
        juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Oscillator","params":{"fine":-500.0}}],"connections":[]})");

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, true);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph.getNumNodes(), 1);

    auto* osc = dynamic_cast<OscillatorModule*>(graph.getNodes().getUnchecked(0)->getProcessor());
    ASSERT_NE(osc, nullptr);
    bool foundFine = false;
    for (auto* param : osc->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "fine") {
                foundFine = true;
                // fine's range is -100..100; -500 should clamp to the minimum (normalized 0.0),
                // not be rejected outright.
                EXPECT_NEAR(p->getValue(), 0.0f, 0.001f);
            }
        }
    }
    EXPECT_TRUE(foundFine);
}

TEST(AIStateMapperTest, TrustedModeStillAcceptsExistingValidPresets) {
    auto presetNames = synth::PresetManager::getPresetNames();
    for (int i = 0; i < presetNames.size(); ++i) {
        juce::AudioProcessorGraph graph;
        EXPECT_TRUE(synth::PresetManager::loadPreset(i, graph))
            << "Preset \"" << presetNames[i] << "\" (index " << i << ") failed to load";
        EXPECT_GT(graph.getNumNodes(), 0) << "Preset \"" << presetNames[i] << "\" loaded with zero nodes";
    }
}

TEST(AIStateMapperTest, GraphIsUnchangedAfterRejectedPatch) {
    juce::AudioProcessorGraph graph;
    createBasicGraph(graph);

    juce::String beforeJson = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    int beforeNodes = graph.getNumNodes();
    int beforeConnections = (int)graph.getConnections().size();

    // Node 9001 is individually valid and appears first; node 9002 has an unknown type. If
    // validation ran interleaved with apply (rather than fully up front), 9001 would end up
    // added to the graph even though the overall patch is rejected.
    juce::var badJson = juce::JSON::parse(R"({
        "nodes":[
            {"id":9001,"type":"Oscillator"},
            {"id":9002,"type":"NotARealModuleType"}
        ],
        "connections":[{"src":9001,"srcPort":0,"dst":9002,"dstPort":0}]
    })");

    bool success = synth::AIStateMapper::applyJSONToGraph(badJson, graph, false);
    EXPECT_FALSE(success);

    EXPECT_EQ(graph.getNumNodes(), beforeNodes);
    EXPECT_EQ((int)graph.getConnections().size(), beforeConnections);
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), beforeJson);
}
