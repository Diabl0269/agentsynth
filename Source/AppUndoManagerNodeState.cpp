// Concern: AppUndoManager's module-state edits: a module's extra state alone, or together with one parameter in the
// same undo step.
#include "AppUndoManager.h"
#include "Modules/ModuleBase.h"

/**
 * @class NodeExtraStateAction
 * @brief Undoable action that replays a module's extra state (ModuleBase::setExtraState) before/after.
 *
 * Like ParameterChangeAction, the edit is already applied when this is pushed (firstPerform skips
 * it). The payload is whatever the module's setExtraState accepts as a PARTIAL update -- for a
 * HostedPluginModule that is HostedPluginModule::makeCardLayoutPatch(), never a full getExtraState(),
 * which would carry the plugin's state blob and make undo re-load the plugin. A node that no longer
 * exists is skipped and still reports success, as a structural undo can remove it first.
 */
class NodeExtraStateAction : public juce::UndoableAction {
public:
    NodeExtraStateAction(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                         const juce::var& before, const juce::var& after)
        : graph(graph)
        , nodeId(nodeId)
        , before(before)
        , after(after) {}

    bool perform() override {
        if (firstPerform) {
            firstPerform = false;
            return true;
        }
        apply(after);
        return true;
    }

    bool undo() override {
        apply(before);
        return true;
    }

    int getSizeInUnits() override { return 1; }

private:
    void apply(const juce::var& state) {
        if (auto* node = graph.getNodeForId(nodeId))
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                module->setExtraState(state);
    }

    juce::AudioProcessorGraph& graph;
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::var before;
    juce::var after;
    bool firstPerform = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeExtraStateAction)
};

void AppUndoManager::recordNodeExtraStateChange(juce::AudioProcessorGraph& graph,
                                                juce::AudioProcessorGraph::NodeID nodeId,
                                                const juce::var& beforeExtraState, const juce::var& afterExtraState) {
    // Same no-op rule as recordTimelineChange: an edit that left the state as it was is not an undo step.
    if (synth::sameJson(beforeExtraState, afterExtraState))
        return;

    beginTransaction();
    performAction(new NodeExtraStateAction(graph, nodeId, beforeExtraState, afterExtraState));
}

void AppUndoManager::recordNodeExtraStateAndParameterChange(
    juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId, const juce::var& beforeExtraState,
    const juce::var& afterExtraState, const juce::String& paramId, float oldValue, float newValue) {
    // One transaction for both halves: undo reverts the parameter and the state together.
    beginTransaction();
    if (!synth::sameJson(beforeExtraState, afterExtraState))
        performAction(new NodeExtraStateAction(graph, nodeId, beforeExtraState, afterExtraState));
    recordParameterChange(graph, nodeId, paramId, oldValue, newValue);
}
