// ChannelFlowTimelineOpsHostTests.cpp
//
// A timelineOps `addInstrumentTrack` op applied through the REAL app host
// (MainComponentTimelineOpsHost, installed on aiService by MainComponentSetup.cpp): the same
// instrument-track build "Add Track -> Instrument" runs, with the op's exact name and its effect
// inserts between the envelope stage and the Gate, as ONE undo step together with the rest of the
// batch. The op's contract with the host is covered call by call in
// Tests/Timeline/TimelineOpsInstrumentTrackTests.cpp.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/ChannelGainStaging/ChannelGainStaging.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/OfflineTransportDriver.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <set>
#include <vector>

namespace gs = synth::gainstaging;
using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// A stereo source that writes one constant to both outputs.
class ConstantSource : public juce::AudioProcessor {
public:
    explicit ConstantSource(float value)
        : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true))
        , value_(value) {}
    const juce::String getName() const override { return "Constant"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), value_, buffer.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    float value_;
};

void setGain(juce::AudioProcessorGraph& graph, NodeID strip, float db) {
    for (auto* param : graph.getNodeForId(strip)->getProcessor()->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param); ranged && ranged->paramID == "gain")
            ranged->setValueNotifyingHost(ranged->convertTo0to1(db));
}

double gainOf(juce::AudioProcessorGraph& graph, NodeID strip) {
    for (auto* param : graph.getNodeForId(strip)->getProcessor()->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param); ranged && ranged->paramID == "gain")
            return ranged->convertFrom0to1(ranged->getValue());
    return 0.0;
}

double peakOf(const std::vector<gs::ChannelPeak>& peaks, NodeID strip) {
    for (const auto& peak : peaks)
        if (peak.strip == strip)
            return peak.peakDb;
    return -kInf;
}

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
    auto* filter = findMacroFilterCFT(graph, *macro, /*bypassed=*/false);
    auto* defaultFilter = findMacroFilterCFT(graph, *macro, /*bypassed=*/true);
    auto* gate = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Gate);
    auto* strip = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ChannelStrip);
    ASSERT_NE(oscillator, nullptr);
    ASSERT_NE(vca, nullptr);
    ASSERT_NE(filter, nullptr) << "the insert joins the track's macro";
    ASSERT_NE(defaultFilter, nullptr) << "the track's own bypassed Filter sits between the VCA and the insert";
    ASSERT_NE(gate, nullptr);
    ASSERT_NE(strip, nullptr);

    EXPECT_TRUE(graph.isConnected({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {oscillator->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    // instrument -> envelope/amp -> insert -> Gate, both legs.
    auto* filterModule = dynamic_cast<ModuleBase*>(filter->getProcessor());
    ASSERT_NE(filterModule, nullptr);
    const int filterRight = filterModule->rightAudioLegChannel();
    ASSERT_GE(filterRight, 0);
    EXPECT_TRUE(graph.isConnected({{vca->nodeID, 0}, {defaultFilter->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{defaultFilter->nodeID, 0}, {filter->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{defaultFilter->nodeID, FilterModule::kRightBase}, {filter->nodeID, filterRight}}));
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, 0}, {gate->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, filterRight}, {gate->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{defaultFilter->nodeID, 0}, {gate->nodeID, 0}})) << "the insert sits between them";
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

// A Drum Kit track is Track In -> kit -> channel strip: the kit owns its envelopes, so no ADSR or VCA is built,
// and its own params land on the kit.
TEST_F(ChannelFlowTest, TimelineOpsHostBuildsADrumKitTrackWithoutAnEnvelope) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& host = mc.getTimelineOpsHostForTest();

    std::optional<synth::InstrumentTrackBuildResult> result;
    EXPECT_TRUE(host.recordBatch([&] {
        result = host.addInstrumentTrack("Beat", "Drum Kit", false, {}, {}, juce::JSON::parse(R"({"kickDecay": 0.6})"));
    }));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->envelopeUuid.isEmpty());

    auto* kit = nodeForUuidCFT(graph, result->instrumentUuid);
    ASSERT_NE(kit, nullptr);
    EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(kit->getProcessor()), "Drum Kit");
    EXPECT_NEAR(rawParamValue(kit->getProcessor(), "kickDecay"), 0.6, 1.0e-3);

    const auto* macro = findMacroNamed(mc, "Beat");
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(result->instrumentUuid));
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ADSR), nullptr);
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, *macro, ModuleType::VCA), nullptr);

    bool trackInReachesKit = false;
    for (const auto& connection : graph.getConnections())
        if (connection.destination.nodeID == kit->nodeID && connection.source.isMIDI() &&
            connection.destination.isMIDI())
            trackInReachesKit = true;
    EXPECT_TRUE(trackInReachesKit) << "Track In feeds the kit's MIDI input";
}

