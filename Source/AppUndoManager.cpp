#include "AppUndoManager.h"
#include "AI/AIStateMapper.h"
#include "Timeline/TimelineDoc.h"
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

        return restore(afterState);
    }

    bool undo() override { return restore(beforeState); }

    int getSizeInUnits() override {
        return static_cast<int>(
            (juce::JSON::toString(beforeState).length() + juce::JSON::toString(afterState).length()));
    }

private:
    /**
     * Restores one snapshot, keeping every node the target state still contains.
     *
     * The node-preserving apply is tried first: it reaches the same state by diffing, so modules
     * that survive the edit keep their processor instance and all its runtime state (sequencer
     * position, envelope stage, sounding voices) and an undo of a parameter-only change performs
     * no topology operation at all. It refuses — without touching the graph — any snapshot whose
     * node identities it cannot establish, and then the original destroy-and-rebuild apply runs,
     * which is always correct.
     *
     * preRestore is what detaches graph-referencing UI before processors are freed, so it fires
     * lazily: on the fallback (which frees everything) and, on the preserving path, only if a node
     * is actually being removed. When nothing is freed there is nothing to detach from, and
     * GraphEditor::updateComponents reconciles the rest — so a parameter-only undo no longer
     * destroys and re-creates every ModuleComponent either.
     */
    bool restore(const juce::var& state) {
        bool preRestoreFired = false;
        auto firePreRestore = [this, &preRestoreFired] {
            if (preRestoreFired)
                return;
            preRestoreFired = true;
            if (preRestore)
                preRestore();
        };

        if (!synth::AIStateMapper::applySnapshotPreservingNodes(state, graph, firePreRestore)) {
            firePreRestore();
            synth::AIStateMapper::applyJSONToGraph(state, graph, true, true);
        }

        if (postRestore)
            postRestore();
        return true;
    }

    juce::var beforeState;
    juce::var afterState;
    juce::AudioProcessorGraph& graph;
    std::function<void()> preRestore;
    std::function<void()> postRestore;
    bool firstPerform = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SnapshotAction)
};

/**
 * @class TimelineSnapshotAction
 * @brief Undoable action that restores TimelineDoc state from before/after juce::var snapshots.
 *
 * Deliberately a separate action type from SnapshotAction, not a graph-snapshot variant: it carries
 * only the timeline's JSON, so timeline edits don't inflate the size of every graph undo step (see
 * AppUndoManager::recordTimelineChange). Both push onto the same juce::UndoManager, so the app has
 * one undo stack and Cmd+Z stays chronological across the graph and the timeline.
 *
 * Unlike SnapshotAction, there is no diffing restore here: TimelineDoc::fromVar is already the doc's
 * single all-or-nothing load path (its own "restore" primitive), so perform()/undo() just call it
 * directly. Every var this class is constructed with came from this doc's own toVar() (captured by
 * recordTimelineChange/recordCombinedChange immediately before/after the mutation they wrap), so
 * fromVar() of it must always succeed; a failure here means the doc's round-trip contract itself
 * broke, not a user error — jassert catches that in debug builds, but the bool is still returned so a
 * release build fails the undo/redo cleanly rather than crashing.
 */
class TimelineSnapshotAction : public juce::UndoableAction {
public:
    TimelineSnapshotAction(synth::TimelineDoc& doc, const juce::var& beforeState, const juce::var& afterState)
        : doc(doc)
        , beforeState(beforeState)
        , afterState(afterState) {}

    bool perform() override {
        if (firstPerform) {
            firstPerform = false;
            return true;
        }

        return restore(afterState);
    }

    bool undo() override { return restore(beforeState); }

    int getSizeInUnits() override {
        return static_cast<int>(
            (juce::JSON::toString(beforeState).length() + juce::JSON::toString(afterState).length()));
    }

private:
    bool restore(const juce::var& state) {
        const bool ok = doc.fromVar(state);
        jassert(ok); // a var this class produced must always be accepted by fromVar
        return ok;
    }

    synth::TimelineDoc& doc;
    juce::var beforeState;
    juce::var afterState;
    bool firstPerform = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineSnapshotAction)
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

