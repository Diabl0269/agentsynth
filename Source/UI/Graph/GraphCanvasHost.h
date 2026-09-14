// GraphCanvasHost.h
//
// Narrow abstract seam GraphEditor's canvas collaborators (SmartConnectionEngine today; more
// follow in FRO77 PR2/PR3) reach GraphEditor through, so a collaborator depends on this
// interface rather than GraphEditor's whole public surface. GraphEditor implements it privately
// (Source/UI/Graph/GraphEditor/GraphEditor.h) — only code holding a GraphCanvasHost& can call
// through it; GraphEditor's own methods still call each other directly. Self-contained: forward
// declares rather than pulling in AudioEngine.h/ModuleComponent.h/AppUndoManager.h, so this header
// stays cheap for every collaborator that includes it.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;
class AppUndoManager;
class ModuleComponent;

class GraphCanvasHost {
public:
    virtual ~GraphCanvasHost() = default;

    virtual juce::AudioProcessorGraph& graph() = 0;
    virtual AudioEngine& engine() = 0;

    /** The live ModuleComponent for `nodeId`, or nullptr when none is on the canvas. The single
     *  lookup collaborators use instead of scanning modules() by hand at every call site. */
    virtual ModuleComponent* moduleComponentFor(juce::AudioProcessorGraph::NodeID nodeId) = 0;

    /** Every ModuleComponent currently on the canvas, in paint/creation order. */
    virtual juce::OwnedArray<ModuleComponent>& modules() = 0;

    virtual void repaintCanvas() = 0;
    virtual void updateComponents() = 0;

    /** Null when the host was constructed with no undo manager (e.g. a headless test). */
    virtual AppUndoManager* undo() = 0;

    /** Wires two visible jacks the same way a completed cable-drag does (poly fan, MIDI,
     *  attenuverter for mono mod CV) — see GraphEditor::connectPorts. When recordUndo is false the
     *  caller owns the transaction. */
    virtual void connectPorts(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                              juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi, bool recordUndo) = 0;
};
