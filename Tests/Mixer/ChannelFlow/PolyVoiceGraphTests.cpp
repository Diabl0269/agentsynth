// The whole-voice-graph Poly switch at the Core layer (Source/Mixer/ChannelFlows/PolyVoiceGraph.cpp): which modules a
// Poly click on one module reaches (planPolyVoiceGraph) and what switching them does to the graph
// (applyPolyVoiceGraph). Bare graphs wired by hand; the click, the question and the undo step are in
// Tests/UI/Graph/GraphEditor/GraphEditorPolyChainTests.cpp.

#include "ChannelFlowTestRigs.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/ChannelFlows/PolyVoiceGraph.h"
#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <set>
#include <tuple>

namespace {

using Node = juce::AudioProcessorGraph::Node;
using NodeID = juce::AudioProcessorGraph::NodeID;
constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;

struct PolyRig {
    HostedPatchCFT patch;
    int nextX = 0;

    juce::AudioProcessorGraph& graph() { return patch.engine.getGraph(); }

    Node* add(const juce::String& type) {
        juce::String unused;
        return addPlainNodeCFT(graph(), type, {nextX += 300, 0}, unused);
    }
    Node* addWithUuid(const juce::String& type, juce::String& uuid) {
        return addPlainNodeCFT(graph(), type, {nextX += 300, 0}, uuid);
    }
    void midi(Node* from, Node* to) { graph().addConnection({{from->nodeID, kMidi}, {to->nodeID, kMidi}}); }
    void wire(Node* from, int fromCh, Node* to, int toCh) {
        graph().addConnection({{from->nodeID, fromCh}, {to->nodeID, toCh}});
    }

    static bool isPoly(Node* node) { return synth::isProcessorPoly(node->getProcessor()); }

    int count(NodeID from, NodeID to, bool midiOnly = false) {
        int n = 0;
        for (const auto& c : graph().getConnections())
            if (c.source.nodeID == from && c.destination.nodeID == to && (!midiOnly || c.source.isMIDI()))
                ++n;
        return n;
    }
    std::vector<Node*> nodesOfType(const juce::String& name) {
        std::vector<Node*> found;
        for (auto* node : graph().getNodes())
            if (node->getProcessor()->getName() == name)
                found.push_back(node);
        return found;
    }
};

bool has(const std::vector<NodeID>& ids, Node* node) {
    return std::find(ids.begin(), ids.end(), node->nodeID) != ids.end();
}

// MIDI source -> Osc (+ -> ADSR), Osc -> Filter -> VCA, ADSR -> VCA gain CV: the hand-built mono instrument.
struct Instrument {
    Node *source, *osc, *filter, *adsr, *vca;
};
Instrument buildInstrument(PolyRig& rig) {
    Instrument i{};
    i.source = rig.add("Midi Input");
    i.osc = rig.add("Oscillator");
    i.filter = rig.add("Filter");
    i.adsr = rig.add("ADSR");
    i.vca = rig.add("VCA");
    rig.midi(i.source, i.osc);
    rig.midi(i.source, i.adsr);
    rig.wire(i.osc, 0, i.filter, 0);
    rig.wire(i.filter, 0, i.vca, 0);
    rig.wire(i.adsr, 0, i.vca, 1);
    return i;
}

} // namespace

TEST(PolyVoiceGraphTests, DescribesTrackNames) {
    EXPECT_EQ(synth::describeTrackNames({}), "");
    EXPECT_EQ(synth::describeTrackNames({"Bass"}), "Bass");
    EXPECT_EQ(synth::describeTrackNames({"Bass", "Lead"}), "Bass and Lead");
    EXPECT_EQ(synth::describeTrackNames({"Bass", "Lead", "Pad"}), "Bass, Lead and Pad");
}

