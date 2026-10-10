// The model-facing patch schema: module/param coverage and the timelineOps grammar shape.
#include "AI/AIStateMapper/AIStateMapper.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

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
