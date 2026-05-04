#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

/**
 * @class UndoManager
 * @brief Thin wrapper around juce::UndoManager with convenience methods for
 *        structural changes, parameter edits, and module repositioning.
 */
class GraphEditor; // Forward declaration

class UndoManager {
public:
    UndoManager();

    void setGraphEditor(GraphEditor* ge) { graphEditor = ge; }

    juce::UndoManager& getUndoManager() { return undoManager; }

    /**
     * @brief Records a graph-structural mutation (add/remove module, connect/disconnect).
     *
     * Captures before/after JSON snapshots of the graph and pushes a SnapshotAction.
     * The mutation lambda performs the actual graph change.
     * postRestore is called after undo/redo to refresh UI (e.g., updateComponents()).
     *
     * @param graph Reference to the audio processor graph
     * @param mutation Lambda that performs the actual graph mutation
     * @param postRestore Lambda called after undo/redo to refresh UI
     */
    void recordStructuralChange(juce::AudioProcessorGraph& graph, std::function<void()> mutation);

    /**
     * @brief Records a parameter value change.
     *
     * Creates a ParameterChangeAction that can undo/redo individual parameter edits.
     *
     * @param graph Reference to the audio processor graph
     * @param nodeId The node ID containing the parameter
     * @param paramId The parameter ID being changed
     * @param oldValue The previous parameter value
     * @param newValue The new parameter value
     */
    void recordParameterChange(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                               const juce::String& paramId, float oldValue, float newValue);

    /**
     * @brief Records a module position change (drag on the canvas).
     *
     * Creates a PositionChangeAction that can undo/redo module repositioning.
     * postRestore is called after undo/redo to refresh the UI.
     *
     * @param graph Reference to the audio processor graph
     * @param nodeId The module being moved
     * @param oldX Previous X position
     * @param oldY Previous Y position
     * @param newX New X position
     * @param newY New Y position
     * @param postRestore Lambda called after undo/redo to refresh UI
     */
    void recordPositionChange(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId, int oldX,
                              int oldY, int newX, int newY, std::function<void()> postRestore);

    // Snapshot-based parameter/position change recording.
    // Call captureBeforeState at gesture start, then pushSnapshotFromCapture at gesture end.
    void captureBeforeState(juce::AudioProcessorGraph& graph);
    void pushSnapshotFromCapture(juce::AudioProcessorGraph& graph);

    // Convenience methods that delegate to undoManager
    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }
    bool undo() { return undoManager.undo(); }
    bool redo() { return undoManager.redo(); }
    void clearUndoHistory() { undoManager.clearUndoHistory(); }
    void beginNewTransaction() { undoManager.beginNewTransaction(); }

private:
    GraphEditor* graphEditor = nullptr;
    juce::UndoManager undoManager{30000000, 50}; // 30MB limit, 50 min transactions
    juce::var capturedBeforeState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UndoManager)
};