TEST(PolyVoiceGraphTests, OnlyModulesWithAPolyParameterAreCapable) {
    PolyRig rig;
    for (const auto* type : {"Oscillator", "Wavetable", "Noise", "Filter", "ADSR", "VCA"})
        EXPECT_TRUE(synth::hasPolyParameter(rig.add(type)->getProcessor())) << type;
    for (const auto* type : {"Delay", "LFO", "Sampler", "Poly MIDI", "Voice Mixer"})
        EXPECT_FALSE(synth::hasPolyParameter(rig.add(type)->getProcessor())) << type;
}

TEST(PolyVoiceGraphTests, PlanReachesTheWholeChainFromAnyModuleOfIt) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    for (auto* start : {i.osc, i.filter, i.adsr, i.vca}) {
        const auto plan = synth::planPolyVoiceGraph(rig.graph(), start->nodeID, {});
        EXPECT_EQ(plan.thisTrackNodes.size(), 4u);
        EXPECT_TRUE(plan.otherTrackNodes.empty());
        for (auto* module : {i.osc, i.filter, i.adsr, i.vca})
            EXPECT_TRUE(has(plan.thisTrackNodes, module));
    }
}

TEST(PolyVoiceGraphTests, ParallelBranchesAreIncluded) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    auto* osc2 = rig.add("Oscillator");
    auto* adsr2 = rig.add("ADSR");
    auto* filter2 = rig.add("Filter");
    rig.midi(i.source, osc2);
    rig.midi(i.source, adsr2);
    rig.wire(osc2, 0, filter2, 0);
    rig.wire(filter2, 0, i.vca, 0);
    rig.wire(adsr2, 0, i.vca, 1);

    const auto plan = synth::planPolyVoiceGraph(rig.graph(), i.osc->nodeID, {});
    EXPECT_EQ(plan.thisTrackNodes.size(), 7u);
    for (auto* module : {osc2, adsr2, filter2})
        EXPECT_TRUE(has(plan.thisTrackNodes, module));
}

TEST(PolyVoiceGraphTests, ModulatorsAndHiddenAttenuvertersAreWalkedThrough) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    auto* lfo = rig.add("LFO");
    auto* otherFilter = rig.add("Filter");
    // An LFO modulating two filters ties them together; the routing's hidden attenuverter is an ordinary node here.
    rig.patch.engine.addModRouting(lfo->nodeID, 0, i.filter->nodeID, 1);
    rig.patch.engine.addModRouting(lfo->nodeID, 0, otherFilter->nodeID, 1);

    const auto plan = synth::planPolyVoiceGraph(rig.graph(), i.osc->nodeID, {});
    EXPECT_TRUE(has(plan.thisTrackNodes, otherFilter));
}

TEST(PolyVoiceGraphTests, StopsAtAChannelStripSoAnotherTrackIsUntouched) {
    PolyRig rig;
    juce::String uuidA, uuidB;
    auto* trackA = rig.addWithUuid("Track In", uuidA);
    const auto a = buildInstrument(rig);
    auto* trackB = rig.addWithUuid("Track In", uuidB);
    const auto b = buildInstrument(rig);
    auto* stripA = rig.add("Channel Strip");
    auto* stripB = rig.add("Channel Strip");
    auto* master = rig.add("Master");
    rig.midi(trackA, a.osc);
    rig.midi(trackB, b.osc);
    rig.wire(a.vca, 0, stripA, 0);
    rig.wire(b.vca, 0, stripB, 0);
    rig.wire(stripA, 0, master, MasterModule::kMixLeft);
    rig.wire(stripB, 0, master, MasterModule::kMixLeft);

    const auto plan = synth::planPolyVoiceGraph(rig.graph(), a.osc->nodeID, {{uuidA, "Bass"}, {uuidB, "Lead"}});
    EXPECT_EQ(plan.thisTrackNodes.size(), 4u);
    EXPECT_TRUE(plan.otherTrackNodes.empty());
    for (auto* module : {b.osc, b.filter, b.adsr, b.vca})
        EXPECT_FALSE(has(plan.thisTrackNodes, module));
}

