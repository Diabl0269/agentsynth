#include "AppUndoManager.h"
#include "AI/AIStateMapper.h"
#include "UI/GraphEditor.h"

/**
 * @class SnapshotAction
 * @brief Undoable action that restores graph state from JSON snapshots.
 */
class SnapshotAction : public juce::UndoableAction {
public:
    SnapshotAction(const juce::var& beforeState, const juce::var& afterState, juce::AudioProcessorGraph& graph,
                   std::function<void()> preRestore, std::function<void()> postRestore)
        : beforeState(beforeState)
        , afterState(afterState)
        , graph(graph)
        , preRestore(preRestore)
        , postRestore(postRestore) {}

    bool perform() override {
        if (firstPerform) {
            firstPerform = false;
            return true;
        }

        if (preRestore)
            preRestore();
        synth::AIStateMapper::applyJSONToGraph(afterState, graph, true, true);
        if (postRestore)
            postRestore();
        return true;
    }

    bool undo() override {
        if (preRestore)
            preRestore();
        synth::AIStateMapper::applyJSONToGraph(beforeState, graph, true, true);
        if (postRestore)
            postRestore();
        return true;
    }

    int getSizeInUnits() override {
        return static_cast<int>(
            (juce::JSON::toString(beforeState).length() + juce::JSON::toString(afterState).length()));
    }

private:
    juce::var beforeState;
    juce::var afterState;
    juce::AudioProcessorGraph& graph;
    std::function<void()> preRestore;
    std::function<void()> postRestore;
    bool firstPerform = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SnapshotAction)
};

/**
 * @class ParameterChangeAction
 * @brief Undoable action for individual parameter value changes.
 */
class ParameterChangeAction : public juce::UndoableAction {
public:
    ParameterChangeAction(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                          const juce::String& paramId, float oldValue, float newValue)
        : graph(graph)
        , nodeId(nodeId)
        , paramId(paramId)
        , oldValue(oldValue)
        , newValue(newValue) {}

    bool perform() override {
        if (firstPerform) {
            firstPerform = false;
            return true;
        }
        if (auto* p = findParameter())
            p->setValueNotifyingHost(newValue);
        return true; // Always return true — node may not exist after structural undo
    }

    bool undo() override {
        if (auto* p = findParameter())
            p->setValueNotifyingHost(oldValue);
        return true;
    }

    int getSizeInUnits() override { return 1; }

    UndoableAction* createCoalescedAction(UndoableAction* nextAction) override {
        if (auto* next = dynamic_cast<ParameterChangeAction*>(nextAction)) {
            if (next->nodeId == nodeId && next->paramId == paramId) {
                return new ParameterChangeAction(graph, nodeId, paramId, oldValue, next->newValue);
            }
        }
        return nullptr;
    }

    juce::AudioProcessorGraph::NodeID getNodeId() const { return nodeId; }
    const juce::String& getParamId() const { return paramId; }

private:
    juce::RangedAudioParameter* findParameter() const {
        if (auto* node = graph.getNodeForId(nodeId)) {
            for (auto* param : node->getProcessor()->getParameters()) {
                if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                    if (p->paramID == paramId)
                        return dynamic_cast<juce::RangedAudioParameter*>(param);
                }
            }
        }
        return nullptr;
    }

    juce::AudioProcessorGraph& graph;
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::String paramId;
    float oldValue;
    float newValue;
    bool firstPerform = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterChangeAction)
};

/**
 * @class PositionChangeAction
 * @brief Undoable action for module position changes on the canvas.
 */
class PositionChangeAction : public juce::UndoableAction {
public:
    PositionChangeAction(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId, int oldX, int oldY,
                         int newX, int newY, std::function<void()> postRestore)
        : graph(graph)
        , nodeId(nodeId)
        , oldX(oldX)
        , oldY(oldY)
        , newX(newX)
        , newY(newY)
        , postRestore(postRestore) {}

    bool perform() override {
        // Skip the first perform() since the position was already changed
        // by the user dragging on the canvas.
        if (firstPerform) {
            firstPerform = false;
            return true;
        }

        // On redo: apply new position
        return applyPosition(newX, newY);
    }

    bool undo() override {
        // On undo: apply old position
        return applyPosition(oldX, oldY);
    }

    int getSizeInUnits() override {
        return 64; // Small fixed size for position changes
    }

private:
    bool applyPosition(int x, int y) {
        if (auto* node = graph.getNodeForId(nodeId)) {
            node->properties.set("x", x);
            node->properties.set("y", y);
        }
        if (postRestore)
            postRestore();
        return true; // Always return true — node may not exist after structural undo
    }

    juce::AudioProcessorGraph& graph;
    juce::AudioProcessorGraph::NodeID nodeId;
    int oldX, oldY, newX, newY;
    std::function<void()> postRestore;
    bool firstPerform = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PositionChangeAction)
};

// =============================================================================

AppUndoManager::AppUndoManager() {}

void AppUndoManager::recordStructuralChange(juce::AudioProcessorGraph& graph, std::function<void()> mutation) {
    auto beforeState = synth::AIStateMapper::graphToJSON(graph);

    undoManager.beginNewTransaction();

    mutation();

    auto afterState = synth::AIStateMapper::graphToJSON(graph);

    juce::Component::SafePointer<GraphEditor> ge(graphEditor);
    undoManager.perform(new SnapshotAction(
        beforeState, afterState, graph,
        [ge] {
            if (ge)
                ge->detachAllModuleComponents();
        },
        [ge] {
            if (ge)
                ge->updateComponents();
        }));
}

void AppUndoManager::recordParameterChange(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                                           const juce::String& paramId, float oldValue, float newValue) {
    // The firstPerform flag will skip the first perform() since the parameter
    // was already changed by the user.
    undoManager.perform(new ParameterChangeAction(graph, nodeId, paramId, oldValue, newValue));
}

void AppUndoManager::recordPositionChange(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                                          int oldX, int oldY, int newX, int newY, std::function<void()> postRestore) {
    undoManager.beginNewTransaction();

    // The firstPerform flag will skip the first perform() since the position
    // was already changed by the user dragging on the canvas.
    undoManager.perform(new PositionChangeAction(graph, nodeId, oldX, oldY, newX, newY, postRestore));
}

void AppUndoManager::captureBeforeState(juce::AudioProcessorGraph& graph) {
    capturedBeforeState = synth::AIStateMapper::graphToJSON(graph);
}

void AppUndoManager::pushSnapshotFromCapture(juce::AudioProcessorGraph& graph) {
    if (capturedBeforeState.isVoid())
        return;

    auto afterState = synth::AIStateMapper::graphToJSON(graph);

    if (juce::JSON::toString(capturedBeforeState) != juce::JSON::toString(afterState)) {
        juce::Component::SafePointer<GraphEditor> ge(graphEditor);
        undoManager.beginNewTransaction();
        undoManager.perform(new SnapshotAction(
            capturedBeforeState, afterState, graph,
            [ge] {
                if (ge)
                    ge->detachAllModuleComponents();
            },
            [ge] {
                if (ge)
                    ge->updateComponents();
            }));
    }

    capturedBeforeState = juce::var();
}
