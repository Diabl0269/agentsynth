// The AI adds Parametric EQ bands past the first four: a patch sets any of the 24 bands with a type and slope, the
// diff and the model's compact view say so, and the prompt tells the model how.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/PatchDiff.h"
#include "AIStateMapperTestHelpers.h"
#include "Modules/FX/ParametricEQModule.h"
#include <gtest/gtest.h>

namespace {

using synth::AIStateMapper;

juce::var patchWithEqParams(const juce::String& paramsJson) {
    return juce::JSON::parse(R"({"nodes":[{"id":1,"type":"Parametric EQ","params":)" + paramsJson + "}]}");
}

juce::var steepLowCutAndNotchPatch() {
    return patchWithEqParams(R"({"band1On":true,"band1Type":"Low Cut","band1Freq":200,"band1Slope":"48 dB/oct",
        "band7On":true,"band7Type":"Notch","band7Freq":2000,"band7Q":8})");
}

ParametricEQModule* eqOf(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* eq = dynamic_cast<ParametricEQModule*>(node->getProcessor()))
            return eq;
    return nullptr;
}

juce::var eqNodeJson(const juce::var& graphJson) {
    for (const auto& node : *graphJson.getProperty("nodes", {}).getArray())
        if (node.getProperty("type", {}).toString() == "Parametric EQ")
            return node;
    return {};
}

int paramCount(const juce::var& nodeJson) {
    return nodeJson.getProperty("params", {}).getDynamicObject()->getProperties().size();
}

} // namespace

// Regression test for FRO1696: a patch with band 7 as a notch and band 1 as a 48 dB/oct low cut applies and is heard.
TEST(AIEqBands, ALowCutAndABandSevenNotchApplyAndShapeTheResponse) {
    juce::AudioProcessorGraph graph;
    const auto patch = steepLowCutAndNotchPatch();
    ASSERT_TRUE(AIStateMapper::validatePatch(patch, graph, true, false).ok);
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patch, graph, true, false));
    auto* eq = eqOf(graph);
    ASSERT_NE(eq, nullptr);

    EXPECT_EQ(eq->getBandType(0), ParametricEQModule::BandType::LowCut);
    EXPECT_EQ(eq->getBandSlope(0), 48);
    EXPECT_EQ(eq->getBandType(6), ParametricEQModule::BandType::Notch);
    EXPECT_TRUE(eq->isBandEnabled(6));
    EXPECT_FALSE(eq->isBandEnabled(1));

    const auto bands = eq->getBandSnapshots();
    const auto response = [&](float hz) { return ParametricEQModule::responseDb(bands, 0.0f, hz); };
    EXPECT_LT(response(100.0f), -20.0f) << "an octave under a 48 dB/oct cut at 200 Hz";
    EXPECT_LT(response(50.0f), -60.0f);
    EXPECT_LT(response(2000.0f), -20.0f) << "the notch";
    EXPECT_GT(response(800.0f), -1.0f) << "between the cut and the notch the EQ is flat";
    EXPECT_GT(response(8000.0f), -1.0f);
}

TEST(AIEqBands, TypeAndSlopeTakeChoiceIndicesLikeEveryOtherChoice) {
    juce::AudioProcessorGraph graph;
    const auto patch = patchWithEqParams(R"({"band3On":true,"band3Type":5,"band3Slope":6,"band24Type":"High Cut"})");
    ASSERT_TRUE(AIStateMapper::validatePatch(patch, graph, true, false).ok);
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patch, graph, true, false));
    auto* eq = eqOf(graph);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->getBandType(2), ParametricEQModule::BandType::Notch);
    EXPECT_EQ(eq->getBandSlope(2), 48);
    EXPECT_EQ(eq->getBandType(23), ParametricEQModule::BandType::HighCut);
}

TEST(AIEqBands, OutOfRangeTypesSlopesAndBandsAreRejected) {
    juce::AudioProcessorGraph graph;
    for (const char* bad :
         {R"({"band2Type":"Banana"})", R"({"band2Type":99})", R"({"band2Type":-1})", R"({"band2Slope":"7 dB/oct"})",
          R"({"band2Slope":9})", R"({"band25On":true})", R"({"band0On":true})"})
        EXPECT_FALSE(AIStateMapper::validatePatch(patchWithEqParams(bad), graph, true, false).ok) << bad;
    for (const char* good : {R"({"band2Type":"notch"})", R"({"band24Slope":"96 dB/oct"})", R"({"band2Slope":8})"})
        EXPECT_TRUE(AIStateMapper::validatePatch(patchWithEqParams(good), graph, true, false).ok) << good;
}

TEST(AIEqBands, APatchDiffNamesTheTypeAndSlopeInWords) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(AIStateMapper::createModule("Parametric EQ"));
    node->properties.set("uuid", "00000000-0000-4000-8000-00000000e196");
    const auto before = AIStateMapper::graphToJSON(graph);
    auto& eq = *dynamic_cast<ParametricEQModule*>(node->getProcessor());
    eq.setBandEnabled(0, true);
    eq.setBandType(0, ParametricEQModule::BandType::LowCut);
    eq.setBandSlope(0, 48);
    const auto after = AIStateMapper::graphToJSON(graph);

    bool sawType = false, sawSlope = false;
    for (const auto& change : synth::computeDiff(before, after)) {
        if (change.paramId == "band1Type") {
            sawType = true;
            EXPECT_EQ(change.beforeText, "Low Shelf");
            EXPECT_EQ(change.afterText, "Low Cut");
            EXPECT_TRUE(change.describe().contains("Low Cut"));
        }
        if (change.paramId == "band1Slope") {
            sawSlope = true;
            EXPECT_EQ(change.beforeText, "12 dB/oct");
            EXPECT_EQ(change.afterText, "48 dB/oct");
        }
    }
    EXPECT_TRUE(sawType);
    EXPECT_TRUE(sawSlope);
}

