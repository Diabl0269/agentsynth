#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/PresetManager.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <limits>
#include <map>

// Helper function to create a basic graph for testing
static void createBasicGraph(juce::AudioProcessorGraph& graph) {
    graph.clear();

    auto audioInputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    auto audioOutputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    // Connect input to osc, osc to filter, filter to output
    graph.addConnection({{audioInputNode->nodeID, 0}, {oscNode->nodeID, 0}});
    graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}});
    graph.addConnection({{filterNode->nodeID, 0}, {audioOutputNode->nodeID, 0}});
}

TEST(AIStateMapperTest, GraphToJSONRoundTrip) {
    juce::AudioProcessorGraph originalGraph;
    createBasicGraph(originalGraph);

    // Save to JSON
    juce::var json = synth::AIStateMapper::graphToJSON(originalGraph);

    // Create a new graph and load from JSON
    juce::AudioProcessorGraph newGraph;
    bool success = synth::AIStateMapper::applyJSONToGraph(json, newGraph, true);

    ASSERT_TRUE(success);
    ASSERT_EQ(originalGraph.getNumNodes(), newGraph.getNumNodes());
    ASSERT_EQ(originalGraph.getConnections().size(), newGraph.getConnections().size());

    // Basic check: ensure node types match
    for (int i = 0; i < originalGraph.getNumNodes(); ++i) {
        auto originalNode = originalGraph.getNodes().getUnchecked(i);
        auto newNode = newGraph.getNodes().getUnchecked(i);
        ASSERT_EQ(originalNode->getProcessor()->getName(), newNode->getProcessor()->getName());
    }
}

TEST(AIStateMapperTest, ApplyJSONToGraphClearsExisting) {
    juce::AudioProcessorGraph graph;

    // JSON with a simple Filter and Audio Output
    juce::var json = juce::JSON::parse(R"({"nodes":[
        {"id":100,"type":"Filter","params":{"cutoff":0.5,"resonance":0.1}},
        {"id":101,"type":"Audio Output","params":{}}
    ],"connections":[]})");

    // Test 1: Apply patch, clearing existing (graph should be empty initially)
    bool success_clear = synth::AIStateMapper::applyJSONToGraph(json, graph, true);
    ASSERT_TRUE(success_clear);
    ASSERT_EQ(graph.getNumNodes(), 2); // Only nodes from JSON should exist

    // Clear the graph before the second test to ensure a clean state
    graph.clear();

    // Test 2: Apply patch, without clearing existing (should add to existing graph)
    graph.addNode(std::make_unique<OscillatorModule>()); // Add an initial node
    bool success_no_clear = synth::AIStateMapper::applyJSONToGraph(json, graph, false);
    ASSERT_TRUE(success_no_clear);
    ASSERT_EQ(graph.getNumNodes(), 3); // 1 (Oscillator) + 2 (from JSON) = 3
}

TEST(AIStateMapperTest, InvalidJSONReturnsFalse) {
    juce::AudioProcessorGraph graph;

    // Not an object
    juce::var invalidJson1 = juce::JSON::parse(R"("not an object")");
    ASSERT_FALSE(synth::AIStateMapper::applyJSONToGraph(invalidJson1, graph));

    // Missing "nodes"
    juce::var invalidJson2 = juce::JSON::parse(R"({"connections":[]})");
    ASSERT_FALSE(synth::AIStateMapper::applyJSONToGraph(invalidJson2, graph));

    // "nodes" is not an array
    juce::var invalidJson3 = juce::JSON::parse(R"({"nodes":"not an array"})");
    ASSERT_FALSE(synth::AIStateMapper::applyJSONToGraph(invalidJson3, graph));
}

TEST(AIStateMapperTest, ParameterValidationClamping) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(
        R"({"nodes":[{"id":1,"type":"Oscillator","params":{"waveform":0,"fine":200.0}}],"connections":[]})"); // fine >
                                                                                                              // 100.0

    synth::AIStateMapper::applyJSONToGraph(json, graph, true);

    auto oscNode = graph.getNodes().getUnchecked(0);
    ASSERT_NE(oscNode, nullptr);
    auto oscProcessor = dynamic_cast<OscillatorModule*>(oscNode->getProcessor());
    ASSERT_NE(oscProcessor, nullptr);

    float fineParamValue = -1.0f;
    for (auto* param : oscProcessor->getParameters()) {
        if (auto* floatParam = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (floatParam->paramID == "fine") {
                fineParamValue = floatParam->getValue();
                break;
            }
        }
    }
    ASSERT_NE(fineParamValue, -1.0f); // Ensure fine parameter was found

    // Parameter value should be clamped between 0.0 and 1.0
    ASSERT_NEAR(fineParamValue, 1.0f, 0.001f); // Should be clamped to 1.0 (max of range)
}

TEST(AIStateMapperTest, UnknownModuleTypeLogsErrorAndSkips) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(R"({"nodes":[{"id":1,"type":"UnknownModule"}],"connections":[]})");

    // Capture logs to ensure error is logged
    class LogCatcher : public juce::Logger {
    public:
        LogCatcher() { juce::Logger::setCurrentLogger(this); }
        ~LogCatcher() override { juce::Logger::setCurrentLogger(nullptr); }
        void logMessage(const juce::String& message) override { lastMessage = message; }
        juce::String lastMessage;
    };
    LogCatcher logger;

    // Trusted mode (e.g. loading the user's own saved preset) keeps the legacy permissive
    // behavior of skipping an unresolvable node rather than rejecting the whole patch. See
    // RejectsUnknownModuleType below for the strict/untrusted behavior.
    bool success_unknown_module = synth::AIStateMapper::applyJSONToGraph(json, graph, true, /*trusted=*/true);
    ASSERT_TRUE(success_unknown_module); // Should still return true if valid JSON, just skips the node
    ASSERT_EQ(graph.getNumNodes(), 0);   // Unknown module should not be added
    ASSERT_TRUE(logger.lastMessage.contains("Unknown module type"));
}

TEST(AIStateMapperTest, ChoiceParameterStringMapping) {
    juce::AudioProcessorGraph graph;
    // Oscillator waveform choice: 0: Sine, 1: Square, 2: Saw, 3: Triangle
    juce::var json =
        juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Oscillator","params":{"waveform":"Saw"}}],"connections":[]})");

    synth::AIStateMapper::applyJSONToGraph(json, graph, true);

    auto oscNode = graph.getNodes().getUnchecked(0);
    auto* osc = dynamic_cast<juce::AudioProcessor*>(oscNode->getProcessor());
    auto* waveformParam = dynamic_cast<juce::AudioParameterChoice*>(osc->getParameters().getUnchecked(1));

    ASSERT_NE(waveformParam, nullptr);
    ASSERT_EQ(waveformParam->getIndex(), 2); // "Saw" is index 2
}

