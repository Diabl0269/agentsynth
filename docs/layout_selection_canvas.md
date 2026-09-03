# Layout: Selection & Canvas Interaction

This document covers multi-select/group-drag/snippets/clipboard, collapsible library sections,
cable interaction (hit-testing, hover, colouring), and the minimap overlay.

- [1. Multi-Select, Group Drag, Snippets & Clipboard (issue #156)](#1-multi-select-group-drag-snippets--clipboard-issue-156)
- [2. Collapsible Library Sections](#2-collapsible-library-sections)
- [3. Cable Interaction](#3-cable-interaction)
- [4. Minimap Overlay (issue #159)](#4-minimap-overlay-issue-159)

---

## 1. Multi-Select, Group Drag, Snippets & Clipboard (issue #156)

### 1.1 Why the gesture is modifier-gated

Drag on empty canvas already meant **pan**, and that predates selection. Rebinding it to
marquee-select (the Figma/Blender/VCV convention) would have retrained every existing pan habit, so
the marquee is gated behind **Shift** instead and pan is untouched.

| Gesture | Result |
|---|---|
| Drag on empty canvas | Pan — unchanged |
| **Shift** + drag on empty canvas | Marquee-select, *replacing* the selection |
| **Cmd/Ctrl + Shift** + drag | Marquee-select, *adding* to the selection |
| Click a module body | Select just that module |
| **Shift**/**Cmd** + click a module | Toggle that module's membership (does **not** start a drag) |
| Drag any selected module | Move the entire selection together |
| Click empty canvas (no drag) | Clear the selection |
| Right-click a module | Select it if it wasn't, then open the menu |
| Right-click empty canvas | Open the canvas menu (Paste Here / Select All) — the selection is **kept**, so the menu can still act on it |

Two details that are easy to get wrong:

- **Deselect-on-click is deferred to mouse-up.** `GraphEditor::mouseDown` only *arms*
  `pendingEmptyCanvasClick`; the first `mouseDrag` clears it. If the clear happened on mouse-down,
  every pan would wipe the selection.
- **A modifier-click never arms the dragger.** `ModuleComponent::bodyDragActive` gates
  `mouseDrag`/`mouseUp`, because `juce::ComponentDragger::dragComponent` must not run when
  `startDraggingComponent` was never called.

### 1.2 SelectionModel — `Source/UI/SelectionModel.h`

Header-only, no Component or graph dependency, so the selection rules are testable headlessly
(`Tests/SelectionModelTests.cpp`).

- `SelectionModel` wraps a `std::set<NodeID>`: `add` / `remove` / `toggle` / `setSelection` /
  `contains` / `retainOnly`. `NodeID{0}` (the graph's invalid-node sentinel) is rejected outright.
- `getSelected()` returns **ascending uid order**, never click order — snippet extraction walks that
  order, and a snippet's node list must not depend on how the user happened to click.
- `retainOnly(alive)` is the staleness guard. `GraphEditor::pruneSelection()` calls it from
  `updateComponents()`, which every node-removing path (delete, undo/redo, preset load) already
  funnels through, so a selection can never name a freed node.
- `hitTestMarquee` uses **intersection, not containment** — clipping a module's edge selects it.
  Requiring full enclosure makes a 560 px `kDoubleWidth` Sequencer practically unselectable when
  zoomed out. A degenerate (zero-area) band selects nothing, so Shift-click on empty canvas
  deselects rather than grabbing whatever is under the cursor.

The marquee band is stored and painted in **canvas coordinates** (in
`GraphContentComponent::paintOverChildren`), so it stays locked to the modules it is selecting while
zoomed or panned.

### 1.3 Selection repaints stay bounded

`ModuleComponent` is `setBufferedToImage(true)`, so repainting every card on every marquee frame
would re-rasterize the whole canvas — exactly what `docs/layout_visuals_animation.md` §2 forbids.
`GraphEditor::applySelectionChange()` diffs the old and new selection and repaints **only the cards
whose state flipped**. During a marquee drag that is zero repaints until the band actually crosses a
module boundary.

The selected treatment itself is not new drawing code: `AppLookAndFeel::drawModulePanel()` always had
a `selected` parameter (accent border + themed glow) that was hard-coded to `false` pending a
selection model. `ModuleComponent::paint` now passes `owner.isNodeSelected(nodeId)`.

### 1.4 Group drag resolves as one rigid body

Followers are placed from **their own recorded origin plus the initiator's delta**
(`selectionDragStartPositions`), never by accumulating per-frame deltas — incremental application
drifts at non-1.0 zoom and shears the group apart.

`finalizeSelectionDrag()` then treats the selection as a single rigid body:

1. Union every member's bounds into one group box.
2. `LayoutUtil::snap()` its top-left.
3. `LayoutUtil::findFreeSlot()` for the whole box against **unselected modules only** — members are
   moving together and must not be obstacles to one another.
4. Apply that one offset to every member, and `updateModulePosition()` each so positions reach the
   node properties and survive a save/reload.

Calling the single-module `finalizeModuleDrag()` per member instead would spiral them away from each
other and destroy the arrangement the user just made.

`cancelSelectionDrag()` exists for the zero-delta case (a click that never moved). Preset positions
are not necessarily grid-aligned, so running the finalize path on a drag that did not happen would
visibly nudge the group.

### 1.5 Snippets — `Source/SnippetManager.{h,cpp}`

A snippet is the **same JSON dialect as a preset** (`AIStateMapper::graphToJSON`) narrowed to a subset
of nodes. Stored one file per snippet as
`<userAppData>/<settingsFolder>/Snippets/<name>.agsnip`, mirroring
`ThemeManager::getUserThemesFolder()`.

Three rules define what a snippet contains:

| Rule | Reason |
|---|---|
| Only connections with **both** endpoints selected | A snippet must mean the same thing in every patch it lands in; it cannot assume the destination has the node on the other end. Wires to Audio Output are dropped — re-patch the output after dropping. |
| **No Attenuverter nodes**; modulation is stored in the `modulations` array | Same representation the AI patch format uses. `applyJSONToGraph` rebuilds the attenuverter chain on insert. |
| Positions normalised so the selection's top-left is `(0,0)` | `prepareForInsert()` re-offsets by the drop point, so internal layout is preserved wherever it lands. |

Graph I/O nodes (Audio In/Out, MIDI In) are never captured — they are singletons that already exist in
the target patch.

Extraction **filters `graphToJSON` output** rather than re-deriving the per-node JSON shape, so the
params encoding, MIDI port sentinel and attenuverter→modulations folding keep exactly one owner.

#### Id renumbering is mandatory

`prepareForInsert()` renumbers snippet ids to a fresh range starting at `nextFreeIdBase(graph)` (max
existing uid + 1). This is not cosmetic: `applyJSONToGraph` in **merge mode** treats an incoming node
id that already exists as *"update that node"*, so a colliding id would silently retune an existing
module of the same type instead of adding a copy.

#### Validate strictly, apply faithfully

`insertSnippet()` deliberately splits the two:

```cpp
validatePatch(prepared, graph, /*clearExisting=*/false, /*trusted=*/false);   // strict — reject whole
applyJSONToGraph(prepared, graph, /*clearExisting=*/false, /*trusted=*/true,  // faithful — no rescale
                 /*autoConnectNewNodes=*/false);                              // exact — no extra wires
```

- **Strict validation** because a snippet is a file on disk and can be hand-edited, truncated or
  copied in from elsewhere. A malformed snippet is rejected whole, never applied halfway. This *adds*
  a validation gate the trusted path would otherwise skip — it does not relax `validatePatch` (see the
  invariant in `CLAUDE.md`).
- **Trusted apply** because the untrusted apply path carries an AI-output heuristic: when a
  parameter's range extends beyond `[0,1]` but the incoming value sits inside it, the value is treated
  as normalised and rescaled. Snippet values come from `graphToJSON` already denormalised, so that
  heuristic would corrupt legitimate small values — an LFO `rateHz` of 0.5 Hz (range 0.01–20) would
  land at roughly 5 Hz. Guarded by
  `SnippetInsert.PreservesParameterValuesThatLookNormalised`.
- **`autoConnectNewNodes=false`**, and this one is easy to miss. Merge mode otherwise runs a
  convenience pass that wires every newly created audio node with no outgoing wire straight to
  Audio Output, and every new MIDI-accepting node to an existing MIDI source. That is right for an
  AI merge patch (a model that adds an Oscillator means it to be audible) and **wrong** for a
  snippet, where the absent wires are as deliberate as the present ones — without the opt-out,
  dropping a snippet into the patch it came from splices the copy's leaf modules into the live
  output. Guarded by `SnippetInsert.DoesNotSpliceTheInsertedGroupIntoTheSurroundingPatch` and
  `…DoesNotAttachInsertedModulesToAnExistingMidiSource`; the AI-side behaviour it preserves is
  locked by `AIStateMapperTest.MergeAutoConnectsNewAudioNodesToOutputByDefault`.

A snippet is therefore **exactly** its internal wiring: what you selected, nothing the surrounding
patch happened to be connected to, and nothing added for convenience on the way back in.

#### Wiring

`GraphEditor` owns no file dialogs and `ModuleLibraryComponent` owns no filesystem access;
`MainComponent` brokers between them via `GraphEditor::onSaveSnippetRequested` /
`GraphEditor::snippetProvider` and `ModuleLibraryComponent::onSnippetDeleteRequested`.

Snippet drags reuse the **existing** DragAndDrop channel between sidebar and canvas. The payload is
prefixed (`snippet:<name>`, `SnippetManager::kPayloadPrefix`) so `itemDropped` can tell a group drop
from a plain module drop. `itemDragEnter` sizes the landing ghost from the snippet's own bounding box
rather than the single-module estimate table.

Insert is one undoable change (`recordStructuralChange`), and the newly landed group is left
selected — it is what the user will want to move next.

### 1.6 Copy / Paste / Duplicate — `Source/UI/ModuleClipboard.h`

Cmd+C / Cmd+V / Cmd+D, plus the same three actions on the module and canvas context menus.

**They are snippets that never reach disk.** `copySelection()` calls the same
`SnippetManager::extractSnippet` the Save-as-Snippet path calls and parks the result in a
`ModuleClipboard`; paste and duplicate hand it to the same `insertSnippet`. Nothing about the wiring
rules is re-derived, which is the point — the three §1.5 rules give a paste its behaviour for free:

| Snippet rule | What it means for a paste |
|---|---|
| Only connections with **both** endpoints selected | The copies wire to **each other**, never back to the originals. A wire that ran into the group from outside is dropped rather than duplicated onto the copy. |
| Modulation stored as intent | An LFO→Filter routing between two copied modules comes back as a rebuilt attenuverter chain, not as a second wire onto the original attenuverter. |
| Origin-relative positions + fresh id range | The group keeps its internal layout, and `applyJSONToGraph`'s merge mode cannot mistake a copy's id for an existing node and silently retune it. |

`autoConnectNewNodes=false` carries over too, so a pasted module never wires itself to Audio Output.
Guarded by `ClipboardPaste.RewiresInternalConnectionsBetweenTheCopiesNotBackToTheOriginals`,
`…DropsConnectionsThatLeftTheCopiedSelection`, `…DoesNotSpliceTheCopiesIntoTheSurroundingPatch` and
`…RebuildsModulationChainsBetweenTheCopies`.

**Non-parameter state is the one difference from a saved snippet.** `extractSnippet` /
`prepareForInsert` / `insertSnippet` take an `includeExtraState` flag, **off by default**. A `.agsnip`
is a hand-editable file that inserts on the *trusted* apply path, and `applyExtraStateToProcessor`
reads `state` as a filename for `SamplerModule` — so a snippet file must not be able to carry one.
The clipboard has no such exposure: its payload comes straight from the live graph and never leaves
the process, so it opts in and a duplicated Sampler keeps its sample.
(`SnippetExtraState.*`, `ClipboardCopy.CarriesNonParameterModuleStateSoADuplicatedSamplerKeepsItsSample`.)

**Placement.** `SnippetManager::selectionOrigin()` returns the top-left corner extraction normalises
against — the clipboard's anchor. Both paste and duplicate offset from it by
`ModuleClipboard::kOffsetStep` (40 px = 5 grid units, so an offset copy stays on-grid):

- **Cmd+V** cascades: each successive paste steps one more offset down-right, so repeated pastes fan
  out instead of stacking on one pixel and looking like nothing happened.
- **"Paste Here"** (canvas right-click) drops at the click point and re-anchors the cascade there.
- **Cmd+D** offsets one step from the *current* selection and leaves the clipboard untouched —
  Cmd+D must not cost the user what they had copied. Since a duplicate leaves the copies selected,
  repeating it walks a chain across the canvas rather than piling up on one spot.

Neither runs the group through `findFreeSlot`: a fixed offset is predictable (the Figma/Illustrator
convention), the copy is visibly its own card, and it arrives selected and ready to drag.

Both are one undoable change for the whole group, on the same `recordStructuralChange` path as a
snippet drop.

**The clipboard is in-app and in-memory only.** It is deliberately not the system clipboard: Cmd+C on
the canvas must not silently destroy whatever text the user had copied, and text fields keep their
own copy/paste because JUCE's `TextEditor` consumes Cmd+C/Cmd+V before the key reaches
`MainComponent::keyPressed`.

### 1.7 Macros (P8-12) — `Source/MacroSet.h`, `Source/UI/MacroCardComponent.{h,cpp}`

A Macro is a named, coloured, collapsible **container**, not a graph feature: `synth::Macro` is
just an id, a name, a colour, a `collapsed` flag, a card `bounds` rectangle and a list of member
node uuids. Grouping modules into one adds no port, no edge and no processing — it is a persisted
selection plus presentation, layered on top of the multi-select system this section already
describes. Membership is by node **uuid**, the same persistent identity `ModuleBase::setNodeUuid`
mirrors into the processor, never by `NodeID` — a `NodeID` is only valid for one loaded graph and
is meaningless once serialised.

The model is deliberately **flat**: a node already in a macro cannot be grouped into a second one,
and a macro cannot contain another macro. This is a scope decision, not a missing feature — giving
a Macro its own ports (so it could itself be wired and nested like a sub-patch) is the separate,
later P8-15. `GraphEditor::groupSelectionIntoMacro()` (Cmd+G) enforces both halves of the contract
by refusing outright, via `onStatusMessage`, rather than doing something ad hoc: fewer than two
selected nodes, or any selected node already belonging to a macro. `ungroupSelection()`
(Cmd+Shift+G) dissolves every macro touched by the current selection, leaving the member modules
exactly where they are and expanding them back to individual cards; it is a no-op (also surfaced
via `onStatusMessage`) if the selection touches no macro. Deleting a macro is the opposite of
ungrouping it: `deleteMacroAndMembers()` removes the macro **and** every one of its member nodes,
as one step — the right-click "Delete" on a collapsed card, or Delete/Backspace with a macro's
members as the whole selection.

**Collapse/expand hides members, it doesn't destroy them.** Collapsing a macro
(`setMacroCollapsed`) sets each member `ModuleComponent` invisible and shows one
`MacroCardComponent` in its place; the components stay alive so their positions keep tracking a
card drag, and expanding just reverses the visibility flip. `Macro::bounds` is authoritative only
while collapsed — nothing reads it once expanded, since a card is deliberately small and
independent of how far-flung its members are.

Collapsed-macro selection, drag and delete are deliberately **not** a parallel mechanism:
selecting a macro (`selectMacro`) just populates the ordinary `SelectionModel` with its members, so
`beginSelectionDrag`/`dragSelectionBy`/`finalizeSelectionDrag` and `deleteSelection` all work
unchanged on a collapsed macro exactly as they do on any other multi-selection. Dragging the card
itself (`MacroCardComponent`'s own `ComponentDragger`) reuses that same group-drag path — the
`beginMacroCardDrag`/`dragMacroCardBy`/`finalizeMacroCardDrag`/`cancelMacroCardDrag` quartet is a
thin wrapper that resolves the members' rigid-body snap (§1.4) and the card's own position as one
undo step, rather than a second drag implementation living beside it.

**Collapse/expand is a toggle, not a collapse-only command.** Cmd+Alt+G
(`GraphEditor::toggleSelectionMacrosCollapsed`) gathers every macro that owns at least one
currently-selected node: if any of them is expanded, it collapses them ALL; otherwise (every
touched macro is already collapsed) it expands them all. A selection that touches no macro at all
is refused via `onStatusMessage`, same as `ungroupSelection`. The action id/keybinding
(`"collapseMacro"`, Cmd+Alt+G) is unchanged from when this shipped as a collapse-only command — one
command can cover both directions because the label is a single static string, "Collapse / Expand
Macro", that reads right regardless of which way the toggle is about to go. A member module's own
right-click menu offers the same toggle as a single item, labelled "Collapse Macro" or "Expand
Macro" to match the macro's current state.

**An expanded macro's hull is clickable, not just decorative.** `GraphEditor::macroHullBounds`
computes the same rectangle (the union of live member bounds, expanded by a fixed margin) that
`GraphContentComponent::paint` draws the dashed outline + name chip around — ONE definition, so
paint and hit-testing can't drift. `macroHullAt(canvasPos)` returns the (smallest, on overlap)
expanded macro whose hull contains a point. A left click that lands in the hull's empty space
(never on a member module's own card, which JUCE routes to that `ModuleComponent` directly)
selects the whole macro instead of clearing the selection — hooked into the existing
`pendingEmptyCanvasClick` deferral in `mouseUp` so panning is unaffected. A right click there opens
`buildMacroMenu` — the SAME menu builder the collapsed card's own right-click uses (see below) —
after an explicit `selectMacro(id, false)`, since `mouseUp` deliberately preserves whatever was
selected on a right-click and the menu's Ungroup/Save-as-Snippet items act on the *current
selection*.

**The name chip is the expanded hull's drag handle (P8-14).** `GraphEditor::macroChipBounds`
computes the chip rectangle the same way `macroHullBounds` computes the hull — the ONE definition
`GraphContentComponent::paint` draws from and `macroChipAt(canvasPos)` hit-tests against, both using
the same locally-constructed 11pt bold font so the drawn chip and the hit rect never diverge.
Pressing the chip (`mouseDown`, checked before the attenuverter/marquee/empty-canvas-click logic)
selects the macro and starts a group drag through the same `beginSelectionDrag`/`dragSelectionBy`/
`finalizeSelectionDrag` primitives a multi-select body-drag uses, so the whole macro moves as one
rigid body and resolves through the same snap/de-overlap pass on release. The per-frame delta is
computed in CANVAS space (`content.getLocalPoint`), not `GraphEditor`-local space, because the
canvas carries a zoom transform — a raw screen-pixel delta would make the macro drift at any zoom
other than 1.0. A press that never moves cancels the drag (`cancelSelectionDrag`) and pushes no undo
entry, leaving the macro selected; an actual drag is one undo step. Hovering the chip shows
`DraggingHandCursor` (tracked via a small `hoveringMacroChip` bool so leaving the chip resets the
cursor rather than sticking), and the chip paints three short grip lines at its left edge so it
reads as a handle rather than a plain label. Double-clicking the chip calls
`GraphEditor::promptRenameMacro` directly — the same dialog affordance the hull's right-click menu
uses — mirroring the collapsed card's own double-click-to-rename. The chip is a small, specific
target sitting over empty canvas at the hull's top-left, which is exactly why it can be a drag
handle without stealing the pan gesture: a press anywhere else in the hull's empty space (between or
around member cards) still falls through to the ordinary pan/empty-canvas-click path described
above.

**One shared macro menu, not two that can drift.** `GraphEditor::buildMacroMenu(macroId,
renameAction)` is the single builder behind the collapsed card's right-click menu and the expanded
hull's right-click menu: Expand/Collapse, Rename, Change Colour, Save as Snippet, Ungroup, Delete
Macro & Modules. Rename is the one item that varies by caller — the collapsed card passes its own
inline-`TextEditor` opener (`MacroCardComponent::beginRename`); every other caller falls back to
`GraphEditor::promptRenameMacro`'s `juce::AlertWindow` dialog (the same `SafePointer` +
`ModalCallbackFunction` + callback-owned-`unique_ptr` idiom as `MainComponent::promptSaveSnippet`),
since there is no card to host an inline editor at a hull click. Empty/whitespace-only dialog input
cancels without renaming.

**A collapsed card previews its contents.** Below the title/member-count text, the card draws one
small filled rounded rect per member — the live union of member `ModuleComponent` bounds scaled to
fit inside the existing 90px card height (`kMacroCardHeight` never changes; `Macro::bounds` is
persisted, so a taller card would give already-saved macros a second size on the same canvas), each
box coloured by that member's module category (`GraphEditor::categoryPreviewColour`, the same
`themeColourForCategory` token the canvas uses elsewhere) so the preview echoes what expanding
would show. `MacroCardComponent` also implements `juce::TooltipClient`, returning a
newline-separated (capped) list of member names — `MainComponent`'s already-installed
`juce::TooltipWindow` shows it on hover, so a collapsed macro's contents are discoverable without
expanding it.

**Cables re-anchor around a collapsed macro.** A collapsed macro hides its members but not their
graph edges, so `rebuildVisibleCables()` (§3) runs a P8-12 post-process pass right before it
returns: a cable wholly inside one collapsed macro is dropped from the visible list entirely (both
endpoints are off-screen), and a cable crossing a collapsed macro's boundary keeps its outside
endpoint but re-anchors its inside endpoint to the point where the card's edge faces the other
endpoint (`projectToRectEdge`) rather than pointing at a hidden jack. The rectangle projected
against is `GraphEditor::macroCableAnchorBounds(macro)` — the LIVE `MacroCardComponent`'s bounds
while a card exists, not the persisted `macro.bounds`, which `finalizeMacroCardDrag` only writes
back on drop; anchoring on the stale persisted value left a boundary cable pointing at the card's
pre-drag position for the whole drag gesture. `GraphEditor::dragMacroCardBy` calls
`repaintCanvas()` (invalidating the cable cache, same one repaint-per-frame pattern
`ModuleComponent`'s own body drag uses) so the re-anchored cable is visible immediately rather than
catching up on the next 30 Hz tick; `MacroCardComponent::mouseDrag` no longer calls
`getParentComponent()->repaint()` itself, since `dragMacroCardBy` is now the one repaint call for
the gesture.

**Undo.** A macro mutation that also changes the graph (delete) goes through
`AppUndoManager::recordGraphAndMacroChange`, which pushes a graph `SnapshotAction` and/or a
`MacroSnapshotAction` — only for the domain(s) that actually changed — inside one transaction, so
one `undo()` restores the nodes and the macro membership together. A macro-only mutation
(group/ungroup/rename/recolour/collapse) never touches the graph, so only the
`MacroSnapshotAction` is pushed. Both push onto the same `juce::UndoManager` as every graph and
timeline change, so Cmd+Z stays one chronological stack across all three domains. Like the graph's
own `SnapshotAction`, `MacroSnapshotAction` carries a `postRestore` hook that calls
`GraphEditor::updateComponents()` after every undo/redo, so the canvas — cards, member visibility —
resyncs; this was a bug fixed during review, since a macro-set restore with no accompanying
component sync left stale cards on screen.

**Persistence.** `"macros"` is a new reserved top-level `project.json` key, treated **exactly**
like `"timeline"` already is: detached before `AIStateMapper::validatePatch(trusted=false)` runs
(which refuses a payload carrying it — `PatchValidationError::MacrosNotAllowed`), validated
separately and all-or-nothing via `MacroSet::fromVar`, applied only after both that and the
timeline validation pass, and set **last** on save so the live `MacroSet` — never a stashed value
— is authoritative. `MacroSet::retainOnly()` prunes member uuids that don't resolve to a live node
after `TimelineReconciler::reconcile` runs, dissolving any macro left with none — the same
"validate strictly, apply faithfully" trust-boundary shape §1.5 already describes for snippets, on
the same reserved-key footing as the timeline.

**Snippets and clipboard round-trip macro membership too.** `SnippetManager::extractSnippet` takes
the live `MacroSet` and captures a macro into the snippet's own `"macros"` array — a *different*
reserved key from the project-bundle one above, scoped to the snippet file's own JSON dialect —
only when **every** one of its members is inside the selection being extracted, the same
self-contained rule connections and modulations already follow. On the way back in, snippet node
ids get renumbered (§1.5), so a captured macro's membership travels through
`AIStateMapper::applyJSONToGraph`'s `outIdMap` param (json-id → live `NodeID`) and
`SnippetManager::insertSnippet`'s `outMacros` param resolves it to the pasted copies' fresh uuids,
ready to hand straight to `MacroSet::add()`. Cmd+C/Cmd+V/Cmd+D go through this same path since they
are, per §1.6, snippets that never reach disk.

---

## 2. Collapsible Library Sections

With a Snippets section added on top of eight module categories, the sidebar overflows its height.
Every section header is now a disclosure toggle, plus a **COLLAPSE ALL / EXPAND ALL** strip in the top
24 px (`kTopStripHeight`).

- **One layout pass.** `buildRows()` returns `{entryIndex, y, height}` for the currently visible rows
  and is used by *both* `paint()` and `getEntryIndexAt()`. These previously duplicated the y-advance
  arithmetic in two places — a standing invitation for paint and hit-testing to disagree. Guarded by
  `ModuleLibraryStructure.HitTestingAgreesWithTheRowLayout`.
- **Rows carry a `RowKind`** (`Header` / `SubHeader` / `Module` / `Snippet` / `Plugin` / `Action` /
  `EmptyHint`) and their owning `section`, so collapsing is just "skip rows whose section is
  collapsed". Headers stay visible. The Plugins section sub-groups its rows by format (`VST3`,
  `AudioUnit`, …) behind a `SubHeader` row per format — sorted alphabetically by format,
  name-sorted within a group, and shown even for a single format — so a scan with more than one
  plugin format doesn't read as one undifferentiated list; collapsing the section hides its
  sub-labels along with everything else.
- **`SubHeader` rows are independently collapsible**, keyed as `<Section> :: <SubHeader>` composite
  strings via `ModuleLibraryComponent::subsectionKey(section, subHeader)` (a public static helper),
  kept separate from the section's own `Header` key so folding a format group never touches (or is
  touched by) the section header's fold. A `SubHeader`'s fold rides the same single
  `AnimationDriver` as header folds, and persists through the same opaque `collapsedSections`
  StringArray as header keys — no schema change. `setAllSectionsCollapsed()` (COLLAPSE ALL / EXPAND
  ALL) starts from the current `collapsedSections` set and only adds/removes top-level `Header`
  keys, so a subsection fold survives a Collapse All / Expand All untouched.
- **The Snippets section stays visible when empty**, showing a "No snippets yet" hint, so the feature
  is discoverable before the first snippet exists.
- **Chevrons are `juce::Path` triangles, not glyphs** — `▾`/`▸` coverage is not guaranteed across the
  embedded typefaces (see the font limitation in [`theming.md`](theming.md)). The triangle is drawn
  once (pointing down) and rotated by `-90° × progress`, so it turns with the fold; for a square box
  the two endpoints are exactly the shapes the old two-state version switched between.

### Fold animation

Collapsing and expanding tween over `kCollapseAnimMs` (150 ms, `easeInOutCubic`) via
`AnimationDriver` — no free-running repaints, per the animation invariant.

- **`sectionProgress` is purely visual** (0 = open .. 1 = shut). The logical state stays in
  `collapsedSections` and flips *instantly*, so `isSectionCollapsed()`, `areAllSectionsCollapsed()`,
  persistence and `onCollapseStateChanged` never lag a frame behind what the user clicked.
- **One driver for all sections**, so COLLAPSE ALL folds them together instead of racing nine
  animators. Retargeting mid-flight eases on from the current value rather than snapping back.
- **Rows are truncated, not squashed.** `buildRows()` gives each section a band of
  `naturalHeight × (1 - progress)`; rows keep their natural spacing inside it and are clipped at the
  band's bottom edge (`row.height < kItemHeight` marks a partly clipped row; rows past the band are
  dropped, so they stop hit-testing). `juce::Graphics::drawText` does not clip on its own, hence the
  explicit `reduceClipRegion` in `paint()`.
- **It snaps when not `isShowing()`** — there is no VBlank off screen, so a hidden component would
  otherwise freeze mid-fold. This is also what keeps the headless tests deterministic.
  `setCollapsedSections()` (the launch-time restore) always snaps: animating there would look like
  the sidebar folding itself up on startup.
- **Collapse state persists** as newline-joined section names under `libraryCollapsedSections` in
  `juce::ApplicationProperties`. `setCollapsedSections()` skips blank entries, because an unset
  preference arrives from `StringArray::fromLines("")` as a single empty string, and
  `onCollapseStateChanged` deliberately does *not* fire from it — that is the restore path, and
  re-notifying would write back what was just read.

### Scrolling

With every section expanded the rows exceed any realistic panel height, so the sidebar scrolls.

- **No `juce::Viewport`.** The library is a single painted component: rows come from one
  `buildRows()` pass, and it is also the tooltip client and the drag source. A viewport would split
  all three across an outer wrapper and an inner content component — and, because
  `findParentDragContainerFor()` walks to the *nearest* container ancestor, an inner component would
  bind drags to the sidebar instead of `MainComponent`, breaking drops onto the canvas. Instead a
  `juce::ScrollBar` drives a `scrollOffset` that `paint()` and hit-testing both apply.
- **The COLLAPSE ALL strip stays pinned** in the top `kTopStripHeight` px — the one control that
  shortens an overflowing list must never scroll out of reach. `paint()` therefore clips the rows to
  below the strip and `setOrigin(0, -scrollOffset)`s them, then draws the strip last over its own
  background fill.
- **Two coordinate spaces.** `buildRows()`, `getRowCentreY()` and `getEntryIndexAt()` are all
  *content*-space; mouse handlers go through `getEntryIndexAtComponentY()`, which rejects the pinned
  strip and then adds `scrollOffset`. Mixing them up is the failure mode this split exists to
  prevent — guarded by `ModuleLibraryScroll.HitTestingFollowsTheScrollOffset`.
- **`updateScrollBar()` runs after anything that changes content height** — resize, collapse,
  snippet refresh, theme change (the bar's width is the `kScrollbarWidth` token). It shows/hides the
  bar and re-clamps the offset, so a shrinking list can never leave the view scrolled past its end.
- **Rows lose the bar's width** (`getRowContentWidth()`) while it is visible, so row text and the
  snippet count never run under the thumb.

### Help popover

A small themed "?" button sits on the collapse-all strip, left of the COLLAPSE ALL / EXPAND ALL
label — `ModuleLibraryComponent::getHelpButtonBounds()` is the one rect `paint()` and the mouse
handlers (`mouseMove`/`mouseDown`/`mouseExit`) all read, so the drawn button and the clickable one
can never drift apart (the same "one enumeration" rule `buildRows()` follows for the rows
themselves). Its hover state (`helpButtonHovered`) is tracked independently of the rest of the
strip's own hover flag so the tooltip can name the button specifically ("Open a quick guide…")
rather than reusing the collapse-all strip's tooltip.

Clicking it shows `synth::ui::ModuleLibraryHelpPopup` (`Source/UI/ModuleLibraryHelpPopup.h`) — a
self-painted opaque panel, the same pattern `MidiDestinationPicker` documents (a parentless
`CallOutBox` does not necessarily inherit `synth::theme::AppLookAndFeel`, and neither does a
floating window — see "Pin / float it over the canvas" below). The popup holds three plain
disclosure sections — **Using modules**, **Your first patch**, and **Key shortcuts** — open by
default, each collapsible via its own header row (no accordion animation: a one-shot guide read
once and dismissed does not need `ModuleLibraryComponent`'s own animated fold). It scrolls via a
`juce::Viewport` when the expanded content overflows `kMaxHeight`, and collapsing a section
re-sizes the popup itself, the same "rebuild → resize" pattern `MidiDestinationPicker::refreshRows()`
uses.

Content is data first: `usingModulesLines()` / `firstPatchSteps()` / `shortcutLines()` are pure
static helpers, so a test can assert on the guide's text without ever constructing a
`juce::Component` — the same idiom `ModuleLibraryComponent::descriptionFor` already uses. The "Key
shortcuts" section resolves BOTH halves of each line live: the key via `shortcutHintFor` and the
label via `ShortcutManager::getActionDescription` (so it can never drift from the Settings tab's
own wording either). `ModuleLibraryComponent::setShortcutManager()` is how an owner wires the live
manager in — read-only, the sidebar never rebinds anything; unset (every headless test) falls back
to each curated shortcut's shipped default via `shortcutHintFor`'s own null-manager contract. The
"Your first patch" steps are the minimal audible patch verified against
`Source/PresetManager.cpp`'s Default preset and `VCAModule`'s own CV handling (see
[`modules.md`](modules.md#vca-amplifier-module)): Poly MIDI → Oscillator → VCA → Audio Output, with
an ADSR into the VCA's CV input — not optional shaping, since an unpatched VCA CV input reads as
silence rather than an implicit "fully open" value.

**Pin / float it over the canvas.** The popover's header carries a pin icon (default **off**) and a
close (X). Unpinned, it behaves exactly as above: `ModuleLibraryComponent` wraps it in a
`juce::CallOutBox`, dismiss-on-outside-click/Esc. Pinning it re-hosts the SAME popup instance as a
plain, non-modal child of an ancestor component instead — `addAndMakeVisible`, never a new desktop
window — so it stays up while the user builds the "Your first patch" chain on the canvas, closed
only by the header's X. This needed a real design decision, not just a flag: `juce::CallOutBox::
launchAsynchronously()`'s content is owned by an opaque, private `ModalComponentManager::Callback`
that deletes the box AND its content together the instant either is dismissed — there is no safe
way to detach a `launchAsynchronously`'d component and keep using it. So `ModuleLibraryComponent`
never calls it: it owns ONE persistent `ModuleLibraryHelpPopup` (`helpPopup_`, created lazily,
never rebuilt) and, when unpinned, wraps it in a *manually* constructed
`CallOutBox(Component&, area, parent)` — the plain, non-owning constructor — entering modal state
itself with `deleteWhenDismissed = false`. That reproduces the exact dismiss-on-outside-click/Esc
UX (`CallOutBox::inputAttemptWhenModal()` only ever calls `exitModalState()` + `setVisible(false)`,
never `delete`) while keeping full manual ownership, so a pin click can safely reparent the same
object into `floatingHelpHostFor()` — the root of whatever ancestor chain the sidebar is currently
in, walked generically rather than naming `MainComponent`, so the popup can float over the canvas
without this file depending on that class. Un-pinning reverses the transplant by constructing a
fresh callout around the same object; `juce::Component::addAndMakeVisible` auto-detaches a
component from wherever it was parented before, so neither direction needs an explicit
"remove from old parent" step.

A persistent instance being re-hosted (rather than rebuilt fresh per open, as it was before this
feature) has one consequence worth stating: the "Key shortcuts" text is no longer fixed at
construction. `refreshShortcutSection()` re-generates it against the live `ShortcutManager` and
`ModuleLibraryComponent::showHelpPopover()` calls it on every open — otherwise a rebind made in
Settings while the popup object is alive would freeze at whatever it said when the popup was first
built, staling exactly the thing "resolved live" was supposed to guarantee never happens. Dragging
the floating popup by its header ("if cheap") is a two-line `juce::ComponentDragger` use in the
header bar's `mouseDown`/`mouseDrag`, gated off entirely while unpinned (`setDraggable(pinned)`) so
it never fights a `CallOutBox`'s own self-repositioning. Pin state is session-only — it is not
persisted across app restarts, matching the pattern's own preference.

**The header always ships as non-scrolling chrome, and the position it lands at is always
clamped.** A real regression here is worth stating plainly: the first cut of pinning placed the
floating popup at "wherever the `CallOutBox` happened to be on screen", translated verbatim into
the floating host's local space with no bounds-checking. The help button anchor sits in the
sidebar's own topmost strip, only a few px below the very top of the window, so there is almost no
room above it — any small mismatch between the callout's screen position and the host's own (a
border inset, a display-scaling rounding, a host whose own top-left is not literally screen (0,0))
landed the popup with a slightly negative Y. Since the header is drawn at the popup's own local
`(kOuterPadding, kOuterPadding)` — i.e. right at its top — a negative Y pushes exactly the header
above the host's visible area while the scrollable body content, sitting lower in local space,
stays on screen: the reported symptom precisely (header and pin/title/X gone, content starting
mid-sentence, nothing left to click for move or close, only scroll). The header itself was never
the wrong thing — `TopBar` is and always was a plain sibling of `viewport_`, laid out first in
`resized()`, so it can never be *structurally* clipped into the scrollable area; it was the whole
popup's on-screen POSITION that was wrong, taking the header out of view along with it.

The fix is two static-ish helpers on `ModuleLibraryComponent`, used everywhere a floating position
is set: `defaultFloatingPosition()` computes a *sensible* spot — just right of the sidebar, level
with its top, clear of any toolbar above it — instead of trusting the callout's screen position at
all; `clampToHost()` then constrains that (or any other candidate position) so the popup's entire
rect stays inside the host's local bounds, falling back to the host's top-left corner (never
negative) when the popup is larger than the host in either axis. `ModuleLibraryComponent::resized()`
also calls the clamp (`reclampFloatingHelpPopover()`) whenever the sidebar itself reflows, since a
window resize is exactly the other way this class of bug could resurface.

**Pin affordance.** The pin icon now has a real hover state (an accent-tinted highlight behind the
glyph, mirroring the sidebar's own "?" button treatment) and a tooltip ("Pin - keep open while you
work") via `juce::SettableTooltipClient`, resolved dynamically per hovered icon exactly the way
`ModuleLibraryComponent::mouseMove` already resolves its own per-row tooltip. The close (X) gets the
same hover treatment and a plain "Close" tooltip. The header's title reads simply "Help" — short and
panel-like — rather than repeating "Module Library" a second time right next to the button that
opened it.

Opening the popover is split across TWO `protected virtual` leaves, both the same seam idiom
[§3's cable doc](#3-cable-interaction) and `TimelineRulerComponent::openMarkerContextMenu` use for
any real popup/menu window (a `juce::CallOutBox` launched in a display-less test runner is the
exact SIGSEGV trap documented in [`timeline_panel_core.md`](timeline_panel_core.md)):
`showHelpPopover()` is the pin-aware dispatcher (ensure the popup exists, refresh its shortcuts,
decide float-vs-callout), and `launchHelpCallOutBox()` is JUST the real `CallOutBox` construction —
split apart so a test can override only the second (never creating a real window) while the first
still runs its real logic for real, which is what lets pinning, closing, and the "survives an
outside click" guarantee all be exercised headlessly. `ModuleLibraryComponent::
createHelpPopupForTest()` is the separate seam for the popup's CONTENT — it returns the SAME
persistent object the real button shows (creating it on first call), never a fresh lookalike,
mirroring `PreferencesSettingsTab::createDualIOPerModuleDefaultsPopupForTest`.

### Keyboard Shortcuts settings tab mirrors this pattern

`Source/UI/ShortcutsSettingsTab.h/.cpp` — the Settings "Keyboard Shortcuts" tab — grew the same
collapsible-section idiom once its row count passed 49 (see [`shortcuts.md`](shortcuts.md)): one
collapsible section per `ShortcutCategory`, a search field above them, and a top strip whose label
flips between "COLLAPSE ALL"/"EXPAND ALL", deliberately lifted from `ModuleLibraryComponent` so the
app's two collapsible lists behave identically — clickable header rows with a chevron, a
collapsed-set keyed by the header's identity, the same strip idiom.

Two things it deliberately does NOT copy from the library sidebar, both because the two components
live in different contexts:

- **No fold animation.** The library's accordion is a VBlank-driven `AnimationDriver` over a
  hand-laid-out row list; here the rows are real child components inside a `juce::Viewport`, so
  animating a fold would mean animating child bounds every frame for no benefit inside a modal
  settings dialog. Collapsing is instant.
- **No persistence of the collapse state.** This tab is constructed fresh every time the Settings
  window opens and nothing has asked for the folds to survive that, so keeping the set in memory
  costs no new settings key to migrate (contrast the library sidebar's `libraryCollapsedSections`
  persistence above).

**Search matches BOTH the action's description and its current binding text** ("cmd" finds every
Cmd shortcut, "transpose" finds the piano-roll block — searching only the description would make
the list useless for the commonest question, "what is on Shift+Q?"). An active filter FORCES a
matching section open without touching its collapse flag (`sectionIsExpanded`), so a match can
never be trapped inside a fold, and clearing the query restores exactly the folds the user had; a
section with no surviving row is dropped entirely rather than left as a lone header over empty
space (`sectionIsVisible`) — the same rule `ModuleLibraryComponent::buildRows` applies.

**Row indexing is a contract, sections or no sections:** row `i` is always
`ShortcutManager::getActionIds()[i]` — the section headers are separate widgets, never entries in
the row vectors, and `ShortcutManager`'s action table keeps each category's ids CONTIGUOUS so a
section is always one unbroken run (`ShortcutsSettingsTabTests` pins row `i` to `ids[i]`). One
layout pass (`rebuildLayout()`) is what paints the rows, hit-tests the header clicks, and positions
the child bounds, so the three can never disagree about where a row is — the same "one enumeration"
rule `buildRows()`/`buildVisibleCables()` follow elsewhere in this doc. Group-separator hairlines
use `kDividerAlpha = 0.10` here — drawn from the text colour at low alpha rather than a theme token,
so they read on both light and dark themes with no token of their own — and `PreferencesSettingsTab`
keeps its own separate constant (`0.12`, softened from an earlier `0.18` that read as table borders
and boxed each preference in) in step with this one by comment rather than by a shared header, since
a one-line float is not worth a dependency between two settings tabs.

---

## 3. Cable Interaction

### Cables are not graph edges

A **cable** is one wire as the user sees it, which is not the same thing as a
`juce::AudioProcessorGraph::Connection`:

| Drawn as | Backed by |
|---|---|
| Audio / MIDI wire | one graph edge |
| Attenuverter chain | two edges plus a hidden `AttenuverterModule` node |
| Poly bus | `voiceCount` parallel edges |

Anything that identifies, hit-tests, colours or removes a cable therefore keys on the logical
view, not on the raw edge. `GraphEditor::CableId` is that identity
(`srcUid`/`srcPort`/`dstUid`/`dstPort` plus `attenUid`, non-zero only for an attenuverter chain).

### One enumeration for paint and mouse

`GraphEditor::buildVisibleCables()` returns every drawn cable, in paint order, as
`VisibleCable` records carrying geometry, signal kind, source category, activity and bypass
state. **Both** `GraphContentComponent::paint()` and hit-testing consume that one list — they
share the same literal `std::vector`, not two independently-built ones.

This is load-bearing: computing the drawn curve and the clickable curve separately means they
drift apart the first time either is tweaked, and clicks silently miss the wire. For the same
reason the bezier lives in exactly one place, `GraphEditor::buildCablePath()`, which must stay
identical to `AppLookAndFeel::drawConnectionWire`'s default curve. Cable geometry is canvas-space,
so zoom/pan alone can never move a cable — only a graph edit invalidates it.

`buildVisibleCables()` returns a **memoized `const&`**: it is rebuilt only when
`GraphEditor::repaintCanvas()` invalidates the memo (see `docs/layout_visuals_animation.md` §2), not on every call. The returned
reference must never be stored across a `repaintCanvas()`, a `timerCallback()` or a graph edit —
any of those can invalidate and rebuild the backing vector.

### Hit-testing

`GraphEditor::getCableAt(canvasPos, tolerance)` returns the topmost cable within `tolerance`
canvas px (default `kCableHitTolerance` = 7 px, wider than the wire so thin cables stay
grabbable). Distance is the perpendicular distance to the bezier, via `juce::Path::getNearestPoint`
— note that method *returns* the distance **along** the path and writes the nearest point out by
reference, so the perpendicular distance is `pos.getDistanceFrom(nearestPoint)`.

Ties go to the later cable, matching paint order (mod wires draw over audio wires).

### Hover

`mouseMove` resolves the cable under the cursor and stores only its `CableId`. The canvas
repaints **only when the hovered cable changes** — never on every mouse move. Hovered cables are
drawn brighter and one pixel wider by `AppLookAndFeel::drawConnectionWire`'s existing `hovered`
parameter, and the cursor becomes a pointing hand.

This does not violate the no-continuous-repaint invariant: `GraphEditor` already runs a 30 Hz
timer that calls `content.repaint()` for the wire-flow animation, so a hover change only marks
the next frame dirty rather than adding a new repaint source.

Only the `CableId` is retained between frames — geometry is rebuilt each paint anyway, and
holding a stale `VisibleCable` across a graph edit would dangle conceptually (ports move, nodes
disappear).

### Right-click menu

Right-clicking a cable opens a menu with **Disconnect Cable**. Before this, the only way to
remove a connection was to right-click one of its *ports*, which is not where users aim.

`GraphEditor::disconnectCable()` removes every graph edge behind the cable as **one** undoable
action — the whole attenuverter chain via `AudioEngine::removeModRouting()`, or all `voiceCount`
parallel edges of a poly bus. One visible wire, one undo step.

### Colouring

See [`docs/theming.md` §11](theming.md#11-cable-colours) for cable colour modes, the
`cableCategory` palette and the user override layer. `GraphEditor` renders whatever mode and
overrides it is handed via `setCableColourMode()` / `setCableColourOverrides()`; it never reads
`ApplicationProperties` itself.

---

## 4. Minimap Overlay (issue #159)

`Source/UI/MinimapComponent.h/.cpp` — `synth::ui::MinimapComponent`, a small always-current
overview of the graph.

### What it is

An untransformed sibling overlay on `GraphEditor`, the same pattern as `ModMatrixComponent` — it
does not live inside the panned/zoomed `GraphContentComponent`, so its own bounds are plain screen
space. Everything it draws is expressed in **canvas coordinates** (the space `ModuleComponent`s and
cables already live in) and mapped down to the small map area with `computeWorldToMap()`.

### Placement, sizing, auto-hide

Positioned **bottom-left** with a 12 px margin, sized `min(220, w/4) × min(150, h/4)`. Bottom-left
is deliberate: the mod-matrix panel occupies the right-hand 600 px (`docs/layout.md` §5). Below a 480×360 editor the
minimap auto-hides — `GraphEditor::resized()` recomputes `minimapVisible && fits` on every layout
pass against absolute floors (a fraction-of-self test like `w/4 * 2 <= w` is always true, so it
would never actually hide anything). This never clobbers the user's preference: `minimapVisible`
still records what they asked for, and reappears the moment the window grows back past the floor.

### What it draws

Renders from a `MinimapModel` snapshot — nodes, cables, and the current viewport rect, all in
canvas coordinates:

- **Nodes** — filled rounded rects in the module's per-category theme colour
  (`themeColourForCategory`), clamped to a `kMinNodeSize` (2 px) floor so zoomed-out modules stay
  visible; selected nodes get an additional `accent` stroke.
- **Cables** — thin straight lines in the cable's colour at reduced alpha, not the bezier the
  canvas draws — this is a thumbnail, not a second connection view.
- **Viewport** — the area outside the currently visible canvas rect is dimmed with a translucent
  `bg0` wash (an even-odd path fill punches a "hole" over the viewport rect rather than drawing
  four separate bars); the viewport itself is stroked in `accent`.

`computeWorldBounds()` derives what the map actually shows: the union of every node's bounds and
the viewport, inflated by an 80 px margin (`kWorldMargin`), and never narrower/shorter than 1200 px
(`kMinWorldSpan`) per axis, so a single module doesn't blow up to fill the map. All three drawing
and hit-testing helpers (`computeWorldBounds`, `computeWorldToMap`, `mapToWorld`) are pure static
functions with no `Component` state — unit-tested directly (`Tests/MinimapComponentTests.cpp`).

### Interaction

Click or drag on the map converts the event position to a canvas point (`mapToWorld`) and fires
`onNavigate`, which `GraphEditor` wires to `centreViewOn()` — pans so that point is centred; zoom is
unchanged. Scroll wheel fires `onZoom` with the wheel's `deltaY`, wired to `zoomAroundCentre()`.
Both the minimap's wheel-zoom and the canvas's own wheel-zoom (`GraphEditor::mouseWheelMove`) share
one private helper, `applyZoomAt(wheelDelta, screenAnchor)` — the canvas anchors on the mouse
position, the minimap anchors on the visible area's centre, but the zoom curve and the `[0.1, 2.0]`
clamp are identical.

Hovering the map shows a tooltip that leads with the show/hide shortcut, the same way the toolbar
buttons read (`"Hide Minimap  (Cmd+K)  - click or drag to navigate, scroll to zoom"`). The binding
is rebindable, so `MinimapComponent` does **not** depend on `ShortcutManager`: it exposes
`setShortcutHint(displayString)` and `MainComponent::applyToolbarIcons()` resolves the current
binding and pushes it down alongside the toolbar tooltips. Because tooltips embed the resolved
keypress, `MainComponent::updateCommandShortcuts()` (the `ShortcutManager::onBindingsChanged` hook)
re-runs `applyToolbarIcons()` — without that, every toolbar hint *and* the minimap's keeps
advertising the pre-rebind key until some unrelated toggle happens to refresh it.

### Repaint discipline

Follows `docs/layout_visuals_animation.md` §2. `setModel()` and `setViewport()` only `repaint()` when the incoming data actually
differs from the current model (`MinimapModel::operator==`, a field-wise comparison over nodes,
cables and viewport). `GraphEditor::timerCallback()` — the existing 30 Hz tick that already drives
the connection-flow animation — pushes a freshly built model via `buildMinimapModel()` **only while
the minimap is visible**, so a hidden minimap costs nothing: no graph walk, no model diffing.
`updateTransform()` (called on every pan/zoom frame) pushes **only the viewport rect** via
`setViewport()`, because panning/zooming changes what's visible, not where modules or cables are —
rebuilding the full model on every drag frame would re-walk every graph edge for nothing.

### GraphEditor API

| Member | Purpose |
|---|---|
| `setMinimapVisible(bool)` | Sets the user preference and re-runs `resized()`'s fits check; also seeds a full model immediately so the map isn't blank until the next 30 Hz tick |
| `toggleMinimapVisibility()` | `setMinimapVisible(!minimapVisible)` |
| `isMinimapVisible()` | The user preference (not the fits-adjusted effective visibility) |
| `getMinimap()` | Direct access to the `MinimapComponent` |
| `getVisibleCanvasRect()` | The canvas rect currently visible — inverse of the content transform applied to `getLocalBounds()` |
| `centreViewOn(canvasPoint)` | Pans so `canvasPoint` is centred; zoom unchanged |
| `zoomAroundCentre(wheelDelta)` | `applyZoomAt` anchored on the visible area's centre |
| `buildMinimapModel()` | Walks every module, and reuses the `buildVisibleCables()` memo (no separate enumeration walk) for the cable list, into a `MinimapModel` snapshot |

### Toolbar, shortcut, persistence

A toolbar toggle (`ToggleMinimap`, right-hand group, before `ToggleModMatrix`) and the **Cmd+K**
shortcut (action id `toggleMinimap`; see [`shortcuts.md`](shortcuts.md)) both call
`GraphEditor::toggleMinimapVisibility()`. Visibility persists under the `minimapVisible` key in
`juce::ApplicationProperties`, default `true`.
