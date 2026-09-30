#include "MasterSplice.h"

#include "../AI/AIStateMapper/AIStateMapper.h"
#include "../AppUndoManager.h"
#include "../Modules/ChannelStripModule.h"
#include "../Modules/MasterModule.h"
#include "../Modules/RecordTapModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <vector>

namespace synth {

juce::AudioProcessorGraph::Node* findMasterNode(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && dynamic_cast<MasterModule*>(node->getProcessor()) != nullptr)
            return node;
    return nullptr;
}

bool isOutputDockProcessor(const juce::AudioProcessor* processor) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    if (dynamic_cast<const MasterModule*>(processor) != nullptr ||
        dynamic_cast<const RecordTapModule*>(processor) != nullptr)
        return true;
    if (auto* io = dynamic_cast<const IOProcessor*>(processor))
        return io->getType() == IOProcessor::audioOutputNode;
    return false;
}

std::vector<juce::AudioProcessorGraph::Node*> outputDockNodes(juce::AudioProcessorGraph& graph) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    std::vector<juce::AudioProcessorGraph::Node*> master, tap, output;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* processor = node->getProcessor();
        if (dynamic_cast<MasterModule*>(processor) != nullptr)
            master.push_back(node);
        else if (dynamic_cast<RecordTapModule*>(processor) != nullptr)
            tap.push_back(node);
        else if (auto* io = dynamic_cast<IOProcessor*>(processor);
                 io != nullptr && io->getType() == IOProcessor::audioOutputNode)
            output.push_back(node);
    }
    std::vector<juce::AudioProcessorGraph::Node*> chain = master;
    chain.insert(chain.end(), tap.begin(), tap.end());
    chain.insert(chain.end(), output.begin(), output.end());
    return chain;
}

juce::String outputDockDeleteRefusal(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr)
        return {};
    auto* processor = node->getProcessor();
    if (auto* io = dynamic_cast<IOProcessor*>(processor);
        io != nullptr && io->getType() == IOProcessor::audioOutputNode)
        return "The output always stays in the patch";
    if (dynamic_cast<MasterModule*>(processor) != nullptr)
        for (auto* other : graph.getNodes())
            if (other != nullptr && dynamic_cast<ChannelStripModule*>(other->getProcessor()) != nullptr)
                return "Master stays while the mixer has channels";
    return {};
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
