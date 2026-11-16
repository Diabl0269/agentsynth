// AIScopeContextTests.cpp
// A request shows the model each track's macro as one line unless the request names the track or the selection is in
// it; a replace answered from that smaller picture runs as a merge, so the hidden tracks survive.

#include "../Mixer/ChannelFlow/ChannelFlowTestRigs.h"
#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "MacroSet.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

class AIScopeContext : public ::testing::Test {
protected:
    HostedPatchCFT patch;
    GraphEditor editor{patch.engine};
    synth::AIIntegrationService service{patch.engine.getGraph()};
    std::vector<NodeID> selected;
    juce::String leadUuid, bassUuid;
    NodeID leadFilter{};

    void SetUp() override {
        const auto rig = buildLegacyRigCFT(patch.engine, patch.output, RigShapeCFT::SharedLfo);
        leadFilter = rig.filterA->nodeID;
        ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));
        ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInB->nodeID, "Bass"));
        leadUuid = nodeUuid(rig.filterA);
        bassUuid = nodeUuid(rig.filterB);
    }

    void installScope() {
        service.setScopeContext(&editor.getMacros(), [this] { return selected; });
    }

    juce::var currentPatch(const juce::String& request) {
        return service.buildProjectRequestBody(request)["currentPatch"];
    }

    static bool hasNode(const juce::var& json, const juce::String& uuid) {
        if (auto* nodes = json.getProperty("nodes", {}).getArray())
            for (const auto& n : *nodes)
                if (n.getProperty("uuid", {}).toString() == uuid)
                    return true;
        return false;
    }
};
} // namespace

TEST_F(AIScopeContext, ANamedTrackStaysExpandedAndTheOtherCollapsesToALine) {
    installScope();
    const auto json = currentPatch("make the lead brighter");
    EXPECT_TRUE(hasNode(json, leadUuid));
    EXPECT_FALSE(hasNode(json, bassUuid));

    const auto prompt = service.buildProjectRequestBody("make the lead brighter")["userPrompt"].toString();
    EXPECT_TRUE(prompt.contains("- \"Bass\":")) << prompt;
    EXPECT_FALSE(prompt.contains("- \"Lead\":")) << prompt;
    EXPECT_TRUE(prompt.contains("name the track in the request"));
    EXPECT_TRUE(prompt.contains("\"mode\": \"merge\""));
}

TEST_F(AIScopeContext, ASelectedModuleExpandsItsTrack) {
    installScope();
    selected = {leadFilter};
    const auto json = currentPatch("make it brighter");
    EXPECT_TRUE(hasNode(json, leadUuid));
    EXPECT_FALSE(hasNode(json, bassUuid));
}

TEST_F(AIScopeContext, ANameInsideAnotherWordDoesNotExpand) {
    installScope();
    EXPECT_FALSE(hasNode(currentPatch("add a bassoon sound"), bassUuid));
    EXPECT_TRUE(hasNode(currentPatch("BASS, please"), bassUuid));
}

TEST_F(AIScopeContext, WithNoContextTheWholePatchIsSentAsBefore) {
    juce::var expected = synth::AIStateMapper::graphToJSON(patch.engine.getGraph(), /*forModel=*/true);
    for (auto& n : *expected.getProperty("nodes", {}).getArray())
        if (auto* obj = n.getDynamicObject()) {
            const auto type = obj->getProperty("type").toString();
            if (type != "Sequencer" && type != "Poly Sequencer")
                obj->removeProperty("state");
            for (const char* key : {"cardView", "cardLayout", "controlStyle"})
                obj->removeProperty(key);
        }
    const auto body = service.buildProjectRequestBody("make the lead brighter");
    EXPECT_EQ(juce::JSON::toString(body["currentPatch"]), juce::JSON::toString(expected));
    EXPECT_EQ(body["userPrompt"].toString(), "make the lead brighter");
    EXPECT_FALSE(service.lastContextWasCollapsed());
}

TEST_F(AIScopeContext, ASixTrackProjectCollapsesToUnderFortyPercent) {
    auto& graph = patch.engine.getGraph();
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    for (int i = 0; i < 4; ++i) {
        juce::String unused;
        auto* trackIn = addPlainNodeCFT(graph, "Track In", {0, 800 + i * 200}, unused);
        auto* osc = addPlainNodeCFT(graph, "Oscillator", {200, 800 + i * 200}, unused);
        auto* filter = addPlainNodeCFT(graph, "Filter", {500, 800 + i * 200}, unused);
        graph.addConnection({{trackIn->nodeID, midi}, {osc->nodeID, midi}});
        graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
        graph.addConnection({{filter->nodeID, 0}, {patch.output->nodeID, 0}});
        ASSERT_TRUE(editor.makeChannelFromNode(trackIn->nodeID, "Track " + juce::String(i)));
    }
    ASSERT_EQ(editor.getMacros().size(), 6);

    const auto full = juce::JSON::toString(currentPatch("anything")).length();
    installScope();
    const auto collapsed = juce::JSON::toString(currentPatch("anything")).length();
    EXPECT_LT(collapsed * 10, full * 4) << collapsed << " of " << full;
}

TEST_F(AIScopeContext, AReplaceUnderACollapsedContextAppliesAsAMergeAndKeepsTheHiddenTracks) {
    installScope();
    currentPatch("make the lead brighter"); // the request the answer is for
    ASSERT_TRUE(service.lastContextWasCollapsed());
    const auto nodesBefore = patch.engine.getGraph().getNodes().size();

    ASSERT_TRUE(service.applyPatch(R"({"mode":"replace","nodes":[{"id":9001,"type":"LFO"}]})"));
    EXPECT_TRUE(service.didLastPatchRepairMode());
    const auto after = synth::AIStateMapper::graphToJSON(patch.engine.getGraph());
    EXPECT_TRUE(hasNode(after, bassUuid));
    EXPECT_TRUE(hasNode(after, leadUuid));
    EXPECT_EQ(patch.engine.getGraph().getNodes().size(), nodesBefore + 1);
}

TEST_F(AIScopeContext, ThePreviewOfThatReplaceShowsTheMerge) {
    installScope();
    currentPatch("make the lead brighter");
    juce::var before, after;
    ASSERT_TRUE(
        service.computePatchPreview(R"({"mode":"replace","nodes":[{"id":9001,"type":"LFO"}]})", false, before, after));
    EXPECT_TRUE(hasNode(after, bassUuid));
    EXPECT_EQ(after.getProperty("nodes", {}).getArray()->size(),
              before.getProperty("nodes", {}).getArray()->size() + 1);
}

TEST_F(AIScopeContext, ARequestThatNamesEveryTrackCollapsesNothing) {
    installScope();
    currentPatch("make the lead brighter");
    ASSERT_TRUE(service.lastContextWasCollapsed());
    currentPatch("tune the lead and the bass"); // both named: nothing left out
    EXPECT_FALSE(service.lastContextWasCollapsed());
}
