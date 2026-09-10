#include "MasterSplice.h"

#include "../AI/AIStateMapper.h"
#include "../AppUndoManager.h"
#include "../Modules/ChannelStripModule.h"
#include "../Modules/MasterModule.h"
#include "../Modules/RecordTapModule.h"
#include "../Timeline/TimelineDoc.h"
#include <vector>

namespace synth {

juce::AudioProcessorGraph::Node* findMasterNode(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && dynamic_cast<MasterModule*>(node->getProcessor()) != nullptr)
            return node;
    return nullptr;
}

juce::AudioProcessorGraph::Node* spliceMasterNode(juce::AudioProcessorGraph& graph, juce::Point<int> position) {
    if (auto* existing = findMasterNode(graph))
        return existing;

    // The node Master goes in FRONT of: the Rec Tap if one is already spliced, else Audio Output
    // (still a bare juce::AudioGraphIOProcessor, so identified by name like every other lookup).
    juce::AudioProcessorGraph::Node* target = nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr && dynamic_cast<RecordTapModule*>(node->getProcessor()) != nullptr)
            target = node;
    if (target == nullptr)
        for (auto* node : graph.getNodes())
            if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == "Audio Output")
                target = node;
    if (target == nullptr)
        return nullptr; // no master bus to put a Master in front of

    // Through the factory, so the node round-trips through graphToJSON / applyJSONToGraph — which
    // is how undo, redo and save reproduce it.
    auto processor = AIStateMapper::createModule("Master");
    if (processor == nullptr)
        return nullptr;
    auto node = graph.addNode(std::move(processor));
    if (node == nullptr)
        return nullptr;
    auto* created = node.get();

    // Ensure-uuid, mirrored into the processor in the same breath (ModuleBase::setNodeUuid).
    const juce::String uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    node->properties.set("x", position.x);
    node->properties.set("y", position.y);

    // THE SPLICE. Collected first, mutated afterwards: removeConnection invalidates the list.
    // Only the stereo pair (ch0/ch1) moves — Master is a stereo bus, and re-routing a wider
    // master through it would silently drop the rest. MIDI into the target is left alone.
    std::vector<juce::AudioProcessorGraph::Connection> intoTarget;
    for (const auto& connection : graph.getConnections()) {
        if (connection.destination.nodeID != target->nodeID)
            continue;
        const int channel = connection.destination.channelIndex;
        if (channel < 0 || channel >= MasterModule::kNumOutputs)
            continue;
        intoTarget.push_back(connection);
    }
    for (const auto& connection : intoTarget) {
        auto* source = graph.getNodeForId(connection.source.nodeID);
        const bool fromStrip =
            source != nullptr && dynamic_cast<ChannelStripModule*>(source->getProcessor()) != nullptr;
        const int channel = connection.destination.channelIndex;
        const int masterInput = fromStrip ? MasterModule::kMixLeft + channel : MasterModule::kDirectLeft + channel;
        graph.removeConnection(connection);
        graph.addConnection({connection.source, {node->nodeID, masterInput}});
    }
    for (int channel = 0; channel < MasterModule::kNumOutputs; ++channel)
        graph.addConnection({{node->nodeID, channel}, {target->nodeID, channel}});

    return created;
}

juce::AudioProcessorGraph::Node* ensureMasterNode(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager,
                                                  TimelineDoc& doc, juce::Point<int> position) {
    if (auto* existing = findMasterNode(graph))
        return existing;

    juce::AudioProcessorGraph::Node* created = nullptr;
    undoManager.recordCombinedChange(graph, doc, [&] { created = spliceMasterNode(graph, position); });
    return created;
}

} // namespace synth
