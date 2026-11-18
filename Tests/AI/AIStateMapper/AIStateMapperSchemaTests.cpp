// The model-facing patch schema: module/param coverage and the timelineOps grammar shape.
#include "AI/AIStateMapper/AIStateMapper.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include <vector>

TEST(AIStateMapperTest, SchemaGeneration) {
    juce::String schema = synth::AIStateMapper::getModuleSchema();
    ASSERT_FALSE(schema.isEmpty());
    ASSERT_TRUE(schema.contains("Oscillator"));
    ASSERT_TRUE(schema.contains("Filter"));
    ASSERT_TRUE(schema.contains("waveform"));
    ASSERT_TRUE(schema.contains("cutoff"));
}

TEST(AIStateMapperTest, Modulation_SchemaIncludesModulationTargets) {
    juce::String schema = synth::AIStateMapper::getModuleSchema();
    ASSERT_TRUE(schema.contains("Modulation Targets"));
    ASSERT_TRUE(schema.contains("Cutoff"));
    ASSERT_TRUE(schema.contains("Resonance"));
    ASSERT_TRUE(schema.contains("Modulation Sources"));
}

// "track" in the timelineOps grammar used to be `{}` ("anything goes"), a confirmed Ollama
// grammar-compiler bug (an empty-schema subschema gets mangled into a garbage wrapped object
// instead of passing the value through). It must never regress back to that shape, and — per this
// header's own note that llama.cpp's grammar compiler handles anyOf poorly — must not become a
// oneOf/anyOf either.
TEST(AIStateMapperTest, TimelineOpsTrackFieldIsNotOpenSchema) {
    const juce::var schema = synth::AIStateMapper::getPatchSchemaWithTimelineOps();
    auto* rootProperties = schema.getDynamicObject()->getProperty("properties").getDynamicObject();
    ASSERT_NE(rootProperties, nullptr);

    auto* timelineOps = rootProperties->getProperty("timelineOps").getDynamicObject();
    ASSERT_NE(timelineOps, nullptr);
    auto* opItems = timelineOps->getProperty("items").getDynamicObject();
    ASSERT_NE(opItems, nullptr);
    auto* opProperties = opItems->getProperty("properties").getDynamicObject();
    ASSERT_NE(opProperties, nullptr);

    auto* trackDef = opProperties->getProperty("track").getDynamicObject();
    ASSERT_NE(trackDef, nullptr) << "\"track\" must be a real schema object, not `{}`";
    EXPECT_TRUE(trackDef->hasProperty("type")) << "\"track\" must declare a concrete type, not be left open";
    EXPECT_FALSE(trackDef->hasProperty("oneOf"));
    EXPECT_FALSE(trackDef->hasProperty("anyOf"));
}

// addInstrumentTrack's fields must be expressible through a local grammar (a closed item schema
// would forbid them outright), each with a concrete type -- never `{}`, never anyOf/oneOf.
TEST(AIStateMapperTest, TimelineOpsGrammarAllowsAddInstrumentTrackFields) {
    for (const auto& schema : {synth::AIStateMapper::getPatchSchemaWithTimelineOps()}) {
        auto* rootProperties = schema.getDynamicObject()->getProperty("properties").getDynamicObject();
        ASSERT_NE(rootProperties, nullptr);
        auto* opItems =
            rootProperties->getProperty("timelineOps").getDynamicObject()->getProperty("items").getDynamicObject();
        ASSERT_NE(opItems, nullptr);
        auto* opProperties = opItems->getProperty("properties").getDynamicObject();
        ASSERT_NE(opProperties, nullptr);

        EXPECT_TRUE(opProperties->getProperty("op").getProperty("enum", {}).getArray()->contains("addInstrumentTrack"));
        const std::pair<const char*, const char*> expected[] = {
            {"instrument", "string"}, {"poly", "boolean"},    {"instrumentId", "integer"},
            {"inserts", "array"},     {"envelope", "object"}, {"instrumentParams", "object"}};
        for (const auto& [field, type] : expected) {
            auto* def = opProperties->getProperty(field).getDynamicObject();
            ASSERT_NE(def, nullptr) << field;
            EXPECT_EQ(def->getProperty("type").toString(), type) << field;
            EXPECT_FALSE(def->hasProperty("anyOf")) << field;
            EXPECT_FALSE(def->hasProperty("oneOf")) << field;
        }
        // The envelope is a closed pair of concrete types: its id binds in the plan, its params are open.
        const juce::var envelope = opProperties->getProperty("envelope");
        EXPECT_EQ(envelope.getProperty("properties", {}).getProperty("id", {}).getProperty("type", {}).toString(),
                  "integer");
        const juce::var envelopeParams = envelope.getProperty("properties", {}).getProperty("params", {});
        EXPECT_EQ(envelopeParams.getProperty("type", {}).toString(), "object");
        EXPECT_TRUE(static_cast<bool>(envelopeParams.getProperty("additionalProperties", {})));
    }
}

