// Table-driven coverage of PatchValidationError: for every value the enum can take, a
// deliberately malformed patch that must be rejected with exactly that value.
//
// Asserting the exact enumerator (not merely "rejected") is the point. The retry path feeds
// the validation message back to the model and the repair path switches on the code, so a
// patch misclassified into the wrong bucket would silently send the wrong correction.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/OscillatorModule.h"
#include <functional>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include <vector>

using synth::AIStateMapper;
using synth::PatchValidationError;

namespace {

// Builds a JSON array of `count` copies of `element`, for the size-cap cases.
juce::var repeated(const juce::var& element, int count) {
    juce::Array<juce::var> arr;
    for (int i = 0; i < count; ++i)
        arr.add(element);
    return arr;
}

juce::var connectionVar(int src, int dst) {
    juce::DynamicObject::Ptr c = new juce::DynamicObject();
    c->setProperty("src", src);
    c->setProperty("srcPort", 0);
    c->setProperty("dst", dst);
    c->setProperty("dstPort", 0);
    return juce::var(c.get());
}

juce::var modulationVar(int source, int dest, int destPort) {
    juce::DynamicObject::Ptr m = new juce::DynamicObject();
    m->setProperty("source", source);
    m->setProperty("dest", dest);
    m->setProperty("destPort", destPort);
    return juce::var(m.get());
}

struct Case {
    PatchValidationError expected;
    const char* what; // what the malformed patch does wrong
    std::function<juce::var()> build;
    bool mergeMode = false;
};

// A patch whose only defect is the one named. Everything else in each case is valid, so a
// failure means validatePatch classified the intended defect — not something incidental.
std::vector<Case> makeCases() {
    return {
        {PatchValidationError::NotAnObject, "root is a JSON array, not an object",
         [] { return juce::JSON::parse(R"([1,2,3])"); }},

        {PatchValidationError::MissingNodesOrRemove, "neither 'nodes' nor 'remove' present",
         [] { return juce::JSON::parse(R"({"connections":[]})"); }},

        {PatchValidationError::NodesNotArray, "'nodes' is an object",
         [] { return juce::JSON::parse(R"({"nodes":{}})"); }},

        {PatchValidationError::ConnectionsNotArray, "'connections' is an object",
         [] { return juce::JSON::parse(R"({"nodes":[],"connections":{}})"); }},

        {PatchValidationError::ModulationsNotArray, "'modulations' is an object",
         [] { return juce::JSON::parse(R"({"nodes":[],"modulations":{}})"); }},

        {PatchValidationError::RemoveNotArray, "'remove' is an object",
         [] { return juce::JSON::parse(R"({"remove":{}})"); }},

        {PatchValidationError::RemoveModulationsNotArray, "'removeModulations' is an object",
         [] { return juce::JSON::parse(R"({"nodes":[],"removeModulations":{}})"); }},

        {PatchValidationError::TooManyNodes, "one node over kMaxNodes",
         [] {
             juce::DynamicObject::Ptr n = new juce::DynamicObject();
             n->setProperty("id", 1);
             n->setProperty("type", "Oscillator");
             juce::DynamicObject::Ptr root = new juce::DynamicObject();
             root->setProperty("nodes", repeated(juce::var(n.get()), AIStateMapper::kMaxNodes + 1));
             return juce::var(root.get());
         }},

        {PatchValidationError::TooManyConnections, "one connection over kMaxConnections",
         [] {
             juce::DynamicObject::Ptr root = new juce::DynamicObject();
             root->setProperty("nodes", juce::Array<juce::var>());
             root->setProperty("connections", repeated(connectionVar(1, 2), AIStateMapper::kMaxConnections + 1));
             return juce::var(root.get());
         }},

        {PatchValidationError::TooManyModulations, "one modulation over kMaxModulations",
         [] {
             juce::DynamicObject::Ptr root = new juce::DynamicObject();
             root->setProperty("nodes", juce::Array<juce::var>());
             root->setProperty("modulations", repeated(modulationVar(1, 2, 1), AIStateMapper::kMaxModulations + 1));
             return juce::var(root.get());
         }},

        {PatchValidationError::TooManyRemovals, "one removal over kMaxRemovals",
         [] {
             juce::DynamicObject::Ptr root = new juce::DynamicObject();
             root->setProperty("remove", repeated(juce::var(7), AIStateMapper::kMaxRemovals + 1));
             return juce::var(root.get());
         }},

        {PatchValidationError::TooManyRemoveModulations, "one entry over kMaxRemoveModulations",
         [] {
             juce::DynamicObject::Ptr root = new juce::DynamicObject();
             root->setProperty("nodes", juce::Array<juce::var>());
             root->setProperty("removeModulations",
                               repeated(modulationVar(1, 2, 1), AIStateMapper::kMaxRemoveModulations + 1));
             return juce::var(root.get());
         }},

        {PatchValidationError::NodeEntryInvalid, "a node entry is a bare integer",
         [] { return juce::JSON::parse(R"({"nodes":[42],"connections":[]})"); }},

        {PatchValidationError::NodeIdInvalid, "node id is negative",
         [] { return juce::JSON::parse(R"({"nodes":[{"id":-1,"type":"Oscillator"}],"connections":[]})"); }},

        {PatchValidationError::NodeTypeInvalid, "node type is a number, not a string",
         [] { return juce::JSON::parse(R"({"nodes":[{"id":1,"type":123}],"connections":[]})"); }},

        {PatchValidationError::UnknownNodeType, "module type is not in the factory",
         [] { return juce::JSON::parse(R"({"nodes":[{"id":1,"type":"HyperResonator"}],"connections":[]})"); }},

        {PatchValidationError::DuplicateNodeId, "two nodes share id 5",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":5,"type":"Oscillator"},{"id":5,"type":"Filter"}],"connections":[]})");
         }},