TEST(AIStateMapperTest, ChoiceParameterCaseInsensitiveMapping) {
    juce::AudioProcessorGraph graph;
    // Test case-insensitivity: "sawtooth" instead of "Saw" (if applicable) or just "saw"
    juce::var json =
        juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Oscillator","params":{"waveform":"saw"}}],"connections":[]})");

    synth::AIStateMapper::applyJSONToGraph(json, graph, true);

    auto oscNode = graph.getNodes().getUnchecked(0);
    auto* osc = dynamic_cast<juce::AudioProcessor*>(oscNode->getProcessor());
    auto* waveformParam = dynamic_cast<juce::AudioParameterChoice*>(osc->getParameters().getUnchecked(1));

    ASSERT_NE(waveformParam, nullptr);
    ASSERT_EQ(waveformParam->getIndex(), 2); // "saw" matches "Saw"
}

TEST(AIStateMapperTest, SchemaGeneration) {
    juce::String schema = synth::AIStateMapper::getModuleSchema();
    ASSERT_FALSE(schema.isEmpty());
    ASSERT_TRUE(schema.contains("Oscillator"));
    ASSERT_TRUE(schema.contains("Filter"));
    ASSERT_TRUE(schema.contains("waveform"));
    ASSERT_TRUE(schema.contains("cutoff"));
}

TEST(AIStateMapperTest, MergeMode_PrePopulatesIdMapForCrossConnections) {
    juce::AudioProcessorGraph graph;

    // Add an existing VCA node (regular module with proper channel config)
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    ASSERT_NE(vcaNode, nullptr);
    int existingVcaId = (int)vcaNode->nodeID.uid;

    // Delta JSON: add an Oscillator and connect it to the existing VCA
    juce::String jsonStr = "{\"nodes\":[{\"id\":9001,\"type\":\"Oscillator\",\"params\":{\"frequency\":440.0}}],"
                           "\"connections\":[{\"src\":9001,\"srcPort\":0,\"dst\":" +
                           juce::String(existingVcaId) + ",\"dstPort\":0}]}";
    juce::var json = juce::JSON::parse(jsonStr);

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, false);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph.getNumNodes(), 2);           // 1 existing + 1 new
    ASSERT_EQ(graph.getConnections().size(), 1); // Cross-connection should exist
}

TEST(AIStateMapperTest, MergeMode_RemoveNodes) {
    juce::AudioProcessorGraph graph;

    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());

    // Connect osc -> filter -> vca
    graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}});
    graph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}});

    ASSERT_EQ(graph.getNumNodes(), 3);
    ASSERT_EQ(graph.getConnections().size(), 2);

    // Remove the filter node
    int filterNodeId = (int)filterNode->nodeID.uid;
    juce::String jsonStr = "{\"remove\":[" + juce::String(filterNodeId) + "],\"nodes\":[],\"connections\":[]}";
    juce::var json = juce::JSON::parse(jsonStr);

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, false);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph.getNumNodes(), 2);           // Filter removed
    ASSERT_EQ(graph.getConnections().size(), 0); // Connections involving filter removed by JUCE
}

TEST(AIStateMapperTest, MergeMode_UpdateExistingNodeParams) {
    juce::AudioProcessorGraph graph;

    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(oscNode, nullptr);
    int oscId = (int)oscNode->nodeID.uid;

    // Delta JSON: update frequency on existing oscillator (same ID, same type)
    juce::String jsonStr = "{\"nodes\":[{\"id\":" + juce::String(oscId) +
                           ",\"type\":\"Oscillator\",\"params\":{\"frequency\":880.0}}],\"connections\":[]}";
    juce::var json = juce::JSON::parse(jsonStr);

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, false);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph.getNumNodes(), 1); // No new node created, existing one updated

    // Verify parameter was updated
    auto* processor = oscNode->getProcessor();
    for (auto* param : processor->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "frequency") {
                float denormalized =
                    dynamic_cast<juce::RangedAudioParameter*>(param)->getNormalisableRange().convertFrom0to1(
                        param->getValue());
                ASSERT_NEAR(denormalized, 880.0f, 1.0f);
                break;
            }
        }
    }
}

TEST(AIStateMapperTest, FactorySupportsAllModuleTypes) {
    // Verify all expected module types can be created
    juce::StringArray expectedTypes = {
        "Audio Input",   "Audio Output",   "Midi Input",    "Oscillator", "Filter",
        "VCA",           "ADSR",           "Sequencer",     "LFO",        "Distortion",
        "Delay",         "Reverb",         "MIDI Keyboard", "Amp Env",    "Filter Env",
        "Poly MIDI",     "Poly Sequencer", "Attenuverter",  "Chorus",     "Phaser",
        "Compressor",    "Flanger",        "Limiter",       "Bitcrusher", "Pitch Shifter",
        "Parametric EQ", "Macros",         "Sample & Hold", "Math"};
    for (const auto& type : expectedTypes) {
        auto module = synth::AIStateMapper::createModule(type);
        EXPECT_NE(module, nullptr) << "Failed to create module: " << type.toStdString();
    }
}

