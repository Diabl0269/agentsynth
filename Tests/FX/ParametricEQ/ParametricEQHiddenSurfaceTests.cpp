// ParametricEQHiddenSurfaceTests.cpp
//
// Every surface that lists parameters shows all 24 bands and every band's Type and Slope: the timeline's
// automation picker, the modulation targets (Freq, Gain and Q of every band, knob-only), a patch diff, the AI's
// view. (Once the surfaces hid bands 5-24; the file keeps its name.) The AI's node JSON, catalog entry and schema
// keep every entry the four-band module wrote (ParametricEQSavedModelTests.cpp); the card and its MIDI Learn
// bindings are in CardBodyWidgetKindTests.cpp and ModuleComponentEqBandRowTests.cpp.

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/PatchDiff.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "ParametricEQTestHelpers.h"
#include <algorithm>
#include <set>

using namespace eqtest;

namespace {

class QuietProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "Quiet"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"Model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return "Model"; }
    void setRequestTimeoutMs(int) override {}
    int getRequestTimeoutMs() const override { return 240000; }
};

} // namespace

TEST(ParametricEQSurfaces, ModulationTargetsCoverEveryBandsFreqGainAndQ) {
    ParametricEQModule eq;
    const auto targets = eq.getModulationTargets();
    std::set<int> channels;
    for (int b = 1; b <= ParametricEQModule::kMaxBands; ++b)
        for (const char* field : {"Freq", "Gain", "Q"}) {
            const auto id = "band" + juce::String(b) + field;
            const auto it =
                std::find_if(targets.begin(), targets.end(), [&id](const auto& t) { return t.paramId == id; });
            ASSERT_NE(it, targets.end()) << id;
            EXPECT_TRUE(it->knobOnly) << id << " is reached through its knob, never a gutter jack";
            EXPECT_TRUE(channels.insert(it->channelIndex).second) << id << " shares a channel";
            EXPECT_LT(it->channelIndex, eq.getTotalNumInputChannels()) << id;
            ASSERT_NE(eq.parameterForModTarget(*it), nullptr) << id;
        }
    EXPECT_EQ(eq.modulationChannelForParam("band2Freq"), 2) << "the original four keep their channels";
    EXPECT_EQ(eq.modulationChannelForParam("band4Q"), 13);
}

TEST(ParametricEQSurfaces, TypeAndSlopeAreAutomatableChoicesOfEveryBand) {
    ParametricEQModule eq;
    for (int b = 1; b <= ParametricEQModule::kMaxBands; ++b)
        for (const char* suffix : {"Type", "Slope"})
            EXPECT_TRUE(eq.isParameterExposed("band" + juce::String(b) + suffix));
}

TEST(ParametricEQSurfaces, TheAutomationPickerOffersEveryBand) {
    MainComponent mc(std::make_unique<QuietProvider>());
    mc.simulateAddMidiTrackClick();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto& track = mc.getTimelineDoc().getTracks().front();
    juce::AudioProcessorGraph::Node::Ptr trackIn;
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == track.bindingUuid)
            trackIn = node;
    ASSERT_NE(trackIn, nullptr);
    auto osc = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    auto eqNode = graph.addNode(synth::AIStateMapper::createModule("Parametric EQ"));
    const auto eqUuid = juce::Uuid().toDashedString();
    for (auto& node : {osc, eqNode}) {
        const auto uuid = node == eqNode ? eqUuid : juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        dynamic_cast<ModuleBase*>(node->getProcessor())->setNodeUuid(uuid);
    }
    ASSERT_TRUE(graph.addConnection({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                     {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    ASSERT_TRUE(graph.addConnection({{osc->nodeID, 0}, {eqNode->nodeID, 0}}));

    juce::StringArray offered;
    for (const auto& p : mc.getAutomatableParametersForTest(track.id))
        if (p.nodeUuid == eqUuid)
            offered.add(p.paramId);
    EXPECT_GT(offered.size(), 0) << "the EQ is on the track";
    for (int b = 1; b <= ParametricEQModule::kMaxBands; ++b)
        for (const char* field : {"Freq", "Gain", "Q"})
            EXPECT_TRUE(offered.contains("band" + juce::String(b) + field)) << b << field;
}

// A band past the first four is a reviewable line in a patch diff like any other.
TEST(ParametricEQSurfaces, APatchDiffShowsBandsFiveToTwentyFour) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(synth::AIStateMapper::createModule("Parametric EQ"));
    node->properties.set("uuid", "00000000-0000-4000-8000-0000000000e2");
    const auto before = synth::AIStateMapper::graphToJSON(graph);
    auto& eq = *dynamic_cast<ParametricEQModule*>(node->getProcessor());
    for (int b = 0; b < ParametricEQModule::kOriginalBands; ++b)
        eq.setBandEnabled(b, true);
    ASSERT_EQ(eq.addBand(400.0f, BandType::Notch), 4);
    eq.setBandGain(4, 5.0f);
    eq.setBandType(5, BandType::Tilt);
    const auto after = synth::AIStateMapper::graphToJSON(graph);
    juce::StringArray changed;
    for (const auto& change : synth::computeDiff(before, after))
        if (change.kind == synth::PatchChange::Kind::ParamChanged)
            changed.add(change.paramId);
    for (const char* id : {"band1On", "band5On", "band5Gain", "band5Freq", "band5Type", "band6Type"})
        EXPECT_TRUE(changed.contains(id)) << id;
}
