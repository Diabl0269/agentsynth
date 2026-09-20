// GraphEditorMacroApi.cpp
//
// GraphEditor's public/private macro API: out-of-line definitions for the one-line forwarders
// onto MacroGroupController (FRO77 PR2) that used to have their bodies inline in GraphEditor.h.
// Header hygiene only (FRO91 lever E) — GraphEditor.h is included by nearly every UI translation
// unit, so an inline body there forces every includer to recompile whenever the body changes;
// moving the body here (unchanged) means only this one file recompiles instead. Declarations,
// signatures and access levels stay on GraphEditor.h exactly as they were; each method's doc
// comment now lives here, next to its definition (FRO91 lever C — GraphEditor.h's own file-size
// cap). GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory
// hold the rest of the class.

#include "GraphEditor.h"

GraphEditor::MacroPortOwner GraphEditor::macroPortOwnerFor(juce::AudioProcessorGraph::NodeID nodeId) const {
    return macroController_.macroPortOwnerFor(nodeId);
}

// Wraps the current selection in a new macro (Cmd+G). See
// MacroGroupController::groupSelectionIntoMacro for the full refusal/auto-port contract.
juce::String GraphEditor::groupSelectionIntoMacro(bool autoCreatePorts) {
    return macroController_.groupSelectionIntoMacro(autoCreatePorts);
}

// Builds a new collapsed macro from an explicit member-uuid list (T173a's addAudioTrack).
// NON-RECORDING — see MacroGroupController::addMacroForMembers for the full contract.
juce::String GraphEditor::addMacroForMembers(const std::vector<juce::String>& memberUuids, const juce::String& name,
                                             juce::Point<int> origin) {
    return macroController_.addMacroForMembers(memberUuids, name, origin);
}

// True when grouping the CURRENT selection right now would cross at least one graph
// connection. See MacroGroupController::selectionHasCrossingMacroCable.
bool GraphEditor::selectionHasCrossingMacroCable() const { return macroController_.selectionHasCrossingMacroCable(); }

// Ungroups (Cmd+Shift+G). See MacroGroupController::ungroupSelection.
void GraphEditor::ungroupSelection() { macroController_.ungroupSelection(); }

// T138: adds every uuid in `memberUuids` to the EXISTING macro `macroId`. See
// MacroGroupController::addSelectionToMacro.
void GraphEditor::addSelectionToMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids) {
    macroController_.addSelectionToMacro(macroId, memberUuids);
}

// T138: removes every uuid in `memberUuids` from macro `macroId`. See
// MacroGroupController::removeSelectionFromMacro.
void GraphEditor::removeSelectionFromMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids) {
    macroController_.removeSelectionFromMacro(macroId, memberUuids);
}

// `nodeId`'s uuid, resolved to `removeSelectionFromMacro(macro->id, {uuid})`. See
// MacroGroupController::removeNodeFromMacro.
void GraphEditor::removeNodeFromMacro(juce::AudioProcessorGraph::NodeID nodeId) {
    macroController_.removeNodeFromMacro(nodeId);
}

// Toggles (Cmd+Alt+G) the collapsed state of every macro that owns at least one
// currently-selected node. See MacroGroupController::toggleSelectionMacrosCollapsed for the
// deterministic mixed-selection convergence rule.
void GraphEditor::toggleSelectionMacrosCollapsed() { macroController_.toggleSelectionMacrosCollapsed(); }

// Cmd+G's single entry point: toggles the macros the selection touches, or groups the
// selection into a new macro when it touches none. See
// MacroGroupController::groupOrToggleSelectionMacros.
void GraphEditor::groupOrToggleSelectionMacros() { macroController_.groupOrToggleSelectionMacros(); }

// Selects every member of `macroId`, replacing the current selection unless `additive`. See
// MacroGroupController::selectMacro.
void GraphEditor::selectMacro(const juce::String& macroId, bool additive) {
    macroController_.selectMacro(macroId, additive);
}

// True when every member of `macroId` is selected and nothing else is.
bool GraphEditor::isMacroSelected(const juce::String& macroId) const {
    return macroController_.isMacroSelected(macroId);
}

// The macro `nodeId` belongs to, or nullptr if it isn't a member of any.
const synth::Macro* GraphEditor::macroForNode(juce::AudioProcessorGraph::NodeID nodeId) const {
    return macroController_.macroForNode(nodeId);
}

