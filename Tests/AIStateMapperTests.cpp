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

TEST(AIStateMapperTest, MergeMode_TypeMismatchCreatesNewNode) {
    juce::AudioProcessorGraph graph;

    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(oscNode, nullptr);
    int oscId = (int)oscNode->nodeID.uid;

    // Delta JSON: same ID but different type — should create a new node
    juce::String jsonStr = "{\"nodes\":[{\"id\":" + juce::String(oscId) +
                           ",\"type\":\"Filter\",\"params\":{\"cutoff\":1000.0}}],\"connections\":[]}";
    juce::var json = juce::JSON::parse(jsonStr);

    bool success = synth::AIStateMapper::applyJSONToGraph(json, graph, false);
    ASSERT_TRUE(success);
    ASSERT_EQ(graph.getNumNodes(), 2); // Original Oscillator + new Filter
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