        {PatchValidationError::InvalidParameterValue, "a float parameter is given a string",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":1,"type":"Oscillator","params":{"fine":"loud"}}],"connections":[]})");
         }},

        {PatchValidationError::InvalidChoiceValue, "waveform is not one of the declared choices",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":1,"type":"Oscillator","params":{"waveform":"Zigzag"}}],"connections":[]})");
         }},

        {PatchValidationError::UnknownParameterKey, "params has a key that matches no real parameter",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":1,"type":"Oscillator","params":{"waveform":"Saw","bogusKey":1.0}}],"connections":[]})");
         }},

        {PatchValidationError::ConnectionEntryInvalid, "a connection entry is a bare integer",
         [] { return juce::JSON::parse(R"({"nodes":[],"connections":[7]})"); }},

        {PatchValidationError::ConnectionUnknownNode, "connection references an id no node defines",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":1,"type":"Oscillator"}],"connections":[{"src":1,"srcPort":0,"dst":999,"dstPort":0}]})");
         }},

        {PatchValidationError::ConnectionInvalidPort, "connection port index far beyond kMaxPortIndex",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Filter"}],
                     "connections":[{"src":1,"srcPort":9999,"dst":2,"dstPort":0}]})");
         }},

        {PatchValidationError::ConnectionSelfCycle, "connection from a node to itself",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":1,"type":"Filter"}],"connections":[{"src":1,"srcPort":0,"dst":1,"dstPort":0}]})");
         }},

        {PatchValidationError::ModulationEntryInvalid, "a modulation entry is a bare integer",
         [] { return juce::JSON::parse(R"({"nodes":[],"modulations":[7]})"); }},

        {PatchValidationError::ModulationUnknownNode, "modulation references an id no node defines",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":1,"type":"LFO"}],"modulations":[{"source":1,"dest":999,"destPort":1}]})");
         }},

        {PatchValidationError::ModulationInvalidPort, "modulation destPort uses the MIDI sentinel -1",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":1,"type":"LFO"},{"id":2,"type":"Filter"}],
                     "modulations":[{"source":1,"dest":2,"destPort":-1}]})");
         }},

        {PatchValidationError::ModulationSelfCycle, "modulation from a node to itself",
         [] {
             return juce::JSON::parse(
                 R"({"nodes":[{"id":1,"type":"Filter"}],"modulations":[{"source":1,"dest":1,"destPort":1}]})");
         }},

        {PatchValidationError::RemoveEntryInvalid, "'remove' contains a string",
         [] { return juce::JSON::parse(R"({"nodes":[],"remove":["abc"]})"); }},

        {PatchValidationError::RemoveModulationEntryInvalid, "removeModulations entry is missing destPort",
         [] { return juce::JSON::parse(R"({"nodes":[],"removeModulations":[{"source":1,"dest":2}]})"); }},

        {PatchValidationError::NodeIdTypeMismatch, "merge patch reuses a live node's id for another type",
         [] { return juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Filter"}],"connections":[]})"); },
         /*mergeMode=*/true},

        {PatchValidationError::TimelineNotAllowed, "patch carries a root 'timeline' key",
         [] { return juce::JSON::parse(R"({"nodes":[],"connections":[],"timeline":{"tracks":[]}})"); }},

        {PatchValidationError::MacrosNotAllowed, "patch carries a root 'macros' key",
         [] { return juce::JSON::parse(R"({"nodes":[],"connections":[],"macros":[]})"); }},

        {PatchValidationError::InternalModuleNotAllowed, "patch names an internal-only module type",
         [] { return juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Attenuverter"}],"connections":[]})"); }},
    };
}