TEST(PolyVoiceGraphTests, ACableThatReallyJoinsTwoTracksNamesTheOtherOne) {
    PolyRig rig;
    juce::String uuidA, uuidB;
    auto* trackA = rig.addWithUuid("Track In", uuidA);
    const auto a = buildInstrument(rig);
    auto* trackB = rig.addWithUuid("Track In", uuidB);
    auto* oscB = rig.add("Oscillator");
    rig.midi(trackA, a.osc);
    rig.midi(trackB, oscB);
    rig.wire(oscB, 0, a.filter, 0); // track B's oscillator also plays through track A's filter

    const auto plan = synth::planPolyVoiceGraph(rig.graph(), a.osc->nodeID, {{uuidA, "Bass"}, {uuidB, "Lead"}});
    ASSERT_EQ(plan.otherTrackNodes.size(), 1u);
    EXPECT_TRUE(has(plan.otherTrackNodes, oscB));
    EXPECT_TRUE(has(plan.thisTrackNodes, a.filter)); // reached by both tracks: it is this track's too
    ASSERT_EQ(plan.otherTrackNames.size(), 1u);
    EXPECT_EQ(plan.otherTrackNames[0], "Lead");
}

TEST(PolyVoiceGraphTests, ModulesNoTrackReachesCountAsThisTrack) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    const auto plan = synth::planPolyVoiceGraph(rig.graph(), i.vca->nodeID, {{"no-such-node", "Ghost"}});
    EXPECT_EQ(plan.thisTrackNodes.size(), 4u);
    EXPECT_TRUE(plan.otherTrackNodes.empty());
}

TEST(PolyVoiceGraphTests, PolyOnInsertsOnePolyMidiWiredPitchAndGate) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    const auto plan = synth::planPolyVoiceGraph(rig.graph(), i.osc->nodeID, {});

    const auto result = synth::applyPolyVoiceGraph(rig.graph(), plan.thisTrackNodes, true);

    EXPECT_EQ(result.flipped.size(), 4u);
    for (auto* module : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_TRUE(PolyRig::isPoly(module));
    const auto polyMidis = rig.nodesOfType("Poly MIDI");
    ASSERT_EQ(polyMidis.size(), 1u);
    ASSERT_EQ(result.addedPolyMidi.size(), 1u);
    auto* pm = polyMidis.front();
    EXPECT_EQ(rig.count(i.source->nodeID, pm->nodeID, true), 1);
    for (int voice = 0; voice < 8; ++voice) {
        EXPECT_EQ(rig.count(pm->nodeID, i.osc->nodeID), 16) << "the Pitch fan and the Gate fan";
        const juce::AudioProcessorGraph::Connection pitch{{pm->nodeID, voice}, {i.osc->nodeID, voice}};
        const juce::AudioProcessorGraph::Connection gate{{pm->nodeID, 8 + voice}, {i.adsr->nodeID, voice}};
        const juce::AudioProcessorGraph::Connection oscGate{{pm->nodeID, 8 + voice},
                                                            {i.osc->nodeID, OscillatorModule::kGateBase + voice}};
        EXPECT_TRUE(rig.graph().isConnected(pitch)) << voice;
        EXPECT_TRUE(rig.graph().isConnected(gate)) << voice;
        EXPECT_TRUE(rig.graph().isConnected(oscGate))
            << "Poly MIDI's Gate fan restarts the oscillator's phase " << voice;
    }
    EXPECT_EQ(rig.count(pm->nodeID, i.adsr->nodeID), 8);
    EXPECT_EQ(rig.count(i.source->nodeID, i.osc->nodeID, true), 1)
        << "an oscillator's raw MIDI cable stays: poly voice 0 still reads it";
    EXPECT_EQ(rig.count(i.source->nodeID, i.adsr->nodeID, true), 0) << "a poly ADSR's raw MIDI cable is redundant";
}

