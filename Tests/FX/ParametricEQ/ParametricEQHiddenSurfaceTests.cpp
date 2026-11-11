// ParametricEQHiddenSurfaceTests.cpp
//
// Slots 5-24 and every band's Type and Slope exist in the engine and the saved model but no surface offers
// them yet: not the timeline's
// automation picker, not the modulation targets, not a patch diff. The AI's node JSON, catalog entry and
// schema are pinned byte-for-byte to the four-band module by ParametricEQSavedModelTests.cpp; the card (and so
// MIDI learn, which registers the card's widgets) by CardBodyWidgetKindTests.cpp.

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/PatchDiff.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "ParametricEQTestHelpers.h"

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

TEST(ParametricEQHiddenSurfaces, ModulationTargetsAreTheFourBandSet) {
    ParametricEQModule eq;
    const auto targets = eq.getModulationTargets();
    EXPECT_FALSE(targets.empty());
    for (const auto& target : targets)
        EXPECT_FALSE(ParametricEQModule::isHiddenParamId(target.paramId)) << target.paramId;
}

TEST(ParametricEQHiddenSurfaces, TheAutomationPickerOffersOnlyTheShownBands) {
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

    int eqRows = 0;
    bool offersBand1Gain = false;
    for (const auto& p : mc.getAutomatableParametersForTest(track.id)) {
        if (p.nodeUuid != eqUuid)
            continue;
        ++eqRows;
        offersBand1Gain = offersBand1Gain || p.paramId == "band1Gain";
        EXPECT_FALSE(ParametricEQModule::isHiddenParamId(p.paramId)) << p.paramId;
    }
    EXPECT_GT(eqRows, 0) << "the EQ is on the track";
    EXPECT_TRUE(offersBand1Gain);
}

// A hidden band's change is not a reviewable line: the reviewer could not find the control it names.
TEST(ParametricEQHiddenSurfaces, APatchDiffSkipsHiddenParameters) {
    juce::AudioProcessorGraph graph;
    auto node = graph.addNode(synth::AIStateMapper::createModule("Parametric EQ"));
    node->properties.set("uuid", "00000000-0000-4000-8000-0000000000e2");
    const auto before = synth::AIStateMapper::graphToJSON(graph);
    auto& eq = *dynamic_cast<ParametricEQModule*>(node->getProcessor());
    ASSERT_EQ(eq.addBand(400.0f, BandType::Notch), 0);
    eq.setBandGain(0, 5.0f);
    eq.setBandType(1, BandType::Tilt);
    const auto after = synth::AIStateMapper::graphToJSON(graph);
    juce::StringArray changed;
    for (const auto& change : synth::computeDiff(before, after))
        if (change.kind == synth::PatchChange::Kind::ParamChanged)
            changed.add(change.paramId);
    EXPECT_TRUE(changed.contains("band1Gain"));
    EXPECT_TRUE(changed.contains("band1On")) << "addBand took the lowest free slot, a shown one";
    for (const auto& id : changed)
        EXPECT_FALSE(ParametricEQModule::isHiddenParamId(id)) << id;
}