// Merge-mode cases run against a graph that already holds one Oscillator, so a patch can collide
// with a live node. A fresh juce::AudioProcessorGraph hands out uids from 1, so that node is id 1 —
// asserted at the call site rather than assumed, since the patches above address it by that literal.
void seedMergeGraph(juce::AudioProcessorGraph& graph) {
    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    ASSERT_EQ((int)node->nodeID.uid, 1);
}

} // namespace

TEST(AIPatchValidationTest, EachMalformedPatchYieldsItsExactError) {
    for (const auto& testCase : makeCases()) {
        // Replace-mode cases run against an empty graph — none of them depends on pre-existing
        // nodes; merge-mode cases need one to collide with.
        juce::AudioProcessorGraph graph;
        if (testCase.mergeMode) {
            seedMergeGraph(graph);
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
        }

        const auto result = AIStateMapper::validatePatch(testCase.build(), graph,
                                                         /*clearExisting=*/!testCase.mergeMode, /*trusted=*/false);

        EXPECT_FALSE(result.ok) << "expected rejection: " << testCase.what;
        EXPECT_EQ(result.error, testCase.expected)
            << "case: " << testCase.what << "\n  expected " << synth::patchValidationErrorName(testCase.expected)
            << " but got " << synth::patchValidationErrorName(result.error) << " (\"" << result.message << "\")";
        EXPECT_TRUE(result.message.isNotEmpty()) << "case: " << testCase.what << " — rejection must explain itself";
    }
}

// Guards the table against the enum growing underneath it: a new PatchValidationError with no
// case here fails this test rather than quietly going unexercised.
TEST(AIPatchValidationTest, EveryErrorValueIsCovered) {
    std::set<PatchValidationError> covered;
    for (const auto& testCase : makeCases())
        covered.insert(testCase.expected);

    // The enumeration is well short of 63 values, so probing that span is well-defined;
    // patchValidationErrorName() returns "Unknown" past the last enumerator.
    for (int i = 0; i <= 63; ++i) {
        const auto value = static_cast<PatchValidationError>(i);
        const auto name = synth::patchValidationErrorName(value);
        if (name == "Unknown")
            break;
        if (value == PatchValidationError::None)
            continue;

        EXPECT_TRUE(covered.count(value) > 0) << "PatchValidationError::" << name << " has no case in makeCases()";
    }
}

