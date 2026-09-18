// =================================================================================================
// FRO14 (P9-4, docs/mixer/mixer.md#channels-follow-audio-not-tracks) — the LINK RULE itself, at the Core layer: the two signal-reach
// queries (Source/Mixer/ChannelFlows/ChannelFlowsTrackChannelLink.cpp) and the combining query plus
// channel naming (Source/Mixer/TrackChannelLink.cpp). No MainComponent, no GraphEditor, no undo —
// bare graphs wired by hand, exactly like the Core half of the "Make channel" tests next door.
//
// A track and a channel are LINKED when the track is that channel's ONLY source. The app-level
// consequences of that (name/colour/mute/solo sync, the channel chip) are in
// ChannelFlowTrackChannelLinkTests.cpp.
// =================================================================================================

#include "ChannelFlowTestFixture.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/TrackChannelLink.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/ModuleBase.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>

namespace {

// A bare graph plus the doc that names its tracks. Nodes are added through the same
// uuid-mirroring helper the other Core tests use, so a track's bindingUuid resolves exactly as it
// does in the app.
struct LinkRigTCL {
    HostedPatchCFT patch;
    synth::TimelineDoc doc;

    juce::AudioProcessorGraph& graph() { return patch.engine.getGraph(); }

    juce::AudioProcessorGraph::Node* add(const juce::String& type, juce::String& uuidOut) {
        return addPlainNodeCFT(graph(), type, {0, 0}, uuidOut);
    }
    juce::AudioProcessorGraph::Node* add(const juce::String& type) {
        juce::String unused;
        return add(type, unused);
    }

    /** A Track In bound to a new MIDI track named `name`. */
    synth::TrackId addMidiTrack(const juce::String& name, juce::AudioProcessorGraph::Node*& nodeOut) {
        juce::String uuid;
        nodeOut = add("Track In", uuid);
        const auto id = doc.addTrack(synth::TrackKind::Midi, name);
        doc.setTrackBinding(id, uuid);
        return id;
    }

    void wireAudio(juce::AudioProcessorGraph::Node* from, juce::AudioProcessorGraph::Node* to) {
        graph().addConnection({{from->nodeID, 0}, {to->nodeID, 0}});
    }
    void wireMidi(juce::AudioProcessorGraph::Node* from, juce::AudioProcessorGraph::Node* to) {
        constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
        graph().addConnection({{from->nodeID, midi}, {to->nodeID, midi}});
    }
};

} // namespace

// -------------------------------------------------------------------------------------------
// findStripFedByTrackSource — forward to the channel
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowTrackChannelLinkCore, FindStripFedByTrackSourceReachesTheStripThroughTheChain) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    rig.addMidiTrack("Lead", trackIn);
    auto* osc = rig.add("Oscillator");
    auto* filter = rig.add("Filter");
    auto* strip = rig.add("Channel Strip");
    rig.wireMidi(trackIn, osc);
    rig.wireAudio(osc, filter);
    rig.wireAudio(filter, strip);

    EXPECT_EQ(synth::findStripFedByTrackSource(rig.graph(), trackIn->nodeID), strip->nodeID)
        << "the walk crosses MIDI and audio hops alike to reach the channel";
}

TEST(ChannelFlowTrackChannelLinkCore, FindStripFedByTrackSourceStopsAtTheFirstStripNotASecond) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    rig.addMidiTrack("Lead", trackIn);
    auto* osc = rig.add("Oscillator");
    auto* first = rig.add("Channel Strip");
    auto* second = rig.add("Channel Strip");
    rig.wireMidi(trackIn, osc);
    rig.wireAudio(osc, first);
    rig.wireAudio(first, second);

    const auto found = synth::findStripFedByTrackSource(rig.graph(), trackIn->nodeID);
    EXPECT_EQ(found, first->nodeID);
    EXPECT_NE(found, second->nodeID) << "a bus strip downstream is not this track's own channel";
}

TEST(ChannelFlowTrackChannelLinkCore, FindStripFedByTrackSourceIsInvalidWhenTheChainNeverReachesAStrip) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    rig.addMidiTrack("Lead", trackIn);
    auto* osc = rig.add("Oscillator");
    rig.wireMidi(trackIn, osc);
    rig.wireAudio(osc, rig.patch.output);

    EXPECT_FALSE(rig.graph().getNodeForId(synth::findStripFedByTrackSource(rig.graph(), trackIn->nodeID)) != nullptr)
        << "audio that reaches the output with no strip on the way has no channel yet";
}

