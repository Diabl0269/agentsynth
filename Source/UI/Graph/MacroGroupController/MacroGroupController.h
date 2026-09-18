// MacroGroupController.h
//
// Macro grouping/membership/collapse, macro geometry + card jacks, macro port CRUD, the
// port-crossing-plan math, and the bypass/mute fan-out (docs/macros/macros.md, docs/macros/ports.md).
// Reaches its owning canvas only through GraphCanvasHost — see that header for the narrow seam
// this depends on. GraphEditor holds one instance (`macroController_`) and forwards its own
// (unchanged) public macro API to it; the `synth::MacroSet` state itself stays on GraphEditor
// (see GraphCanvasHost::macroSet()'s comment for why) and is reached through the host on every
// call.
//
// NOT everything macro-related moved here (FRO77 PR2). A handful of methods construct a
// `MacroCardComponent(GraphEditor&, ...)` or a `juce::Component::SafePointer<GraphEditor>` for an
// async dialog/popup callback — both need a genuine GraphEditor&, which this class deliberately
// cannot obtain through GraphCanvasHost. Those stay implemented on GraphEditor (their PUBLIC
// signatures are unchanged either way): syncMacroCards()/dockMacroPortWidgets() callers now call
// through this class only for dockMacroPortWidgets (moved; no SafePointer/Component need),
// GraphEditor::syncMacroCards() itself, the macro chip-drag quartet (begin/drag/finalize/
// cancelMacroCardDrag — thin wrappers over GraphEditor's own selection-drag primitives),
// categoryPreviewColour() (needs getLookAndFeel()), and six SafePointer-based
// prompt/menu builders: requestGroupSelectionIntoMacro()/showMacroAutoPortModal(),
// promptRenameMacro(), buildMacroColourPicker()/promptRecolourMacro()/
// createMacroColourPickerForTest(), buildMacroMenu(), promptConfigureMacroIO(),
// promptRenameMacroPort().
//
// Self-contained header: never includes GraphEditor.h (GraphEditor.h includes THIS header to
// declare its `macroController_` member, so the reverse would cycle). Implementation split across
// sibling MacroGroupController*.cpp files in this directory.
#pragma once

#include "MacroSet.h"
#include "Modules/MacroPortShape.h"
#include "UI/Graph/CableColour.h"
#include "UI/Graph/GraphCanvasHost.h"
#include "UI/Macros/MacroPortConfigDialog/MacroPortConfigDialog.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class ModuleBase;
class MacroCardComponent;

class MacroGroupController {
public:
    explicit MacroGroupController(GraphCanvasHost& host)
        : host_(host) {}

    // ---- Nested types (aliased on GraphEditor as `using X = MacroGroupController::X;`) --------

    /** The MacroPort `nodeId` fronts, and the macro that owns it, resolved together. `macro` is
     *  null when `nodeId` doesn't front any macro's port. See GraphEditor::MacroPortOwner (P8-15
     *  founder-review fix F2) for the full original contract. */
    struct MacroPortOwner {
        const synth::Macro* macro = nullptr;
        const synth::MacroPort* port = nullptr;
    };

    /** Live bounds + colour category for one resolvable MODULE member (a port node is excluded —
     *  founder-review fix G6) — the data a collapsed card's content preview scales into itself.
     *  See GraphEditor::MacroMemberPreview. */
    struct MacroMemberPreview {
        juce::Rectangle<int> bounds;
        synth::ui::ModuleCategory category = synth::ui::ModuleCategory::Utility;
    };