TEST(AIPatchValidationTest, ErrorNamesAreDistinctAndNonEmpty) {
    std::set<juce::String> names;
    for (int i = 0; i <= 63; ++i) {
        const auto name = synth::patchValidationErrorName(static_cast<PatchValidationError>(i));
        if (name == "Unknown")
            break;
        EXPECT_TRUE(name.isNotEmpty());
        EXPECT_TRUE(names.insert(name).second) << "duplicate error name: " << name;
    }
    EXPECT_GT(names.size(), 20u);
}

// Merge mode must accept ids that exist only in the live graph — the counterpart to
// ConnectionUnknownNode, and the case that separates "stale id" from "hallucinated id".
TEST(AIPatchValidationTest, MergeModeAcceptsIdsFromTheLiveGraph) {
    juce::AudioProcessorGraph graph;
    auto existing = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    ASSERT_NE(existing, nullptr);
    const int existingId = (int)existing->nodeID.uid;

    juce::DynamicObject::Ptr node = new juce::DynamicObject();
    node->setProperty("id", 9001);
    node->setProperty("type", "Oscillator");

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", juce::Array<juce::var>({juce::var(node.get())}));
    root->setProperty("connections", juce::Array<juce::var>({connectionVar(9001, existingId)}));

    const auto merged = AIStateMapper::validatePatch(juce::var(root.get()), graph, /*clearExisting=*/false,
                                                     /*trusted=*/false);
    EXPECT_TRUE(merged.ok) << merged.message;

    // The very same patch in replace mode has no such node to reference.
    const auto replaced = AIStateMapper::validatePatch(juce::var(root.get()), graph, /*clearExisting=*/true,
                                                       /*trusted=*/false);
    EXPECT_FALSE(replaced.ok);
    EXPECT_EQ(replaced.error, PatchValidationError::ConnectionUnknownNode);
}

// ---------------------------------------------------------------------------------------------
// getPatchSchema() — the output contract handed to the provider as `format`. What it encodes is
// what a constrained decoder can enforce, so these assertions guard the enforcement itself.
// ---------------------------------------------------------------------------------------------

namespace {

juce::var schemaNodeProperty(const juce::String& name) {
    const juce::var schema = AIStateMapper::getPatchSchema();
    return schema["properties"]["nodes"]["items"]["properties"][juce::Identifier(name)];
}

} // namespace

// The type list used to be a hand-maintained string literal and had drifted from the factory:
// "Voice Mixer" was creatable but absent from the schema, so a constrained decoder could never
// emit one. Deriving it from the factory is what stops that recurring.
TEST(AIPatchValidationTest, SchemaModuleTypesMatchTheFactory) {
    const juce::var typeEnum = schemaNodeProperty("type")["enum"];
    auto* values = typeEnum.getArray();
    ASSERT_NE(values, nullptr) << "node.type must constrain the model to an enum of real modules";

    for (const auto& value : *values) {
        const juce::String typeName = value.toString();
        EXPECT_NE(AIStateMapper::createModule(typeName), nullptr)
            << "schema offers \"" << typeName << "\" but the factory cannot build it";
    }

    juce::StringArray offered;
    for (const auto& value : *values)
        offered.add(value.toString());

    EXPECT_TRUE(offered.contains("Voice Mixer")) << "a creatable module missing from the schema is unreachable";
    EXPECT_TRUE(offered.contains("Oscillator"));

    // Attenuverters are created by applyJSONToGraph for the `modulations` array; the model must
    // not wire them by hand or the mod matrix cannot read the routing back.
    EXPECT_FALSE(offered.contains("Attenuverter"));
    EXPECT_FALSE(offered.contains("Mod Slot"));
}

