// Regression test for FRO1230: a project saved while a joined Oscillator or Wavetable jack carried its left leg alone
// holds that one cable as the left leg fanned onto both legs of the far pair. Now the jack owns both legs, so loading
// such a project re-points the far Right onto the jack's right leg (bug kind jack-raw-mismatch: the jack would claim
// both legs while its raw edges played the left one twice, and Width never reached the far Right). A poly jack, a
// split jack and a lone cable onto a far Right are left exactly as saved.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include <gtest/gtest.h>
#include <set>
#include <vector>

namespace {

using Graph = juce::AudioProcessorGraph;
using NodeID = Graph::NodeID;

void setParam(juce::AudioProcessor& m, const char* id, float value) {
    auto* p = findParameterByID(&m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

struct Saved {
    Graph graph;
    NodeID src, far;
};

/** A saved patch: `srcType` (Dual I/O off, `poly`) feeding `far` through `edges` (raw source -> raw far channel). */
void build(Saved& s, const char* srcType, bool poly, bool dual, std::unique_ptr<juce::AudioProcessor> far,
           const std::vector<std::pair<int, int>>& edges) {
    auto src = synth::AIStateMapper::createModule(srcType);
    setParam(*src, "poly", poly ? 1.0f : 0.0f);
    setParam(*src, "dualIO", dual ? 1.0f : 0.0f);
    s.src = s.graph.addNode(std::move(src))->nodeID;
    s.far = s.graph.addNode(std::move(far))->nodeID;
    for (const auto& [from, to] : edges)
        ASSERT_TRUE(s.graph.addConnection({{s.src, from}, {s.far, to}})) << from << " -> " << to;
}

/** Saves and reopens the patch; returns the reopened graph's audio edges between the two modules. */
std::set<std::pair<int, int>> reopen(Saved& s, Graph& reopened) {
    const auto json = synth::AIStateMapper::graphToJSON(s.graph);
    EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, reopened, /*clearExisting*/ true, /*trusted*/ true));
    std::set<std::pair<int, int>> out;
    for (const auto& c : reopened.getConnections())
        if (!c.source.isMIDI() && !c.destination.isMIDI())
            out.insert({c.source.channelIndex, c.destination.channelIndex});
    return out;
}

} // namespace

TEST(SingleCableWidthLoad, ASavedLeftLegFanIntoACollapsedPairTakesTheJacksRightLeg) {
    for (const char* type : {"Oscillator", "Wavetable"}) {
        SCOPED_TRACE(type);
        Saved s;
        build(s, type, /*poly*/ false, /*dual*/ false, std::make_unique<DelayModule>(), {{0, 0}, {0, 1}});
        const int right =
            dynamic_cast<ModuleBase*>(s.graph.getNodeForId(s.src)->getProcessor())->rightAudioLegChannel();
        Graph reopened;
        const auto edges = reopen(s, reopened);
        EXPECT_EQ(edges, (std::set<std::pair<int, int>>{{0, 0}, {right, 1}}));
    }
}

TEST(SingleCableWidthLoad, ASavedLeftLegFanIntoASplitFilterTakesTheJacksRightLeg) {
    Saved s;
    build(s, "Oscillator", false, false, std::make_unique<FilterModule>(), {{0, 0}, {0, FilterModule::kRightBase}});
    Graph reopened;
    EXPECT_EQ(reopen(s, reopened),
              (std::set<std::pair<int, int>>{{0, 0}, {OscillatorModule::kRightBase, FilterModule::kRightBase}}));
}

TEST(SingleCableWidthLoad, PolySplitAndLoneCablesLoadExactlyAsSaved) {
    {
        SCOPED_TRACE("poly: the joined jack is the left legs only");
        Saved s;
        build(s, "Oscillator", /*poly*/ true, false, std::make_unique<DelayModule>(), {{0, 0}, {0, 1}});
        Graph reopened;
        EXPECT_EQ(reopen(s, reopened), (std::set<std::pair<int, int>>{{0, 0}, {0, 1}}));
    }
    {
        SCOPED_TRACE("split: Audio L was patched to both legs by hand");
        Saved s;
        build(s, "Oscillator", false, /*dual*/ true, std::make_unique<DelayModule>(), {{0, 0}, {0, 1}});
        Graph reopened;
        EXPECT_EQ(reopen(s, reopened), (std::set<std::pair<int, int>>{{0, 0}, {0, 1}}));
    }
    {
        SCOPED_TRACE("a lone cable onto the far Right is not a fan");
        Saved s;
        build(s, "Oscillator", false, false, std::make_unique<DelayModule>(), {{0, 1}});
        Graph reopened;
        EXPECT_EQ(reopen(s, reopened), (std::set<std::pair<int, int>>{{0, 1}}));
    }
}

TEST(SingleCableWidthLoad, AReopenedDefaultOscillatorPlaysTheSameSamplesOnBothLegs) {
    // Sound compatibility: at Unison 1 (or Width 0) the right leg is a copy of the left, so moving the far Right from
    // the left leg to the right leg changes no sample.
    OscillatorModule osc;
    setParam(osc, "dualIO", 0.0f);
    osc.setPlayConfigDetails(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels(), 44100.0, 512);
    osc.prepareToPlay(44100.0, 512);
    juce::AudioBuffer<float> buf(std::max(osc.getTotalNumInputChannels(), osc.getTotalNumOutputChannels()), 512);
    for (int b = 0; b < 8; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
        osc.processBlock(buf, midi);
        for (int i = 0; i < 512; ++i)
            ASSERT_EQ(buf.getSample(0, i), buf.getSample(OscillatorModule::kRightBase, i)) << "block " << b;
    }
    EXPECT_GT(buf.getMagnitude(0, 0, 512), 0.05f);
}
