// The Pitch cable lookup behind the Custom filter's per-note modes (Source/Mixer/ChannelFlows/UpstreamPitchSource.cpp):
// which output jack feeds the voice chain a Filter sits in, and whether the Filter's own Pitch input is cabled.
// Bare graphs wired by hand.

#include "AudioEngine/ConnectionIndex.h"
#include "ChannelFlowTestRigs.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/ChannelFlows/UpstreamPitchSource.h"
#include "Modules/FilterModule.h"
#include <gtest/gtest.h>

namespace {

using Node = juce::AudioProcessorGraph::Node;

struct PitchRig {
    HostedPatchCFT patch;
    int nextX = 0;

    juce::AudioProcessorGraph& graph() { return patch.engine.getGraph(); }
    Node* add(const juce::String& type) {
        juce::String unused;
        auto* node = addPlainNodeCFT(graph(), type, {nextX += 300, 0}, unused);
        synth::setProcessorPoly(node->getProcessor(), true); // Poly MIDI and the like ignore it
        return node;
    }
    void wire(Node* from, int fromCh, Node* to, int toCh) {
        graph().addConnection({{from->nodeID, fromCh}, {to->nodeID, toCh}});
    }
    /** The raw channel of `node`'s first input with this role. */
    static int inputWith(Node* node, PortRole role) {
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        for (int ch = 0; ch < 40; ++ch)
            if (module->mapInputChannel(ch).role == role)
                return ch;
        return -1;
    }
    std::optional<synth::PitchSource> sourceFor(Node* filter) {
        const synth::ConnectionIndex cables(graph());
        return synth::upstreamPitchSource(cables, graph(), filter->nodeID);
    }
    bool pitchCabled(Node* filter) {
        const synth::ConnectionIndex cables(graph());
        return synth::hasPitchCable(cables, graph(), filter->nodeID);
    }
};

} // namespace

TEST(UpstreamPitchSource, FindsTheJackThatFeedsTheOscillatorBehindTheFilter) {
    PitchRig rig;
    auto* polyMidi = rig.add("Poly MIDI");
    auto* osc = rig.add("Oscillator");
    auto* filter = rig.add("Filter");
    rig.wire(polyMidi, 0, osc, PitchRig::inputWith(osc, PortRole::Pitch));
    rig.wire(osc, 0, filter, 0);

    const auto source = rig.sourceFor(filter);
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->node, polyMidi->nodeID);
    EXPECT_EQ(source->visibleJack, 0);
    EXPECT_FALSE(rig.pitchCabled(filter));
}

TEST(UpstreamPitchSource, WalksThroughAnotherModuleInTheChain) {
    PitchRig rig;
    auto* polyMidi = rig.add("Poly MIDI");
    auto* osc = rig.add("Oscillator");
    auto* first = rig.add("Filter");
    auto* second = rig.add("Filter");
    rig.wire(polyMidi, 0, osc, PitchRig::inputWith(osc, PortRole::Pitch));
    rig.wire(osc, 0, first, 0);
    rig.wire(first, 0, second, 0);

    const auto source = rig.sourceFor(second);
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->node, polyMidi->nodeID);
}

TEST(UpstreamPitchSource, NothingUpstreamCarriesPitchGivesNoSource) {
    PitchRig rig;
    auto* osc = rig.add("Oscillator"); // no Pitch cable
    auto* filter = rig.add("Filter");
    rig.wire(osc, 0, filter, 0);
    EXPECT_FALSE(rig.sourceFor(filter).has_value());
}

TEST(UpstreamPitchSource, TheFiltersOwnPitchCableIsReportedAndNotTakenForAnUpstreamOne) {
    PitchRig rig;
    auto* polyMidi = rig.add("Poly MIDI");
    auto* filter = rig.add("Filter");
    rig.wire(polyMidi, 0, filter, FilterModule::kPitchBase);
    EXPECT_TRUE(rig.pitchCabled(filter));
    EXPECT_FALSE(rig.sourceFor(filter).has_value());
}

// Regression test for FRO1097: a mono Oscillator's pitch jack is a plain CV input, so the walk found no Pitch source
// and a mono Filter's per-note modes could not cable themselves.
TEST(UpstreamPitchSource, FindsThePolyMidiThatPlaysAMonoOscillator) {
    PitchRig rig;
    auto* polyMidi = rig.add("Poly MIDI");
    auto* osc = rig.add("Oscillator");
    auto* filter = rig.add("Filter");
    synth::setProcessorPoly(osc->getProcessor(), false);
    synth::setProcessorPoly(filter->getProcessor(), false);
    rig.wire(polyMidi, 0, osc, 0); // the mono Oscillator's pitch CV
    rig.wire(osc, 0, filter, 0);

    const auto source = rig.sourceFor(filter);
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->node, polyMidi->nodeID);
    EXPECT_EQ(source->visibleJack, 0);
}

TEST(UpstreamPitchSource, AnLfoOnAMonoOscillatorsPitchIsNotAPitchSource) {
    PitchRig rig;
    auto* lfo = rig.add("LFO");
    auto* osc = rig.add("Oscillator");
    auto* filter = rig.add("Filter");
    synth::setProcessorPoly(osc->getProcessor(), false);
    rig.wire(lfo, 0, osc, 0);
    rig.wire(osc, 0, filter, 0);
    EXPECT_FALSE(rig.sourceFor(filter).has_value());
}
