// KnobCableRoundTripTests.cpp
// A cable on a knob-only channel added later survives save and load: the saved connection names the channel, and the
// loaded module still has it.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MacroControlModule.h"
#include "Modules/MasterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/SequencerModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include <gtest/gtest.h>

using synth::AIStateMapper;

namespace {

// Counts the cables that end on `channel` of a node whose processor is a `T`.
template <typename T>
int cablesInto(juce::AudioProcessorGraph& graph, int channel) {
    int count = 0;
    for (const auto& c : graph.getConnections()) {
        auto* node = graph.getNodeForId(c.destination.nodeID);
        if (node != nullptr && dynamic_cast<T*>(node->getProcessor()) != nullptr &&
            c.destination.channelIndex == channel)
            ++count;
    }
    return count;
}

template <typename T>
void roundTrip(int channel) {
    juce::AudioProcessorGraph graph;
    auto target = graph.addNode(std::make_unique<T>());
    auto lfo = graph.addNode(std::make_unique<LFOModule>());
    ASSERT_TRUE(graph.addConnection({{lfo->nodeID, 0}, {target->nodeID, channel}}));
    ASSERT_EQ(cablesInto<T>(graph, channel), 1);

    const auto json = AIStateMapper::graphToJSON(graph);
    juce::AudioProcessorGraph loaded;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(json, loaded, /*clearExisting=*/true, /*trusted=*/true));
    // An attenuverter may sit between the two; either way a cable ends on the same channel.
    EXPECT_EQ(cablesInto<T>(loaded, channel), 1);
}

} // namespace

// Regression test for FRO1556: these knobs took no cable and so had no channel to save.
TEST(KnobCableRoundTrip, WavetableUnison) {
    roundTrip<WavetableOscillatorModule>(WavetableOscillatorModule::kUnisonCvChannel);
}
TEST(KnobCableRoundTrip, OscillatorPhase) { roundTrip<OscillatorModule>(OscillatorModule::kJackInputs); }
TEST(KnobCableRoundTrip, SequencerBpm) { roundTrip<SequencerModule>(0); }
TEST(KnobCableRoundTrip, ChannelStripGain) { roundTrip<ChannelStripModule>(ChannelStripModule::kFirstCvChannel); }
TEST(KnobCableRoundTrip, MasterGain) { roundTrip<MasterModule>(MasterModule::kGainCvChannel); }
TEST(KnobCableRoundTrip, MacroKnob) { roundTrip<MacroControlModule>(3); }