// setTempo and addMarker must be expressible through the local grammar: their ops in the enum, their
// "bpm" and "beat" fields typed (no other op-specific field is shared with them).
TEST(AIStateMapperTest, TimelineOpsGrammarAllowsSetTempoAndAddMarker) {
    const juce::var schema = synth::AIStateMapper::getPatchSchemaWithTimelineOps();
    auto* opProperties = schema.getDynamicObject()
                             ->getProperty("properties")
                             .getDynamicObject()
                             ->getProperty("timelineOps")
                             .getDynamicObject()
                             ->getProperty("items")
                             .getDynamicObject()
                             ->getProperty("properties")
                             .getDynamicObject();
    ASSERT_NE(opProperties, nullptr);
    auto* ops = opProperties->getProperty("op").getProperty("enum", {}).getArray();
    ASSERT_NE(ops, nullptr);
    EXPECT_TRUE(ops->contains("setTempo"));
    EXPECT_TRUE(ops->contains("addMarker"));
    EXPECT_EQ(opProperties->getProperty("bpm").getProperty("type", {}).toString(), "number");
    EXPECT_EQ(opProperties->getProperty("beat").getProperty("type", {}).toString(), "number");
}

// makeSection and placeSection are expressible through the local grammar: both ops in the enum, with the
// "endBeat" field they add typed ("name", "startBeat" and "beat" are shared with other ops).
TEST(AIStateMapperTest, TimelineOpsGrammarAllowsMakeSectionAndPlaceSection) {
    const juce::var schema = synth::AIStateMapper::getPatchSchemaWithTimelineOps();
    auto* opProperties = schema.getDynamicObject()
                             ->getProperty("properties")
                             .getDynamicObject()
                             ->getProperty("timelineOps")
                             .getDynamicObject()
                             ->getProperty("items")
                             .getDynamicObject()
                             ->getProperty("properties")
                             .getDynamicObject();
    ASSERT_NE(opProperties, nullptr);
    auto* ops = opProperties->getProperty("op").getProperty("enum", {}).getArray();
    ASSERT_NE(ops, nullptr);
    EXPECT_TRUE(ops->contains("makeSection"));
    EXPECT_TRUE(ops->contains("placeSection"));
    for (const char* field : {"startBeat", "endBeat", "beat"})
        EXPECT_EQ(opProperties->getProperty(field).getProperty("type", {}).toString(), "number") << field;
    EXPECT_EQ(opProperties->getProperty("name").getProperty("type", {}).toString(), "string");
}

// ---- Parity with the hosted server's timeline-ops grammar ------------------------------------------------------
//
// The hosted server authors its timeline-ops grammar separately (and does not know this build's module registry),
// so nothing generates one from the other. This pins the two together: kServer* below is a vendored copy of the
// server's op set and each shape's allowed/required keys, and the client's local schema is asserted against it.
// When either side changes alone this fails. To refresh after a server change, copy the op names, per-op
// properties and required lists from the server's project-edit schema (its golden JSON schema) into the tables
// below, then update the client schema in AIStateMapperSchema.cpp to match and list any client-only extra below.
namespace {
using Keys = std::set<juce::String>;

struct ServerShape {
    const char* name;
    Keys allowed;
    Keys required;
};

// Per-op shapes of the server's timeline ops.
const std::vector<ServerShape> kServerOps = {
    {"addTrack", {"op", "kind", "name"}, {"op", "kind", "name"}},
    {"placeClips", {"op", "track", "clips"}, {"op", "track", "clips"}},
    {"writeLane", {"op", "nodeId", "nodeUuid", "paramId", "points"}, {"op", "paramId", "points"}},
    {"addInstrumentTrack",
     {"op", "name", "instrument", "poly", "instrumentId", "instrumentParams", "inserts", "envelope"},
     {"op", "name", "instrument"}},
    {"setTempo", {"op", "bpm"}, {"op", "bpm"}},
    {"addMarker", {"op", "beat", "name"}, {"op", "beat", "name"}},
    {"setSongKey", {"op", "root", "scale"}, {"op", "root", "scale"}},
};

// Nested shapes shared by the ops.
const ServerShape kServerClip = {"clip", {"startBeat", "lengthBeats", "name", "notes"}, {"startBeat", "lengthBeats"}};
const ServerShape kServerNote = {
    "note",
    {"startBeat", "lengthBeats", "pitch", "velocity", "channel", "chance", "repeat", "flam", "nudge", "condition"},
    {"startBeat", "lengthBeats", "pitch", "velocity"}};
const ServerShape kServerPoint = {"point", {"beat", "value", "tension", "curve"}, {"beat", "value"}};
const ServerShape kServerInsert = {"insert", {"type", "id", "params"}, {"type"}};
const ServerShape kServerEnvelope = {"envelope", {"id", "params"}, {}};

// Ops and keys only the client's local grammar has (song-build and MIDI-file ops the server schema does not carry).
const Keys kClientOnlyOps = {"placeMidiClip", "makeSection", "placeSection"};
const Keys kClientOnlyOpKeys = {"startBeat", "endBeat", "midBase64"};

Keys keysOf(const juce::var& properties) {
    Keys out;
    if (auto* object = properties.getDynamicObject())
        for (const auto& property : object->getProperties())
            out.insert(property.name.toString());
    return out;
}

Keys stringsOf(const juce::var& array) {
    Keys out;
    if (auto* values = array.getArray())
        for (const auto& value : *values)
            out.insert(value.toString());
    return out;
}

juce::var clientOpItems() {
    return synth::AIStateMapper::getPatchSchemaWithTimelineOps()
        .getProperty("properties", {})
        .getProperty("timelineOps", {})
        .getProperty("items", {});
}

// Asserts one nested client shape has exactly the server's keys; `extraClientRequired` names a key the client
// deliberately demands beyond the server.
void expectShapeMatchesServer(const juce::var& clientShape, const ServerShape& server,
                              const Keys& extraClientRequired = {}) {
    EXPECT_EQ(keysOf(clientShape.getProperty("properties", {})), server.allowed) << server.name;
    // The client may require a subset of what the server requires (it fills defaults) plus its documented extras.
    for (const auto& key : stringsOf(clientShape.getProperty("required", {})))
        EXPECT_TRUE(server.required.count(key) > 0 || extraClientRequired.count(key) > 0)
            << server.name << " requires \"" << key << "\" only on the client";
}
} // namespace