TEST(PolyVoiceGraphTests, PolyOnReusesAPolyMidiThatTheSameSourceAlreadyFeeds) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    auto* existing = rig.add("Poly MIDI");
    rig.midi(i.source, existing);

    const auto result = synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.adsr->nodeID}, true);

    EXPECT_TRUE(result.addedPolyMidi.empty());
    EXPECT_EQ(rig.nodesOfType("Poly MIDI").size(), 1u);
    EXPECT_EQ(rig.count(existing->nodeID, i.osc->nodeID), 16);
    EXPECT_EQ(rig.count(existing->nodeID, i.adsr->nodeID), 8);
}

TEST(PolyVoiceGraphTests, PolyOnLeavesAUnplacedPolyMidiForTheCallerWhenAsked) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    synth::PolyVoiceGraphOptions options;
    options.leavePolyMidiUnplaced = true;
    const auto result = synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.adsr->nodeID}, true, options);
    ASSERT_EQ(result.addedPolyMidi.size(), 1u);
    EXPECT_FALSE(rig.graph().getNodeForId(result.addedPolyMidi[0])->properties.contains("x"));

    PolyRig placed;
    const auto j = buildInstrument(placed);
    const auto placedResult = synth::applyPolyVoiceGraph(placed.graph(), {j.osc->nodeID}, true);
    auto* node = placed.graph().getNodeForId(placedResult.addedPolyMidi[0]);
    EXPECT_TRUE(node->properties.contains("x"));
    EXPECT_EQ((int)node->properties["x"] % 8, 0);
}

TEST(PolyVoiceGraphTests, NodesAlreadyInTheTargetStateAreNotCounted) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    synth::setProcessorPoly(i.filter->getProcessor(), true);
    const auto result = synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.filter->nodeID, i.vca->nodeID}, true);
    EXPECT_EQ(result.flipped.size(), 2u);
}

TEST(PolyVoiceGraphTests, PolyOffRestoresRawMidiAndRemovesTheOrphanedPolyMidi) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    const std::vector<NodeID> chain{i.osc->nodeID, i.filter->nodeID, i.adsr->nodeID, i.vca->nodeID};
    synth::applyPolyVoiceGraph(rig.graph(), chain, true);
    // The user took the oscillator's raw cable away too while poly was on.
    rig.graph().removeConnection({{i.source->nodeID, kMidi}, {i.osc->nodeID, kMidi}});
    ASSERT_EQ(rig.nodesOfType("Poly MIDI").size(), 1u);

    const auto result = synth::applyPolyVoiceGraph(rig.graph(), chain, false);

    EXPECT_EQ(result.flipped.size(), 4u);
    for (auto* module : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_FALSE(PolyRig::isPoly(module));
    EXPECT_TRUE(rig.nodesOfType("Poly MIDI").empty());
    EXPECT_EQ(result.removedPolyMidi.size(), 1u);
    EXPECT_EQ(rig.count(i.source->nodeID, i.osc->nodeID, true), 1);
    EXPECT_EQ(rig.count(i.source->nodeID, i.adsr->nodeID, true), 1);
}

TEST(PolyVoiceGraphTests, PolyOffKeepsAPolyMidiThatStillFeedsSomethingElse) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    auto* otherOsc = rig.add("Oscillator");
    rig.midi(i.source, otherOsc);
    synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.adsr->nodeID, otherOsc->nodeID}, true);
    ASSERT_EQ(rig.nodesOfType("Poly MIDI").size(), 1u);

    synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.adsr->nodeID}, false);

    const auto polyMidis = rig.nodesOfType("Poly MIDI");
    ASSERT_EQ(polyMidis.size(), 1u);
    EXPECT_EQ(rig.count(polyMidis[0]->nodeID, otherOsc->nodeID), 16);
    EXPECT_EQ(rig.count(polyMidis[0]->nodeID, i.osc->nodeID), 0);
}

