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

// P6-13: "track" in the timelineOps grammar used to be `{}` ("anything goes"), a confirmed Ollama
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
