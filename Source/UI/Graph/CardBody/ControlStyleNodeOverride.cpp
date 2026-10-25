// ControlStyleNodeOverride.cpp -- get/set of a node's "controlStyle" property with undo, modelled on
// CardLayoutOverride.cpp.
#include "ControlStyleNodeOverride.h"
#include "AppUndoManager.h"

namespace synth {

theme::ControlStyleOverride getControlStyleNodeOverride(const juce::AudioProcessorGraph& graph,
                                                        juce::AudioProcessorGraph::NodeID nodeId) {
    if (const auto* node = graph.getNodeForId(nodeId))
        return theme::controlStyleOverrideFromVar(node->properties[kControlStyleNodeProperty]);
    return {};
}

// The undo step is a whole-graph snapshot pair (recordStructuralChange); restoring it goes through
// AIStateMapper::applySnapshotPreservingNodes, which puts the property back or removes it.
bool setControlStyleNodeOverride(juce::AudioProcessorGraph& graph, AppUndoManager* undo,
                                 juce::AudioProcessorGraph::NodeID nodeId, const theme::ControlStyleOverride& style) {
    if (graph.getNodeForId(nodeId) == nullptr)
        return false;
    if (style == getControlStyleNodeOverride(graph, nodeId))
        return true; // no-op: do not burn an undo step on it

    const juce::var wanted = theme::toVar(style);
    auto apply = [&graph, nodeId, wanted] {
        if (auto* node = graph.getNodeForId(nodeId)) {
            if (wanted.isVoid())
                node->properties.remove(kControlStyleNodeProperty);
            else
                node->properties.set(kControlStyleNodeProperty, wanted.clone());
        }
    };

    if (undo != nullptr)
        undo->recordStructuralChange(graph, apply);
    else
        apply();
    return true;
}

} // namespace synth
