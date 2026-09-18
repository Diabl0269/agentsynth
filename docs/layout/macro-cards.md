# Macro Cards on the Canvas

`Source/MacroSet.h`, `Source/UI/MacroCardComponent.{h,cpp}`. How a macro looks and behaves on the
patch canvas. The Macro I/O port model — node types, port ordering, poly/stereo shape, cable
rendering across the boundary — is [macros_ports.md](../macros_ports.md); the container concept is
[macros.md](../macros.md).

## A macro is a persisted selection plus presentation

`synth::Macro` is an id, a name, a colour, a `collapsed` flag, a card `bounds` rectangle and a list
of member node uuids. Grouping modules into one adds no port, no edge and no processing — it is a
persisted selection layered on top of the multi-select system in [selection](selection.md).

**Membership is by node uuid**, the same persistent identity `ModuleBase::setNodeUuid` mirrors into
the processor, never by `NodeID` — a `NodeID` is valid only for one loaded graph and is meaningless
once serialised.

**The model is flat.** A node already in a macro cannot be grouped into a second one, and a macro
cannot contain another macro. `GraphEditor::groupSelectionIntoMacro()` enforces both halves by
refusing outright via `onStatusMessage`, rather than doing something ad hoc: fewer than two selected
nodes, or any selected node already belonging to a macro.

The right-click "Create Macro from N Modules" item — on `ModuleComponent`'s menu and
`GraphEditor::showCanvasContextMenu` — calls `GraphEditor::requestGroupSelectionIntoMacro()`, which
always means exactly that verb: it wraps `groupSelectionIntoMacro()` with the auto-port-preference
gate (see [macros_implementation.md](../macros_implementation.md)) before delegating.
`ungroupSelection()` (Cmd+Shift+G) dissolves every macro touched by the current selection, leaving
the member modules exactly where they are and expanding them back to individual cards; it is a no-op
(also surfaced via `onStatusMessage`) when the selection touches no macro. Deleting a macro is the
opposite of ungrouping it: `deleteMacroAndMembers()` removes the macro **and** every one of its
member nodes as one step — the right-click "Delete" on a collapsed card, or Delete/Backspace with a
macro's members as the whole selection.

**Cmd+G does not call `groupSelectionIntoMacro()` directly.** It goes through
`GraphEditor::groupOrToggleSelectionMacros()`, the single dispatch point shared by the command
handler: it groups the selection into a new macro only when the selection touches no macro at all
(via `requestGroupSelectionIntoMacro()`, same as the context-menu items); otherwise it toggles the
touched macro or macros collapsed/expanded (`toggleSelectionMacrosCollapsed()`, the same behaviour
as the standalone Cmd+Alt+G binding) and leaves any loose, ungrouped modules in the selection
untouched. A mixed selection — some selected nodes already in a macro, some not — takes the toggle
branch: the touched macros toggle, the loose modules are silently excluded from the gesture (not
folded into the macro, not refused), and a status message names how many macros toggled and that
modules outside a macro were left alone. That is why the nested-macro refusal above is reachable
only via a direct `groupSelectionIntoMacro()` call — the context-menu items, or Cmd+G on a selection
with no macro members yet.

## Collapse and expand

**Collapsing hides members, it does not destroy them.** `setMacroCollapsed` sets each member
`ModuleComponent` invisible and shows one `MacroCardComponent` in its place; the components stay
alive so their positions keep tracking a card drag, and expanding reverses the visibility flip.
`Macro::bounds` is authoritative only while collapsed — nothing reads it once expanded, since a card
is deliberately small and independent of how far-flung its members are.

