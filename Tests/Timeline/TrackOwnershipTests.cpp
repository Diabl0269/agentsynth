// TrackOwnershipTests.cpp -- synth::resolveTrackOwners, the pure rule deciding which timeline track plays a node
// (docs/timeline/automation.md#which-track-a-lane-lands-on). Uuids and edges only, no graph.
#include "Timeline/TrackOwnership.h"
#include <gtest/gtest.h>

namespace {

using synth::OwnershipGraph;
using synth::TrackId;

const TrackId kTrack1{1};
const TrackId kTrack2{2};

OwnershipGraph graphOf(std::initializer_list<const char*> nodes,
                       std::initializer_list<std::pair<const char*, const char*>> flow,
                       std::initializer_list<std::pair<const char*, const char*>> mod = {}) {
    OwnershipGraph g;
    for (auto n : nodes)
        g.nodes.push_back(n);
    for (auto [a, b] : flow)
        g.flowEdges.push_back({a, b});
    for (auto [a, b] : mod)
        g.modEdges.push_back({a, b});
    return g;
}

const std::vector<std::pair<TrackId, juce::String>> kTwoTracks{{kTrack1, "in1"}, {kTrack2, "in2"}};

} // namespace

TEST(TrackOwnership, SingleChainIsOwnedByItsTrack) {
    const auto g = graphOf({"in1", "synth", "fx", "out"}, {{"in1", "synth"}, {"synth", "fx"}, {"fx", "out"}});
    const auto owners = synth::resolveTrackOwners(g, {{kTrack1, "in1"}});
    EXPECT_EQ(owners.size(), 4u);
    for (auto n : {"in1", "synth", "fx", "out"})
        EXPECT_EQ(owners.at(n), kTrack1) << n;
}

TEST(TrackOwnership, NodeBothTracksReachIsUnowned) {
    const auto g =
        graphOf({"in1", "a", "in2", "b", "master"}, {{"in1", "a"}, {"a", "master"}, {"in2", "b"}, {"b", "master"}});
    const auto owners = synth::resolveTrackOwners(g, kTwoTracks);
    EXPECT_EQ(owners.at("a"), kTrack1);
    EXPECT_EQ(owners.at("b"), kTrack2);
    EXPECT_EQ(owners.count("master"), 0u);
}

TEST(TrackOwnership, InstrumentFeedingATrackChainJoinsIt) {
    // The instrument is not reachable from Track In (it feeds the channel), but only track 1 touches it.
    const auto g = graphOf({"in1", "chan", "inst"}, {{"in1", "chan"}, {"inst", "chan"}});
    const auto owners = synth::resolveTrackOwners(g, {{kTrack1, "in1"}});
    EXPECT_EQ(owners.at("inst"), kTrack1);
}

TEST(TrackOwnership, FeederTouchingTwoOwnersStaysUnowned) {
    const auto g =
        graphOf({"in1", "a", "in2", "b", "feeder"}, {{"in1", "a"}, {"in2", "b"}, {"feeder", "a"}, {"feeder", "b"}});
    const auto owners = synth::resolveTrackOwners(g, kTwoTracks);
    EXPECT_EQ(owners.count("feeder"), 0u);
}

TEST(TrackOwnership, LfoCabledIntoACvJackJoinsTheFilterTrack) {
    // lfo -> filter is a plain cable (a CV jack) and the lfo takes no input: a feeder like any other.
    const auto g = graphOf({"in1", "filter", "lfo"}, {{"in1", "filter"}, {"lfo", "filter"}});
    const auto owners = synth::resolveTrackOwners(g, {{kTrack1, "in1"}});
    EXPECT_EQ(owners.at("lfo"), kTrack1);
}

TEST(TrackOwnership, ModulatorWithConsumersInTwoTracksIsUnowned) {
    const auto g = graphOf({"in1", "a", "in2", "b", "lfo"}, {{"in1", "a"}, {"in2", "b"}}, {{"lfo", "a"}, {"lfo", "b"}});
    const auto owners = synth::resolveTrackOwners(g, kTwoTracks);
    EXPECT_EQ(owners.count("lfo"), 0u);
}

TEST(TrackOwnership, ModulatorWithConsumersInOneTrackJoinsIt) {
    const auto g = graphOf({"in1", "a", "b", "lfo"}, {{"in1", "a"}, {"a", "b"}}, {{"lfo", "a"}, {"lfo", "b"}});
    const auto owners = synth::resolveTrackOwners(g, {{kTrack1, "in1"}});
    EXPECT_EQ(owners.at("lfo"), kTrack1);
}

TEST(TrackOwnership, ModulatorRetriggeredByTrackAButModulatingTrackBBelongsToB) {
    // The incoming MIDI cable from track 1 must not claim the LFO: it has no signal leaving, so it follows
    // what it modulates (auto-arrange makes the same call).
    const auto g =
        graphOf({"in1", "a", "in2", "b", "lfo"}, {{"in1", "a"}, {"in2", "b"}, {"in1", "lfo"}}, {{"lfo", "b"}});
    const auto owners = synth::resolveTrackOwners(g, kTwoTracks);
    EXPECT_EQ(owners.at("lfo"), kTrack2);
    EXPECT_EQ(owners.at("a"), kTrack1);
}

TEST(TrackOwnership, ChainWalksThroughMacroPortNodes) {
    // A macro port node is an ordinary graph node: in1 -> portIn -> inner -> portOut -> out.
    const auto g = graphOf({"in1", "portIn", "inner", "portOut", "out"},
                           {{"in1", "portIn"}, {"portIn", "inner"}, {"inner", "portOut"}, {"portOut", "out"}});
    const auto owners = synth::resolveTrackOwners(g, {{kTrack1, "in1"}});
    EXPECT_EQ(owners.at("inner"), kTrack1);
    EXPECT_EQ(owners.at("out"), kTrack1);
}

TEST(TrackOwnership, NoTracksMeansNoOwners) {
    const auto g = graphOf({"a", "b"}, {{"a", "b"}});
    EXPECT_TRUE(synth::resolveTrackOwners(g, {}).empty());
}

TEST(TrackOwnership, UnconnectedNodeIsUnowned) {
    const auto g = graphOf({"in1", "a", "loose"}, {{"in1", "a"}});
    const auto owners = synth::resolveTrackOwners(g, {{kTrack1, "in1"}});
    EXPECT_EQ(owners.count("loose"), 0u);
}
