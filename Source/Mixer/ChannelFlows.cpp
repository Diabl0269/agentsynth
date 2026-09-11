#include "ChannelFlows.h"

#include "../AI/AIStateMapper.h"
#include "../Modules/ChannelStripModule.h"
#include "../Modules/MasterModule.h"
#include "../Modules/ModuleBase.h"
#include "MasterSplice.h"

namespace synth {

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

} // namespace

DefaultChannel buildDefaultAudioChannel(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node& source,
                                        const DefaultChannelLayout& layout) {
    DefaultChannel result;

    juce::String eqUuid;
    auto* eq = addChainNode(graph, "Parametric EQ", layout.eq, eqUuid);
    if (eq == nullptr)
        return result;
    // Factory default: present but bypassed until the user opts in (docs/mixer.md §5.7/D3).
    if (auto* module = dynamic_cast<ModuleBase*>(eq->getProcessor()))
        module->setBypassed(true);

    juce::String compressorUuid;
    auto* compressor = addChainNode(graph, "Compressor", layout.compressor, compressorUuid);
    if (compressor == nullptr) {
        result.eqUuid = eqUuid;
        return result;
    }
    if (auto* module = dynamic_cast<ModuleBase*>(compressor->getProcessor()))
        module->setBypassed(true);

    // ChannelStripModule::setShape() must run BEFORE graph.addNode(): adding to a live graph can
    // prepareToPlay and lock the shape (see that method's own contract).
    auto stripProcessor = AIStateMapper::createModule("Channel Strip");
    if (stripProcessor == nullptr) {
        result.eqUuid = eqUuid;
        result.compressorUuid = compressorUuid;
        return result;
    }
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(stripProcessor.get()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    auto stripNode = graph.addNode(std::move(stripProcessor));
    if (stripNode == nullptr) {
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

    // The chain: source -> EQ -> Compressor -> Strip. Stereo on raw ch0/ch1 throughout, except the
    // strip's right leg, which is ChannelStripModule::kRightBase — NEVER ch1 (Source/Modules/
    // CLAUDE.md), see ChannelFlows.h's own comment for why that's the one place the number jumps.
    graph.addConnection({{source.nodeID, 0}, {eq->nodeID, 0}});
    graph.addConnection({{source.nodeID, 1}, {eq->nodeID, 1}});
    graph.addConnection({{eq->nodeID, 0}, {compressor->nodeID, 0}});
    graph.addConnection({{eq->nodeID, 1}, {compressor->nodeID, 1}});
    graph.addConnection({{compressor->nodeID, 0}, {strip->nodeID, 0}});
    graph.addConnection({{compressor->nodeID, 1}, {strip->nodeID, ChannelStripModule::kRightBase}});

    // Master AFTER the chain above is wired, BEFORE the Strip->Master edges below: spliceMasterNode
    // re-routes whatever already feeds the audio output, and nothing of this channel's own should be
    // among that yet (see ChannelFlows.h's own comment on the ordering).
    auto* master = spliceMasterNode(graph, layout.master);
    if (master != nullptr) {
        // A PLAIN graph edge, never a macro port — see ChannelFlows.h's own comment for why.
        graph.addConnection({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}});
        graph.addConnection(
            {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}});
    }

    result.eqUuid = eqUuid;
    result.compressorUuid = compressorUuid;
    result.stripUuid = stripUuid;
    result.strip = strip;
    result.master = master;
    return result;
}

} // namespace synth
