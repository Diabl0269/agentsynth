# Selection and Group Drag

Multi-select on the patch canvas: which gesture selects what, the headless `SelectionModel` behind
it, and how a group of cards moves as one rigid body. What a selection can then be turned into is
[snippets-clipboard](snippets-clipboard.md) and [macro-cards](macro-cards.md).

## The gesture table

Drag on empty canvas already meant **pan**, and that predates selection. Rebinding it to
marquee-select (the Figma/Blender/VCV convention) would have retrained every existing pan habit, so
the marquee is gated behind **Shift** instead and pan is untouched.

| Gesture | Result |
|---|---|
| Drag on empty canvas | Pan — unchanged |
| **Shift** + drag on empty canvas | Marquee-select, *replacing* the selection |
| **Cmd/Ctrl + Shift** + drag | Marquee-select, *adding* to the selection |
| Click a module body | Select just that module |
| **Shift** + click a module | Toggle that module's membership (does **not** start a drag) |
| **Cmd** + click a module (no movement) | Toggle that module's membership, same as Shift |
| **Cmd** + drag a module across an expanded macro's hull | Joins or leaves that macro — [`macros_ports.md`](../macros_ports.md) has the full gesture and undo contract |
| Drag any selected module | Move the entire selection together |
| Click empty canvas (no drag) | Clear the selection |
| Right-click a module | Select it if it was not, then open the menu |
| Right-click empty canvas | Open the canvas menu (Paste Here / Select All / Locate Master — [shortcuts](../shortcuts.md)) — the selection is **kept**, so the menu can still act on it |

Two details that are easy to get wrong:

- **Deselect-on-click is deferred to mouse-up.** `GraphEditor::mouseDown` only *arms*
  `pendingEmptyCanvasClick`; the first `mouseDrag` clears it. If the clear happened on mouse-down,
  every pan would wipe the selection.
- **A Shift-modifier-click never arms the dragger; Cmd and Ctrl do.**
  `ModuleComponent::bodyDragActive` still gates `mouseDrag` / `mouseUp` for Shift, because
  `juce::ComponentDragger::dragComponent` must not run when `startDraggingComponent` was never
  called. Ctrl (insert-between, see [smart-connections](smart-connections.md)) and Cmd (macro
  reparent) each arm a DEFERRED click-versus-drag classification instead, resolved at `mouseUp` by
  whether the press moved.

## SelectionModel

`Source/UI/Graph/SelectionModel.h` is header-only with no `Component` or graph dependency, so the
selection rules are testable headlessly (`Tests/UI/Graph/SelectionModelTests.cpp`).

- `SelectionModel` wraps a `std::set<NodeID>`: `add` / `remove` / `toggle` / `setSelection` /
  `contains` / `retainOnly`. `NodeID{0}`, the graph's invalid-node sentinel, is rejected outright.
- `getSelected()` returns **ascending uid order**, never click order. Snippet extraction walks that
  order, and a snippet's node list must not depend on how the user happened to click.
- `retainOnly(alive)` is the staleness guard. `GraphEditor::pruneSelection()` calls it from
  `updateComponents()`, which every node-removing path (delete, undo/redo, preset load) already
  funnels through, so a selection can never name a freed node.
- `hitTestMarquee` uses **intersection, not containment** — clipping a module's edge selects it.
  Requiring full enclosure makes a 560 px `kDoubleWidth` Sequencer practically unselectable when
  zoomed out. A degenerate, zero-area band selects nothing, so Shift-click on empty canvas
  deselects rather than grabbing whatever is under the cursor.

The marquee band is stored and painted in **canvas coordinates**, in
`GraphContentComponent::paintOverChildren`, so it stays locked to the modules it is selecting while
zoomed or panned.

## Selection repaints stay bounded

`ModuleComponent` is buffered to an image, so repainting every card on every marquee frame would
re-rasterize the whole canvas — exactly what [rendering](rendering.md) forbids.
`GraphEditor::applySelectionChange()` diffs the old and new selection and repaints **only the cards
whose state flipped**. During a marquee drag that is zero repaints until the band actually crosses a
module boundary.

The selected treatment itself is not new drawing code: `AppLookAndFeel::drawModulePanel()` always
had a `selected` parameter (accent border plus themed glow) that was hard-coded to `false` pending a
selection model. `ModuleComponent::paint` passes `owner.isNodeSelected(nodeId)`.

## Group drag resolves as one rigid body

