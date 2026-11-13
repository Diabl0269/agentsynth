// PortConnectorFilterPitch.cpp -- the Pitch cable of a Custom Filter whose per-note mode needs one (By hand, By pitch).
// It is made on the one event that gives the Filter a Pitch source, a cable from PortConnector::connectJacks that
// completes the voice chain feeding it, by the path of a hand-drawn cable. Never on a project load and never for a
// cable the person deleted: only a Filter that had no source before the new cable and has one after it qualifies.
// docs/layout/cables.md#port-connections-panel.

#include "PortConnector.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ConnectionIndex.h"
#include "Mixer/ChannelFlows/UpstreamPitchSource.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {
bool wantsPitchCable(FilterModule& filter) {
    if (filter.getCurrentFilterType() != FilterModule::kCustomTypeIndex)
        return false;
    const auto mode = filter.getCustomPerNote().mode;
    return mode == CustomFilterPerNote::Mode::ByHand || mode == CustomFilterPerNote::Mode::ByPitch;
}
} // namespace

bool PortConnector::connectFilterPitch(GraphEditor& editor, NodeID filterId) {
    auto& graph = editor.audioEngine.getGraph();
    auto* node = graph.getNodeForId(filterId);
    auto* filter = node != nullptr ? dynamic_cast<FilterModule*>(node->getProcessor()) : nullptr;
    if (filter == nullptr)
        return false;
    const synth::ConnectionIndex cables(graph);
    if (synth::hasPitchCable(cables, graph, filterId))
        return false;
    const auto source = synth::upstreamPitchSource(cables, graph, filterId);
    if (!source)
        return false;
    const int pitchJack = filter->mapInputChannel(FilterModule::kPitchBase).visibleJackIndex;
    editor.connectPorts(source->node, source->visibleJack, filterId, pitchJack, false, false);
    return true;
}

std::vector<NodeID> PortConnector::filtersWaitingForPitch(GraphEditor& editor) {
    auto& graph = editor.audioEngine.getGraph();
    std::vector<NodeID> waiting;
    std::optional<synth::ConnectionIndex> cables;
    for (auto* node : graph.getNodes()) {
        auto* filter = dynamic_cast<FilterModule*>(node->getProcessor());
        if (filter == nullptr || !wantsPitchCable(*filter))
            continue;
        if (!cables)
            cables.emplace(graph);
        if (!synth::hasPitchCable(*cables, graph, node->nodeID) &&
            !synth::upstreamPitchSource(*cables, graph, node->nodeID))
            waiting.push_back(node->nodeID);
    }
    return waiting;
}

bool PortConnector::cableFiltersNowFed(GraphEditor& editor, const std::vector<NodeID>& waiting, bool recordUndo) {
    auto& graph = editor.audioEngine.getGraph();
    std::vector<NodeID> fed;
    {
        const synth::ConnectionIndex cables(graph);
        for (const auto id : waiting)
            if (!synth::hasPitchCable(cables, graph, id) && synth::upstreamPitchSource(cables, graph, id))
                fed.push_back(id);
    }
    if (fed.empty())
        return false; // nothing to record: no empty undo step
    const auto cableAll = [&] {
        for (const auto id : fed)
            connectFilterPitch(editor, id);
    };
    if (recordUndo && editor.undoManager)
        editor.undoManager->recordStructuralChange(graph, cableAll);
    else
        cableAll();
    editor.notifyModuleContentChanged();
    return true;
}

} // namespace synth::ui