TEST(AIStateMapperTest, MidiConnectionsSerialized) {
    juce::AudioProcessorGraph graph;
    auto midiIn = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
    auto oscModule = synth::AIStateMapper::createModule("Oscillator");
    auto oscNode = graph.addNode(std::move(oscModule));

    graph.addConnection({{midiIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                         {oscNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    auto json = synth::AIStateMapper::graphToJSON(graph);

    // Verify MIDI ports are serialized as -1
    auto* connections = json.getDynamicObject()->getProperty("connections").getArray();
    ASSERT_NE(connections, nullptr);
    ASSERT_GT(connections->size(), 0);
    auto* conn = (*connections)[0].getDynamicObject();
    EXPECT_EQ((int)conn->getProperty("srcPort"), -1);
    EXPECT_EQ((int)conn->getProperty("dstPort"), -1);
}

TEST(AIStateMapperTest, ParameterValuesAreUnnormalized) {
    // Create a graph with an oscillator, set a param to a known denormalized value
    juce::AudioProcessorGraph graph;
    auto osc = std::make_unique<OscillatorModule>();
    // Set fine tuning to 50.0 (range is -100 to 100)
    for (auto* param : osc->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "fine") {
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                ranged->setValueNotifyingHost(ranged->getNormalisableRange().convertTo0to1(50.0f));
            }
        }
    }
    auto node = graph.addNode(std::move(osc));

    auto json = synth::AIStateMapper::graphToJSON(graph);

    // Check the serialized value is denormalized (50.0, not 0.75)
    auto* nodes = json.getDynamicObject()->getProperty("nodes").getArray();
    ASSERT_NE(nodes, nullptr);
    auto* nodeObj = (*nodes)[0].getDynamicObject();
    auto* params = nodeObj->getProperty("params").getDynamicObject();
    float fineValue = (float)params->getProperty("fine");
    EXPECT_NEAR(fineValue, 50.0f, 1.0f);
}

TEST(AIStateMapperTest, RoundTripPreservesParameters) {
    // Build a graph, serialize, deserialize, check parameters match
    juce::AudioProcessorGraph originalGraph;
    auto osc = std::make_unique<OscillatorModule>();
    for (auto* param : osc->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "fine") {
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                ranged->setValueNotifyingHost(ranged->getNormalisableRange().convertTo0to1(25.0f));
            }
        }
    }
    originalGraph.addNode(std::move(osc));

    // Round trip
    auto json = synth::AIStateMapper::graphToJSON(originalGraph);
    juce::AudioProcessorGraph newGraph;
    synth::AIStateMapper::applyJSONToGraph(json, newGraph, true);

    // Check parameter value survived
    ASSERT_EQ(newGraph.getNumNodes(), 1);
    auto* newOsc = newGraph.getNodes().getUnchecked(0)->getProcessor();
    for (auto* param : newOsc->getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "fine") {
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                float value = ranged->getNormalisableRange().convertFrom0to1(ranged->getValue());
                EXPECT_NEAR(value, 25.0f, 1.0f);
            }
        }
    }
}

// A merge patch that reuses a live node's id for a different module is an identity collision: the
// old behaviour created a second node and rebound idMap[id] to it, so every later reference in the
// same patch that meant the original node silently re-pointed at the new one. Untrusted input is
// now refused whole.
TEST(AIStateMapperTest, MergeMode_TypeMismatchIsRejectedUntrusted) {
    juce::AudioProcessorGraph graph;

    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(oscNode, nullptr);
    int oscId = (int)oscNode->nodeID.uid;

    // Delta JSON: same ID but different type.
    juce::String jsonStr = "{\"nodes\":[{\"id\":" + juce::String(oscId) +
                           ",\"type\":\"Filter\",\"params\":{\"cutoff\":1000.0}}],\"connections\":[]}";
    juce::var json = juce::JSON::parse(jsonStr);

    auto validation = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/false, /*trusted=*/false);
    EXPECT_FALSE(validation.ok);
    EXPECT_EQ(validation.error, synth::PatchValidationError::NodeIdTypeMismatch);

    EXPECT_FALSE(synth::AIStateMapper::applyJSONToGraph(json, graph, false));
    EXPECT_EQ(graph.getNumNodes(), 1); // nothing applied — the original Oscillator, untouched
    EXPECT_EQ(graph.getNodes().getUnchecked(0)->getProcessor()->getName(), "Oscillator");
}

// The trusted path (preset load, undo replay) skips the strict checks by design, so the same JSON
// still creates a second node there. Pinned so the trust boundary stays where it is documented.
TEST(AIStateMapperTest, MergeMode_TypeMismatchStillCreatesNewNodeWhenTrusted) {
    juce::AudioProcessorGraph graph;

    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(oscNode, nullptr);
    int oscId = (int)oscNode->nodeID.uid;

    juce::String jsonStr = "{\"nodes\":[{\"id\":" + juce::String(oscId) +
                           ",\"type\":\"Filter\",\"params\":{\"cutoff\":1000.0}}],\"connections\":[]}";
    juce::var json = juce::JSON::parse(jsonStr);

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/false, /*trusted=*/true));
    EXPECT_EQ(graph.getNumNodes(), 2); // Original Oscillator + new Filter
}

// The counterpart: an id collision with a MATCHING type is an edit, not a collision, and must
// still update the live node in place rather than adding a duplicate.
TEST(AIStateMapperTest, MergeMode_SameIdSameTypeStillUpdatesInPlace) {
    juce::AudioProcessorGraph graph;

    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_NE(filterNode, nullptr);
    int filterId = (int)filterNode->nodeID.uid;

    juce::String jsonStr = "{\"nodes\":[{\"id\":" + juce::String(filterId) +
                           ",\"type\":\"Filter\",\"params\":{\"cutoff\":1200.0}}],\"connections\":[]}";
    juce::var json = juce::JSON::parse(jsonStr);

    auto validation = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/false, /*trusted=*/false);
    EXPECT_TRUE(validation.ok) << validation.message;

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, false));
    ASSERT_EQ(graph.getNumNodes(), 1);
    EXPECT_EQ(graph.getNodes().getUnchecked(0)->nodeID, filterNode->nodeID);

    auto* cutoff = findParameterByID(graph.getNodes().getUnchecked(0)->getProcessor(), "cutoff");
    ASSERT_NE(cutoff, nullptr);
    EXPECT_NEAR(cutoff->getNormalisableRange().convertFrom0to1(cutoff->getValue()), 1200.0f, 1.0f);
}

// Removing a node and re-declaring its id as a different module is legal: apply processes "remove"
// first, so the re-declared id names a genuinely new node and aliases nothing.
TEST(AIStateMapperTest, MergeMode_RemovedIdMayBeReusedForAnotherType) {
    juce::AudioProcessorGraph graph;

    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(oscNode, nullptr);
    int oscId = (int)oscNode->nodeID.uid;

    juce::String jsonStr = "{\"remove\":[" + juce::String(oscId) + "],\"nodes\":[{\"id\":" + juce::String(oscId) +
                           ",\"type\":\"Filter\"}],\"connections\":[]}";
    juce::var json = juce::JSON::parse(jsonStr);

    auto validation = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/false, /*trusted=*/false);
    EXPECT_TRUE(validation.ok) << validation.message;

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, false));
    ASSERT_EQ(graph.getNumNodes(), 1);
    EXPECT_EQ(graph.getNodes().getUnchecked(0)->getProcessor()->getName(), "Filter");
}

TEST(AIStateMapperTest, ValidateJSON_AllowsRemoveOnly) {
    juce::AudioProcessorGraph graph;
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    int oscId = (int)oscNode->nodeID.uid;

    // JSON with "remove" but no "nodes"
    juce::String jsonStr = "{\"remove\":[" + juce::String(oscId) + "]}";
    juce::var json = juce::JSON::parse(jsonStr);

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, false);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph.getNumNodes(), 0);
}

