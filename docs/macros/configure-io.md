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
per drag tick — a live preview only repaints the swatch. Right-click resets to the kind-tint default
(`onChangePortColour(nodeUuid, std::nullopt)`).

The colour flows through to `GraphEditor::macroCardPortLayout`'s `MacroCardPort::colour`, so a
coloured port's jack dot on the collapsed card matches the colour picked here, and
`ModuleComponent::resolveMacroPortJackColour` reads it for the expanded docked widget too, so an
expanded widget and its own collapsed card read a coloured port's jack identically. Unset falls back
to the same kind tint it always used. Persistence is
[`docs/macros/ports.md`](ports.md#port-set-and-ordering)'s optional `"colour"` field.

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
