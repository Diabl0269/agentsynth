// AutomationPlacementTests.cpp
//
// synth::computeTrackOwnership / resolveOwningTrack / findOrCreateLaneHostTrack
// (Source/Timeline/AutomationPlacement.h, docs/timeline/track-automation.md#which-track-owns-a-lane)
// against small hand-built graphs: a module belongs to a track when that track's source reaches it
// over signal edges and no other track's source does; everything else is global.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "Timeline/AutomationPlacement.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>

namespace {

using Node = juce::AudioProcessorGraph::Node;

struct PlacementRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    synth::TimelineDoc doc;

    juce::AudioProcessorGraph& graph() { return engine.getGraph(); }

    Node* add(const juce::String& type) {
        auto node = graph().addNode(synth::AIStateMapper::createModule(type));
        if (node == nullptr)
            return nullptr;
        const auto uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);
        return node.get();
    }
    static juce::String uuidOf(Node* node) { return node->properties["uuid"].toString(); }

    synth::TrackId addTrack(synth::TrackKind kind, const juce::String& sourceType, Node*& sourceOut) {
        sourceOut = add(sourceType);
        const auto id = doc.addTrack(kind, "T");
        doc.setTrackBinding(id, uuidOf(sourceOut));
        return id;
    }
    void audio(Node* from, Node* to) { graph().addConnection({{from->nodeID, 0}, {to->nodeID, 0}}); }
    void midi(Node* from, Node* to) {
        constexpr int m = juce::AudioProcessorGraph::midiChannelIndex;
        graph().addConnection({{from->nodeID, m}, {to->nodeID, m}});
    }
    std::optional<synth::TrackId> owner(Node* node) { return synth::resolveOwningTrack(graph(), doc, uuidOf(node)); }
};

int cutoffChannelOf(Node* filter) {
    if (auto* module = dynamic_cast<ModuleBase*>(filter->getProcessor()))
        for (const auto& target : module->getModulationTargets())
            return target.channelIndex;
    return -1;
}

} // namespace

TEST(AutomationPlacementTest, AChainReachedByOneTrackBelongsToIt) {
    PlacementRig rig;
    Node* trackIn = nullptr;
    const auto track = rig.addTrack(synth::TrackKind::Midi, "Track In", trackIn);
    auto* osc = rig.add("Oscillator");
    auto* filter = rig.add("Filter");
    auto* strip = rig.add("Channel Strip");
    rig.midi(trackIn, osc);
    rig.audio(osc, filter);
    rig.audio(filter, strip);

    for (auto* node : {trackIn, osc, filter, strip}) {
        const auto owner = rig.owner(node);
        ASSERT_TRUE(owner.has_value()) << node->getProcessor()->getName();
        EXPECT_EQ(*owner, track);
    }
}

TEST(AutomationPlacementTest, AModuleTwoTracksReachIsShared) {
    PlacementRig rig;
    Node *inA = nullptr, *inB = nullptr;
    const auto trackA = rig.addTrack(synth::TrackKind::Midi, "Track In", inA);
    rig.addTrack(synth::TrackKind::Midi, "Track In", inB);
    auto* oscA = rig.add("Oscillator");
    auto* oscB = rig.add("Oscillator");
    auto* bus = rig.add("Filter");
    rig.midi(inA, oscA);
    rig.midi(inB, oscB);
    rig.audio(oscA, bus);
    rig.audio(oscB, bus);

    EXPECT_FALSE(rig.owner(bus).has_value()) << "reached by both tracks: global";
    ASSERT_TRUE(rig.owner(oscA).has_value());
    EXPECT_EQ(*rig.owner(oscA), trackA) << "upstream of the merge each chain is still its own";
}

TEST(AutomationPlacementTest, AFreeModulatorIsGlobalAndItsTargetKeepsItsTrack) {
    PlacementRig rig;
    Node* trackIn = nullptr;
    const auto track = rig.addTrack(synth::TrackKind::Midi, "Track In", trackIn);
    auto* osc = rig.add("Oscillator");
    auto* filter = rig.add("Filter");
    auto* lfo = rig.add("LFO");
    rig.midi(trackIn, osc);
    rig.audio(osc, filter);
    const int cutoff = cutoffChannelOf(filter);
    ASSERT_GE(cutoff, 0);
    rig.engine.addModRouting(lfo->nodeID, 0, filter->nodeID, cutoff);

    EXPECT_FALSE(rig.owner(lfo).has_value()) << "no track reaches a free LFO";
    ASSERT_TRUE(rig.owner(filter).has_value());
    EXPECT_EQ(*rig.owner(filter), track);
}

TEST(AutomationPlacementTest, AModulationLegIsNotASignalPath) {
    PlacementRig rig;
    Node *inA = nullptr, *inB = nullptr;
    const auto trackA = rig.addTrack(synth::TrackKind::Midi, "Track In", inA);
    rig.addTrack(synth::TrackKind::Midi, "Track In", inB);
    auto* lfoA = rig.add("LFO");
    auto* oscB = rig.add("Oscillator");
    auto* filterB = rig.add("Filter");
    rig.midi(inA, lfoA); // track A plays an LFO...
    rig.midi(inB, oscB);
    rig.audio(oscB, filterB);
    rig.engine.addModRouting(lfoA->nodeID, 0, filterB->nodeID, cutoffChannelOf(filterB)); // ...into B's cutoff

    ASSERT_TRUE(rig.owner(lfoA).has_value());
    EXPECT_EQ(*rig.owner(lfoA), trackA);
    ASSERT_TRUE(rig.owner(filterB).has_value()) << "A's modulation cable does not make A reach B's filter";
    EXPECT_NE(*rig.owner(filterB), trackA);
}