TEST(PolyVoiceGraphTests, RemovingThePolyMidiDropsItFromItsMacro) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    const std::vector<NodeID> chain{i.osc->nodeID, i.adsr->nodeID};
    const auto on = synth::applyPolyVoiceGraph(rig.graph(), chain, true);
    ASSERT_EQ(on.addedPolyMidi.size(), 1u);
    const auto uuid = rig.graph().getNodeForId(on.addedPolyMidi[0])->properties["uuid"].toString();

    synth::MacroSet macros;
    synth::Macro macro;
    macro.name = "Voice";
    macro.members = {uuid, i.osc->properties["uuid"].toString()};
    const auto macroId = macros.add(macro);

    synth::PolyVoiceGraphOptions options;
    options.macros = &macros;
    synth::applyPolyVoiceGraph(rig.graph(), chain, false, options);

    ASSERT_NE(macros.find(macroId), nullptr);
    EXPECT_FALSE(macros.find(macroId)->hasMember(uuid));
}

TEST(PolyVoiceGraphTests, PolyMidiHoldsAChordOnSeveralVoices) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    for (auto* module : {i.vca, i.filter})
        rig.wire(module, 0, rig.patch.output, 0);
    synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.adsr->nodeID}, true);

    auto* polyMidi = dynamic_cast<PolyMidiModule*>(rig.nodesOfType("Poly MIDI").front()->getProcessor());
    ASSERT_NE(polyMidi, nullptr);
    rig.patch.engine.prepareForHost(44100.0, 512, 2, 2);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (const int note : {60, 64, 67})
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
    buffer.clear();
    rig.patch.engine.processHostBlock(buffer, midi);
    rig.patch.engine.releaseFromHost();

    EXPECT_EQ(std::popcount(static_cast<unsigned>(polyMidi->getActiveVoiceMask())), 3);
}

// Regression test for FRO848: a single note must be as loud in the default poly chain as in the mono one, so
// both VCAs sit at gain 0.5 (-6 dB per-voice headroom) and the poly VCA sums its voices at unity.
TEST(PolyVoiceGraphTests, DefaultInstrumentChainVcasShareTheSameHeadroomGain) {
    PolyRig rig;
    auto* trackIn = rig.add("Midi Input");
    auto* osc = rig.add("Oscillator");
    *dynamic_cast<juce::AudioParameterBool*>(findParameterByID(osc->getProcessor(), "poly")) = true;

    const auto chain =
        synth::addPolyEnvelopeAndVCAForInstrument(rig.graph(), *trackIn, *osc, {0, 0}, {300, 0}, {600, 0});
    ASSERT_NE(chain.vca, nullptr);

    auto* gain = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(chain.vca->getProcessor(), "gain"));
    ASSERT_NE(gain, nullptr);
    EXPECT_FLOAT_EQ(gain->get(), 0.5f);

    // The mono ADSR + VCA path uses the same gain.
    auto* monoOsc = rig.add("Oscillator");
    const auto mono =
        synth::addEnvelopeAndVCAForRawInstrument(rig.graph(), *trackIn, *monoOsc, 14, {0, 100}, {300, 100});
    ASSERT_NE(mono.vca, nullptr);
    auto* monoGain = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(mono.vca->getProcessor(), "gain"));
    ASSERT_NE(monoGain, nullptr);
    EXPECT_FLOAT_EQ(monoGain->get(), 0.5f);
}

TEST(PolyVoiceGraphTests, PolyOnWiresAWavetablesGateFanToItsOwnGateBase) {
    PolyRig rig;
    auto* source = rig.add("Midi Input");
    auto* wavetable = rig.add("Wavetable");
    rig.midi(source, wavetable);

    synth::applyPolyVoiceGraph(rig.graph(), {wavetable->nodeID}, true);

    const auto polyMidis = rig.nodesOfType("Poly MIDI");
    ASSERT_EQ(polyMidis.size(), 1u);
    for (int voice = 0; voice < 8; ++voice) {
        EXPECT_TRUE(rig.graph().isConnected({{polyMidis[0]->nodeID, voice}, {wavetable->nodeID, voice}})) << voice;
        EXPECT_TRUE(rig.graph().isConnected(
            {{polyMidis[0]->nodeID, 8 + voice}, {wavetable->nodeID, WavetableOscillatorModule::kGateBase + voice}}))
            << voice;
    }
    EXPECT_EQ(rig.count(polyMidis[0]->nodeID, wavetable->nodeID), 16);
}