juce::UndoableAction* AppUndoManager::createGraphSnapshotAction(juce::AudioProcessorGraph& graph,
                                                                const juce::var& beforeState,
                                                                const juce::var& afterState) {
    juce::Component::SafePointer<GraphEditor> ge(graphEditor);
    return new SnapshotAction(
        beforeState, afterState, graph,
        [ge] {
            if (ge)
                ge->detachAllModuleComponents();
        },
        [ge] {
            if (ge)
                ge->updateComponents();
        });
}

void AppUndoManager::recordStructuralChange(juce::AudioProcessorGraph& graph, std::function<void()> mutation) {
    auto beforeState = synth::AIStateMapper::graphToJSON(graph);

    undoManager.beginNewTransaction();

    mutation();

    auto afterState = synth::AIStateMapper::graphToJSON(graph);

    undoManager.perform(createGraphSnapshotAction(graph, beforeState, afterState));
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

bool AppUndoManager::recordAIPatch(juce::AudioProcessorGraph& graph, const juce::String& actionName,
                                   std::function<bool()> mutation, std::function<void()> preRestore,
                                   std::function<void()> postRestore) {
    if (!mutation)
        return false;

    auto beforeState = synth::AIStateMapper::graphToJSON(graph);

    // The mutation performs the apply itself (including its own listener notifications). If the patch is
    // rejected the graph is untouched — return without pushing so the undo stack keeps no no-op entry.
    if (!mutation())
        return false;

    auto afterState = synth::AIStateMapper::graphToJSON(graph);

    undoManager.beginNewTransaction(actionName);
    undoManager.perform(new SnapshotAction(beforeState, afterState, graph, preRestore, postRestore));
    return true;
}

void AppUndoManager::captureBeforeState(juce::AudioProcessorGraph& graph) {
    capturedBeforeState = synth::AIStateMapper::graphToJSON(graph);
}

void AppUndoManager::pushSnapshotFromCapture(juce::AudioProcessorGraph& graph) {
    if (capturedBeforeState.isVoid())
        return;

    auto afterState = synth::AIStateMapper::graphToJSON(graph);

    if (juce::JSON::toString(capturedBeforeState) != juce::JSON::toString(afterState)) {
        undoManager.beginNewTransaction();
        undoManager.perform(createGraphSnapshotAction(graph, capturedBeforeState, afterState));
    }

    capturedBeforeState = juce::var();
}

bool AppUndoManager::recordTimelineChange(synth::TimelineDoc& doc, const std::function<void()>& mutation) {
    if (!mutation)
        return false;

    const juce::var beforeState = doc.toVar();

    mutation();

    const juce::var afterState = doc.toVar();

    if (juce::JSON::toString(beforeState) == juce::JSON::toString(afterState))
        return false; // no-op mutation: don't create an undo step

    undoManager.beginNewTransaction();
    undoManager.perform(new TimelineSnapshotAction(doc, beforeState, afterState));
    return true;
}

bool AppUndoManager::recordCombinedChange(juce::AudioProcessorGraph& graph, synth::TimelineDoc& doc,
                                          const std::function<void()>& mutation) {
    if (!mutation)
        return false;

    undoManager.beginNewTransaction();

    const juce::var graphBefore = synth::AIStateMapper::graphToJSON(graph);
    const juce::var timelineBefore = doc.toVar();

    mutation();

    const juce::var graphAfter = synth::AIStateMapper::graphToJSON(graph);
    const juce::var timelineAfter = doc.toVar();

    const bool graphChanged = juce::JSON::toString(graphBefore) != juce::JSON::toString(graphAfter);
    const bool timelineChanged = juce::JSON::toString(timelineBefore) != juce::JSON::toString(timelineAfter);

    if (!graphChanged && !timelineChanged)
        return false; // neither domain changed: no transaction pushed

    // Both perform() calls land in the same transaction (no beginNewTransaction between them), so a
    // single undo()/redo() reverts or re-applies whichever of the two actually changed, together.
    if (graphChanged)
        undoManager.perform(createGraphSnapshotAction(graph, graphBefore, graphAfter));
    if (timelineChanged)
        undoManager.perform(new TimelineSnapshotAction(doc, timelineBefore, timelineAfter));

    return true;
}