// Expands or collapses a macro. See MacroGroupController::setMacroCollapsed.
void GraphEditor::setMacroCollapsed(const juce::String& macroId, bool collapsed) {
    macroController_.setMacroCollapsed(macroId, collapsed);
}

void GraphEditor::renameMacro(const juce::String& macroId, const juce::String& newName) {
    macroController_.renameMacro(macroId, newName);
}

void GraphEditor::setMacroColour(const juce::String& macroId, juce::Colour colour) {
    macroController_.setMacroColour(macroId, colour);
}

// Removes the macro AND every one of its member nodes, as one undo step. See
// MacroGroupController::deleteMacroAndMembers.
void GraphEditor::deleteMacroAndMembers(const juce::String& macroId) {
    macroController_.deleteMacroAndMembers(macroId);
}

GraphEditor::MacroToggleState GraphEditor::macroBypassState(const juce::String& macroId) const {
    return macroController_.macroBypassState(macroId);
}

GraphEditor::MacroToggleState GraphEditor::macroMuteState(const juce::String& macroId) const {
    return macroController_.macroMuteState(macroId);
}

void GraphEditor::setMacroBypassed(const juce::String& macroId, bool bypassed) {
    macroController_.setMacroBypassed(macroId, bypassed);
}

void GraphEditor::setMacroMuted(const juce::String& macroId, bool muted) {
    macroController_.setMacroMuted(macroId, muted);
}

void GraphEditor::toggleMacroBypassed(const juce::String& macroId) { macroController_.toggleMacroBypassed(macroId); }

void GraphEditor::toggleMacroMuted(const juce::String& macroId) { macroController_.toggleMacroMuted(macroId); }

// Canvas-space bounds of an EXPANDED macro's grouping hull. See
// MacroGroupController::macroHullBounds for the full port-exclusion/fallback contract.
juce::Rectangle<int> GraphEditor::macroHullBounds(const juce::String& macroId) const {
    return macroController_.macroHullBounds(macroId);
}

// The expanded macro whose hull contains `canvasPos`, or an empty string. Smallest hull
// wins when hulls overlap.
juce::String GraphEditor::macroHullAt(juce::Point<int> canvasPos) const {
    return macroController_.macroHullAt(canvasPos);
}

// Canvas-space bounds of an EXPANDED macro's name chip - the tab drawn on the hull's top
// edge, which doubles as the macro's drag handle. Empty rect if the macro is collapsed or
// unknown. The ONE definition: paint and hit-testing must never diverge.
juce::Rectangle<int> GraphEditor::macroChipBounds(const juce::String& macroId) const {
    return macroController_.macroChipBounds(macroId);
}

// The expanded macro whose name chip contains `canvasPos`, or an empty string. Smallest
// chip wins if two ever overlap.
juce::String GraphEditor::macroChipAt(juce::Point<int> canvasPos) const {
    return macroController_.macroChipAt(canvasPos);
}

// Canvas-space bounds of an EXPANDED macro's collapse button (founder-review fix G5). See
// MacroGroupController::macroCollapseButtonBounds.
juce::Rectangle<int> GraphEditor::macroCollapseButtonBounds(const juce::String& macroId) const {
    return macroController_.macroCollapseButtonBounds(macroId);
}

// The expanded macro whose collapse button contains `canvasPos`, or an empty string.
// Smallest button wins if two ever overlap (mirrors macroChipAt/macroHullAt).
juce::String GraphEditor::macroCollapseButtonAt(juce::Point<int> canvasPos) const {
    return macroController_.macroCollapseButtonAt(canvasPos);
}

// Test accessor: the live MacroCardComponent for `macroId`, or nullptr. Exposed so a test can
// move the real card directly (bypassing the drag gesture entirely) to prove
// rebuildVisibleCables() anchors on the card's CURRENT bounds rather than the persisted
// `macro.bounds`, which only updates on finalizeMacroCardDrag — see Fix 3/P8-12 follow-up.
MacroCardComponent* GraphEditor::getMacroCardForTest(const juce::String& macroId) {
    return macroController_.getMacroCardForTest(macroId);
}

std::vector<GraphEditor::MacroMemberPreview> GraphEditor::macroMemberPreviews(const juce::String& macroId) const {
    return macroController_.macroMemberPreviews(macroId);
}

// Display names of `macroId`'s MODULE members, in order — a port node is excluded
// (founder-review fix G6). Feeds MacroCardComponent's tooltip.
juce::StringArray GraphEditor::macroMemberNames(const juce::String& macroId) const {
    return macroController_.macroMemberNames(macroId);
}