TEST(PolyVoiceGraphTests, PolyOffRemovesTheGateFanCablesToo) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.adsr->nodeID}, true);
    ASSERT_EQ(rig.nodesOfType("Poly MIDI").size(), 1u);

    synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.adsr->nodeID}, false);

    for (const auto& c : rig.graph().getConnections())
        EXPECT_FALSE(c.destination.nodeID == i.osc->nodeID && !c.destination.isMIDI() &&
                     c.destination.channelIndex >= OscillatorModule::kGateBase)
            << "no cable may be left on the Gate fan";
}

TEST(PolyVoiceGraphTests, TheDefaultPolyInstrumentChainWiresTheGateFanOfAnOscillatorAndAWavetable) {
    for (const char* type : {"Oscillator", "Wavetable"}) {
        PolyRig rig;
        auto* trackIn = rig.add("Midi Input");
        auto* instrument = rig.add(type);
        *dynamic_cast<juce::AudioParameterBool*>(findParameterByID(instrument->getProcessor(), "poly")) = true;
        const int gateBase = dynamic_cast<ModuleBase*>(instrument->getProcessor())->polyGateInputChannel();
        ASSERT_GE(gateBase, 0) << type;

        const auto chain =
            synth::addPolyEnvelopeAndVCAForInstrument(rig.graph(), *trackIn, *instrument, {0, 0}, {300, 0}, {600, 0});
        ASSERT_NE(chain.vca, nullptr);

        auto* polyMidi = rig.nodesOfType("Poly MIDI").front();
        for (int voice = 0; voice < 8; ++voice)
            EXPECT_TRUE(
                rig.graph().isConnected({{polyMidi->nodeID, 8 + voice}, {instrument->nodeID, gateBase + voice}}))
                << type << " voice " << voice;
    }
}

// ---- no redundant raw MIDI cable into a poly ADSR ----

namespace {

using Connection = juce::AudioProcessorGraph::Connection;

std::set<std::tuple<juce::uint32, int, juce::uint32, int>> connectionSet(PolyRig& rig) {
    std::set<std::tuple<juce::uint32, int, juce::uint32, int>> out;
    for (const auto& c : rig.graph().getConnections())
        out.insert({c.source.nodeID.uid, c.source.channelIndex, c.destination.nodeID.uid, c.destination.channelIndex});
    return out;
}

// Plays one chord with a release through the Hosted engine and returns every output sample of every block.
std::vector<float> renderChord(PolyRig& rig) {
    rig.patch.engine.prepareForHost(44100.0, 512, 2, 2);
    std::vector<float> samples;
    for (int block = 0; block < 24; ++block) {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        if (block == 0)
            for (const int note : {60, 64, 67})
                midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 40);
        if (block == 3)
            midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.6f), 100);
        if (block == 12)
            for (const int note : {60, 64, 67, 72})
                midi.addEvent(juce::MidiMessage::noteOff(1, note), 7);
        rig.patch.engine.processHostBlock(buffer, midi);
        samples.insert(samples.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 512);
    }
    rig.patch.engine.releaseFromHost();
    return samples;
}

} // namespace

TEST(PolyVoiceGraphTests, PolyOnDropsTheRawMidiCableIntoAnAdsrAndPolyOffBringsItBack) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    const std::vector<NodeID> chain{i.osc->nodeID, i.filter->nodeID, i.adsr->nodeID, i.vca->nodeID};
    const auto before = connectionSet(rig);

    synth::applyPolyVoiceGraph(rig.graph(), chain, true);
    EXPECT_FALSE(rig.graph().isConnected({{i.source->nodeID, kMidi}, {i.adsr->nodeID, kMidi}}));
    EXPECT_TRUE(rig.graph().isConnected({{i.source->nodeID, kMidi}, {i.osc->nodeID, kMidi}}));
    ASSERT_EQ(rig.nodesOfType("Poly MIDI").size(), 1u);

    synth::applyPolyVoiceGraph(rig.graph(), chain, false);
    EXPECT_TRUE(rig.graph().isConnected({{i.source->nodeID, kMidi}, {i.adsr->nodeID, kMidi}}));
    EXPECT_EQ(connectionSet(rig), before) << "poly on then off returns exactly the original connections";
}