TEST(AIStateMapperTest, Modulation_GraphToJSON_SerializesModulations) {
    juce::AudioProcessorGraph graph;

    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    // Create attenuverter-based modulation: LFO -> Attenuverter -> Filter cutoff (channel 1)
    auto attenNode = graph.addNode(std::make_unique<AttenuverterModule>());
    // Set amount to 0.7
    if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(attenNode->getProcessor()->getParameters()[1]))
        param->setValueNotifyingHost(param->convertTo0to1(0.7f));

    graph.addConnection({{lfoNode->nodeID, 0}, {attenNode->nodeID, 0}});
    graph.addConnection({{attenNode->nodeID, 0}, {filterNode->nodeID, 1}});

    auto json = synth::AIStateMapper::graphToJSON(graph);

    // Verify modulations array exists and has one entry
    auto* rootObj = json.getDynamicObject();
    ASSERT_TRUE(rootObj->hasProperty("modulations"));
    auto* modArr = rootObj->getProperty("modulations").getArray();
    ASSERT_NE(modArr, nullptr);
    ASSERT_EQ(modArr->size(), 1);

    auto* mod = (*modArr)[0].getDynamicObject();
    EXPECT_EQ((int)mod->getProperty("source"), (int)lfoNode->nodeID.uid);
    EXPECT_EQ((int)mod->getProperty("sourcePort"), 0);
    EXPECT_EQ((int)mod->getProperty("dest"), (int)filterNode->nodeID.uid);
    EXPECT_EQ((int)mod->getProperty("destPort"), 1);
    EXPECT_NEAR((float)mod->getProperty("amount"), 0.7f, 0.05f);
    EXPECT_EQ((bool)mod->getProperty("bypass"), false);
}

TEST(AIStateMapperTest, Modulation_ApplyJSON_CreatesModulationChain) {
    juce::AudioProcessorGraph graph;

    juce::var json = juce::JSON::parse(R"({
        "nodes": [
            {"id": 1, "type": "LFO", "params": {"rateHz": 2.0}},
            {"id": 2, "type": "Filter", "params": {"cutoff": 1000.0}}
        ],
        "connections": [],
        "modulations": [
            {"source": 1, "dest": 2, "destPort": 1, "amount": 0.5}
        ]
    })");

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, true);
    ASSERT_TRUE(success);

    // Should have 3 nodes: LFO, Filter, and auto-created Attenuverter
    ASSERT_EQ(graph.getNumNodes(), 3);

    // Find the attenuverter node
    juce::AudioProcessorGraph::Node* attenNode = nullptr;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr) {
            attenNode = node;
            break;
        }
    }
    ASSERT_NE(attenNode, nullptr);

    // Verify amount parameter is set to 0.5
    auto* amountParam = dynamic_cast<juce::RangedAudioParameter*>(attenNode->getProcessor()->getParameters()[1]);
    ASSERT_NE(amountParam, nullptr);
    float amount = amountParam->getNormalisableRange().convertFrom0to1(amountParam->getValue());
    EXPECT_NEAR(amount, 0.5f, 0.05f);

    // Verify connections exist: source->atten and atten->dest
    bool hasSourceToAtten = false;
    bool hasAttenToDest = false;
    for (const auto& conn : graph.getConnections()) {
        if (conn.destination.nodeID == attenNode->nodeID && conn.destination.channelIndex == 0)
            hasSourceToAtten = true;
        if (conn.source.nodeID == attenNode->nodeID && conn.destination.channelIndex == 1)
            hasAttenToDest = true;
    }
    EXPECT_TRUE(hasSourceToAtten);
    EXPECT_TRUE(hasAttenToDest);
}

TEST(AIStateMapperTest, Modulation_RemoveModulations) {
    juce::AudioProcessorGraph graph;

    // First, create a graph with a modulation
    juce::var setupJson = juce::JSON::parse(R"({
        "nodes": [
            {"id": 1, "type": "LFO"},
            {"id": 2, "type": "Filter"}
        ],
        "connections": [],
        "modulations": [
            {"source": 1, "dest": 2, "destPort": 1, "amount": 0.8}
        ]
    })");
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(setupJson, graph, true));
    ASSERT_EQ(graph.getNumNodes(), 3); // LFO + Filter + Attenuverter

    // Now remove the modulation in merge mode
    // We need the mapped IDs - find LFO and Filter node IDs
    juce::AudioProcessorGraph::NodeID lfoId, filterId;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<LFOModule*>(node->getProcessor()))
            lfoId = node->nodeID;
        if (dynamic_cast<FilterModule*>(node->getProcessor()))
            filterId = node->nodeID;
    }

    juce::String removeJson = "{\"removeModulations\": [{\"source\": " + juce::String((int)lfoId.uid) +
                              ", \"dest\": " + juce::String((int)filterId.uid) +
                              ", \"destPort\": 1}], \"nodes\": [], \"connections\": []}";

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(removeJson), graph, false));

    // Attenuverter should be removed, only LFO and Filter remain
    ASSERT_EQ(graph.getNumNodes(), 2);

    // No attenuverter should exist
    for (auto* node : graph.getNodes()) {
        EXPECT_EQ(dynamic_cast<AttenuverterModule*>(node->getProcessor()), nullptr);
    }
}

TEST(AIStateMapperTest, Modulation_MergeMode_AddModulation) {
    juce::AudioProcessorGraph graph;

    // Create initial graph with LFO and Filter (no modulation)
    auto lfoNode = graph.addNode(std::make_unique<LFOModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());

    int lfoId = (int)lfoNode->nodeID.uid;
    int filterId = (int)filterNode->nodeID.uid;

    ASSERT_EQ(graph.getNumNodes(), 2);

    // Merge: add modulation between existing nodes
    juce::String jsonStr = "{\"nodes\": [], \"connections\": [], \"modulations\": ["
                           "{\"source\": " +
                           juce::String(lfoId) + ", \"dest\": " + juce::String(filterId) +
                           ", \"destPort\": 1, \"amount\": 0.6}]}";

    bool success = synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(jsonStr), graph, false);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph.getNumNodes(), 3); // LFO + Filter + new Attenuverter
}

TEST(AIStateMapperTest, Modulation_SchemaIncludesModulationTargets) {
    juce::String schema = synth::AIStateMapper::getModuleSchema();
    ASSERT_TRUE(schema.contains("Modulation Targets"));
    ASSERT_TRUE(schema.contains("Cutoff"));
    ASSERT_TRUE(schema.contains("Resonance"));
    ASSERT_TRUE(schema.contains("Modulation Sources"));
}