TEST(ChannelFlowTrackChannelLinkCore, FindStripFedByTrackSourceIgnoresAModulationLeg) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    rig.addMidiTrack("Mod source", trackIn);
    auto* lfo = rig.add("LFO");
    auto* filter = rig.add("Filter");
    auto* strip = rig.add("Channel Strip");
    rig.wireAudio(filter, strip);
    // The track drives an LFO that only MODULATES the channel's filter — a hidden attenuverter leg,
    // never part of the signal path, so this track plays into no channel of its own.
    rig.wireMidi(trackIn, lfo);
    const int cutoff = cutoffChannelCFT(filter);
    ASSERT_GE(cutoff, 0);
    rig.patch.engine.addModRouting(lfo->nodeID, 0, filter->nodeID, cutoff);

    EXPECT_NE(synth::findStripFedByTrackSource(rig.graph(), trackIn->nodeID), strip->nodeID);
}

// -------------------------------------------------------------------------------------------
// findTrackSourcesFeedingStrip — back to the tracks
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowTrackChannelLinkCore, FindTrackSourcesFeedingStripFindsTheOneFeeder) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    rig.addMidiTrack("Lead", trackIn);
    auto* osc = rig.add("Oscillator");
    auto* strip = rig.add("Channel Strip");
    rig.wireMidi(trackIn, osc);
    rig.wireAudio(osc, strip);

    const auto feeders = synth::findTrackSourcesFeedingStrip(rig.graph(), strip->nodeID);
    ASSERT_EQ(feeders.size(), 1u);
    EXPECT_EQ(feeders.front(), trackIn->nodeID);
}

TEST(ChannelFlowTrackChannelLinkCore, FindTrackSourcesFeedingStripFindsBothOfASharedSampler) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* kickIn = nullptr;
    juce::AudioProcessorGraph::Node* snareIn = nullptr;
    rig.addMidiTrack("Kick", kickIn);
    rig.addMidiTrack("Snare", snareIn);
    auto* sampler = rig.add("Sampler");
    auto* strip = rig.add("Channel Strip");
    rig.wireMidi(kickIn, sampler);
    rig.wireMidi(snareIn, sampler);
    rig.wireAudio(sampler, strip);

    const auto feeders = synth::findTrackSourcesFeedingStrip(rig.graph(), strip->nodeID);
    EXPECT_EQ(feeders.size(), 2u) << "both tracks feed the shared sampler's one channel";
}

// -------------------------------------------------------------------------------------------
// resolveTrackChannelLink — the rule, over the five rows of docs/mixer/mixer.md#channels-follow-audio-not-tracks
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowTrackChannelLinkCore, ResolveTrackChannelLinkLinksAnAudioTrack) {
    LinkRigTCL rig;
    juce::String uuid;
    auto* trackAudio = rig.add("Track Audio", uuid);
    const auto track = rig.doc.addTrack(synth::TrackKind::Audio, "Vocals");
    rig.doc.setTrackBinding(track, uuid);
    auto* strip = rig.add("Channel Strip");
    rig.wireAudio(trackAudio, strip);

    const auto info = synth::resolveTrackChannelLink(rig.graph(), rig.doc, track);
    EXPECT_TRUE(info.hasChannel);
    EXPECT_TRUE(info.linked);
    EXPECT_EQ(info.stripId, strip->nodeID);
    EXPECT_EQ(info.stripUuid, rig.graph().getNodeForId(strip->nodeID)->properties["uuid"].toString());
}

TEST(ChannelFlowTrackChannelLinkCore, ResolveTrackChannelLinkLinksAMidiTrackThatAloneDrivesAnInstrument) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    const auto track = rig.addMidiTrack("Lead", trackIn);
    auto* osc = rig.add("Oscillator");
    auto* strip = rig.add("Channel Strip");
    rig.wireMidi(trackIn, osc);
    rig.wireAudio(osc, strip);

    const auto info = synth::resolveTrackChannelLink(rig.graph(), rig.doc, track);
    EXPECT_TRUE(info.linked);
    EXPECT_EQ(info.feedingTrackSources.size(), 1u);
}

TEST(ChannelFlowTrackChannelLinkCore, ResolveTrackChannelLinkDoesNotLinkASharedChannel) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* kickIn = nullptr;
    juce::AudioProcessorGraph::Node* snareIn = nullptr;
    const auto kick = rig.addMidiTrack("Kick", kickIn);
    rig.addMidiTrack("Snare", snareIn);
    auto* sampler = rig.add("Sampler");
    auto* strip = rig.add("Channel Strip");
    rig.wireMidi(kickIn, sampler);
    rig.wireMidi(snareIn, sampler);
    rig.wireAudio(sampler, strip);

    const auto info = synth::resolveTrackChannelLink(rig.graph(), rig.doc, kick);
    EXPECT_TRUE(info.hasChannel) << "a shared channel still shows a chip";
    EXPECT_FALSE(info.linked);
    EXPECT_EQ(info.feedingTrackSources.size(), 2u) << "the chip's shared-channel case lists both tracks";
}

