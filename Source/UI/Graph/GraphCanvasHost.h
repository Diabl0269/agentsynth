// GraphCanvasHost.h
//
// Narrow abstract seam GraphEditor's canvas collaborators (SmartConnectionEngine, PR1;
// MacroGroupController, PR2; GraphDragDropController, PR3) reach GraphEditor through, so a
// collaborator depends on this interface rather than GraphEditor's whole public surface.
// GraphEditor implements it privately (Source/UI/Graph/GraphEditor/GraphEditor.h) — only code
// holding a GraphCanvasHost& can call through it; GraphEditor's own methods still call each other
// directly. Self-contained: forward declares rather than pulling in AudioEngine.h/
// ModuleComponent.h/AppUndoManager.h, so this header stays cheap for every collaborator that
// includes it.
#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class AudioEngine;
class AppUndoManager;
class ModuleComponent;
class MacroCardComponent;
namespace synth {
class MacroSet;
struct PluginIdentity;
} // namespace synth
namespace synth::ui {
class SelectionModel;
}

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

    // ---- FRO77 PR2: MacroGroupController additions ----------------------------------------
    //
    // Nine of these thirteen additions reuse an EXISTING GraphEditor method's name — GraphEditor's
    // own getMacros()/getSelection()/applySelectionChange()/setSelectedNodes()/deleteSelection()/
    // getModuleTitle()/syncMacroCards()/requestGroupSelectionIntoMacro()/
    // getAutoDeleteMacroPortsOnLastCableEnabled() already do exactly what MacroGroupController
    // needs, so each simply gains `override` (see GraphEditor.h) rather than growing a
    // differently-named wrapper — the same trick repaintCanvas()/updateComponents()/
    // connectPorts() already use above. Only macroCards()/reportStatusMessage()/
    // clearModMatrixRows()/requestRepaint() are genuinely new.

    /** GraphEditor's live set of Macros for the current patch (Source/MacroSet.h). Stays a
     *  GraphEditor member rather than moving into MacroGroupController: non-macro units
     *  (persistence, GraphEditorCanvas.cpp's updateComponents() pruning it, GraphEditorCables.cpp/
     *  Channels.cpp reading/writing it directly) touch it too widely for a clean full move. */
    virtual synth::MacroSet& getMacros() = 0;

    /** Every MacroCardComponent currently on the canvas (one per collapsed-or-not macro), in
     *  creation order — the macro counterpart to modules() above. */
    virtual juce::OwnedArray<MacroCardComponent>& macroCards() = 0;

    /** Read-only access to the canvas selection. Selection itself is not a macro concern and
     *  stays on GraphEditor; macro-selection sugar (selectMacro/isMacroSelected) needs to read
     *  it. */
    virtual const synth::ui::SelectionModel& getSelection() const = 0;

    /** Replaces the selection, repainting only the cards whose selected state actually changed. */
    virtual void applySelectionChange(const std::vector<juce::AudioProcessorGraph::NodeID>& newSelection) = 0;

    /** Same operation as applySelectionChange — GraphEditor::setSelectedNodes is its own public
     *  spelling of it; both names are preserved as separate host methods only because macro code
     *  calls both by their original name (verbatim-body rule) and neither wraps the other on
     *  GraphEditor either. */
    virtual void setSelectedNodes(const std::vector<juce::AudioProcessorGraph::NodeID>& ids) = 0;

    /** Removes every selected module as one undoable change. deleteMacroAndMembers/removeMacroPort
     *  select the nodes to remove, then call this. */
    virtual void deleteSelection() = 0;

    /** Status-bar surface for a refused/soft-failed action — wraps `if (onStatusMessage)
     *  onStatusMessage(message);` so macro code never reaches GraphEditor's callback field
     *  directly. */
    virtual void reportStatusMessage(const juce::String& message) = 0;

    /** The title a card should paint for `nodeId` (custom display name, else the processor's
     *  auto-numbered name). Not macro-specific; exposed because macroMemberNames() needs it and
     *  title resolution otherwise requires a live GraphEditor. */
    virtual juce::String getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId,
                                        juce::AudioProcessor* processor) const = 0;

    /** Drops every live mod-matrix row — GraphEditor's ModMatrixComponent is not reachable any
     *  other way, and a node-removing macro mutation (spliceOutMacroPort, changeMacroPortShape)
     *  must clear it first, exactly like every other node-removal site in GraphEditor does. */
    virtual void clearModMatrixRows() = 0;

    /** Rebuilds the MacroCardComponent set to match getMacros(). GraphEditor's own method, not
     *  moved into MacroGroupController: it constructs `new MacroCardComponent(*this, ...)`, which
     *  needs a genuine GraphEditor&, not obtainable through this narrow seam. Macro mutations that
     *  change a macro's presentation (rename, recolour) call this afterward. */
    virtual void syncMacroCards() = 0;

    /** Plain `juce::Component::repaint()` on the canvas — distinct from repaintCanvas() above,
     *  which ALSO drops the cable-geometry memo. Macro presentation-only mutations (rename,
     *  recolour, port add/remove/rename/reorder/recolour) call this, matching their pre-PR2
     *  behaviour of a bare repaint() with no cable-cache invalidation. */
    virtual void requestRepaint() = 0;

    /** Cmd+G's real entry point — GraphEditor's own method, not moved into MacroGroupController
     *  (it shows a SafePointer-based async modal, docs/macros/auto-ports.md#auto-creating-ports-when-grouping).
     *  MacroGroupController::groupOrToggleSelectionMacros() calls this for a selection that
     *  touches no macro yet. */
    virtual void requestGroupSelectionIntoMacro() = 0;

    /** The user preference gating auto-delete of a macro port on its last cable being removed
     *  (Preferences). GraphEditor's own getAutoDeleteMacroPortsOnLastCableEnabled() already
     *  returns exactly this, so it simply gains `override` (see GraphEditor.h) rather than
     *  growing a differently-named wrapper — autoDeleteOrphanedMacroPort's very first check. */
    virtual bool getAutoDeleteMacroPortsOnLastCableEnabled() const noexcept = 0;

    // ---- FRO77 PR3: GraphDragDropController additions --------------------------------------
    //
    // Nine of these fourteen additions reuse an EXISTING GraphEditor method's name, the same
    // dual-purpose-override trick PR2 used above — clearSmartSuggestions()/refreshSmartSuggestions()/
    // applyDefaultDualIOForNewModule()/resolvePlacement()/estimateSnippetSize()/
    // isSelectionDragActive()/insertSnippetAt()/addHostedPluginAtCanvasPosition()/
    // addModuleAtCanvasPosition() already do exactly what GraphDragDropController needs, so each
    // simply gains `override` (see GraphEditor.h). Only lookAndFeel()/seedInsertModifierSample()/
    // canvasPositionOfLocalPoint()/estimateModuleSizeForType()/resolveSnippetPayload() are
    // genuinely new.

    /** Repaints suggestion candidacy from the current drag-preview state — GraphEditor's own
     *  private forwarder onto SmartConnectionEngine::refreshSmartSuggestions(). */
    virtual void refreshSmartSuggestions() = 0;

    /** Drops the current suggestion set — GraphEditor's own private forwarder onto
     *  SmartConnectionEngine::clearSmartSuggestions(). */
    virtual void clearSmartSuggestions() = 0;

    /** Applies the app/per-type Dual I/O default to a newly created (not yet on-canvas) processor —
     *  GraphEditor's own method; unchanged by FRO77 PR3, just exposed for the drag-preview probe
     *  and library-drop paths that used to reach it directly as a same-class private call. */
    virtual void applyDefaultDualIOForNewModule(juce::AudioProcessor& processor,
                                                const juce::String& moduleType) const = 0;

    /** Snapped + anti-overlapped placement for a footprint at a desired canvas position —
     *  GraphEditor's own (already public) resolvePlacement(), simply gaining `override`. */
    virtual juce::Point<int> resolvePlacement(juce::Point<int> desired, int w, int h,
                                              juce::AudioProcessorGraph::NodeID selfId) = 0;

    /** Footprint of the group a snippet drag payload would drop — GraphEditor's own (private)
     *  estimateSnippetSize(), simply gaining `override`. */
    virtual juce::Point<int> estimateSnippetSize(const juce::String& payload) const = 0;

    /** Whether a multi-selection group drag is currently live — GraphEditor's own (already
     *  public) isSelectionDragActive(), needed by buildDragPreviewState's suggestion-suppression
     *  guard now that the drag-preview fields it packages live off GraphEditor. */
    virtual bool isSelectionDragActive() const = 0;

    /** Inserts a resolved snippet payload at a canvas position — GraphEditor's own (already
     *  public) insertSnippetAt(), simply gaining `override`. */
    virtual bool insertSnippetAt(const juce::var& snippet, juce::Point<int> canvasPos) = 0;

    /** Creates a Hosted Plugin node pointed at `identity` — GraphEditor's own (already public)
     *  addHostedPluginAtCanvasPosition(), simply gaining `override`. */
    virtual void addHostedPluginAtCanvasPosition(const synth::PluginIdentity& identity, juce::Point<int> dropPos) = 0;

    /** Creates `name` at a canvas position with undo recorded — GraphEditor's own (already
     *  public) addModuleAtCanvasPosition(), simply gaining `override`. Also the drop-side half of
     *  the itemDropped/filesDropped bodies that moved into GraphDragDropController. */
    virtual void addModuleAtCanvasPosition(const juce::String& name, juce::Point<int> dropPos,
                                           const std::function<void(juce::AudioProcessor&)>& configure,
                                           const juce::String& joinMacroId) = 0;

    /** Whether a library drag over an expanded macro's hull should join that macro: Cmd held, or the
     *  drag-without-Cmd preference on. The one place the drop-side modifier is read. */
    virtual bool isMacroJoinModifierDown() const = 0;

    /** The expanded macro a library module dropped with its centre at `canvasCentre` would join, or
     *  empty when the join modifier is up or the point is over no expanded hull. */
    virtual juce::String macroJoinTargetAt(juce::Point<int> canvasCentre) const = 0;

    /** Emphasises `macroId`'s hull as the live drop target (empty clears it). */
    virtual void setMacroDropCandidate(const juce::String& macroId) = 0;

    /** The canvas's active LookAndFeel — plain `Component::getLookAndFeel()`, exposed because
     *  updateDragPreview's grid-metric lookup needs the theme's `AppLookAndFeel` (via dynamic_cast)
     *  and GraphDragDropController is not itself a Component. */
    virtual juce::LookAndFeel& lookAndFeel() = 0;

    /** Seeds the drag-tick insert-modifier comparison from the state right now — GraphEditor's own
     *  beginDragPreview used to reach smartConnections_.seedInsertModifierSample() directly as a
     *  same-class private member; this is the host-mediated equivalent now that beginDragPreview
     *  itself lives on GraphDragDropController. */
    virtual void seedInsertModifierSample() = 0;

    /** Converts a point expressed relative to the host Component (e.g. a SourceDetails::localPosition)
     *  into canvas-content coordinates — GraphEditor's own `content.getLocalPoint(this, point)`,
     *  exposed because GraphDragDropController holds no Component of its own to convert through. */
    virtual juce::Point<int> canvasPositionOfLocalPoint(juce::Point<int> pointOnHost) const = 0;

    /** Estimated (w, h) footprint for a module type name — GraphEditor's own STATIC
     *  estimateModuleSize(), wrapped in a non-static forwarder: a static member can't itself
     *  `override` a virtual, so this is a distinct name rather than the dual-purpose-override
     *  trick used elsewhere on this interface. */
    virtual juce::Point<int> estimateModuleSizeForType(const juce::String& typeName) const = 0;

    /** Resolves a snippet drag payload's name to its JSON via the owner's snippetProvider
     *  callback, or a default (non-object) juce::var when no provider is set — collapses
     *  GraphEditor's former `if (!snippetProvider) return;` guard into the same isObject() check
     *  itemDropped already makes for "provider returned something unusable", since an unset
     *  provider and a default-constructed juce::var both fail isObject(). */
    virtual juce::var resolveSnippetPayload(const juce::String& name) const = 0;
};