TEST(AIStateMapperTest, Modulation_DefaultAmount) {
    juce::AudioProcessorGraph graph;

    // No "amount" field — should default to 1.0
    juce::var json = juce::JSON::parse(R"({
        "nodes": [
            {"id": 1, "type": "LFO"},
            {"id": 2, "type": "VCA"}
        ],
        "connections": [],
        "modulations": [
            {"source": 1, "dest": 2, "destPort": 1}
        ]
    })");

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, true));

    // Find attenuverter and check amount is 1.0
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor())) {
            auto* amountParam = dynamic_cast<juce::RangedAudioParameter*>(node->getProcessor()->getParameters()[1]);
            float amount = amountParam->getNormalisableRange().convertFrom0to1(amountParam->getValue());
            EXPECT_NEAR(amount, 1.0f, 0.05f);
            return;
        }
    }
    FAIL() << "No attenuverter node found";
}

TEST(AIStateMapperTest, Modulation_UnconnectedAttenuverterNotSerialized) {
    juce::AudioProcessorGraph graph;

    // Add a standalone attenuverter with no connections
    graph.addNode(std::make_unique<AttenuverterModule>());

    auto json = synth::AIStateMapper::graphToJSON(graph);

    auto* rootObj = json.getDynamicObject();
    ASSERT_TRUE(rootObj->hasProperty("modulations"));
    auto* modArr = rootObj->getProperty("modulations").getArray();
    ASSERT_NE(modArr, nullptr);
    EXPECT_EQ(modArr->size(), 0); // Unconnected attenuverter should NOT appear
}

// Regression test: AIStateMapper::applyJSONToGraph must NOT spam the logger
// (one line per parameter per module) on preset load.
//
// Before the fix, AIChatComponent's juce::Logger implementation piped every
// log line into an O(n^2) TextEditor append, causing multi-second UI freezes
// on preset load.  A healthy load should produce at most a handful of
// error/diagnostic lines — never one per parameter.
TEST(AIStateMapperTest, PresetLoadDoesNotSpamLogger) {
    struct CountingLogger : juce::Logger {
        std::atomic<int> count{0};
        void logMessage(const juce::String&) override { ++count; }
    };

    CountingLogger cl;
    juce::Logger::setCurrentLogger(&cl);

    // Load three representative presets (Default=0, Modulated Bass=3, Poly Pad=6)
    // into fresh graphs so applyJSONToGraph is exercised in full each time.
    {
        juce::AudioProcessorGraph g0;
        synth::PresetManager::loadPreset(0, g0);
    }
    {
        juce::AudioProcessorGraph g3;
        synth::PresetManager::loadPreset(3, g3);
    }
    {
        juce::AudioProcessorGraph g6;
        synth::PresetManager::loadPreset(6, g6);
    }

    // Capture count and reset logger BEFORE assertions so the logger is never
    // left pointing at a destroyed object even if an EXPECT fires.
    const int logCount = cl.count.load();
    juce::Logger::setCurrentLogger(nullptr);

    std::cout << "[PresetLoadDoesNotSpamLogger] logger calls across 3 presets: " << logCount << "\n";

    // A healthy run emits ~0 lines for valid presets.  20 gives ample margin
    // for any genuine one-off diagnostics without allowing per-parameter spam
    // (which would be 100+ for a typical multi-module preset).
    EXPECT_LT(logCount, 20) << "applyJSONToGraph is spamming the logger (" << logCount
                            << " lines for 3 presets). This causes UI freezes in AIChatComponent.";
}

// ---------------------------------------------------------------------------------------------
// Strict/untrusted patch validation (default mode) — hardening against a patch source that
// isn't just a trusted local Ollama: a remote server, or a local model emitting garbage under
// load. Every test below relies on applyJSONToGraph's default trusted=false.
// ---------------------------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------------------
// Merge-mode auto-connect (the `autoConnectNewNodes` parameter)
// ---------------------------------------------------------------------------------------

namespace {

// Builds a minimal patch with a usable Audio Output to merge into.
// setPlayConfigDetails MUST come first: the Audio Output node's input channel count derives from
// the graph's output channel count, and on an unconfigured graph it is zero — every addConnection
// into it then fails silently.
juce::AudioProcessorGraph::NodeID addAudioOutput(juce::AudioProcessorGraph& graph) {
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto node = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    return node->nodeID;
}

bool hasConnectionTo(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID dest,
                     const juce::String& sourceTypeName) {
    for (const auto& conn : graph.getConnections()) {
        if (conn.destination.nodeID != dest)
            continue;
        if (auto* src = graph.getNodeForId(conn.source.nodeID))
            if (src->getProcessor()->getName() == sourceTypeName)
                return true;
    }
    return false;
}

} // namespace

TEST(AIStateMapperTest, MergeAutoConnectsNewAudioNodesToOutputByDefault) {
    // This is a deliberate affordance for AI-authored merge patches: a model that adds an
    // Oscillator to an existing patch means for it to be audible. Locked here because snippet
    // insertion opts OUT of it, and the two behaviours must not drift into each other.
    juce::AudioProcessorGraph graph;
    auto out = addAudioOutput(graph);

    juce::var patch = juce::JSON::parse(R"({"nodes":[{"id":4001,"type":"Oscillator"}]})");
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, graph, /*clearExisting=*/false));

    EXPECT_TRUE(hasConnectionTo(graph, out, "Oscillator"))
        << "an AI merge patch's new audio node should reach the output";
}

TEST(AIStateMapperTest, MergeSkipsAutoConnectWhenTheCallerOptsOut) {
    juce::AudioProcessorGraph graph;
    auto out = addAudioOutput(graph);

    juce::var patch = juce::JSON::parse(R"({"nodes":[{"id":4002,"type":"Oscillator"}]})");
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(patch, graph, /*clearExisting=*/false, /*trusted=*/false,
                                                       /*autoConnectNewNodes=*/false));

    EXPECT_EQ(graph.getNumNodes(), 2) << "the node is still added";
    EXPECT_FALSE(hasConnectionTo(graph, out, "Oscillator"))
        << "opting out must leave the new node exactly as the patch described it";
}

// ---------------------------------------------------------------------------------------------
// TL0 — patch format: type-name fidelity, paramID stability, schemaVersion + node uuid,
// the authorable-module allowlist, and the reserved "timeline" key.
// ---------------------------------------------------------------------------------------------