// InvalidChoiceValue was the single largest schema-expressible rejection cause. `params` used to
// be an unconstrained object, so nothing stopped "waveform":"White Noise".
TEST(AIPatchValidationTest, SchemaConstrainsChoiceParametersToTheirOptions) {
    const juce::var params = schemaNodeProperty("params");
    const juce::var waveform = params["properties"]["waveform"];

    auto* options = waveform["enum"].getArray();
    ASSERT_NE(options, nullptr) << "waveform must be enumerated so the decoder cannot invent a value";

    juce::StringArray allowed;
    for (const auto& option : *options)
        allowed.add(option.toString());

    EXPECT_TRUE(allowed.contains("Saw"));
    EXPECT_FALSE(allowed.contains("White Noise")) << "only the module's real options may appear";

    // Every enumerated option must be one validatePatch accepts, or the schema would push the
    // model toward values the gate then rejects.
    for (const auto& option : allowed) {
        juce::DynamicObject::Ptr paramsObj = new juce::DynamicObject();
        paramsObj->setProperty("waveform", option);
        juce::DynamicObject::Ptr node = new juce::DynamicObject();
        node->setProperty("id", 1);
        node->setProperty("type", "Oscillator");
        node->setProperty("params", juce::var(paramsObj.get()));
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("nodes", juce::Array<juce::var>({juce::var(node.get())}));
        root->setProperty("connections", juce::Array<juce::var>());

        juce::AudioProcessorGraph graph;
        const auto result = AIStateMapper::validatePatch(juce::var(root.get()), graph, true, false);
        EXPECT_TRUE(result.ok) << "schema offers waveform \"" << option
                               << "\" but validation rejects it: " << result.message;
    }
}

// Numeric parameters must remain expressible; a grammar that allowed only the enumerated keys
// would make cutoff/rateHz unreachable — a far worse regression than the one being fixed.
TEST(AIPatchValidationTest, SchemaStillAllowsNonChoiceParameters) {
    const juce::var params = schemaNodeProperty("params");
    EXPECT_TRUE((bool)params["additionalProperties"])
        << "params must stay open, or numeric parameters could not be emitted";
}

// The schema is an *output* contract: everything in it is something the model may emit. Reference
// data does not belong there.
TEST(AIPatchValidationTest, SchemaDoesNotOfferReferenceDataAsOutput) {
    const juce::var schema = AIStateMapper::getPatchSchema();
    EXPECT_TRUE(schema["properties"]["parameterChoices"].isVoid())
        << "parameterChoices is documentation, not a field of a patch";
}

// The union-of-choice-ids schema is only sound while an id means the same thing everywhere. If a
// new module reuses an id with different options, that id is silently left unconstrained — this
// test turns that into a visible decision.
TEST(AIPatchValidationTest, SchemaChoiceParamIdsAreUnambiguous) {
    const juce::var typeEnum = schemaNodeProperty("type")["enum"];
    auto* values = typeEnum.getArray();
    ASSERT_NE(values, nullptr);

    std::map<juce::String, juce::StringArray> seen;
    std::vector<juce::String> conflicts;

    for (const auto& value : *values) {
        auto processor = AIStateMapper::createModule(value.toString());
        if (!processor)
            continue;
        for (auto* param : processor->getParameters()) {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                auto existing = seen.find(choice->paramID);
                if (existing == seen.end())
                    seen[choice->paramID] = choice->choices;
                else if (existing->second != choice->choices)
                    conflicts.push_back(choice->paramID);
            }
        }
    }

    EXPECT_TRUE(conflicts.empty()) << "choice parameter id \"" << (conflicts.empty() ? juce::String() : conflicts[0])
                                   << "\" means different things on different modules, so it cannot be enumerated "
                                      "globally in the schema";

    // Every unambiguous choice id should actually be constrained in the schema.
    const juce::var params = schemaNodeProperty("params");
    for (const auto& [paramId, choices] : seen) {
        juce::ignoreUnused(choices);
        EXPECT_FALSE(params["properties"][juce::Identifier(paramId)].isVoid())
            << "choice parameter \"" << paramId << "\" is not constrained in the schema";
    }
}