TEST(AIStateMapperTest, TimelineOpsGrammarMatchesTheHostedServersOpSet) {
    const juce::var items = clientOpItems();
    const juce::var properties = items.getProperty("properties", {});

    Keys expectedOps = kClientOnlyOps;
    Keys expectedKeys = kClientOnlyOpKeys;
    for (const auto& op : kServerOps) {
        expectedOps.insert(op.name);
        expectedKeys.insert(op.allowed.begin(), op.allowed.end());
    }
    EXPECT_EQ(stringsOf(properties.getProperty("op", {}).getProperty("enum", {})), expectedOps);
    EXPECT_EQ(keysOf(properties), expectedKeys);

    // Every key a server op needs is a key the client grammar can emit (the client's flat shape requires only "op").
    EXPECT_EQ(stringsOf(items.getProperty("required", {})), Keys{"op"});
}

TEST(AIStateMapperTest, TimelineOpsGrammarNestedShapesMatchTheHostedServer) {
    const juce::var properties = clientOpItems().getProperty("properties", {});
    const juce::var clip = properties.getProperty("clips", {}).getProperty("items", {});
    expectShapeMatchesServer(clip, kServerClip, {"notes"});
    expectShapeMatchesServer(clip.getProperty("properties", {}).getProperty("notes", {}).getProperty("items", {}),
                             kServerNote);
    expectShapeMatchesServer(properties.getProperty("points", {}).getProperty("items", {}), kServerPoint);
    expectShapeMatchesServer(properties.getProperty("inserts", {}).getProperty("items", {}), kServerInsert);
    expectShapeMatchesServer(properties.getProperty("envelope", {}), kServerEnvelope);
}

namespace {
// Walks a schema and counts integers with no minimum/maximum and free strings with no maxLength: each is a way for
// a small model's grammar-constrained decoding to run away (digits or characters until the timeout).
void countUnbounded(const juce::var& node, const juce::String& key, int& integers, int& strings) {
    if (auto* object = node.getDynamicObject()) {
        const auto type = object->getProperty("type").toString();
        if (type == "integer" && !(object->hasProperty("minimum") && object->hasProperty("maximum")))
            ++integers;
        if (type == "string" && !object->hasProperty("enum") && !object->hasProperty("maxLength") && key != "midBase64")
            ++strings;
        for (const auto& entry : object->getProperties())
            countUnbounded(entry.value, entry.name.toString(), integers, strings);
    } else if (auto* array = node.getArray()) {
        for (const auto& item : *array)
            countUnbounded(item, key, integers, strings);
    }
}
} // namespace

// A runaway such as "nodeId": 1000000000000000000 ran digits until the timeout on the local path.
TEST(AIStateMapperTest, ProjectEditSchemaBoundsEveryIntegerAndFreeString) {
    int integers = 0;
    int strings = 0;
    countUnbounded(synth::AIStateMapper::getProjectEditSchema(), {}, integers, strings);
    EXPECT_EQ(integers, 0) << "integers without minimum/maximum";
    EXPECT_EQ(strings, 0) << "free strings without maxLength";

    // The hosted/shipped patch schema is untouched.
    int patchIntegers = 0;
    int patchStrings = 0;
    countUnbounded(synth::AIStateMapper::getPatchSchema(), {}, patchIntegers, patchStrings);
    EXPECT_GT(patchIntegers, 0);
}