TEST(PolyVoiceGraphTests, ARawMidiCableToAMonoAdsrFromTheSameSourceStays) {
    PolyRig rig;
    const auto i = buildInstrument(rig);
    auto* monoAdsr = rig.add("ADSR");
    rig.midi(i.source, monoAdsr);
    const auto before = connectionSet(rig);

    synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.adsr->nodeID}, true);
    EXPECT_FALSE(rig.graph().isConnected({{i.source->nodeID, kMidi}, {i.adsr->nodeID, kMidi}}));
    EXPECT_TRUE(rig.graph().isConnected({{i.source->nodeID, kMidi}, {monoAdsr->nodeID, kMidi}}))
        << "the mono module keeps its cable";
    EXPECT_FALSE(PolyRig::isPoly(monoAdsr));

    synth::applyPolyVoiceGraph(rig.graph(), {i.osc->nodeID, i.adsr->nodeID}, false);
    EXPECT_EQ(connectionSet(rig), before);
}

TEST(PolyVoiceGraphTests, TrackInIntoAnAdsrLosesTheRawCableAndGetsItBackOnMono) {
    PolyRig rig;
    juce::String uuid;
    auto* trackIn = rig.addWithUuid("Track In", uuid);
    auto* osc = rig.add("Oscillator");
    auto* adsr = rig.add("ADSR");
    auto* vca = rig.add("VCA");
    rig.midi(trackIn, osc);
    rig.midi(trackIn, adsr);
    rig.wire(osc, 0, vca, 0);
    rig.wire(adsr, 0, vca, 1);
    const auto before = connectionSet(rig);
    const auto plan = synth::planPolyVoiceGraph(rig.graph(), adsr->nodeID, {{uuid, "Keys"}});

    synth::applyPolyVoiceGraph(rig.graph(), plan.thisTrackNodes, true);
    EXPECT_FALSE(rig.graph().isConnected({{trackIn->nodeID, kMidi}, {adsr->nodeID, kMidi}}));
    EXPECT_TRUE(rig.graph().isConnected({{trackIn->nodeID, kMidi}, {osc->nodeID, kMidi}}));
    auto* polyMidi = rig.nodesOfType("Poly MIDI").front();
    EXPECT_EQ(rig.count(polyMidi->nodeID, adsr->nodeID), 8);
    const auto replan = synth::planPolyVoiceGraph(rig.graph(), adsr->nodeID, {{uuid, "Keys"}});
    EXPECT_TRUE(has(replan.thisTrackNodes, vca)) << "the track still reaches the chain through Poly MIDI";

    synth::applyPolyVoiceGraph(rig.graph(), replan.thisTrackNodes, false);
    EXPECT_EQ(connectionSet(rig), before);
}