namespace {

// The only factory key that cannot round-trip, and why: "Mod Slot" is a SECOND registration of
// AttenuverterModule under the name the modulation UI uses. The two produce an identical
// processor with nothing on it to tell them apart, so a Mod Slot necessarily serializes as
// "Attenuverter" — and, since applyJSONToGraph builds attenuverter chains itself from the
// "modulations" array, nothing is lost by that.
//
// Nothing else belongs here. A new entry means some module's saved type string does not name a
// module the factory can rebuild, which is the class of bug that silently turned every saved Poly
// Sequencer into a mono Sequencer (issue #196).
const std::map<juce::String, juce::String> kFactoryTypeNameAliases = {{"Mod Slot", "Attenuverter"}};

juce::String sortedParamIds(juce::AudioProcessor& processor) {
    juce::StringArray ids;
    for (auto* param : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            ids.add(withId->paramID);
    ids.sort(false);
    return ids.joinIntoString(", ");
}

juce::var nodeWithId(const juce::var& patch, int id) {
    if (auto* nodes = patch.getDynamicObject()->getProperty("nodes").getArray())
        for (const auto& n : *nodes)
            if ((int)n.getDynamicObject()->getProperty("id") == id)
                return n;
    return {};
}

// Navigates to schema.properties.nodes.items.properties. The schema var must outlive the returned
// pointer — every step here borrows from the object the caller is holding.
juce::DynamicObject* schemaNodeProperties(const juce::var& schema) {
    return schema.getDynamicObject()
        ->getProperty("properties")
        .getDynamicObject()
        ->getProperty("nodes")
        .getDynamicObject()
        ->getProperty("items")
        .getDynamicObject()
        ->getProperty("properties")
        .getDynamicObject();
}

juce::StringArray uuidsOf(const juce::var& patch) {
    juce::StringArray uuids;
    if (auto* nodes = patch.getDynamicObject()->getProperty("nodes").getArray())
        for (const auto& n : *nodes)
            uuids.add(n.getDynamicObject()->getProperty("uuid").toString());
    uuids.sort(false);
    return uuids;
}

} // namespace

// Every module the factory can build must serialize under the key that rebuilds it. graphToJSON
// writes getFactoryTypeName() and applyJSONToGraph feeds that string straight back to
// createModule, so any mismatch is silent data loss on every save/load AND on every structural
// undo (which replays the same JSON) — exactly what ModuleType::PolySequencer → "Sequencer" did.
TEST(AIStateMapperTest, FactoryTypeNamesRoundTrip) {
    for (const auto& key : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto module = synth::AIStateMapper::createModule(key);
        ASSERT_NE(module, nullptr) << "factory key \"" << key << "\" produced nothing";

        const juce::String serialized = synth::AIStateMapper::getFactoryTypeName(module.get());
        const auto alias = kFactoryTypeNameAliases.find(key);
        const juce::String expected = alias == kFactoryTypeNameAliases.end() ? key : alias->second;

        EXPECT_EQ(serialized, expected)
            << "createModule(\"" << key << "\") serializes as \"" << serialized
            << "\" — a saved patch would rebuild it as that instead. Fix getFactoryTypeName, or "
               "record the alias in kFactoryTypeNameAliases if the two really are one module.";

        // The serialized name must itself be rebuildable, or the node vanishes on load.
        EXPECT_NE(synth::AIStateMapper::createModule(serialized), nullptr)
            << "\"" << serialized << "\" is not resolvable by createModule";
    }
}

// The regression that motivated the fix: a Poly Sequencer used to come back as a mono Sequencer
// from every save/load and every undo (issue #196).
TEST(AIStateMapperTest, PolySequencerSurvivesRoundTrip) {
    juce::AudioProcessorGraph graph;
    ASSERT_NE(graph.addNode(synth::AIStateMapper::createModule("Poly Sequencer")), nullptr);

    juce::var json = synth::AIStateMapper::graphToJSON(graph);
    EXPECT_EQ(nodeWithId(json, (int)graph.getNodes().getUnchecked(0)->nodeID.uid)
                  .getDynamicObject()
                  ->getProperty("type")
                  .toString(),
              "Poly Sequencer");

    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, reloaded, /*clearExisting=*/true, /*trusted=*/true));
    ASSERT_EQ(reloaded.getNumNodes(), 1);
    EXPECT_EQ(reloaded.getNodes().getUnchecked(0)->getProcessor()->getName(), "Poly Sequencer");
}