TEST(ChannelGainStagingRender, ARenderedWindowReadsEachChannelsPeakAndTheTrimPutsItAtTheTarget) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, 48000.0, 512);
    auto addFactory = [&](const juce::String& type) {
        return graph.addNode(synth::AIStateMapper::createModule(type))->nodeID;
    };
    const auto out = addFactory("Audio Output");
    const auto master = addFactory("Master");
    const auto hotStrip = addFactory("Channel Strip");
    const auto quietStrip = addFactory("Channel Strip");
    const auto hotSource = graph.addNode(std::make_unique<ConstantSource>(2.0f))->nodeID;
    const auto quietSource = graph.addNode(std::make_unique<ConstantSource>(0.05f))->nodeID;
    constexpr int kRight = ChannelStripModule::kRightBase;
    for (auto [src, strip] : {std::pair{hotSource, hotStrip}, std::pair{quietSource, quietStrip}}) {
        graph.addConnection({{src, 0}, {strip, 0}});
        graph.addConnection({{src, 1}, {strip, kRight}});
        graph.addConnection({{strip, 0}, {master, MasterModule::kMixLeft}});
        graph.addConnection({{strip, kRight}, {master, MasterModule::kMixRight}});
    }
    graph.addConnection({{master, 0}, {out, 0}});
    graph.addConnection({{master, 1}, {out, 1}});
    synth::OfflineTransportDriver driver(engine, 48000.0, 512, 2);

    const std::vector<gs::BeatWindow> windows{{0.0, 1.0}};
    auto peaks = gs::measureChannelPeaks(engine, windows);
    EXPECT_NEAR(peakOf(peaks, hotStrip), 6.0, 0.2);
    EXPECT_NEAR(peakOf(peaks, quietStrip), -26.0, 0.2);

    const auto trims = gs::planTrims(peaks, {hotStrip, quietStrip}, [&](NodeID id) { return gainOf(graph, id); });
    ASSERT_EQ(trims.size(), 1u);
    setGain(graph, trims[0].strip, (float)trims[0].toDb);

    peaks = gs::measureChannelPeaks(engine, windows);
    EXPECT_NEAR(peakOf(peaks, hotStrip), gs::kTargetPeakDb - gs::kTrimMarginDb, 0.3);
    EXPECT_NEAR(peakOf(peaks, quietStrip), -26.0, 0.2) << "a quiet channel is never raised";
    engine.releaseFromHost();
    engine.shutdown();
}

// The app path: an edit plan with a track too hot for its own channel, applied through the real host, and the one
// undo step it comes in.
TEST_F(ChannelFlowTest, AnAiBuiltTrackThatIsOverTheTargetComesOutAtItAsPartOfTheSameUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& aiService = mc.getAiServiceForTest();

    const juce::var envelope = juce::JSON::parse(R"({"timelineOps": [
        {"op": "addInstrumentTrack", "name": "Lead", "instrument": "Oscillator", "poly": true,
         "instrumentParams": {"waveform": "Saw", "level": 1.0, "unison": 7, "detune": 0.5},
         "inserts": [{"type": "Filter", "params": {"cutoff": 20000, "outputLevel": 12}}]},
        {"op": "placeClips", "track": "Lead", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
            {"startBeat": 0, "lengthBeats": 2, "pitch": 36, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 39, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 42, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 45, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 48, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 51, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 54, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 57, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 60, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 63, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 66, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 69, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 72, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 75, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 78, "velocity": 127},
            {"startBeat": 0, "lengthBeats": 2, "pitch": 81, "velocity": 127}]}]}]})");
    const auto before = gs::channelStripIds(graph);
    const auto applied = aiService.applyTimelineOps(envelope);
    ASSERT_TRUE(applied.ok) << applied.message;

    std::set<NodeID> added;
    for (const auto id : gs::channelStripIds(graph))
        if (before.count(id) == 0)
            added.insert(id);
    ASSERT_EQ(added.size(), 1u);
    const auto strip = *added.begin();

    const auto peaks = gs::measureChannelPeaks(mc.getAudioEngine(), gs::planWindows(mc.getTimelineDoc()));
    EXPECT_LE(peakOf(peaks, strip), gs::kTargetPeakDb + 0.3) << "the new channel is at or under the target";
    EXPECT_GE(peakOf(peaks, strip), gs::kTargetPeakDb - 3.0) << "and was lowered only as far as needed";
    EXPECT_LT(gainOf(graph, strip), 0.0);

    mc.getUndoManager().undo();
    EXPECT_TRUE(gs::channelStripIds(graph).size() == before.size()) << "one undo takes the whole build away";
    mc.getUndoManager().redo();
    ASSERT_EQ(gs::channelStripIds(graph).size(), before.size() + 1);
    EXPECT_LT(gainOf(graph, strip), 0.0) << "redo brings the build back with its fader";
}