TEST(AIEqBands, TheModelViewOmitsOffDefaultBandsAndKeepsChangedOnes) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(patchWithEqParams("{}"), graph, true, false));
    const auto untouchedCompact = eqNodeJson(AIStateMapper::graphToJSON(graph, true));
    const auto untouchedFull = eqNodeJson(AIStateMapper::graphToJSON(graph));
    EXPECT_FALSE(untouchedCompact.getProperty("params", {}).getDynamicObject()->hasProperty("band1On"))
        << "an EQ with no bands on shows no band at all";
    EXPECT_LT(paramCount(untouchedCompact), 10);
    EXPECT_EQ(paramCount(untouchedFull), paramCount(untouchedCompact) + 144) << "saving keeps every band";

    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(steepLowCutAndNotchPatch(), graph, true, false));
    const auto compact = eqNodeJson(AIStateMapper::graphToJSON(graph, true));
    auto* params = compact.getProperty("params", {}).getDynamicObject();
    ASSERT_NE(params, nullptr);
    for (const char* field : {"On", "Freq", "Gain", "Q", "Type", "Slope"}) {
        EXPECT_TRUE(params->hasProperty(juce::Identifier("band1" + juce::String(field)))) << field;
        EXPECT_TRUE(params->hasProperty(juce::Identifier("band7" + juce::String(field)))) << field;
        EXPECT_FALSE(params->hasProperty(juce::Identifier("band2" + juce::String(field)))) << field;
        EXPECT_FALSE(params->hasProperty(juce::Identifier("band24" + juce::String(field)))) << field;
    }
    EXPECT_EQ(params->getProperty("band7Type").toString(), "Notch");
    EXPECT_EQ(params->getProperty("band1Slope").toString(), "48 dB/oct");
    EXPECT_EQ(paramCount(compact), paramCount(untouchedCompact) + 12);

    // A band that is off but changed is kept whole: its values are the user's, not a default.
    auto* eq = eqOf(graph);
    eq->setBandFreq(3, 777.0f);
    const auto withOffEdited = eqNodeJson(AIStateMapper::graphToJSON(graph, true));
    EXPECT_TRUE(withOffEdited.getProperty("params", {}).getDynamicObject()->hasProperty("band4Freq"));
}

TEST(AIEqBands, PatchToGraphToModelViewToPatchIsStable) {
    juce::AudioProcessorGraph first;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(steepLowCutAndNotchPatch(), first, true, false));
    const auto modelView = AIStateMapper::graphToJSON(first, true);

    juce::AudioProcessorGraph second;
    ASSERT_TRUE(AIStateMapper::validatePatch(modelView, second, true, false).ok);
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(modelView, second, true, false));
    EXPECT_EQ(juce::JSON::toString(eqNodeJson(AIStateMapper::graphToJSON(second)).getProperty("params", {})),
              juce::JSON::toString(eqNodeJson(AIStateMapper::graphToJSON(first)).getProperty("params", {})));
    EXPECT_EQ(juce::JSON::toString(eqNodeJson(AIStateMapper::graphToJSON(second, true)).getProperty("params", {})),
              juce::JSON::toString(eqNodeJson(modelView).getProperty("params", {})));
}

TEST(AIEqBands, ThePromptTellsTheModelBandsAreAddedAndNamesEveryTypeAndSlope) {
    const auto schema = AIStateMapper::getModuleSchema();
    EXPECT_TRUE(schema.contains("Bands are ADDED, not slots to fill"));
    EXPECT_TRUE(schema.contains("first free (off) slot"));
    EXPECT_TRUE(schema.contains("Low Shelf, Bell, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt"));
    EXPECT_TRUE(schema.contains("6 dB/oct, 12 dB/oct, 18 dB/oct, 24 dB/oct, 30 dB/oct, 36 dB/oct, 48 dB/oct, "
                                "72 dB/oct, 96 dB/oct"));
}

TEST(AIEqBands, ThePromptListsTheBandParametersOnceNotOncePerBand) {
    const auto schema = AIStateMapper::getModuleSchema();
    for (const char* field : {"On", "Freq", "Gain", "Q", "Type", "Slope"})
        EXPECT_TRUE(schema.contains("| `bandN" + juce::String(field) + "` |")) << field;
    EXPECT_FALSE(schema.contains("| `band2Freq` |")) << "no per-band rows";
    EXPECT_FALSE(schema.contains("| `band24Gain` |"));
    EXPECT_FALSE(schema.contains("| `band1On` |"));
    EXPECT_TRUE(schema.contains("`band24On`"));
}

TEST(AIEqBands, TheCatalogAndValidationStillCarryEveryBandId) {
    const auto catalog = AIStateMapper::getModuleCatalogJson();
    for (const char* id : {"band1On", "band24Slope", "band13Q", "band24Type"})
        EXPECT_TRUE(catalog.contains(id)) << id;
    juce::AudioProcessorGraph graph;
    EXPECT_TRUE(
        AIStateMapper::validatePatch(patchWithEqParams(R"({"band24Gain":3,"band13Q":2})"), graph, true, false).ok);
}