**Collapsed-macro selection, drag and delete are deliberately not a parallel mechanism.** Selecting
a macro (`selectMacro`) populates the ordinary `SelectionModel` with its members, so
`beginSelectionDrag` / `dragSelectionBy` / `finalizeSelectionDrag` and `deleteSelection` all work
unchanged on a collapsed macro exactly as they do on any other multi-selection. Dragging the card
itself, through `MacroCardComponent`'s own `ComponentDragger`, reuses that same group-drag path —
the `beginMacroCardDrag` / `dragMacroCardBy` / `finalizeMacroCardDrag` / `cancelMacroCardDrag`
quartet is a thin wrapper resolving the members' rigid-body snap (see
[selection](selection.md#group-drag-resolves-as-one-rigid-body)) and the card's own position as one
undo step, rather than a second drag implementation living beside it.

**Collapse/expand is a toggle, not a collapse-only command.** Cmd+Alt+G
(`GraphEditor::toggleSelectionMacrosCollapsed`) gathers every macro owning at least one
currently-selected node: if any is expanded it collapses them ALL; otherwise, every touched macro
already being collapsed, it expands them all. A selection touching no macro is refused via
`onStatusMessage`, the same as `ungroupSelection`. One command covers both directions because the
label is a single static string, "Collapse / Expand Macro", that reads right whichever way the
toggle is about to go; the action id and keybinding are `"collapseMacro"` and Cmd+Alt+G. A member
module's own right-click menu offers the same toggle as a single item, labelled "Collapse Macro" or
"Expand Macro" to match the macro's current state.

## The expanded hull

**An expanded macro's hull is clickable, not just decorative.** `GraphEditor::macroHullBounds`
computes the same rectangle — the union of live member bounds, expanded by a fixed margin — that
`GraphContentComponent::paint` draws the dashed outline and name chip around. ONE definition, so
paint and hit-testing cannot drift. `macroHullAt(canvasPos)` returns the expanded macro whose hull
contains a point, the smallest one on overlap.

A left click landing in the hull's empty space — never on a member module's own card, which JUCE
routes to that `ModuleComponent` directly — selects the whole macro instead of clearing the
selection, hooked into the existing `pendingEmptyCanvasClick` deferral in `mouseUp` so panning is
unaffected. A right click there opens `buildMacroMenu`, the SAME menu builder the collapsed card's
right-click uses, after an explicit `selectMacro(id, false)` — `mouseUp` deliberately preserves
whatever was selected on a right-click, and the menu's Ungroup and Save-as-Snippet items act on the
*current selection*.

**The name chip is the expanded hull's drag handle.** `GraphEditor::macroChipBounds` computes the
chip rectangle the same way `macroHullBounds` computes the hull — the ONE definition
`GraphContentComponent::paint` draws from and `macroChipAt(canvasPos)` hit-tests against, both using
the same locally-constructed 11 pt bold font, so the drawn chip and the hit rect never diverge.

Pressing the chip (`mouseDown`, checked before the attenuverter, marquee and empty-canvas-click
logic) selects the macro and starts a group drag through the same `beginSelectionDrag` /
`dragSelectionBy` / `finalizeSelectionDrag` primitives a multi-select body drag uses, so the whole
macro moves as one rigid body and resolves through the same snap and de-overlap pass on release.
**The per-frame delta is computed in CANVAS space** (`content.getLocalPoint`), not `GraphEditor`
-local space, because the canvas carries a zoom transform — a raw screen-pixel delta would make the
macro drift at any zoom other than 1.0. A press that never moves cancels the drag
(`cancelSelectionDrag`) and pushes no undo entry, leaving the macro selected; an actual drag is one
undo step.

Hovering the chip shows `DraggingHandCursor`, tracked via a small `hoveringMacroChip` bool so
leaving the chip resets the cursor rather than sticking, and the chip paints three short grip lines
at its left edge so it reads as a handle rather than a plain label. Double-clicking the chip calls
`GraphEditor::promptRenameMacro` directly, mirroring the collapsed card's own double-click-to-rename.

The chip is a small, specific target sitting over empty canvas at the hull's top-left, which is
exactly why it can be a drag handle without stealing the pan gesture: a press anywhere else in the
hull's empty space, between or around member cards, still falls through to the ordinary pan and
empty-canvas-click path.

## The macro menu

`GraphEditor::buildMacroMenu(macroId, renameAction)` is the single builder behind both the collapsed
card's right-click menu and the expanded hull's: Expand/Collapse, Rename, Change Colour, Save as
Snippet, Ungroup, Delete Macro & Modules. Rename is the one item that varies by caller — the
collapsed card passes its own inline-`TextEditor` opener (`MacroCardComponent::beginRename`), and
every other caller falls back to `GraphEditor::promptRenameMacro`'s `juce::AlertWindow` dialog (the
same `SafePointer` plus `ModalCallbackFunction` plus callback-owned-`unique_ptr` idiom as
`MainComponent::promptSaveSnippet`), since there is no card to host an inline editor at a hull
click. Empty or whitespace-only dialog input cancels without renaming.

