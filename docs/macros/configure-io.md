# Configure I/O

The one dialog that edits a macro's ports by hand: `synth::ui::MacroPortConfigDialog`, opened by
`GraphEditor::promptConfigureMacroIO` from the macro menu
([`docs/macros/menu-and-membership.md`](menu-and-membership.md)). Adding, renaming, reordering,
recolouring and reshaping a port are all one dialog rather than separate menu actions. The port model
it edits is [`docs/macros/ports.md`](ports.md); the paths that create ports without it are
[`docs/macros/auto-ports.md`](auto-ports.md).

Launched through the async-modal idiom (`DialogWindow::LaunchOptions::launchAsync` plus a callback),
never a blocking modal loop. Rows group under **Inputs** and **Outputs** section headers; the dialog
sizes itself to its content, clamped, with the row list scrolling past the clamp.

---

## Adding a port

`GraphEditor::promptConfigureMacroIO`'s "Add a port" panel picks direction, kind and shape at
creation and writes the port's user-visible name. Mono, Stereo and Poly-N are all reachable here, and
the dialog is the only surface that reaches Stereo and Poly-N at all — every automatic path is Mono
only. A MIDI row hides its shape controls entirely rather than showing them disabled, because no
shape or poly concept applies to a MIDI port. The poly voice count is labelled and shown only for a
Poly shape.

`GraphEditor::createMacroPortFromDroppedCable` is the separate convenience path for a cable dropped
on a collapsed card's body; it is Mono only and wires the external side only
([`docs/macros/ports.md`](ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed)).

**Why the panel's height is computed from all three rows at once.** The panel has a label row, a
direction/kind/shape row and a name-plus-Add row; budgeting for only two of them squeezed the third
to zero height, so the name field and the Add button were simply absent from the render while the
combos above them painted fine. One `kAddBlockHeight` constant is used by both `resized()` and
`idealDialogHeight()`, so they can never drift apart again — the same "one function, never two" rule
`layOutOrMeasureRows` already documents for the row list.

## Renaming and reordering ports

`GraphEditor::renameMacroPort` and `GraphEditor::reorderMacroPortToIndex` back the row's name field
and its drag handle. A row's drag handle (left of the name field) reorders it within its own
direction group by an arbitrary number of slots in one gesture.

**Reordering is structurally scoped to one direction.** `onReorderPortTo`'s index carries no
`isInput` argument at all, which is what makes "an input cannot be dragged into the output section"
structural rather than a value the mutation has to reject. An arbitrary-distance move renumbers the
whole group's `order` fields sequentially afterwards — the only way to guarantee a gap-free order
after landing anywhere in the list, rather than one step over.

**Return commits a rename.** The row's name and voice-count fields commit on Return, and so do the
"Add a port" panel's own name and voice fields (`onReturnKey` triggers Add, matching a click).

## Changing a port shape

Changing an existing port's shape is reached from the same dialog
(`GraphEditor::changeMacroPortShape`). Per the immutability rule
([`docs/macros/ports.md`](ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed)) this is
delete-node plus create-node plus rewire underneath, landed as **ONE undo step** through
`AppUndoManager`'s `GraphAndMacroSnapshotAction` — see that class's own comment for why the combined
graph-and-macro restore has to be a single action rather than two pushed into one transaction, to get
both undo and redo right. **A saved cable on a raw channel the new shape no longer exposes is
dropped, not adapted**, applying `dropRoutingsOnHiddenJacks`' rule honestly.

Picking a new shape in the row's combo box, or typing a new poly voice count and pressing Return or
losing focus, commits immediately; there is no per-row "Apply Shape" button. Only the UI gesture
collapsed from two steps to one — the underlying delete, create and rewire is still exactly one undo
step.

## Per port colour

Each row's kind-tinted left-edge bar is a real clickable swatch (`synth::ui::ColourPickerPopup`, the
same favourites-shelf picker the timeline track header and the Appearance tab's note swatches use)
that sets `MacroPort::colour`. Left-click opens the picker and **commits once, when it closes**, not
per drag tick — a live preview repainted only the picker's own swatch, not the port's jack (T165, the
sections below, closes that: a re-colour now repaints the jack on *both* the collapsed card and the
docked widget in real time, and the picker previews it live during the drag). Right-click resets to the kind-tint default
(`onChangePortColour(nodeUuid, std::nullopt)`).

