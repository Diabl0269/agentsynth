#pragma once

#include "AI/AIStateMapper/AIStateMapper.h"
#include "ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"

namespace synth {

// Shared across the several ChannelFlows*.cpp concern units — defined once in
// ChannelFlowsMakeChannel.cpp (FRO13, P9-7: lifted out of that file's anonymous namespace so
// ChannelFlowsTrackPreset.cpp's outside-modulator walk can reuse them too), same "extern
// declaration in the shared internal header, one definition in one .cpp" pattern
// PreferencesSettingsTabInternal.h uses for comboIdFromMode/modeFromComboId.
extern juce::AudioProcessor* processorFor(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID id);
extern bool isAttenuverter(const juce::AudioProcessor* p);
extern bool isStrip(const juce::AudioProcessor* p);
extern bool isMacroPortNode(const juce::AudioProcessor* p);

// Shared internals behind ChannelFlows.cpp's several concern units (ChannelFlowsDefaultChannel.cpp,
// ChannelFlowsAutoChannel.cpp, ChannelFlowsMakeChannel.cpp): the one node-creation helper and the one
// Gate->EQ->Compressor->Strip chain builder every one of them calls into.
namespace {

// Creates one node through the factory (so it round-trips through graphToJSON/applyJSONToGraph,
// exactly like every other node-creation call site), assigns it a fresh uuid mirrored into the
// processor (ModuleBase::setNodeUuid), and records its canvas position. Returns nullptr on any
// factory/addNode failure, leaving `uuidOut` untouched.
juce::AudioProcessorGraph::Node* addChainNode(juce::AudioProcessorGraph& graph, const juce::String& moduleType,
                                              juce::Point<int> position, juce::String& uuidOut) {
    auto processor = AIStateMapper::createModule(moduleType);
    if (processor == nullptr)
        return nullptr;
    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return nullptr;

    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    node->properties.set("x", position.x);
    node->properties.set("y", position.y);

    uuidOut = uuid;
    return node.get();
}

// The shared builder behind both buildDefaultAudioChannel (one fixed stereo-pair source) and
// buildChannelForFeeds (T184: an arbitrary set of left/right feeds gathered from
// findUnchanneledOutputFeeds). Builds Gate(bypassed) -> EQ(bypassed) -> Compressor(bypassed) ->
// Channel Strip (Stereo) -> Master (Mix) and wires every entry in `leftFeeds`/`rightFeeds` into the
// Gate's ch0/ch1 respectively (AudioProcessorGraph sums multiple sources landing on the same input
// channel, so more than one feed a side is fine). Same ordering as buildDefaultAudioChannel's own
// contract: chain wired first, THEN spliceMasterNode, THEN Strip->Master as plain edges.
//
// FRO226: Gate is added exactly like EQ/Compressor below (bypassed, same node-creation helper) so
// every flow through this one builder gets it uniformly, matching this file's "one chain builder,
// no parallel implementations" rule (docs/mixer/mixer.md#building-a-channel). An old saved
// project/preset's own JSON has no Gate node and is never migrated to add one (docs/mixer/track-presets.md)
// — this only changes what a NEW channel is built from.
//
// FRO25 (P9-3d): `sink` says where the strip's output goes. The default (toMaster, no extra
// destinations) is every pre-FRO25 caller's behaviour. "Make channel" on a track that merges into a
// shared module sends the strip's L/R to that module's original input pins as well (or instead,
// toMaster=false, when the track has no output of its own) — plain edges, same reason as
// Strip->Master (see ChannelFlowsDefaultChannel.cpp's buildDefaultAudioChannel comment).
struct ChannelSink {
    bool toMaster = true;
    std::vector<juce::AudioProcessorGraph::NodeAndChannel> leftDests, rightDests;
};

DefaultChannel buildChannelChain(juce::AudioProcessorGraph& graph,
                                 const std::vector<juce::AudioProcessorGraph::NodeAndChannel>& leftFeeds,
                                 const std::vector<juce::AudioProcessorGraph::NodeAndChannel>& rightFeeds,
                                 const DefaultChannelLayout& layout, const ChannelSink& sink = {}) {
    DefaultChannel result;

    juce::String gateUuid;
    auto* gate = addChainNode(graph, "Gate", layout.gate, gateUuid);
    if (gate == nullptr)
        return result;
    // Factory default: present but bypassed until the user opts in (docs/mixer/mixer.md#the-factory-default-chain).
    if (auto* module = dynamic_cast<ModuleBase*>(gate->getProcessor()))
        module->setBypassed(true);

    juce::String eqUuid;
    auto* eq = addChainNode(graph, "Parametric EQ", layout.eq, eqUuid);
    if (eq == nullptr) {
        result.gateUuid = gateUuid;
        return result;
    }
    // Factory default: present but bypassed until the user opts in (docs/mixer/mixer.md#the-factory-default-chain).
    if (auto* module = dynamic_cast<ModuleBase*>(eq->getProcessor()))
        module->setBypassed(true);

    juce::String compressorUuid;
    auto* compressor = addChainNode(graph, "Compressor", layout.compressor, compressorUuid);
    if (compressor == nullptr) {
        result.gateUuid = gateUuid;
        result.eqUuid = eqUuid;
        return result;
    }
    if (auto* module = dynamic_cast<ModuleBase*>(compressor->getProcessor()))
        module->setBypassed(true);

    // ChannelStripModule::setShape() must run BEFORE graph.addNode(): adding to a live graph can
    // prepareToPlay and lock the shape (see that method's own contract).
    auto stripProcessor = AIStateMapper::createModule("Channel Strip");
    if (stripProcessor == nullptr) {
        result.gateUuid = gateUuid;
        result.eqUuid = eqUuid;
        result.compressorUuid = compressorUuid;
        return result;
    }
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(stripProcessor.get()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    auto stripNode = graph.addNode(std::move(stripProcessor));
    if (stripNode == nullptr) {
        result.gateUuid = gateUuid;
        result.eqUuid = eqUuid;
        result.compressorUuid = compressorUuid;
        return result;
    }
    auto* strip = stripNode.get();
    const juce::String stripUuid = juce::Uuid().toDashedString();
    strip->properties.set("uuid", stripUuid);
    if (auto* module = dynamic_cast<ModuleBase*>(strip->getProcessor()))
        module->setNodeUuid(stripUuid);
    strip->properties.set("x", layout.strip.x);
    strip->properties.set("y", layout.strip.y);

    // feeds -> Gate. Stereo on raw ch0/ch1 throughout, except the strip's right leg, which is
    // ChannelStripModule::kRightBase — NEVER ch1 (Source/Modules/CLAUDE.md), see
    // ChannelFlowsDefaultChannel.cpp's buildDefaultAudioChannel comment for why that's the one place
    // the number jumps.
    for (const auto& feed : leftFeeds)
        graph.addConnection({feed, {gate->nodeID, 0}});
    for (const auto& feed : rightFeeds)
        graph.addConnection({feed, {gate->nodeID, 1}});
    graph.addConnection({{gate->nodeID, 0}, {eq->nodeID, 0}});
    graph.addConnection({{gate->nodeID, 1}, {eq->nodeID, 1}});
    graph.addConnection({{eq->nodeID, 0}, {compressor->nodeID, 0}});
    graph.addConnection({{eq->nodeID, 1}, {compressor->nodeID, 1}});
    graph.addConnection({{compressor->nodeID, 0}, {strip->nodeID, 0}});
    graph.addConnection({{compressor->nodeID, 1}, {strip->nodeID, ChannelStripModule::kRightBase}});

    // Master AFTER the chain above is wired, BEFORE the Strip->Master edges below: spliceMasterNode
    // re-routes whatever already feeds the audio output, and nothing of this channel's own should be
    // among that yet (see ChannelFlowsDefaultChannel.cpp's buildDefaultAudioChannel comment on the
    // ordering).
    auto* master = sink.toMaster ? spliceMasterNode(graph, layout.master) : findMasterNode(graph);
    if (master != nullptr && sink.toMaster) {
        // A PLAIN graph edge, never a macro port — see ChannelFlowsDefaultChannel.cpp's
        // buildDefaultAudioChannel comment for why.
        graph.addConnection({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}});
        graph.addConnection(
            {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}});
    }
    for (const auto& dest : sink.leftDests)
        graph.addConnection({{strip->nodeID, 0}, dest});
    for (const auto& dest : sink.rightDests)
        graph.addConnection({{strip->nodeID, ChannelStripModule::kRightBase}, dest});

    result.gateUuid = gateUuid;
    result.eqUuid = eqUuid;
    result.compressorUuid = compressorUuid;
    result.stripUuid = stripUuid;
    result.strip = strip;
    result.master = master;
    return result;
}

} // namespace

} // namespace synth
