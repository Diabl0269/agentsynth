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
#include <optional>
#include <vector>

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
    // Split jacks, so every module's Right leg has a jack and the chain wires it.
    mc.getGraphEditor().setDefaultDualIOForNewModules(true);
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

// The real host hands back the uuid of every node an op can address: the Track In the track is
// bound to, the instrument, and one per insert in op order, each resolving to a node of the
// declared type inside the track's macro. In-response references (an insert's "id", the op's
// "instrumentId") will resolve against these.
TEST_F(ChannelFlowTest, TimelineOpsHostReturnsTheUuidOfEveryNodeItCreated) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& host = mc.getTimelineOpsHostForTest();

    const std::vector<synth::InstrumentTrackInsert> inserts{{"Filter", juce::JSON::parse(R"({"cutoff": 800})")},
                                                            {"Distortion", {}}};
    std::optional<synth::InstrumentTrackBuildResult> result;
    EXPECT_TRUE(
        host.recordBatch([&] { result = host.addInstrumentTrack("Bass", "Wavetable", false, inserts, {}, {}); }));
    ASSERT_TRUE(result.has_value());

    const auto* macro = findMacroNamed(mc, "Bass");
    ASSERT_NE(macro, nullptr);
    const auto& tracks = mc.getTimelineDoc().getTracks();
    ASSERT_FALSE(tracks.empty());
    EXPECT_EQ(tracks.back().bindingUuid, result->trackInUuid);
    EXPECT_TRUE(isModuleOfTypeCFT(nodeForUuidCFT(graph, result->trackInUuid), ModuleType::TimelineMidiSource));
    auto* instrument = nodeForUuidCFT(graph, result->instrumentUuid);
    ASSERT_NE(instrument, nullptr);
    EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(instrument->getProcessor()), "Wavetable");
    EXPECT_TRUE(macro->hasMember(result->instrumentUuid));

    ASSERT_EQ(result->insertUuids.size(), inserts.size());
    for (size_t i = 0; i < inserts.size(); ++i) {
        auto* node = nodeForUuidCFT(graph, result->insertUuids[i]);
        ASSERT_NE(node, nullptr) << inserts[i].type;
        EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(node->getProcessor()), inserts[i].type);
        EXPECT_TRUE(macro->hasMember(result->insertUuids[i])) << inserts[i].type;
    }
    EXPECT_NE(result->insertUuids[0], result->insertUuids[1]);
}

// The op's "instrumentId" and an insert's "id" are accepted through the real apply path and change
// nothing: neither becomes a node identity.
TEST_F(ChannelFlowTest, TimelineOpsAddInstrumentTrackIdsAreAcceptedAndInert) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    const auto applied = mc.getAiServiceForTest().applyTimelineOps(juce::JSON::parse(R"({"timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Sampler", "instrumentId": 7,
         "inserts": [{"type": "Filter", "id": 3}]}]})"));
    ASSERT_TRUE(applied.ok) << applied.message;
    const auto* macro = findMacroNamed(mc, "Bass");
    ASSERT_NE(macro, nullptr);
    EXPECT_NE(findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Filter), nullptr);
    EXPECT_EQ(nodeForUuidCFT(graph, "7"), nullptr) << "an id never becomes a node identity";
    EXPECT_EQ(nodeForUuidCFT(graph, "3"), nullptr);
}

// The real host applies the envelope params to the ADSR it builds (mono and poly) and returns that
// ADSR's uuid; a Sampler builds no ADSR and returns none.
TEST_F(ChannelFlowTest, TimelineOpsHostReturnsTheEnvelopeUuidAndAppliesItsParams) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& host = mc.getTimelineOpsHostForTest();
    const juce::var params = juce::JSON::parse(R"({"sustain": 0.0, "decay": 0.2, "release": 0.15})");

    std::optional<synth::InstrumentTrackBuildResult> mono, poly, sampler, saw;
    EXPECT_TRUE(host.recordBatch([&] {
        mono = host.addInstrumentTrack("Bass", "Oscillator", false, {}, params, {});
        poly = host.addInstrumentTrack("Pad", "Wavetable", true, {}, params, {});
        sampler = host.addInstrumentTrack("Keys", "Sampler", false, {}, {}, {});
        saw = host.addInstrumentTrack("Acid", "Oscillator", false, {}, {}, juce::JSON::parse(R"({"waveform": "Saw"})"));
    }));
    ASSERT_TRUE(mono.has_value());
    ASSERT_TRUE(poly.has_value());
    ASSERT_TRUE(sampler.has_value());

    for (const auto& [name, result] :
         {std::pair<const char*, const synth::InstrumentTrackBuildResult*>{"Bass", &*mono}, {"Pad", &*poly}}) {
        SCOPED_TRACE(name);
        ASSERT_TRUE(result->envelopeUuid.isNotEmpty());
        auto* node = nodeForUuidCFT(graph, result->envelopeUuid);
        ASSERT_TRUE(isModuleOfTypeCFT(node, ModuleType::ADSR));
        EXPECT_TRUE(findMacroNamed(mc, name)->hasMember(result->envelopeUuid));
        EXPECT_NEAR(rawParamValue(node->getProcessor(), "sustain"), 0.0, 1.0e-3);
        EXPECT_NEAR(rawParamValue(node->getProcessor(), "decay"), 0.2, 1.0e-3);
        EXPECT_NEAR(rawParamValue(node->getProcessor(), "release"), 0.15, 1.0e-3);
    }
    // instrumentParams land on the instrument itself (waveform choice 2 is Saw; the default is Sine).
    ASSERT_TRUE(saw.has_value());
    auto* sawNode = nodeForUuidCFT(graph, saw->instrumentUuid);
    ASSERT_NE(sawNode, nullptr);
    EXPECT_NEAR(rawParamValue(sawNode->getProcessor(), "waveform"), 2.0, 1.0e-3);
    auto* monoOsc = nodeForUuidCFT(graph, mono->instrumentUuid);
    ASSERT_NE(monoOsc, nullptr);
    EXPECT_NEAR(rawParamValue(monoOsc->getProcessor(), "waveform"), 0.0, 1.0e-3);
    EXPECT_TRUE(sampler->envelopeUuid.isEmpty()) << "a Sampler plays through its own one-shot envelope";
}