// The oscillator is the other module the question named: its raw cable is kept because poly voice 0 reads it (a raw
// note-on restarts voice 0, a raw note is its pitch when no pitch CV is patched). This pins that the raw cable is not
// provably redundant: with it, voice 0 sounds a note that Poly MIDI does not feed.
TEST(PolyVoiceGraphTests, APolyOscillatorStillUsesItsRawMidiCable) {
    auto build = [](PolyRig& rig, bool withRaw) {
        auto* source = rig.add("Midi Input");
        auto* osc = rig.add("Oscillator");
        rig.wire(osc, 0, rig.patch.output, 0);
        rig.midi(source, osc);
        synth::applyPolyVoiceGraph(rig.graph(), {osc->nodeID}, true);
        if (!withRaw)
            rig.graph().removeConnection({{source->nodeID, kMidi}, {osc->nodeID, kMidi}});
        // Cut Poly MIDI -> osc so only the raw MIDI could ever make voice 0 sound.
        for (const auto& c : rig.graph().getConnections())
            if (c.destination.nodeID == osc->nodeID && !c.source.isMIDI())
                rig.graph().removeConnection(c);
    };
    PolyRig with;
    build(with, true);
    PolyRig without;
    build(without, false);
    const auto a = renderChord(with);
    const auto b = renderChord(without);
    float peakWith = 0.0f, peakWithout = 0.0f;
    for (const auto x : a)
        peakWith = std::max(peakWith, std::abs(x));
    for (const auto x : b)
        peakWithout = std::max(peakWithout, std::abs(x));
    EXPECT_GT(peakWith, peakWithout) << "voice 0 plays from the raw MIDI alone";
}

// The user note "with poly on the pitch controls don't work": Octave, Coarse and Fine move the pitch of a poly
// oscillator that Poly MIDI feeds, and Poly MIDI's cables land only on the per-voice Pitch and Gate inputs.
TEST(PolyVoiceGraphTests, PitchKnobsMovePolyOscillatorFedByPolyMidiAndItsCablesLandOnlyOnPitchAndGate) {
    auto zeroCrossingsFor = [](const juce::String& param, float value) {
        PolyRig rig;
        auto* source = rig.add("Midi Input");
        auto* osc = rig.add("Oscillator");
        rig.wire(osc, 0, rig.patch.output, 0);
        rig.midi(source, osc);
        synth::applyPolyVoiceGraph(rig.graph(), {osc->nodeID}, true);
        if (param.isNotEmpty())
            if (auto* p = findParameterByID(osc->getProcessor(), param))
                p->setValueNotifyingHost(p->convertTo0to1(value));
        const auto out = renderChord(rig);
        int crossings = 0;
        for (size_t n = 1; n < out.size(); ++n)
            crossings += (out[n - 1] < 0.0f) != (out[n] < 0.0f) ? 1 : 0;
        return crossings;
    };
    const int base = zeroCrossingsFor({}, 0.0f);
    ASSERT_GT(base, 50);
    EXPECT_GT(zeroCrossingsFor("octave", 1.0f), base * 3 / 2) << "one octave up roughly doubles the pitch";
    EXPECT_GT(zeroCrossingsFor("coarse", 12.0f), base * 3 / 2) << "twelve semitones up roughly doubles the pitch";
    EXPECT_GT(zeroCrossingsFor("fine", 100.0f), base) << "fine moves the pitch up";

    PolyRig rig;
    auto* source = rig.add("Midi Input");
    auto* osc = rig.add("Oscillator");
    auto* adsr = rig.add("ADSR");
    rig.midi(source, osc);
    rig.midi(source, adsr);
    synth::applyPolyVoiceGraph(rig.graph(), {osc->nodeID, adsr->nodeID}, true);
    auto* polyMidi = rig.nodesOfType("Poly MIDI").front();
    for (const auto& c : rig.graph().getConnections()) {
        if (c.source.nodeID != polyMidi->nodeID)
            continue;
        if (c.destination.nodeID == osc->nodeID) {
            const bool pitch = c.source.channelIndex < 8 && c.destination.channelIndex == c.source.channelIndex;
            const bool gate = c.source.channelIndex >= 8 &&
                              c.destination.channelIndex == OscillatorModule::kGateBase + c.source.channelIndex - 8;
            EXPECT_TRUE(pitch || gate) << "Poly MIDI " << c.source.channelIndex << " -> osc "
                                       << c.destination.channelIndex;
        } else {
            EXPECT_EQ(c.destination.nodeID, adsr->nodeID);
            EXPECT_EQ(c.destination.channelIndex, c.source.channelIndex - 8) << "ADSR takes only the gate fan";
        }
    }
}