The colour flows through to `GraphEditor::macroCardPortLayout`'s `MacroCardPort::colour`, so a
coloured port's jack dot on the collapsed card matches the colour picked here, and
`ModuleComponent::resolveMacroPortJackColour` reads it for the expanded docked widget too, so an
expanded widget and its own collapsed card read a coloured port's jack identically. Unset falls back
to the same kind tint it always used. Persistence is
[`docs/macros/ports.md`](ports.md#port-set-and-ordering)'s optional `"colour"` field.

## Real-time re-colour and live preview (T165)

Each port's user colour is not only *painted* on the two surfaces; a port-colour change *repaints* them
live. Two-part fix, both T165: a real-time re-colour so a committed change repaints **both** surfaces,
and a live preview so the picker tracks the jack during the drag.

**Real-time re-colour.** Per-port colour made the collapsed card AND the expanded docked widget both
*paint* a port's user colour, but a port-colour change is a **macro-set** change, not a graph or
structural change, so neither surface has a listener that notices it on its own.
`GraphEditor::changeMacroPortColour` — a one-line forwarder onto `MacroGroupController::changeMacroPortColour`,
which records the macro-set mutation and repaints through the controller's `host_.requestRepaint()` — now
ends by calling **both** `GraphEditor::repaintMacroPortColourTargets(macroId, nodeUuid)` *and*
`clearMacroPortColourPreview(macroId, nodeUuid)`: the first repaints **both** surfaces in real time via the
shared `findMacroPortRecolourTargets` lookup (forced, so a NON-previewed commit — an AI or programmatic
colour — still shows the new value immediately, the repaint a preview never had); the second disarms any
armed preview (a *real* no-op — it repaints nothing — when the commit was not preceded by a preview). That
lookup is the single seam every colour path runs through.

- **Card.** Found via `getMacroCard(macroId)` (the non-test twin of `getMacroCardForTest`, used because
   this path now runs on a real user path: the live collapsed `MacroCardComponent`, correct whether the
   macro is folded or not).
- **Docked widget.** Found via `resolveMemberNodeId(nodeUuid)` + `moduleComponentFor(nodeId)` — the port
   node persists even while its macro is collapsed (its widget just goes hidden, same shape as its sibling
   members), so the widget is *always* found; a repaint of a hidden widget is a harmless no-op until the
   macro is expanded, at which point its first paint already reads the new colour. This is what made a
   port's jack "only show the new colour after a collapse/expand" — an expand re-runs the layout and forces
   a fresh paint (the old `repaintMacroPortColourTargets` was a `const` no-op that could not repaint
   anything).

The method returns the (possibly-null) `MacroPortRecolourTargets{card, widget}` it targeted, precisely
because a headless test cannot observe a `repaint()` (a no-op with no window; `StatusBarTests`'
gated-repaint precedent): the observable seam is "did the fix reach the two paint surfaces?", not "did a
frame paint?". No data change — this is a paint-timing fix on top of the per-port-colour work. Covered by
`MacroPortWidget.{ExpandsRecolourReachesBothTheCardAndTheDockedWidget, EveryPortKindReachesItsDockedWidget,
CollapsedRecolourStillTargetsTheCardAndTheHiddenWidget, MissingMacroIdReachesNoSurfacesAndDoesNotCrash,
ChangeMacroPortColourIsOneUndoStep}`.

**Live colour preview.** Real-time re-colour closed the first symptom — a *committed* re-colour now
repaints both surfaces. But the picker still only *committed* once, on close: while the user dragged the
selector the jack did not move. Per-port colour deliberately kept the picker "commit-once" because
`changeMacroPortColour` records an undo step, so an *every*-tick commit would be an every-step undo flood.
The fix keeps that property AND adds live feedback, exactly like the timeline track colour: the picker's
`onPreview` (fired on every selector tick / favourite click) now drives a **view-layer-only** preview, and
the single `onCommit` (on close) still writes the stored `MacroPort::colour`.

- **View-layer preview, not data.** A live preview lives only on the two paint surfaces: a single
   `std::optional<juce::Colour>` on `ModuleComponent` (one open picker previews one port) and a per-port
   `std::optional<std::pair<juce::String, juce::Colour>>` on `MacroCardComponent` (a card draws *every*
   port's jack at once, so the preview is keyed by the port's `nodeUuid`). Neither ever writes
   `MacroPort::colour`, so a drag pushes **no** undo step and dirties no data. Both
    `ModuleComponent::paintMacroPortWidget` and `MacroCardComponent::paint` now resolve the jack colour
     **preview-first** — routed through `ModuleComponent::effectiveMacroPortJackColour` /
     `MacroCardComponent::resolvePortJackColour` (the one function each paint site calls, so a headless
     test can assert the live resolution without capturing the pixel), and only then fall back to the
     stored colour, then the kind tint.
- **Wiring.** `MacroPortConfigDialog` gained an `onPreviewPortColour` callback (distinct from
   `onChangePortColour`); `buildColourPicker` fires it on every preview;
   `GraphEditor::promptConfigureMacroIO` routes it to
   `GraphEditor::previewMacroPortColour(macroId, nodeUuid, colour)`, which arms the preview on both
   surfaces and repaints them via the shared `findMacroPortRecolourTargets` — the very same lookup the
   commit path uses, so a preview and its commit can never target different surfaces.
- **The commit repaints THEN disarms.** `GraphEditor::changeMacroPortColour` now ends by calling BOTH
`repaintMacroPortColourTargets(macroId, nodeUuid)` (forces the two surfaces to the just-stored value — so a NON-previewed commit, e.g. an AI or programmatic colour, shows it immediately: the repaint a preview alone never had) and `clearMacroPortColourPreview(macroId, nodeUuid)` (a REAL no-op — it repaints nothing — when no preview was armed), so **every** path that commits a colour — not just the modal — shows it on both surfaces and disarms the armed preview in the same call. Because the committed colour equals the one that was armed, the jack shows it continuously and never glitches.
- Per-tick previews aren’t re-scanned: the picker — the modal — owns a frozen graph for its whole session and nothing mutates the macro set on its thread, so `previewMacroPortColour` resolves the two targets ONCE when the session first arms and reuses the cached pair on every subsequent tick.
- `setPortColourPreview` / `clearPortColourPreview` report whether a surface actually moved, so an idempotent re-press (and the popup’s `commitOnce()` re-firing the same colour just before the store) repaints nothing.
- **The teardown backstop.** A picker the user *abandons* without committing — a Close/Escape whose `CallOutBox` outlives the dialog’s own destruction (the `safeDialog`-vs-`safeRow` window its `onPreview` lambda names) — leaves an armed preview the commit path’s clear can never reach. `MacroPortConfigDialog::onRequestClose` now also calls `GraphEditor::cancelArmedMacroPortColourPreview()`, which disarms whatever the open session armed (by the node it cached, no node arg at teardown). A no-op when nothing was armed, so an unrelated Close costs zero repaints.
- **The docked-widget variant ("only updates after closing the modal").** The docked
   `ModuleComponent::paintMacroPortWidget` must paint through `effectiveMacroPortJackColour` (the
   preview-first path), NOT `resolveMacroPortJackColour` (the committed-only path) — the original symptom
   was that painting through the committed path made a docked widget ignore the armed preview and update
   its jack only once the pick committed on close. Both the MIDI and the CV jack resolve through
   `effectiveMacroPortJackColour`, so the docked widget tracks the open selector in real time just like
   the card. The test pin is `MacroPortWidget.DockedWidgetResolvesPreviewThenStoredThenKindTint`: a
   headless test cannot observe a repaint, so the guarantee is "the paint path resolves the preview", not
   "a frame painted".

Covered by `MacroPortWidget.{ExpandsRecolourReachesBothTheCardAndTheDockedWidget,
EveryPortKindReachesItsDockedWidget, CollapsedRecolourStillTargetsTheCardAndTheHiddenWidget,
MissingMacroIdReachesNoSurfacesAndDoesNotCrash, ChangeMacroPortColourIsOneUndoStep,
PreviewArmsBothSurfacesButWritesNoStoredColourAndNoUndo, DockedWidgetResolvesPreviewThenStoredThenKindTint,
CollapsedCardPreviewIsScopedToOnePort, PreviewThenCommitIsOneUndoStepAndShowsStoredColour,
ColourPickerFiresOnPreviewThenCommitsOnce, AbandonedPickerTearsDownTheArmedPreview,
CancelWithNoArmedPreviewIsNoOp}`. No data
or undo change here: the preview is view-layer only, and the commit is the same single
`MacroSnapshotAction` as before.

## Keyboard handling

Every real control gets Tab, Return and Space for free from `juce::Button`, `juce::ComboBox` and
`juce::TextEditor`, so only these gaps needed closing:

- **Escape closes the dialog** — wired as `MacroPortConfigDialog::keyPressed` (the bubble-up path,
  for whichever control does not consume the key) **plus** an explicit `onEscapeKey` on every
  `juce::TextEditor` in the dialog. A `TextEditor` consumes Escape itself before it would ever bubble
  (`TextEditorKeyMapper`/`consumeEscAndReturnKeys`), so relying on the bubble alone would silently do
  nothing while a name or voices field has focus. Escape follows the exact same path as clicking
  Close (`onRequestClose`), committing whatever edit currently has focus; it does not invent a
  separate discard semantic. `options.escapeKeyTriggersCloseButton = false` at every launch site
  makes the dialog's own override the ONE Escape route — `juce::DialogWindow`'s default (a `Button`
  shortcut on the native close button, a *different* dispatch path than the `keyPressed` bubble)
  would otherwise race it.
- **A rename typed right before Close must still apply.** `nameEditor.onFocusLost` was once the only
  path a rename reached `onRenamePort` through, and real `juce::TextEditor::focusLost()` posts an
  async command message rather than calling `onFocusLost` synchronously — Close's own `mouseDown`
  grabs keyboard focus away from the name editor, queuing that async commit, so tearing the dialog
  down straight afterwards meant the commit either never ran or ran after the dialog already looked
  closed. Every close path (the Close button, all four `onEscapeKey` sites, the `keyPressed` Escape
  bubble) now funnels through `MacroPortConfigDialog::requestClose()`, which **synchronously** calls
  each row's `maybeCommitName()` before firing `onRequestClose`. `maybeCommitName()` mirrors
  `maybeCommitShape()`'s no-op-on-unchanged-text guard (`committedName_`), so calling it
  unconditionally on every row on every close is safe.
- **Closing deliberately does NOT re-derive every row's shape.** `shapeBox` has no combo item of its
  own for `StereoCollapsed` (`comboIndexFromShape` maps it to the same id as `Stereo`), so
  re-deriving "the current shape" from the combo at close time would silently reclassify every
  untouched `StereoCollapsed` port as `Stereo` — a real delete-and-recreate — just from opening and
  closing the dialog. Voices only matter while a row shows Poly, never the lossy `StereoCollapsed`
  case, so `requestClose()` calls a narrower `maybeCommitVoicesOnClose()` that commits only when
  `voicesEditor.isVisible()`.
- **Arrow Up and Down move focus between rows** — wired on the row's colour swatch and Delete glyph
  button only (`GlyphButton`/`PortColourSwatch::onVerticalArrow` to
  `MacroPortConfigDialog::moveRowFocus`), never on the shape combo or a text field, which already have
  their own meaning for up and down that this must not shadow. Focus moves to the SAME control on the
  row immediately above or below, with no wraparound past either end, and crosses the input/output
  boundary freely — this is plain focus navigation, not a reorder.
- **Cmd+Up and Cmd+Down reorder a row.** `GlyphButton`/`PortColourSwatch::keyPressed` check the
  command modifier FIRST and fire `onReorderPort`, falling through to the bare-arrow row navigation
  above only when it is not held. This matches the app's existing convention of the command modifier
  for editing-type actions (Cmd+D duplicate, Cmd+R repeat —
  [`docs/control/shortcuts.md`](../control/shortcuts.md#surface-routing-who-cmdcvdxr-and-cmda-act-on))
  and is dialog-local key handling, not a `ShortcutManager`
  action.
- **Keyboard focus is visible.** `juce::Button::paint()` hands `paintButton()` only `isOver()` and
  `isDown()`, never keyboard-focus state, so a custom `paintButton` override draws nothing different
  when tabbed to. `GlyphButton` and `PortColourSwatch` both check `hasKeyboardFocus(true)` and draw
  an accent outline, reusing `AppLookAndFeel::drawTextEditorOutline`/`drawComboBox`'s own
  accent-when-focused convention. `AppLookAndFeel::drawButtonBackground` carries the same fix, so
  every plain `juce::TextButton` app-wide shows focus too. Every repaint here is the one JUCE's own
  `Button::focusGained`/`focusLost` already trigger — no new timer (`Source/UI/CLAUDE.md`).

## Deleting a port from the dialog

`GraphEditor::removeMacroPort()` reuses the ordinary multi-select delete path and **drops** the
cable rather than splicing it back, pinned by
`Tests/Macros/MacroPortFlow/MacroPortFlowEditTests.cpp`'s `RemoveDeletesTheNodeAndDropsThePort`.
This differs on purpose from every other delete path, which splices
([`docs/macros/auto-ports.md`](auto-ports.md#ungroup-and-direct-deletion-of-a-port)):
`spliceOutMacroPort` is shared code, not shared semantics, and an explicit delete from a
configuration dialog arguably should drop the cable rather than silently rewire the patch around it.

## Related

- [`docs/macros/ports.md`](ports.md) — the port model this dialog edits.
- [`docs/macros/auto-ports.md`](auto-ports.md) — the automatic creation and removal paths.
- [`docs/macros/menu-and-membership.md`](menu-and-membership.md) — how the dialog is reached.
- [`docs/layout/colour-overrides.md#colour-picker-popup`](../layout/colour-overrides.md#colour-picker-popup)
  — the picker's preview and commit split.
