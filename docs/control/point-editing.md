# Point editing: one key and mouse map for every editor with ordered dots

Every editor whose points sit in order along a line follows the same keys, so what works in one works in the others.
The keys act only while that editor has keyboard focus and are fixed (not rebindable actions), like Delete and Escape
on the piano roll. Shortcut table of the app-level commands: [`shortcuts.md`](shortcuts.md).

## Who follows it

| Editor | Where | Notes |
|---|---|---|
| Curve editor (`CurveEditorComponent`) | Envelope and LFO card curves, the Custom filter and LFO shape window | Free mode takes every key; a Fixed (envelope-shaped) curve has no insert, duplicate, paste or delete, and a pinned axis never moves. [`layout/visualizers.md`](../layout/visualizers.md) |
| Automation and modulation lanes (`AutomationLaneEditor`) | Under each track | Points have a beat and a value; insert needs a next point. [`timeline/automation.md`](../timeline/automation.md) |

**The exception: the morph map.** It is a 2D map with no left-to-right order, so it keeps its own keys: the arrows move
the focused dot (Shift: a bigger step; held, they glide on; two held at once move it diagonally), and Tab steps to the
next dot. See
[`modules/custom-filter.md`](../modules/custom-filter.md).

## The keys

| Key | Effect |
|---|---|
| Left / Right | Pick the previous / next dot. The cursor and the selection move to it; nothing moves in time. With nothing picked, the first press lands on the nearest end |
| Shift+Left / Shift+Right | Extend the selection one dot toward that side (the cursor moves with it) |
| Alt+Left / Alt+Right | Move the selection in time (a grid step with snap on, else a small step: four pixels in a lane). Hold the key and it glides, one undo step ([`timeline/automation-points.md`](../timeline/automation-points.md#keyboard-moves)) |
| Alt+Up / Alt+Down | Move the selection's value (holding glides, like Alt+Left / Right) |
| Two arrows held | Diagonal: hold Alt+Right, then also Up, and time and value move at once; let one go and the other carries on alone. The whole hold, both keys, is one undo step (`KeyHold` glides each axis on its own) |
| Alt+Shift+arrows | The fine move: a fifth of the step in the curve editor, a quarter in a lane, never snapped. In a lane with two or more points selected, Alt+Shift+arrows stretch or scale the selection instead ([`timeline/automation.md`](../timeline/automation-points.md#stretching-a-selection)) |
| Up / Down | Move the selection's value (the same step as Alt+Up / Down). A lane also takes Shift+Up / Down for a coarse step. With nothing selected the key is left to the app |
| Cmd+A | Select every dot that can move |
| Cmd+C / Cmd+V | Copy the selection; paste it at the cursor (the curve editor) or at the transport position (a lane) |
| Cmd+D | Duplicate the selection; the copies become the selection. Where they start is the Timeline preference Duplicate points: right after the last dot (default, one tick past it), one dot-spacing after, or the next grid line after the selection ([`timeline/automation-points.md`](../timeline/automation-points.md#duplicating)) |
| Ctrl+T | Insert a new dot halfway between the cursor dot and the next one (value: the midpoint of the two); the new dot becomes the selection and the cursor. A curve's last dot has no room after it (the range ends there) and a lane's last point has no next one, so the key does nothing there |
| Delete / Backspace | Remove the selection |
| Cmd+Z | Undo. Every press above that changes something is exactly one undo step; a press that changes nothing (a clamped end, a pinned axis, a full curve) opens none |

**Ctrl+T is the physical Control key.** On the Mac it is distinct from Cmd, so it never collides with the app's
Cmd+T (Add Track). Off the Mac Cmd is Ctrl, so the same flag stands for both and Ctrl+T inserts while the editor has
focus (Add Track is Ctrl+Alt+T there). The focused editor sees a key before the app's shortcut table does, so a rebound
app shortcut on Ctrl+T loses to the insert while an editor has focus and wins everywhere else.

The shared test of the map is one typed suite run against both editors through a small adapter per editor:
`Tests/UI/Layout/PointEditing/PointEditorSharedKeyTests.cpp` (the pure model's own tests sit beside it in
`PointEditingModelTests.cpp`).

## The shared model

Both editors run on one pure model in `Source/UI/Layout/PointEditing/` (no components, no doc), so a fix or a new key
lands in both at once:

| Part | File | What it owns |
|---|---|---|
| Keys | `PointEditorKeys.h` | `pointKeyAction` decodes a key press into one action of the map above (pick, extend, value, move, select all, copy, paste, duplicate, insert, remove, clear); `isInsertPointKey` holds the Ctrl+T rule |
| Selection | `PointSelectionModel.{h,cpp}` | The selected set (ascending), the cursor and the anchor, keyed by node index in the curve editor and by beat in a lane, and what Left/Right, Shift+Left/Right and Cmd+A do to them |
| Group move | `PointGroupMove.{h,cpp}` | How far each moving dot may go (`roomWithin`, `roomBetweenNeighbours`) and the one delta the block moves by (`clampGroupDelta`) |
| Clipboard | `PointClipboard.h` | The copy/paste/duplicate payload: one entry per dot as its offset from the earliest, its value and the editor's extra data (a curve bend, a lane point's tension and curve) |
| Duplicate placement | `PointEditorDuplicate.{h,cpp}` (one level up, in `Layout/`) | Where Cmd+D puts the copies (the Duplicate points preference) |

The editors keep only their glue: the curve editor in `CurveEditorSelection.cpp`, `CurveEditorKeyboard.cpp` and
`CurveEditorClipboard.cpp`; a lane in `LanePointSelection` (the model plus the box-select gesture and repaints) and
`AutomationLaneEditorPoints.cpp`. The keyboard glide, the selection-box ease and the undo glides are unchanged on top.

**Adding a key once:** add its `Kind` and decoding to `pointKeyAction`, then a `case` in each editor's switch
(`CurveEditorComponent::keyPressed`, `AutomationLaneEditor::handleSelectionKey`), and a case to the shared suite.

### Where the editors differ

Each editor declares one `PointSelectionRules` constant (`CurveEditorComponent::kSelectionRules`,
`LanePointSelection::kRules`); unifying a row is changing one field and the tests that pin it.

| Rule | Curve editor | Lane |
|---|---|---|
| First Left/Right with no cursor | The first dot either way | The nearest end |
| Plain arrow at the last dot | Changes nothing | Collapses the selection onto the cursor |
| Shift+Left/Right | Selects the range from the anchor to the cursor, so reversing shrinks it | Adds the next dot past the selection's end, never shrinks |
| Cmd+A cursor | Kept if selected, else the last dot | Kept, else the first dot |

Outside the rules: a lane's block move stops only at beat 0 and clamps each value on its own (a move onto another
point's beat replaces it), the curve's stops at unmoving neighbours and clamps the level as a group; a lane leaves
Cmd+A/C/V/D to the app's commands and Escape and Delete only act without modifiers.

## Mouse

| Gesture | Effect |
|---|---|
| Click a dot | Select only it |
| Shift- or Cmd-click | Add or remove it from the selection without moving it |
| Drag a selected dot | Move the whole selection |
| Double-click empty line | Add a dot; double-click a dot in the curve editor removes it |
| Drag a box (lanes) | Select the dots inside it |

Everything drawn around a dot (its selection ring or halo, the keyboard focus ring, a morph dot's focus stop, a lane's
selection box) follows the dot where it is drawn: under the pointer during a drag, at the preview during a key hold,
and along the glide of a step, undo or redo, never at the old place. A lane takes both from one place
(`AutomationLaneEditorDrawnPoints.cpp`); the guard is `PointMarkersFollowTheDot` (`Tests/UI/Layout/PointMarkers/`),
which records every marker painted and fails one left off the dot.

## Adding a new point editor

1. Order the dots along one axis; if they have none (a 2D map), say so here and keep the arrows for moving.
2. Take the keys from the table above, nothing else for the same job: Left/Right pick, Alt+arrows move, Up/Down change
   the value, Ctrl+T inserts (decode with `pointKeyAction` in `UI/Layout/PointEditing/PointEditorKeys.h`, whose
   `isInsertPointKey` keeps the Mac and
   non-Mac Control rules in one place).
3. One press is one undo step; open no gesture for a press that changes nothing.
4. Make it a Tab stop with `paintFocusRing` (around the cursor dot where it is drawn), a `setTitle` name, a tooltip and a screen-reader description that list the
   keys ([`development/accessibility.md`](../development/accessibility.md)), and read the cursor dot aloud.
5. Run on the shared model (selection, group move, clipboard) with its own `PointSelectionRules`, add the editor to
   the tables above, an adapter to the shared suite's `PointEditorTypes` and an entry to `pointSurfaces()` in
   `PointMarkersFollowTheDotTests.cpp`.