Followers are placed from **their own recorded origin plus the initiator's delta**
(`selectionDragStartPositions`), never by accumulating per-frame deltas — incremental application
drifts at non-1.0 zoom and shears the group apart.

`finalizeSelectionDrag()` then treats the selection as a single rigid body:

1. Union every member's bounds into one group box.
2. `LayoutUtil::snap()` its top-left.
3. `LayoutUtil::findFreeSlot()` for the whole box against **unselected modules only** — members are
   moving together and must not be obstacles to one another.
4. Apply that one offset to every member, and `updateModulePosition()` each, so positions reach the
   node properties and survive a save and reload.

Calling the single-module `finalizeModuleDrag()` per member instead would spiral them away from each
other and destroy the arrangement the user just made.

`cancelSelectionDrag()` exists for the zero-delta case, a click that never moved. Preset positions
are not necessarily grid-aligned, so running the finalize path on a drag that did not happen would
visibly nudge the group.

## Drag-in-progress flags and their reset sites

`GraphEditor` owns a small family of gesture-scoped flags — `dragPreviewActive` /
`dragPreviewGhost`, `marqueeActive` / `marqueeRect`, `selectionDragActive` /
`selectionDragStartPositions`, and `macroChipDragId` — each normally cleared by exactly one or two
functions (`endDragPreview`, `endMarquee`, `cancelSelectionDrag` / `finalizeSelectionDrag`), reached
from the real `mouseUp` of whichever component armed them (`ModuleComponent`, `MacroCardComponent`,
or `GraphEditor` itself).

**A `mouseUp` that never runs its matching cleanup leaves a ghost or marquee rectangle drawn
indefinitely.** Two ways that happens, and what closes each:

1. **A modal window swallows the `mouseUp`.** A double-click-to-rename on an expanded macro's chip,
   and on a collapsed macro's card, each opens a modal `AlertWindow`
   (`enterModalState(true, ...)`) whose global input grab swallows the drag's own `mouseUp`. Both
   `mouseDoubleClick` overrides cancel the armed drag unconditionally rather than depending on that
   `mouseUp` ever arriving.
2. **A component rebuild mid-gesture destroys the component that owns the live drag**, so its
   `mouseUp` can never arrive at all — not swallowed, simply undeliverable, since JUCE has nothing
   left to deliver it to. `ModuleComponent::mouseDown` arms `dragPreviewActive` /
   `selectionDragActive` and `MacroCardComponent::mouseDown` (`beginMacroCardDrag`) arms
   `selectionDragActive`, both resetting only from their own `mouseUp`. Two rebuild paths can
   destroy that component first: an async apply — for example an AI patch landing mid-gesture via
   `MainComponent::aiPatchAboutToApply` into `GraphEditor::detachAllModuleComponents()`, which
   deletes every `ModuleComponent` unconditionally — or the dragged node or macro itself being
   removed (undo, any other document mutation) and pruned by the next
   `GraphEditor::updateComponents()` / `syncMacroCards()` pass.

   `GraphEditor::cancelLiveDragGestures()`, which cancels `selectionDragActive` and
   `dragPreviewActive` together, is called from all three sites: unconditionally in
   `detachAllModuleComponents()`, where every component is going so cancelling is always correct;
   and, in `updateComponents()` / `syncMacroCards()`, only when the SPECIFIC component being removed
   is the drag's own initiator (`dragPreviewSelfId` for a module drag,
   `MacroCardComponent::isBodyDragActive()` for a card drag). A non-initiating group member
   vanishing on its own is harmless: the initiator survives, its real `mouseUp` is still coming, and
   `finalizeSelectionDrag`'s lookup simply skips a stale id it cannot find.

`marqueeActive` / `marqueeRect` and `macroChipDragId` need no such guard: both are armed and reset
entirely within `GraphEditor::mouseDown` / `mouseUp` itself, which is never one of the components a
rebuild destroys.

`Tests/UI/Graph/DragStateResetTests.cpp` drives every flag-arming real gesture — plain and
multi-select body drag, Ctrl-insert drag, marquee, macro chip drag, macro card drag, both
double-click-rename cases, and a release reported far outside the pressed component's own bounds —
through the actual `mouseDown` / `mouseDrag` / `mouseUp` callbacks and asserts every flag clears.
`Tests/UI/Graph/DragStateAsyncRebuildTests.cpp` covers the second case: it arms a drag through the
same real gesture path, then drives `detachAllModuleComponents()` / `updateComponents()` / a
member-removing `updateComponents()` in place of the `mouseUp` that a rebuild makes impossible, and
asserts the flags still end clear with nothing crashing.