TEST(AutomationPlacementTest, MasterIsNeverOwnedEvenWithOneTrack) {
    PlacementRig rig;
    Node* trackIn = nullptr;
    rig.addTrack(synth::TrackKind::Midi, "Track In", trackIn);
    auto* osc = rig.add("Oscillator");
    auto* master = rig.add("Master");
    rig.midi(trackIn, osc);
    rig.audio(osc, master);
    EXPECT_FALSE(rig.owner(master).has_value());
}

TEST(AutomationPlacementTest, AnAudioTrackOwnsItsChainThroughItsTrackAudioNode) {
    PlacementRig rig;
    Node* trackAudio = nullptr;
    const auto track = rig.addTrack(synth::TrackKind::Audio, "Track Audio", trackAudio);
    ASSERT_NE(trackAudio, nullptr);
    auto* filter = rig.add("Filter");
    rig.audio(trackAudio, filter);
    ASSERT_TRUE(rig.owner(filter).has_value());
    EXPECT_EQ(*rig.owner(filter), track);
}

TEST(AutomationPlacementTest, UnboundAndAutomationTracksOwnNothing) {
    PlacementRig rig;
    rig.doc.addTrack(synth::TrackKind::Midi, "Unbound");
    rig.doc.addTrack(synth::TrackKind::Automation, "Automation");
    auto* filter = rig.add("Filter");
    EXPECT_FALSE(rig.owner(filter).has_value());
    EXPECT_TRUE(synth::computeTrackOwnership(rig.graph(), rig.doc).empty());
    EXPECT_FALSE(synth::resolveOwningTrack(rig.graph(), rig.doc, "no-such-uuid").has_value());
}

// ---- the placement seam ----------------------------------------------------------------------

TEST(AutomationPlacementTest, SeamPicksTheOwningTrackForATrackModule) {
    PlacementRig rig;
    Node* trackIn = nullptr;
    const auto track = rig.addTrack(synth::TrackKind::Midi, "Track In", trackIn);
    auto* osc = rig.add("Oscillator");
    rig.midi(trackIn, osc);
    EXPECT_EQ(synth::findOrCreateLaneHostTrack(rig.graph(), rig.doc, PlacementRig::uuidOf(osc)), track);
    EXPECT_FALSE(synth::findAutomationTrack(rig.doc).isValid()) << "no Automation track was created";
}

TEST(AutomationPlacementTest, SeamFallsBackToOneAutomationTrackForGlobalModules) {
    PlacementRig rig;
    Node *inA = nullptr, *inB = nullptr;
    rig.addTrack(synth::TrackKind::Midi, "Track In", inA);
    rig.addTrack(synth::TrackKind::Midi, "Track In", inB);
    auto* shared = rig.add("Oscillator"); // one oscillator both tracks play
    rig.midi(inA, shared);
    rig.midi(inB, shared);
    ASSERT_EQ(rig.graph().getConnections().size(), 2u);
    auto* free = rig.add("LFO");

    const auto first = synth::findOrCreateLaneHostTrack(rig.graph(), rig.doc, PlacementRig::uuidOf(shared));
    ASSERT_TRUE(first.isValid());
    EXPECT_EQ(rig.doc.getTrack(first)->kind, synth::TrackKind::Automation);
    EXPECT_EQ(synth::findOrCreateLaneHostTrack(rig.graph(), rig.doc, PlacementRig::uuidOf(free)), first)
        << "reused, not a second Automation track";
    EXPECT_EQ(rig.doc.getTracks().size(), 3u);
}

TEST(AutomationPlacementTest, SeamIsInvalidWhenAnAutomationTrackCannotBeCreated) {
    PlacementRig rig;
    auto* free = rig.add("LFO");
    for (int i = 0; i < synth::TimelineDoc::kMaxTracks; ++i)
        rig.doc.addTrack(synth::TrackKind::Midi, "T");
    EXPECT_FALSE(synth::findOrCreateLaneHostTrack(rig.graph(), rig.doc, PlacementRig::uuidOf(free)).isValid());
}

TEST(AutomationPlacementTest, AnExistingLaneIsNeverMigratedByLaterPatching) {
    PlacementRig rig;
    auto* filter = rig.add("Filter");
    const auto uuid = PlacementRig::uuidOf(filter);
    synth::AutomationLane::RangeSnapshot range;
    const auto host = synth::findOrCreateLaneHostTrack(rig.graph(), rig.doc, uuid);
    const auto lane = rig.doc.addLane(host, uuid, "cutoff", range);

    // Patch the filter into a track: it is now track-owned...
    Node* trackIn = nullptr;
    const auto track = rig.addTrack(synth::TrackKind::Midi, "Track In", trackIn);
    auto* osc = rig.add("Oscillator");
    rig.midi(trackIn, osc);
    rig.audio(osc, filter);
    EXPECT_EQ(synth::findOrCreateLaneHostTrack(rig.graph(), rig.doc, uuid), track);
    // ...but the lane it already has stays where it was: addLane dedupes doc-wide.
    EXPECT_EQ(rig.doc.addLane(track, uuid, "cutoff", range), lane);
    EXPECT_EQ(rig.doc.getTrackForLane(lane)->id, host);
}