TEST(ChannelFlowTrackChannelLinkCore, ResolveTrackChannelLinkHasNoChannelBeforeOneExists) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    const auto track = rig.addMidiTrack("Lead", trackIn);
    auto* osc = rig.add("Oscillator");
    rig.wireMidi(trackIn, osc);
    rig.wireAudio(osc, rig.patch.output);

    const auto info = synth::resolveTrackChannelLink(rig.graph(), rig.doc, track);
    EXPECT_FALSE(info.hasChannel);
    EXPECT_FALSE(info.linked);
}

TEST(ChannelFlowTrackChannelLinkCore, ResolveTrackChannelLinkHasNoChannelForAnUnboundOrAutomationTrack) {
    LinkRigTCL rig;
    const auto unbound = rig.doc.addTrack(synth::TrackKind::Midi, "Unbound");
    EXPECT_FALSE(synth::resolveTrackChannelLink(rig.graph(), rig.doc, unbound).hasChannel);

    juce::String uuid;
    rig.add("Track In", uuid);
    const auto automation = rig.doc.addTrack(synth::TrackKind::Automation, "Automation");
    rig.doc.setTrackBinding(automation, uuid);
    EXPECT_FALSE(synth::resolveTrackChannelLink(rig.graph(), rig.doc, automation).hasChannel)
        << "an Automation track hosts lanes; it plays into no channel";
}

TEST(ChannelFlowTrackChannelLinkCore, ResolveTrackChannelLinkBreaksWhenASecondTrackJoinsAndReformsWhenItLeaves) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* leadIn = nullptr;
    juce::AudioProcessorGraph::Node* padIn = nullptr;
    const auto lead = rig.addMidiTrack("Lead", leadIn);
    rig.addMidiTrack("Pad", padIn);
    auto* osc = rig.add("Oscillator");
    auto* strip = rig.add("Channel Strip");
    rig.wireMidi(leadIn, osc);
    rig.wireAudio(osc, strip);
    ASSERT_TRUE(synth::resolveTrackChannelLink(rig.graph(), rig.doc, lead).linked);

    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    const juce::AudioProcessorGraph::Connection joining{{padIn->nodeID, midi}, {osc->nodeID, midi}};
    ASSERT_TRUE(rig.graph().addConnection(joining));
    EXPECT_FALSE(synth::resolveTrackChannelLink(rig.graph(), rig.doc, lead).linked)
        << "the link breaks the moment a second track feeds the channel";

    ASSERT_TRUE(rig.graph().removeConnection(joining));
    EXPECT_TRUE(synth::resolveTrackChannelLink(rig.graph(), rig.doc, lead).linked)
        << "and re-forms when the channel goes back down to one source";
}

// -------------------------------------------------------------------------------------------
// channelDisplayName — shared with FRO55's stem file naming
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowTrackChannelLinkCore, ChannelDisplayNameUsesTheOneFeedingTracksName) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* trackIn = nullptr;
    rig.addMidiTrack("Lead", trackIn);
    auto* osc = rig.add("Oscillator");
    auto* strip = rig.add("Channel Strip");
    rig.wireMidi(trackIn, osc);
    rig.wireAudio(osc, strip);

    EXPECT_EQ(synth::channelDisplayName(rig.graph(), strip->nodeID, rig.doc, "Channel"), "Lead");
}

TEST(ChannelFlowTrackChannelLinkCore, ChannelDisplayNameFallsBackWhenSeveralOrNoTracksFeedTheStrip) {
    LinkRigTCL rig;
    juce::AudioProcessorGraph::Node* kickIn = nullptr;
    juce::AudioProcessorGraph::Node* snareIn = nullptr;
    rig.addMidiTrack("Kick", kickIn);
    rig.addMidiTrack("Snare", snareIn);
    auto* sampler = rig.add("Sampler");
    auto* shared = rig.add("Channel Strip");
    rig.wireMidi(kickIn, sampler);
    rig.wireMidi(snareIn, sampler);
    rig.wireAudio(sampler, shared);
    EXPECT_EQ(synth::channelDisplayName(rig.graph(), shared->nodeID, rig.doc, "Channel 3"), "Channel 3");

    auto* untracked = rig.add("Channel Strip");
    auto* freeOsc = rig.add("Oscillator");
    rig.wireAudio(freeOsc, untracked);
    EXPECT_EQ(synth::channelDisplayName(rig.graph(), untracked->nodeID, rig.doc, "Channel 4"), "Channel 4")
        << "a strip no track feeds keeps its positional fallback";
}