// Adds a new port to `macroId`. See MacroGroupController::addMacroPort for the full
// construction-order/naming contract.
juce::String GraphEditor::addMacroPort(const juce::String& macroId, bool isInput, synth::MacroPortKind kind,
                                       MacroPortShape shape, int voiceCount, const juce::String& portName) {
    return macroController_.addMacroPort(macroId, isInput, kind, shape, voiceCount, portName);
}

// Removes the port fronted by `nodeUuid` from `macroId` by deleting its node (drops the
// cables). See MacroGroupController::removeMacroPort.
void GraphEditor::removeMacroPort(const juce::String& macroId, const juce::String& nodeUuid) {
    macroController_.removeMacroPort(macroId, nodeUuid);
}

// Deletes ONE macro port node directly while its macro stays alive, SPLICING the boundary
// cable back together (unlike removeMacroPort()). See MacroGroupController::deleteMacroPortNode.
void GraphEditor::deleteMacroPortNode(const juce::String& macroId, const juce::String& nodeUuid) {
    macroController_.deleteMacroPortNode(macroId, nodeUuid);
}

// Renames the port fronted by `nodeUuid`. Empty/whitespace-only `newName` is a no-op.
void GraphEditor::renameMacroPort(const juce::String& macroId, const juce::String& nodeUuid,
                                  const juce::String& newName) {
    macroController_.renameMacroPort(macroId, nodeUuid, newName);
}

// Moves the port fronted by `nodeUuid` one step earlier/later in its own direction's draw
// order. The keyboard-accessible fallback (T153) for reorderMacroPortToIndex below.
void GraphEditor::moveMacroPortOrder(const juce::String& macroId, const juce::String& nodeUuid, bool moveUp) {
    macroController_.moveMacroPortOrder(macroId, nodeUuid, moveUp);
}

// T152 drag-to-reorder: moves the port fronted by `nodeUuid` to `newIndexInGroup` (0-based,
// clamped) within its own direction group. See MacroGroupController::reorderMacroPortToIndex.
void GraphEditor::reorderMacroPortToIndex(const juce::String& macroId, const juce::String& nodeUuid,
                                          int newIndexInGroup) {
    macroController_.reorderMacroPortToIndex(macroId, nodeUuid, newIndexInGroup);
}

// Changes the shape of an existing audio/CV port as ONE edit (delete-node + create-node +
// rewire underneath). See MacroGroupController::changeMacroPortShape.
juce::String GraphEditor::changeMacroPortShape(const juce::String& macroId, const juce::String& nodeUuid,
                                               MacroPortShape newShape, int newVoiceCount) {
    return macroController_.changeMacroPortShape(macroId, nodeUuid, newShape, newVoiceCount);
}

// T152: sets (or, with nullopt, clears back to the kind-tint default) the user colour for the
// port fronted by `nodeUuid`.
void GraphEditor::changeMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                                        std::optional<juce::Colour> newColour) {
    macroController_.changeMacroPortColour(macroId, nodeUuid, newColour);

    // A commit is exactly when any armed live preview must be disarmed, so BOTH: repaint the just-stored
    // colour on the two surfaces through the shared finder (forced, so a NON-previewed commit - an AI or
    // programmatic colour - still shows it immediately: the repaint guarantee a preview alone never had),
    // then clear the armed preview (a real no-op when nothing was previewed, so a previewed-then-committed
    // port paints only that one new colour). Doing it here, the public commit boundary, means EVERY path
    // that commits a colour does both - not just the Configure I/O modal - so the jack shows the just-
    // stored value (which equals the armed preview) and never glitches on commit.
    repaintMacroPortColourTargets(macroId, nodeUuid);
    clearMacroPortColourPreview(macroId, nodeUuid);
}

// Every port on `macroId`'s collapsed card, laid out. Empty if `macroId` doesn't resolve or
// has no ports yet. See MacroGroupController::macroCardPortLayout.
std::vector<GraphEditor::MacroCardPort> GraphEditor::macroCardPortLayout(const juce::String& macroId) const {
    return macroController_.macroCardPortLayout(macroId);
}

// The port whose jack contains `cardLocalPos`, or nullopt. See
// MacroGroupController::macroCardPortForPoint.
std::optional<GraphEditor::MacroCardPort> GraphEditor::macroCardPortForPoint(const juce::String& macroId,
                                                                             juce::Point<int> cardLocalPos) const {
    return macroController_.macroCardPortForPoint(macroId, cardLocalPos);
}