// Golden: the exact paramIDs every factory module exposes.
//
// paramIDs are this app's real parameter ABI — presets, undo snapshots, AI patches and (next)
// automation lanes all address parameters by these strings, and nothing else pins them. Renaming
// one compiles cleanly and silently drops that value from every patch already saved with it.
//
// TO UPDATE: read the failure, which prints the actual line in pasteable form, and replace the
// matching row below — but only once you have confirmed the change is intentional and that any
// RENAME ships with migration for existing presets. Adding a module adds a row; adding or
// removing a parameter edits one row.
TEST(AIStateMapperTest, ParamIdsGolden) {
    const std::map<juce::String, juce::String> golden = {
        {"ADSR", "attack, bypassed, decay, muted, poly, release, sustain"},
        {"Amp Env", "attack, bypassed, decay, muted, poly, release, sustain"},
        {"Attenuverter", "amount, bypassed"},
        {"Audio Input", ""},
        {"Audio Output", ""},
        {"Bitcrusher", "bypassed, depth, dither, mix, muted, outputLevel, rate"},
        {"Chorus", "bypassed, centreDelay, depth, feedback, mix, muted, outputLevel, rate"},
        {"Compressor", "attack, bypassed, makeupGain, muted, ratio, release, threshold"},
        {"Delay", "bypassed, feedback, mix, muted, outputLevel, time"},
        {"Distortion", "bypassed, drive, mix, muted, outputLevel, oversampling, type"},
        {"Envelope Follower", "attack, bypassed, detection, muted, release, sensitivity"},
        {"External MIDI", "bypassed, channel, deviceIndex"},
        {"Filter", "bypassed, cutoff, drive, filterType, muted, outputLevel, poly, resonance"},
        {"Filter Env", "attack, bypassed, decay, muted, poly, release, sustain"},
        {"Flanger", "bypassed, centreDelay, depth, feedback, mix, muted, outputLevel, rate"},
        {"LFO", "bipolar, bypassed, glide, level, mode, muted, rateHz, rateSync, retrig, shape"},
        {"Limiter", "bypassed, inputGain, muted, release, threshold"},
        {"MIDI Keyboard", "bypassed, octave"},
        {"Macros", "bypassed, macro1, macro10, macro11, macro12, macro13, macro14, macro15, macro16, macro2, macro3, "
                   "macro4, macro5, macro6, macro7, macro8, macro9, macroBipolar, macroCount, muted"},
        {"Math", "bypassed, clip, muted"},
        {"Midi Input", ""},
        {"Mod Slot", "amount, bypassed"},
        {"Noise", "bypassed, color, level, muted, noiseType, poly"},
        {"Oscillator", "bypassed, coarse, detune, fine, level, muted, octave, poly, unison, waveform"},
        {"Parametric EQ", "band1Freq, band1Gain, band1On, band1Q, band2Freq, band2Gain, band2On, band2Q, band3Freq, "
                          "band3Gain, band3On, band3Q, band4Freq, band4Gain, band4On, band4Q, bypassed, muted, "
                          "outputGain"},
        {"Phaser", "bypassed, centreFreq, depth, feedback, mix, muted, outputLevel, rate"},
        {"Pitch Shifter", "bypassed, feedback, fine, mix, muted, outputLevel, pitch, shiftHz, shiftMode, window"},
        {"Poly MIDI", "bypassed, velToGate, voiceSteal"},
        {"Poly Sequencer", "Gate 1, Gate 2, Gate 3, Gate 4, Gate 5, Gate 6, Gate 7, Gate 8, Step 1 Chord, Step 1 Root, "
                           "Step 2 Chord, Step 2 Root, Step 3 Chord, Step 3 Root, Step 4 Chord, Step 4 Root, "
                           "Step 5 Chord, Step 5 Root, Step 6 Chord, Step 6 Root, Step 7 Chord, Step 7 Root, "
                           "Step 8 Chord, Step 8 Root, bpm, bypassed, run, syncToTransport"},
        {"Reverb", "bypassed, damping, dry, muted, outputLevel, roomSize, wet, width"},
        {"Sample & Hold", "bypassed, clock, holdMode, level, muted, offset, rate, slew, source, trigThreshold"},
        {"Sampler", "bypassed, density, grainSize, level, loop, muted, pitch, playMode, rootNote, spray, start"},
        {"Sequencer", "F.Env 1, F.Env 2, F.Env 3, F.Env 4, F.Env 5, F.Env 6, F.Env 7, F.Env 8, Gate 1, Gate 2, "
                      "Gate 3, Gate 4, Gate 5, Gate 6, Gate 7, Gate 8, Pitch 1, Pitch 2, Pitch 3, Pitch 4, Pitch 5, "
                      "Pitch 6, Pitch 7, Pitch 8, bpm, bypassed, run, syncToTransport"},
        {"VCA", "bypassed, gain, muted, poly"},
        {"Voice Mixer", "bypassed, level"},
        {"Wavetable", "blend, bypassed, coarse, detune, fine, importMode, interpolation, level, muted, octave, pan, "
                      "phase, poly, position, randomPhase, spread, stack, subLevel, subOctave, subShape, syncMode, "
                      "table, unison, warp, warpAmount, width"},
    };

    const auto keys = synth::AIStateMapper::moduleFactoryTypeNames();
    EXPECT_EQ((int)golden.size(), keys.size()) << "a module was added to or removed from the factory";

    for (const auto& key : keys) {
        auto module = synth::AIStateMapper::createModule(key);
        ASSERT_NE(module, nullptr);
        const juce::String actual = sortedParamIds(*module);

        auto pinned = golden.find(key);
        if (pinned == golden.end()) {
            ADD_FAILURE() << "new module \"" << key << "\" — add this row to the golden:\n        {\"" << key
                          << "\", \"" << actual << "\"},";
            continue;
        }

        EXPECT_EQ(actual, pinned->second)
            << "paramIDs changed for \"" << key << "\". If that is intended, replace its row with:\n        {\"" << key
            << "\", \"" << actual << "\"},";
    }
}

// Golden: exactly which module types the model is allowed to author.
//
// The list is DERIVED from the factory, so registering a module makes it model-authorable by
// default — which is the wrong default for anything that names an external resource or carries
// privileged state (a hosted plugin, a timeline feed). This test exists to make that a decision:
// any registration changes the list and MUST consciously update the golden below, either by adding
// the new type here or by adding it to kNonAuthorableModuleTypes in AIStateMapper.cpp.
TEST(AIStateMapperTest, AuthorableModuleTypesGolden) {
    const juce::StringArray golden = {"ADSR",
                                      "Amp Env",
                                      "Audio Input",
                                      "Audio Output",
                                      "Bitcrusher",
                                      "Chorus",
                                      "Compressor",
                                      "Delay",
                                      "Distortion",
                                      "Envelope Follower",
                                      "External MIDI",
                                      "Filter",
                                      "Filter Env",
                                      "Flanger",
                                      "LFO",
                                      "Limiter",
                                      "MIDI Keyboard",
                                      "Macros",
                                      "Math",
                                      "Midi Input",
                                      "Noise",
                                      "Oscillator",
                                      "Parametric EQ",
                                      "Phaser",
                                      "Pitch Shifter",
                                      "Poly MIDI",
                                      "Poly Sequencer",
                                      "Reverb",
                                      "Sample & Hold",
                                      "Sampler",
                                      "Sequencer",
                                      "VCA",
                                      "Voice Mixer",
                                      "Wavetable"};

    const auto actual = synth::AIStateMapper::authorableModuleTypes();
    EXPECT_EQ(actual.joinIntoString(", "), golden.joinIntoString(", "))
        << "the set of model-authorable modules changed — update this golden deliberately";

    // The two deliberate exclusions, stated positively so a silent removal of the deny set fails.
    EXPECT_FALSE(actual.contains("Attenuverter"));
    EXPECT_FALSE(actual.contains("Mod Slot"));

    // The schema hands the model exactly this list.
    const juce::var schema = synth::AIStateMapper::getPatchSchema(); // held: the chain below points into it
    const juce::var typeDef = schemaNodeProperties(schema)->getProperty("type");
    auto* typeEnum = typeDef.getDynamicObject()->getProperty("enum").getArray();
    ASSERT_NE(typeEnum, nullptr);

    juce::StringArray fromSchema;
    for (const auto& entry : *typeEnum)
        fromSchema.add(entry.toString());
    EXPECT_EQ(fromSchema.joinIntoString(", "), actual.joinIntoString(", "));
}

