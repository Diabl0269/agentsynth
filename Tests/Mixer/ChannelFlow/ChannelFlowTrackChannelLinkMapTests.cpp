// =================================================================================================
// TrackChannelLinkMap (Source/Mixer/TrackChannelLink.h): the one-scan, many-tracks form of the link rule must give
// every track exactly the answer the per-track walk (resolveTrackChannelLink / channelDisplayName) gives, before and
// after graph edits. Not a timing test: wall-clock thresholds flake on a loaded machine, and the equivalence is what
// keeps the optimisation behaviour-neutral (the cost itself is measured by the disabled ManyTracksProfile bench).
// =================================================================================================

#include "ChannelFlowTestFixture.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/TrackChannelLink.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>

namespace {

struct MapRigTCM {
    HostedPatchCFT patch;
    synth::TimelineDoc doc;

    juce::AudioProcessorGraph& graph() { return patch.engine.getGraph(); }
    juce::AudioProcessorGraph::Node* add(const juce::String& type, juce::String& uuid) {
        return addPlainNodeCFT(graph(), type, {0, 0}, uuid);
    }

    // Track In -> Oscillator -> strip (when `strip` is non-null); the Track In node is returned.
    juce::AudioProcessorGraph::Node* addTrack(const juce::String& name, juce::AudioProcessorGraph::Node* strip) {
        juce::String uuid, unused;
        auto* trackIn = add("Track In", uuid);
        const auto id = doc.addTrack(synth::TrackKind::Midi, name);
        doc.setTrackBinding(id, uuid);
        auto* osc = add("Oscillator", unused);
        constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
        graph().addConnection({{trackIn->nodeID, midi}, {osc->nodeID, midi}});
        if (strip != nullptr)
            graph().addConnection({{osc->nodeID, 0}, {strip->nodeID, 0}});
        return osc;
    }
};

void expectMapMatchesWalk(MapRigTCM& rig) {
    const synth::TrackChannelLinkMap map(rig.graph());
    for (const auto& track : rig.doc.getTracks()) {
        const auto walk = synth::resolveTrackChannelLink(rig.graph(), rig.doc, track.id);
        const auto fast = map.resolve(rig.doc, track.id);
        EXPECT_EQ(fast.hasChannel, walk.hasChannel) << track.name;
        EXPECT_EQ(fast.linked, walk.linked) << track.name;
        EXPECT_EQ(fast.stripId, walk.stripId) << track.name;
        EXPECT_EQ(fast.stripUuid, walk.stripUuid) << track.name;
        EXPECT_EQ(fast.feedingTrackSources, walk.feedingTrackSources) << track.name;
        if (walk.hasChannel)
            EXPECT_EQ(map.displayName(walk.stripId, rig.doc, "fallback"),
                      synth::channelDisplayName(rig.graph(), walk.stripId, rig.doc, "fallback"))
                << track.name;
    }
}

} // namespace

TEST(ChannelFlowTrackChannelLinkMap, MatchesThePerTrackWalkForEveryTrackAndAfterGraphChanges) {
    MapRigTCM rig;
    juce::String u;
    auto* stripA = rig.add("Channel Strip", u);
    auto* stripB = rig.add("Channel Strip", u);
    auto* oscSolo = rig.addTrack("Solo", stripA);
    rig.addTrack("SharedOne", stripB);
    auto* oscShared2 = rig.addTrack("SharedTwo", stripB);
    rig.addTrack("NoChannel", nullptr);
    rig.doc.addTrack(synth::TrackKind::Automation, "Auto");
    rig.doc.addTrack(synth::TrackKind::Midi, "Unbound");
    expectMapMatchesWalk(rig);

    // A second track joins strip A (breaks the link), then the shared one leaves strip B (re-forms it).
    rig.graph().addConnection({{oscShared2->nodeID, 0}, {stripA->nodeID, 0}});
    rig.graph().removeConnection({{oscShared2->nodeID, 0}, {stripB->nodeID, 0}});
    expectMapMatchesWalk(rig);

    // A re-route (the solo track's oscillator moves to strip B), then a removed node (orphaned binding).
    rig.graph().removeConnection({{oscSolo->nodeID, 0}, {stripA->nodeID, 0}});
    rig.graph().addConnection({{oscSolo->nodeID, 0}, {stripB->nodeID, 0}});
    expectMapMatchesWalk(rig);
    rig.graph().removeNode(oscSolo->nodeID);
    expectMapMatchesWalk(rig);
}