    /** One port's on-card jack, in the collapsed card's OWN local coordinates (add the live
     *  card's top-left — macroCableAnchorBounds — for canvas coords). Inputs run down the card's
     *  left edge, outputs down its right edge, each side ordered by MacroPort::order — the SAME
     *  order macroPortRowsForDialog sorts by, so a port's jack position and its row in the
     *  Configure I/O dialog always agree on which port is "first". The ONE layout definition:
     *  MacroCardComponent::paint(), buildVisibleCables()'s boundary-cable anchoring, and
     *  endConnectionDrag()'s jack hit-test all read this rather than recomputing it, so the drawn
     *  dot, the anchored cable and the drop target can never drift apart. (P8-15c, T141,
     *  docs/macros/ports.md#cable-rendering-across-the-boundary). Aliased as GraphEditor::MacroCardPort. */
    struct MacroCardPort {
        juce::String nodeUuid;
        bool isInput = false;
        synth::MacroPortKind kind = synth::MacroPortKind::AudioCV;
        juce::String name;
        juce::Point<int> jackPos;           // card-local
        std::optional<juce::Colour> colour; // T152; unset -> kind tint fallback
    };

    /** Tri-state read of a macro's members' bypass (or mute) state (docs/macros/ports.md#bypass-and-mute). See
     *  GraphEditor::MacroToggleState. */
    enum class MacroToggleState { AllOff, AllOn, Mixed };

    /** One raw graph connection crossing a would-be macro's boundary. See
     *  GraphEditor::MacroPortCrossingEdge. */
    struct MacroPortCrossingEdge {
        juce::AudioProcessorGraph::NodeID externalNodeId;
        int externalRawChannel = 0;
        int internalRawChannel = 0;
        int legIndex = 0;
    };

    /** Every crossing connection landing on the SAME (internal node, direction, visible jack) —
     *  the de-duplication key a crossing plan groups edges by. See
     *  GraphEditor::MacroPortCrossingGroup for the full field-by-field contract. */
    struct MacroPortCrossingGroup {
        juce::AudioProcessorGraph::NodeID internalNodeId;
        juce::String internalUuid;
        bool isInput = false;
        bool isMidi = false;
        int visibleJack = -1;
        int headRawChannel = 0;
        MacroPortShape shape = MacroPortShape::Mono;
        int voiceCount = 1;
        std::vector<MacroPortCrossingEdge> edges;
    };

    // ---- Grouping / membership / collapse (docs/macros/macros.md) -------------------------------------
    //
    // A Macro is a named, coloured, collapsible container: membership plus presentation, no
    // graph change. Flat model — a node already in a macro cannot be grouped into a second one.
    // Membership is by node UUID, so it survives save/load and undo/redo exactly like everything
    // else in synth::MacroSet. Collapsed-macro selection/drag/delete are deliberately NOT a
    // parallel mechanism: selecting a macro selects its members in the ordinary SelectionModel, so
    // beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag and deleteSelection all just work,
    // unchanged, on a collapsed macro's members exactly as on any other multi-selection.

