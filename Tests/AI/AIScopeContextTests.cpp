// AIScopeContextTests.cpp
// A request shows the model each track's macro as one line unless the request names the track or the selection is in
// it; a replace answered from that smaller picture runs as a merge, so the hidden tracks survive.

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

class AIScopeContext : public ::testing::Test {
protected:
    struct HostedPatch {
        AudioEngine engine{AudioEngine::HostMode::Hosted};
        juce::AudioProcessorGraph::Node* output = nullptr;
        HostedPatch() {
            engine.getGraph().setPlayConfigDetails(2, 2, 44100.0, 256);
            output = engine.getGraph().addNode(synth::AIStateMapper::createModule("Audio Output")).get();
        }
    };
    HostedPatch patch;
    GraphEditor editor{patch.engine};
    synth::AIIntegrationService service{patch.engine.getGraph()};

    /** Adds Track In -> Oscillator -> Filter -> Audio Output at row `y` and returns {Track In, Filter}. */
    std::pair<juce::AudioProcessorGraph::Node*, juce::AudioProcessorGraph::Node*> addTrackChain(int y) {
        auto& graph = patch.engine.getGraph();
        constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
        const auto add = [&](const char* type, int x) {
            auto node = graph.addNode(synth::AIStateMapper::createModule(type));
            const auto uuid = juce::Uuid().toDashedString();
            node->properties.set("uuid", uuid);
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                module->setNodeUuid(uuid);
            node->properties.set("x", x);
            node->properties.set("y", y);
            return node.get();
        };
        auto* trackIn = add("Track In", 0);
        auto* osc = add("Oscillator", 200);
        auto* filter = add("Filter", 500);
        graph.addConnection({{trackIn->nodeID, midi}, {osc->nodeID, midi}});
        graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
        graph.addConnection({{filter->nodeID, 0}, {patch.output->nodeID, 0}});
        return {trackIn, filter};
    }

    static juce::String nodeUuid(juce::AudioProcessorGraph::Node* node) { return node->properties["uuid"].toString(); }
    std::vector<NodeID> selected;
    juce::String leadUuid, bassUuid;
    NodeID leadFilter{};

    void SetUp() override {
        const auto [leadIn, leadFilterNode] = addTrackChain(0);
        const auto [bassIn, bassFilterNode] = addTrackChain(400);
        leadFilter = leadFilterNode->nodeID;
        ASSERT_TRUE(editor.makeChannelFromNode(leadIn->nodeID, "Lead"));
        ASSERT_TRUE(editor.makeChannelFromNode(bassIn->nodeID, "Bass"));
        leadUuid = nodeUuid(leadFilterNode);
        bassUuid = nodeUuid(bassFilterNode);
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

// A new module given the id of a hidden module is refused ("Node id 115 already exists in this patch") after the
// request was paid for, so the model is told which ids the collapsed tracks use.
TEST_F(AIScopeContext, TheRequestListsTheNodeIdsOfTheCollapsedTracksSoANewModuleAvoidsThem) {
    installScope();
    const auto prompt = service.buildProjectRequestBody("make the lead brighter")["userPrompt"].toString();
    const auto marker = juce::String("Node ids used by the collapsed tracks (a new module must not reuse one): ");
    ASSERT_TRUE(prompt.contains(marker)) << prompt;
    const auto listed = prompt.fromFirstOccurrenceOf(marker, false, false).upToFirstOccurrenceOf("\n", false, false);

    juce::Array<int> bassIds;
    for (auto* node : patch.engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == bassUuid)
            bassIds.add((int)node->nodeID.uid);
    ASSERT_FALSE(bassIds.isEmpty());
    for (const int id : bassIds) {
        bool covered = false;
        for (const auto& part : juce::StringArray::fromTokens(listed, ", ", "")) {
            const auto lo = part.upToFirstOccurrenceOf("-", false, false).getIntValue();
            const auto hi = part.contains("-") ? part.fromFirstOccurrenceOf("-", false, false).getIntValue() : lo;
            covered = covered || (id >= lo && id <= hi);
        }
        EXPECT_TRUE(covered) << "hidden id " << id << " missing from: " << listed;
    }
    const auto nothingHidden = service.buildProjectRequestBody("tune the lead and the bass")["userPrompt"].toString();
    EXPECT_FALSE(nothingHidden.contains(marker));
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
    for (int i = 0; i < 4; ++i) {
        const auto [trackIn, filter] = addTrackChain(800 + i * 200);
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

// A live send hands applyPatch a plan with no "mode" after a collapsed context; the automation test mode never sets
// the collapsed flag, so only this test covers that path.
TEST_F(AIScopeContext, AModeLessPlanThatModulatesTheNamedTracksFilterAppliesAsAMergeUnderACollapsedContext) {
    installScope();
    currentPatch("make the lead brighter");
    ASSERT_TRUE(service.lastContextWasCollapsed());
    const auto nodesBefore = patch.engine.getGraph().getNodes().size();

    const auto plan = juce::String(R"({"nodes":[{"id":9001,"type":"LFO"}],"modulations":[{"source":9001,"dest":)") +
                      juce::String((int)leadFilter.uid) + R"(,"destParam":"cutoff"}]})";
    ASSERT_TRUE(service.applyPatch(plan)) << service.getLastPatchError();
    EXPECT_TRUE(service.didLastPatchRepairMode());
    const auto after = synth::AIStateMapper::graphToJSON(patch.engine.getGraph());
    EXPECT_TRUE(hasNode(after, bassUuid));
    // The LFO plus the attenuverter a modulation cable gets for its depth; nothing hidden is dropped.
    EXPECT_EQ(patch.engine.getGraph().getNodes().size(), nodesBefore + 2);

    bool modulatesLeadFilter = false;
    if (auto* mods = after.getProperty("modulations", {}).getArray())
        for (const auto& m : *mods)
            modulatesLeadFilter = modulatesLeadFilter || (int)m.getProperty("dest", {}) == (int)leadFilter.uid;
    EXPECT_TRUE(modulatesLeadFilter);
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