**"Change Colour..." opens the same shared picker as everywhere else.** The item calls
`GraphEditor::promptRecolourMacro(macroId, screenArea)`, which launches
`synth::ui::ColourPickerPopup` — the same `juce::ColourSelector` plus favourites-shelf popup the
timeline ruler's marker menu and the track header swatch use — rather than a hardcoded swatch list;
see [colour-overrides](colour-overrides.md#colour-picker-popup). The anchor rect is re-derived inside
the item's click handler, not captured at menu-build time, since the macro could collapse or expand
or its card or chip could move in between: the collapsed card's screen bounds, or the expanded
hull's chip bounds converted to screen space via `content.localAreaToGlobal`.

Preview and commit follow `TimelineRulerComponent::buildMarkerColourPicker`'s exact split.
`onPreview` writes `synth::Macro::colour` directly, with no undo, so a drag repaints live without
flooding the undo stack; `onCommit` either restores the captured original colour with no undo entry
(a no-net-change close) or restores the original first and then re-applies the final colour through
`setMacroColour` as the one recorded step, so a single undo restores the ORIGINAL colour rather than
the last preview value. Favourites persist via `GraphEditor::setPropertiesFile`, wired from
`MainComponent` to the same `juce::PropertiesFile` the ruler's marker picker uses; a `nullptr`
default keeps favourites in-memory only, which is what a headless test gets.

## The collapsed card

**Double-clicking renames or expands, depending on where.** A double-click on the title row calls
`MacroCardComponent::beginRename()` directly — the same inline `TextEditor` affordance
`ModuleComponent` gives its own title — rather than expanding; a double-click anywhere else on the
card expands. `getTitleRowBounds()` is the ONE rectangle both `paint()` (what is drawn) and
`mouseDoubleClick()` (what is clickable) use, so the two cannot drift apart, and it excludes the
top-right expand-chevron's hit zone.

Because `mouseDown` already arms a card body-drag (`dragStartPosition` / `bodyDragActive` /
`dragger.startDraggingComponent` / `GraphEditor::beginMacroCardDrag`) before a double-click's second
press resolves, opening the rename editor first cancels that armed drag
(`GraphEditor::cancelMacroCardDrag` plus clearing `bodyDragActive`) — otherwise the drag stays armed
on this now-live card and the next gesture anywhere moves the macro instead of whatever was actually
grabbed. The expanded hull's chip keeps using the modal `promptRenameMacro` dialog, since there is
no card there to host an inline editor.

**A collapsed card previews its contents.** Below the title and member-count text the card draws one
small filled rounded rect per member — the live union of member `ModuleComponent` bounds, scaled to
fit inside the existing 90 px card height. `kMacroCardHeight` never changes, because `Macro::bounds`
is persisted and a taller card would give already-saved macros a second size on the same canvas.
Each box is coloured by that member's module category (`GraphEditor::categoryPreviewColour`, the
same `themeColourForCategory` token the canvas uses elsewhere), so the preview echoes what expanding
would show. `MacroCardComponent` also implements `juce::TooltipClient`, returning a
newline-separated, capped list of member names, which `MainComponent`'s already-installed
`juce::TooltipWindow` shows on hover — so a collapsed macro's contents are discoverable without
expanding it.

## Cables re-anchor around a collapsed macro

A collapsed macro hides its members but not their graph edges, so `rebuildVisibleCables()` (see
[cables](cables.md)) runs a post-process pass right before it returns: a cable wholly inside one
collapsed macro is dropped from the visible list entirely, both endpoints being off-screen, and a
cable crossing a collapsed macro's boundary keeps its outside endpoint but re-anchors its inside
endpoint to the point where the card's edge faces the other endpoint (`projectToRectEdge`) rather
than pointing at a hidden jack.

