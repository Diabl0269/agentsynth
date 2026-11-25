// CardLayoutOverride.cpp -- get/set of a node's "cardLayout" property with undo, modelled on the
// custom card title (GraphEditorModuleTitles.cpp) but needing nothing from GraphEditor.
#include "CardLayoutOverride.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "ModuleCardLayoutResolver.h"

namespace synth {

juce::var getCardLayoutOverride(const juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId) {
    if (const auto* node = graph.getNodeForId(nodeId)) {
        const auto& value = node->properties[kCardLayoutNodeProperty];
        if (value.isObject())
            return value;
    }
    return {};
}

bool restoreCardLayoutOverride(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                               const juce::var& json) {
    auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr)
        return false;
    if (json.isVoid())
        node->properties.remove(kCardLayoutNodeProperty);
    else
        node->properties.set(kCardLayoutNodeProperty, json.clone());
    return true;
}

// The undo step is a whole-graph snapshot pair (recordStructuralChange); restoring it goes through
// AIStateMapper::applySnapshotPreservingNodes, which puts the property back or removes it. The
// stored value is a deep copy, so a later edit of the caller's CardLayout never aliases the node.
bool setCardLayoutOverride(juce::AudioProcessorGraph& graph, AppUndoManager* undo,
                           juce::AudioProcessorGraph::NodeID nodeId, const std::optional<CardLayout>& layout) {
    const auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr)
        return false;

    // Every saved layout is marked as based on the code default's current revision, so the resolver keeps it until the
    // default is redesigned (a layout with no mark is skipped as older).
    const juce::var wanted =
        layout && node->getProcessor() != nullptr
            ? withBasedOnCurrentDefault(*layout, AIStateMapper::getFactoryTypeName(node->getProcessor()),
                                        DefaultCardLayouts::builtIn())
                  .toVar()
            : (layout ? layout->toVar() : juce::var());
    if (juce::JSON::toString(wanted) == juce::JSON::toString(getCardLayoutOverride(graph, nodeId)))
        return true; // no-op: do not burn an undo step on it

    auto apply = [&graph, nodeId, wanted] {
        if (auto* node = graph.getNodeForId(nodeId)) {
            if (wanted.isVoid())
                node->properties.remove(kCardLayoutNodeProperty);
            else
                node->properties.set(kCardLayoutNodeProperty, wanted.clone());
        }
    };

    if (undo != nullptr)
        undo->recordStructuralChange(graph, apply);
    else
        apply();
    return true;
}

} // namespace synth
