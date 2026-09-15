#pragma once

// MixerModelTestFixture.h -- shared helpers for Tests/Mixer/MixerModel/*Tests.cpp. Header-only;
// not compiled on its own and not registered in Tests/CMakeLists.txt. Same "bare AudioEngine +
// TimelineDoc + MacroSet, no MainComponent" rig style as ChannelFlowAutoChannelCore
// (Tests/Mixer/ChannelFlow/ChannelFlowTestFixture.h), since MixerModel is headless Core.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/ModuleBase.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>

// Adds one node through the factory and mirrors a fresh uuid into both the node property and the
// processor (ModuleBase::setNodeUuid, Source/CLAUDE.md's uuid-mirroring invariant) -- same shape as
// ChannelFlowTestFixture.h's addPlainNodeCFT, duplicated here (its own file is scoped to the
// ChannelFlow suite) rather than shared across test areas.
inline juce::AudioProcessorGraph::Node* addPlainNodeMMT(juce::AudioProcessorGraph& graph, const juce::String& typeName,
                                                        juce::String& uuidOut) {
    auto processor = synth::AIStateMapper::createModule(typeName);
    if (processor == nullptr)
        return nullptr;
    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return nullptr;
    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    uuidOut = uuid;
    return node.get();
}

// Wires a stereo audio connection between two nodes across both legs (ch0/ch0, rightLeg/rightLeg),
// mirroring the stereo-pair convention (Source/Modules/CLAUDE.md) every real channel chain follows.
inline void connectStereoMMT(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node& from,
                             juce::AudioProcessorGraph::Node& to) {
    auto* fromModule = dynamic_cast<ModuleBase*>(from.getProcessor());
    auto* toModule = dynamic_cast<ModuleBase*>(to.getProcessor());
    ASSERT_NE(fromModule, nullptr);
    ASSERT_NE(toModule, nullptr);
    graph.addConnection({{from.nodeID, 0}, {to.nodeID, 0}});
    graph.addConnection(
        {{from.nodeID, fromModule->rightAudioLegChannel()}, {to.nodeID, toModule->rightAudioLegChannel()}});
}

// Builds Track Audio -> EQ -> Compressor -> Channel Strip, all wired stereo, `track`'s binding set
// to the Track Audio node's uuid. Returns the created nodes so a test can inspect/mutate them
// further (e.g. tee a shared node off `eq` or `compressor` to make the chain branch).
struct LinearChannelRigMMT {
    juce::AudioProcessorGraph::Node* trackAudio = nullptr;
    juce::AudioProcessorGraph::Node* eq = nullptr;
    juce::AudioProcessorGraph::Node* compressor = nullptr;
    juce::AudioProcessorGraph::Node* strip = nullptr;
    juce::String trackAudioUuid;
};

inline LinearChannelRigMMT buildLinearChannelRigMMT(juce::AudioProcessorGraph& graph, synth::TimelineDoc& doc,
                                                    synth::TrackId trackId) {
    LinearChannelRigMMT rig;
    juce::String unused;
    rig.trackAudio = addPlainNodeMMT(graph, "Track Audio", rig.trackAudioUuid);
    rig.eq = addPlainNodeMMT(graph, "Parametric EQ", unused);
    rig.compressor = addPlainNodeMMT(graph, "Compressor", unused);
    juce::String stripUuid;
    rig.strip = addPlainNodeMMT(graph, "Channel Strip", stripUuid);
    if (rig.strip != nullptr)
        if (auto* stripModule = dynamic_cast<ChannelStripModule*>(rig.strip->getProcessor()))
            stripModule->setShape(ChannelStripModule::Shape::Stereo);

    if (rig.trackAudio != nullptr && rig.eq != nullptr && rig.compressor != nullptr && rig.strip != nullptr) {
        connectStereoMMT(graph, *rig.trackAudio, *rig.eq);
        connectStereoMMT(graph, *rig.eq, *rig.compressor);
        connectStereoMMT(graph, *rig.compressor, *rig.strip);
    }

    doc.setTrackBinding(trackId, rig.trackAudioUuid);
    return rig;
}