**The rectangle projected against is `GraphEditor::macroCableAnchorBounds(macro)` — the LIVE
`MacroCardComponent`'s bounds while a card exists, not the persisted `macro.bounds`**, which
`finalizeMacroCardDrag` only writes back on drop; anchoring on the stale persisted value left a
boundary cable pointing at the card's pre-drag position for the whole drag gesture.
`GraphEditor::dragMacroCardBy` calls `repaintCanvas()`, invalidating the cable cache in the same
one-repaint-per-frame pattern `ModuleComponent`'s own body drag uses, so the re-anchored cable is
visible immediately rather than catching up on the next 30 Hz tick; `MacroCardComponent::mouseDrag`
does not call `getParentComponent()->repaint()` itself, since `dragMacroCardBy` is the one repaint
call for the gesture.

## Undo and persistence

A macro mutation that also changes the graph (delete) goes through
`AppUndoManager::recordGraphAndMacroChange`, which pushes a graph `SnapshotAction` and/or a
`MacroSnapshotAction` — only for the domain or domains that actually changed — inside one
transaction, so one `undo()` restores the nodes and the macro membership together. A macro-only
mutation (group, ungroup, rename, recolour, collapse) never touches the graph, so only the
`MacroSnapshotAction` is pushed. Both push onto the same `juce::UndoManager` as every graph and
timeline change, so Cmd+Z stays one chronological stack across all three domains. Like the graph's
own `SnapshotAction`, `MacroSnapshotAction` carries a `postRestore` hook calling
`GraphEditor::updateComponents()` after every undo and redo, so the canvas — cards, member
visibility — resyncs; a macro-set restore with no accompanying component sync leaves stale cards on
screen.

**Persistence.** `"macros"` is a reserved top-level `project.json` key, treated **exactly** like
`"timeline"`: detached before `AIStateMapper::validatePatch(trusted=false)` runs, which refuses a
payload carrying it (`PatchValidationError::MacrosNotAllowed`); validated separately and
all-or-nothing via `MacroSet::fromVar`; applied only after both that and the timeline validation
pass; and set **last** on save, so the live `MacroSet` — never a stashed value — is authoritative.
`MacroSet::retainOnly()` prunes member uuids that do not resolve to a live node after
`TimelineReconciler::reconcile` runs, dissolving any macro left with none — the same validate
strictly, apply faithfully trust-boundary shape [snippets-clipboard](snippets-clipboard.md) describes
for snippets, on the same reserved-key footing as the timeline.

**Snippets and the clipboard round-trip macro membership too.**
`SnippetManager::extractSnippet` takes the live `MacroSet` and captures a macro into the snippet's
own `"macros"` array — a *different* reserved key from the project-bundle one above, scoped to the
snippet file's own JSON dialect — only when **every** one of its members is inside the selection
being extracted, the same self-contained rule connections and modulations already follow. On the way
back in, snippet node ids are renumbered, so a captured macro's membership travels through
`AIStateMapper::applyJSONToGraph`'s `outIdMap` parameter (json id to live `NodeID`) and
`SnippetManager::insertSnippet`'s `outMacros` parameter resolves it to the pasted copies' fresh
uuids, ready to hand straight to `MacroSet::add()`. Cmd+C / Cmd+V / Cmd+D go through this same path,
being snippets that never reach disk.

**Configured I/O (`Macro::ports`) travels with the macro, not separately.** Each macro entry's
`"ports"` array is keyed by the same snippet node id as `"members"` — never by `nodeUuid`, since a
snippet renumbers ids on insert — and is built FROM the same resolved member map `outMacros`
populates, never independently: a port whose underlying node failed to resolve, or was outside the
selection (which cannot happen for a fully-contained macro but is guarded anyway), is dropped along
with it.

This is load-bearing, not defensive style: `MacroSet::fromVar` rejects the **whole** macro set if
any port's `nodeUuid` is not one of its own macro's `members`, so a mismatched pairing here would
not surface until the next project or snippet round-trip, as a silent "every macro vanished"
failure.

One thing does NOT travel through a `.agsnip` file: a port's channel shape (Mono/Stereo/Poly,
`MacroPortShape`) rides on the underlying `MacroInlet` / `MacroOutlet` node's `getExtraState()`,
which is trusted-path only (root `CLAUDE.md`). `includeExtraState=false` (Save as Snippet) resets it
to Mono on reload, the same way a Sampler's loaded file or a Wavetable's custom table is dropped
from a saved snippet. Copy, paste and duplicate pass `includeExtraState=true`, the payload never
leaving the process, and keep the shape, the same as they keep a Sampler's sample.