TEST(AIStateMapperTest, GraphToJSONEmitsSchemaVersionAndNodeUuids) {
    juce::AudioProcessorGraph graph;
    createBasicGraph(graph);

    juce::var json = synth::AIStateMapper::graphToJSON(graph);
    ASSERT_NE(json.getDynamicObject(), nullptr);
    EXPECT_EQ((int)json.getDynamicObject()->getProperty("schemaVersion"), synth::AIStateMapper::kSchemaVersion);

    auto uuids = uuidsOf(json);
    EXPECT_EQ(uuids.size(), graph.getNumNodes());
    for (const auto& uuid : uuids)
        EXPECT_FALSE(uuid.isEmpty()) << "every node must carry a uuid";

    juce::StringArray unique = uuids;
    unique.removeDuplicates(false);
    EXPECT_EQ(unique.size(), uuids.size()) << "uuids must be unique within a patch";
}

// An absent schemaVersion means 1 and gates nothing: patches written before the field existed must
// keep loading unchanged.
TEST(AIStateMapperTest, PatchWithoutSchemaVersionStillApplies) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Filter"}],"connections":[]})");
    EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));
    EXPECT_EQ(graph.getNumNodes(), 1);
}

// Save → load → save on the trusted path must reproduce the same identities: this is what lets a
// long-lived reference (an automation lane, a timeline track binding) survive a preset round trip.
TEST(AIStateMapperTest, NodeUuidsAreStableAcrossTrustedRoundTrip) {
    juce::AudioProcessorGraph graph;
    createBasicGraph(graph);

    juce::var firstSave = synth::AIStateMapper::graphToJSON(graph);
    // Saving the same live graph twice must not mint new identities either.
    EXPECT_EQ(uuidsOf(synth::AIStateMapper::graphToJSON(graph)).joinIntoString(","),
              uuidsOf(firstSave).joinIntoString(","));

    juce::AudioProcessorGraph reloaded;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(firstSave, reloaded, /*clearExisting=*/true, /*trusted=*/true));

    juce::var secondSave = synth::AIStateMapper::graphToJSON(reloaded);
    EXPECT_EQ(uuidsOf(secondSave).joinIntoString(","), uuidsOf(firstSave).joinIntoString(","));
}

// Untrusted input must never dictate identity — otherwise a model could hand two nodes the same
// uuid, or claim the uuid of a node something else already points at.
TEST(AIStateMapperTest, UntrustedApplyIgnoresIncomingNodeUuids) {
    const juce::String claimed = "11111111-2222-3333-4444-555555555555";
    juce::String jsonStr = "{\"nodes\":[{\"id\":1,\"type\":\"Filter\",\"uuid\":\"" + claimed +
                           "\"},{\"id\":2,\"type\":\"Oscillator\",\"uuid\":\"" + claimed + "\"}],\"connections\":[]}";
    juce::var json = juce::JSON::parse(jsonStr);

    juce::AudioProcessorGraph untrustedGraph;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, untrustedGraph, /*clearExisting=*/true,
                                                       /*trusted=*/false));
    auto regenerated = uuidsOf(synth::AIStateMapper::graphToJSON(untrustedGraph));
    ASSERT_EQ(regenerated.size(), 2);
    for (const auto& uuid : regenerated) {
        EXPECT_FALSE(uuid.isEmpty());
        EXPECT_NE(uuid, claimed) << "a model must not be able to choose a node's identity";
    }
    EXPECT_NE(regenerated[0], regenerated[1]) << "colliding uuids must not survive the untrusted path";

    // The same JSON on the trusted path (our own preset/undo replay) keeps what it was given.
    juce::AudioProcessorGraph trustedGraph;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, trustedGraph, /*clearExisting=*/true, /*trusted=*/true));
    auto honoured = uuidsOf(synth::AIStateMapper::graphToJSON(trustedGraph));
    ASSERT_EQ(honoured.size(), 2);
    EXPECT_EQ(honoured[0], claimed);
    EXPECT_EQ(honoured[1], claimed);
}

// The model-facing schema is an invitation list: anything named in it is something the model is
// being asked to emit. The three reserved fields are ours to write, never its.
TEST(AIStateMapperTest, SchemaOmitsReservedFields) {
    const juce::var schema = synth::AIStateMapper::getPatchSchema();
    const juce::String schemaText = juce::JSON::toString(schema);

    for (const char* reserved : {"schemaVersion", "uuid", "timeline"})
        EXPECT_FALSE(schemaText.contains(reserved))
            << "\"" << reserved << "\" must not appear anywhere in the model-facing patch schema";

    auto* nodeProperties = schemaNodeProperties(schema);
    ASSERT_NE(nodeProperties, nullptr);
    for (const char* reserved : {"schemaVersion", "uuid", "timeline"})
        EXPECT_FALSE(nodeProperties->hasProperty(reserved)) << "node schema must not offer \"" << reserved << "\"";

    auto* rootProperties = schema.getDynamicObject()->getProperty("properties").getDynamicObject();
    ASSERT_NE(rootProperties, nullptr);
    for (const char* reserved : {"schemaVersion", "uuid", "timeline"})
        EXPECT_FALSE(rootProperties->hasProperty(reserved)) << "root schema must not offer \"" << reserved << "\"";
}

// "timeline" is refused rather than ignored: the validator lets unknown keys through, so a later
// build that starts honouring timeline data would silently begin executing provider-authored
// automation against patches accepted today.
TEST(AIStateMapperTest, TimelineIsRefusedFromUntrustedPatchesOnly) {
    juce::AudioProcessorGraph graph;
    juce::var json = juce::JSON::parse(
        R"({"nodes":[{"id":1,"type":"Filter"}],"connections":[],"timeline":{"tracks":[{"clips":[]}]}})");

    auto untrusted = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/true, /*trusted=*/false);
    EXPECT_FALSE(untrusted.ok);
    EXPECT_EQ(untrusted.error, synth::PatchValidationError::TimelineNotAllowed);
    EXPECT_TRUE(untrusted.message.containsIgnoreCase("timeline")) << "the model must be told what to remove";
    EXPECT_EQ(synth::patchValidationErrorName(synth::PatchValidationError::TimelineNotAllowed), "TimelineNotAllowed");

    EXPECT_FALSE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/false));
    EXPECT_EQ(graph.getNumNodes(), 0) << "a refused patch must not be partially applied";

    // The SAME JSON is accepted on the trusted path — future project files ride this key.
    auto trusted = synth::AIStateMapper::validatePatch(json, graph, /*clearExisting=*/true, /*trusted=*/true);
    EXPECT_TRUE(trusted.ok) << trusted.message;
    EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));
    EXPECT_EQ(graph.getNumNodes(), 1);
}
