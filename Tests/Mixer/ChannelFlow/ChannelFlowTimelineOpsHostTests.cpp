// ChannelFlowTimelineOpsHostTests.cpp
//
// A timelineOps `addInstrumentTrack` op applied through the REAL app host
// (MainComponentTimelineOpsHost, installed on aiService by MainComponentSetup.cpp): the same
// instrument-track build "+ Track -> Instrument" runs, with the op's exact name and its effect
// inserts between the envelope stage and the Gate, as ONE undo step together with the rest of the
// batch. The op's contract with the host is covered call by call in
// Tests/Timeline/TimelineOpsInstrumentTrackTests.cpp.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>

namespace {

const synth::Macro* findMacroNamed(MainComponent& mc, const juce::String& name) {
    for (const auto& macro : mc.getGraphEditor().getMacros().getAll())
        if (macro.name == name)
            return &macro;
    return nullptr;
}

double rawParamValue(juce::AudioProcessor* processor, const juce::String& paramId) {
    for (auto* param : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
            ranged != nullptr && ranged->paramID == paramId)
            return ranged->convertFrom0to1(ranged->getValue());
    return -1.0;
}

constexpr const char* kEnvelopeJson = R"({"timelineOps": [
    {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
     "inserts": [{"type": "Filter", "params": {"cutoff": 800}}]},
    {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
        {"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100}]}]}]})";

} // namespace

TEST_F(ChannelFlowTest, TimelineOpsAddInstrumentTrackBuildsTheMenuChainThroughTheRealHost) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& aiService = mc.getAiServiceForTest();
    const auto before = snapshotCFT(mc);

    const juce::var envelope = juce::JSON::parse(kEnvelopeJson);
    const auto preview = aiService.previewTimelineOps(envelope);
    ASSERT_TRUE(preview.ok) << "the app must have installed its host: " << preview.message;
    EXPECT_EQ(snapshotCFT(mc).graph, before.graph) << "previewing builds nothing";

    const auto applied = aiService.applyTimelineOps(envelope);
    ASSERT_TRUE(applied.ok) << applied.message;

    // The track: named exactly as asked, bound to a Track In, carrying the placed clip.
    const synth::Track* bass = nullptr;
    for (const auto& track : mc.getTimelineDoc().getTracks())
        if (track.name == "Bass")
            bass = &track;
    ASSERT_NE(bass, nullptr);
    EXPECT_EQ(bass->kind, synth::TrackKind::Midi);
    EXPECT_EQ(bass->clips.size(), 1u);
    auto* trackIn = nodeForUuidCFT(graph, bass->bindingUuid);
    ASSERT_TRUE(isModuleOfTypeCFT(trackIn, ModuleType::TimelineMidiSource)) << "bound to its own Track In";

    // The macro, named after the track, holding the chain.
    const auto* macro = findMacroNamed(mc, "Bass");
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(bass->bindingUuid));
    auto* oscillator = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Oscillator);
    auto* vca = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::VCA);
    auto* filter = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Filter);
    auto* gate = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Gate);
    auto* strip = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ChannelStrip);
    ASSERT_NE(oscillator, nullptr);
    ASSERT_NE(vca, nullptr);
    ASSERT_NE(filter, nullptr) << "the insert joins the track's macro";
    ASSERT_NE(gate, nullptr);
    ASSERT_NE(strip, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {oscillator->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    // instrument -> envelope/amp -> insert -> Gate, both legs.
    auto* filterModule = dynamic_cast<ModuleBase*>(filter->getProcessor());
    ASSERT_NE(filterModule, nullptr);
    const int filterRight = filterModule->rightAudioLegChannel();
    ASSERT_GE(filterRight, 0);
    EXPECT_TRUE(graph.isConnected({{vca->nodeID, 0}, {filter->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{vca->nodeID, VCAModule::kRightBase}, {filter->nodeID, filterRight}}));
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, 0}, {gate->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, filterRight}, {gate->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{vca->nodeID, 0}, {gate->nodeID, 0}})) << "the insert sits between them";
    EXPECT_NEAR(rawParamValue(filter->getProcessor(), "cutoff"), 800.0, 1.0) << "the insert's params were applied";

    EXPECT_TRUE(stripFeedsMasterMixCFT(graph, strip, findNodeOfTypeCFT(graph, ModuleType::Master)));

    // ONE undo step for the whole batch: undo restores graph, doc and macros exactly; redo rebuilds.
    expectOneUndoStepCFT(mc, before);
    ASSERT_TRUE(mc.getUndoManager().undo());
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    EXPECT_EQ(findMacroNamed(mc, "Bass"), nullptr);
}

TEST_F(ChannelFlowTest, TimelineOpsAddInstrumentTrackRejectsAnExistingTrackNameWithoutBuilding) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    addInstrumentTrack(mc, "Sampler"); // "Sampler 1", through the menu
    const auto before = snapshotCFT(mc);

    const auto applied = mc.getAiServiceForTest().applyTimelineOps(juce::JSON::parse(
        R"({"timelineOps": [{"op": "addInstrumentTrack", "name": "Sampler 1", "instrument": "Oscillator"}]})"));
    EXPECT_FALSE(applied.ok);
    expectSameSnapshotCFT(snapshotCFT(mc), before);
}