    juce::String groupSelectionIntoMacro(bool autoCreatePorts = false);
    juce::String addMacroForMembers(const std::vector<juce::String>& memberUuids, const juce::String& name,
                                    juce::Point<int> origin);
    bool selectionHasCrossingMacroCable() const;
    void ungroupSelection();
    /** `recordUndo=false` (FRO40): runs the `doAdd` mutation directly, with no
     *  `recordGraphAndMacroChange` transaction of its own — for a caller (GraphEditor's Cmd/Ctrl-
     *  drag reparent finalize) that already opened one around a bigger gesture (position + this
     *  membership change + port splicing) and needs all of it in ONE undo step. Every existing
     *  caller keeps the default and stays byte-identical. */
    void addSelectionToMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids,
                             bool recordUndo = true);
    /** `recordUndo=false`: see addSelectionToMacro's doc comment above — same deal, mirrored. */
    void removeSelectionFromMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids,
                                  bool recordUndo = true);
    void removeNodeFromMacro(juce::AudioProcessorGraph::NodeID nodeId);
    void toggleSelectionMacrosCollapsed();
    void groupOrToggleSelectionMacros();
    void selectMacro(const juce::String& macroId, bool additive);
    bool isMacroSelected(const juce::String& macroId) const;
    const synth::Macro* macroForNode(juce::AudioProcessorGraph::NodeID nodeId) const;
    void setMacroCollapsed(const juce::String& macroId, bool collapsed);
    void renameMacro(const juce::String& macroId, const juce::String& newName);

    /** FRO14 (docs/mixer/mixer.md 5.2): installed by the app when renaming a macro may also have to
     *  rename a LINKED track, so both halves land in ONE undo step. Called by renameMacro INSTEAD
     *  of its own recordGraphAndMacroChange, with the macro id, the new name, and the rename
     *  mutation to run inside whatever transaction the hook opens; returning false (nothing linked)
     *  leaves renameMacro to record exactly as it always has. Unset by default -- a standalone
     *  editor and every test keep today's behaviour with no wiring at all. */
    std::function<bool(const juce::String& macroId, const juce::String& newName,
                       const std::function<void()>& renameMutation)>
        recordMacroRenameHook;
    void setMacroColour(const juce::String& macroId, juce::Colour colour);
    void deleteMacroAndMembers(const juce::String& macroId);
    std::vector<MacroMemberPreview> macroMemberPreviews(const juce::String& macroId) const;
    juce::StringArray macroMemberNames(const juce::String& macroId) const;

    // ---- Bypass/mute fan-out (P8-15d, T142, docs/macros/ports.md#bypass-and-mute) -------------------------

    MacroToggleState macroBypassState(const juce::String& macroId) const;
    MacroToggleState macroMuteState(const juce::String& macroId) const;
    void setMacroBypassed(const juce::String& macroId, bool bypassed);
    void setMacroMuted(const juce::String& macroId, bool muted);
    void toggleMacroBypassed(const juce::String& macroId);
    void toggleMacroMuted(const juce::String& macroId);

    // ---- Geometry / hit-testing / card jacks (docs/macros/ports.md#cable-rendering-across-the-boundary)
    // -----------------------

    juce::Rectangle<int> macroHullBounds(const juce::String& macroId) const;
    /** FRO40: `macroHullBounds` above, but with `excludedMemberUuid` left out of the union too —
     *  the LEAVE half of a Cmd/Ctrl-drag needs this because the plain hull is a LIVE union of
     *  member bounds, so the member being dragged OUT keeps inflating its own hull and could never
     *  cross back out of it (see macroDragJoinOrLeaveTarget's own comment). Empty under the same
     *  conditions as macroHullBounds (no macro, collapsed, or nothing left to union). */
    juce::Rectangle<int> macroHullBoundsExcluding(const juce::String& macroId,
                                                  const juce::String& excludedMemberUuid) const;
    juce::String macroHullAt(juce::Point<int> canvasPos) const;
    /** FRO40: the ONE query behind the Cmd/Ctrl-drag-across-a-hull gesture (docs/macros/ports.md).
     *  `draggedNodeId` not a member of any macro: JOIN test — `canvasCentre` (the dragged module's
     *  own centre, not its top-left) against `macroHullAt`, which already only considers EXPANDED
     *  macros, so a collapsed one is never a candidate. `draggedNodeId` already a member of macro
     *  X: LEAVE test — `canvasCentre` against X's `macroHullBoundsExcluding` its own uuid; outside
     *  that hull means "would leave X". Returns X's/the candidate's id, or empty for neither
     *  (no resolvable uuid, inside its own macro's hull, or over no expanded hull at all). The flat
     *  membership model (Macro's own class comment) means a member of one macro is never a JOIN
     *  candidate for a different one, so this never tests JOIN for an already-grouped node. */
    juce::String macroDragJoinOrLeaveTarget(juce::AudioProcessorGraph::NodeID draggedNodeId,
                                            juce::Point<int> canvasCentre) const;
    juce::Rectangle<int> macroChipBounds(const juce::String& macroId) const;
    juce::String macroChipAt(juce::Point<int> canvasPos) const;
    juce::Rectangle<int> macroCollapseButtonBounds(const juce::String& macroId) const;
    juce::String macroCollapseButtonAt(juce::Point<int> canvasPos) const;
    MacroCardComponent* getMacroCardForTest(const juce::String& macroId);
    std::vector<MacroCardPort> macroCardPortLayout(const juce::String& macroId) const;
    std::optional<MacroCardPort> macroCardPortForPoint(const juce::String& macroId,
                                                       juce::Point<int> cardLocalPos) const;
    MacroPortOwner macroPortOwnerFor(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** Positions every EXPANDED macro's port widgets against macroHullBounds() (P8-15 fix F2,
     *  docs/macros/ports.md#how-a-port-is-drawn has the full layout contract). GraphEditorCanvas.cpp/
     *  GraphEditorDragDrop.cpp/GraphEditorSelection.cpp call this directly. */
    void dockMacroPortWidgets();

    // ---- Macro I/O CRUD (P8-15b, T140, docs/macros/ports.md) --------------------------------

    juce::String addMacroPort(const juce::String& macroId, bool isInput, synth::MacroPortKind kind,
                              MacroPortShape shape, int voiceCount, const juce::String& portName);
    void removeMacroPort(const juce::String& macroId, const juce::String& nodeUuid);
    void deleteMacroPortNode(const juce::String& macroId, const juce::String& nodeUuid);
    void renameMacroPort(const juce::String& macroId, const juce::String& nodeUuid, const juce::String& newName);
    void moveMacroPortOrder(const juce::String& macroId, const juce::String& nodeUuid, bool moveUp);
    void reorderMacroPortToIndex(const juce::String& macroId, const juce::String& nodeUuid, int newIndexInGroup);
    juce::String changeMacroPortShape(const juce::String& macroId, const juce::String& nodeUuid,
                                      MacroPortShape newShape, int newVoiceCount);
    void changeMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                               std::optional<juce::Colour> newColour);

    /** Reused by GraphEditor::promptConfigureMacroIO (stays on GraphEditor — SafePointer-based
     *  async dialog), which calls this directly. */
    std::vector<synth::ui::MacroPortConfigDialog::PortRow> macroPortRowsForDialog(const juce::String& macroId) const;

    /** The "shape from a dropped cable" convenience
     * (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed). GraphEditorConnections.cpp's
     *  endConnectionDrag calls this directly. */
    void createMacroPortFromDroppedCable(const juce::String& macroId, bool newPortIsInput, bool isMidi,
                                         juce::AudioProcessorGraph::NodeID otherNodeId, int otherVisibleJack);

    // ---- Auto-create-ports-on-group (founder-review fix F5,
    // docs/macros/auto-ports.md#auto-creating-ports-when-grouping) ----

    /** The crossing plan a would-be macro's members (by NodeID) would need on creation. */
    std::vector<MacroPortCrossingGroup>
    buildMacroPortCrossingPlan(const std::vector<juce::AudioProcessorGraph::NodeID>& memberNodeIds) const;
    std::vector<MacroPortCrossingGroup> buildMacroPortCrossingPlan(const std::vector<juce::String>& memberUuids) const;
    /** Realises a crossing plan as actual macro ports. */
    void spliceMacroPorts(const juce::String& macroId, const std::vector<MacroPortCrossingGroup>& plan);
    /** T138: the crossing plan for members newly ADDED to `macroId` (the mirror image of
     *  buildMacroPortCrossingPlanForRemovedMembers). */
    std::vector<MacroPortCrossingGroup>
    buildMacroPortCrossingPlanForNewMembers(const juce::String& macroId,
                                            const std::vector<juce::String>& addedUuids) const;
    /** Which of `macroId`'s existing ports become interior (no longer cross the boundary) once
     *  `addedUuids` join. */
    std::vector<juce::String> macroPortsThatBecomeInteriorOnAdd(const juce::String& macroId,
                                                                const std::vector<juce::String>& addedUuids) const;
    /** T138: the crossing plan for `removedUuids` leaving the macro `macroId` —
     *  removeSelectionFromMacro()'s auto-port counterpart. Computed off the REMAINING ordinary
     *  members (existing members minus `removedUuids`, minus this macro's own ports) as the
     *  "inside" set, so an edge to a departing member now reads as a genuine crossing; then filtered
     *  to keep only edges whose external endpoint is actually one of `removedUuids`. Empty if
     *  `macroId` doesn't resolve. */
    std::vector<MacroPortCrossingGroup>
    buildMacroPortCrossingPlanForRemovedMembers(const juce::String& macroId,
                                                const std::vector<juce::String>& removedUuids) const;
    /** Splices ONE port node back out of its macro, reconnecting the cable it proxied. */
    void spliceOutMacroPort(synth::Macro& macro, const juce::String& portNodeUuid);

    // ---- Auto-create-port-on-drag / auto-delete-on-last-cable (T148, docs/macros/auto-ports.md#ports-on-a-cable-drag)
    // ----

    /** True if `nodeId` resolves to a live macro member that itself fronts one of that macro's
     *  ports. GraphEditorCables.cpp/Commands.cpp call this directly. */
    bool nodeIsMacroPort(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** The endpoint-needs-a-port rule applied to a completed cable-drag.
     *  GraphEditorConnections.cpp calls this directly. */
    bool maybeAutoCreateMacroPortsForDrag(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                          juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi,
                                          bool recordUndo = true);

    /** The auto-delete half of T148: after a mutation removes a connection that may have touched a
     *  macro port, call this on every node the mutation touched.
     *  GraphEditorCables.cpp/Commands.cpp/Selection.cpp call this directly. */
    void autoDeleteOrphanedMacroPort(juce::AudioProcessorGraph::NodeID nodeId);

    /** T154: the auto-delete-orphaned-port scan's candidate list for a BATCH node deletion.
     *  GraphEditorCommands.cpp/Selection.cpp call this directly. */
    std::vector<juce::AudioProcessorGraph::NodeID>
    macroPortDeletionNeighbors(const std::vector<juce::AudioProcessorGraph::NodeID>& deletedIds) const;

    // ---- General-purpose node-uuid plumbing --------------------------------------------------
    //
    // These two are general graph-uuid lookups, not macro-specific state — they happen to have
    // lived in the macro geometry file since before this split and are kept here rather than
    // invented a new home for. GraphEditorCables.cpp/Channels.cpp/Connections.cpp call both
    // directly.

    /** The persistent "uuid" node property for `nodeId`, or empty if none. */
    juce::String nodeUuidFor(juce::AudioProcessorGraph::NodeID nodeId) const;
    /** The NodeID currently backing `memberUuid`, or an invalid NodeID if none does. */
    juce::AudioProcessorGraph::NodeID resolveMemberNodeId(const juce::String& memberUuid) const;

    /** The rectangle to anchor a collapsed macro's boundary cables against.
     *  GraphEditorCables.cpp's rebuildVisibleCables() calls this directly. */
    juce::Rectangle<int> macroCableAnchorBounds(const synth::Macro& macro) const;

    /** True if at least one of `macroId`'s members has a "muted" parameter. PUBLIC (not
     *  internal-only, despite matching GraphEditor's original private visibility): C++ access
     *  control is per-class, so GraphEditorMacroPrompts.cpp's buildMacroMenu() — a GraphEditor
     *  member, i.e. a different class — reaches this from outside, and a private member here
     *  would refuse that call. */
    bool macroHasMuteEligibleMember(const juce::String& macroId) const;

private:
    GraphCanvasHost& host_;

    // ---- Internal-only helpers (no cross-file caller outside this class; original visibility
    // on GraphEditor was private and stays private here) ----
    void applyMacroCollapsed(const juce::String& macroId, bool collapsed);
    std::vector<juce::AudioProcessorGraph::NodeID> resolvedMacroMemberModuleNodes(const juce::String& macroId) const;
    static juce::String macroPortNodeTypeName(bool isInput, synth::MacroPortKind kind);
    static juce::String defaultMacroPortName(bool isInput, synth::MacroPortKind kind);
    static int nextMacroPortOrder(const synth::Macro& macro, bool isInput);
    static juce::String autoMacroPortName(ModuleBase* internalMb, bool isInput, int visibleJack, bool isMidi);
    juce::AudioProcessorGraph::NodeID mintMacroPortForAutoCreate(const juce::String& macroId, bool isInput, bool isMidi,
                                                                 juce::AudioProcessorGraph::NodeID internalNodeId,
                                                                 int internalVisibleJack);
};
