# Point editing: one key and mouse map for every editor with ordered dots

Every editor whose points sit in order along a line follows the same keys, so what works in one works in the others.
The keys act only while that editor has keyboard focus and are fixed (not rebindable actions), like Delete and Escape
on the piano roll. Shortcut table of the app-level commands: [`shortcuts.md`](shortcuts.md).

## Who follows it

| Editor | Where | Notes |
|---|---|---|
| Curve editor (`CurveEditorComponent`) | Envelope and LFO card curves, the Custom filter and LFO shape window | Free mode takes every key; a Fixed (envelope-shaped) curve has no insert, duplicate, paste or delete, and a pinned axis never moves. [`layout/visualizers.md`](../layout/visualizers.md) |
| Automation and modulation lanes (`AutomationLaneEditor`) | Under each track | Points have a beat and a value; insert needs a next point. [`timeline/automation.md`](../timeline/automation.md) |
| Mod curve editor (`ModCurveEditorView`, a `CurveEditorComponent` writing through its edit target) | The attenuverter's response curve | The curve editor's keys and mouse; a right-click on a point removes it at once, with no menu. |
| EQ curve (`EQCurveComponent`) | The Parametric EQ card | Points are the enabled bands (frequency along the line, gain up it); one band is selected, Delete switches it off and the selection stays on it. A band has no copy. |

**The exception: the morph map.** It is a 2D map with no left-to-right order, so it keeps its own keys: the arrows move
the focused dot (Shift: a bigger step; held, they glide on; two held at once move it diagonally), and Tab steps to the
next dot. Ctrl+T on a dot adds a shape
halfway between it and the next dot in the list; on the last dot, which has no next one, it adds a shape in the
emptiest spot, as the Add shape button does. Cmd+D on a dot duplicates its shape next to it. Each is one undo step and
the new dot is selected and focused; with the shape list full nothing is added and the window says so under the map
("No room for another shape"). Every other dot keeps its colour: a shape's colour belongs to the shape, not to its
place in the list. The same goes for a curve: a Ctrl+T it cannot act on says why ("Pick a point first", "No room after
this point", "No room: the curve has the most points it can hold") in the shape window. See
[`modules/custom-filter.md`](../modules/custom-filter.md).

## The keys

| Key | Effect |
|---|---|
| Left / Right | Pick the previous / next dot. The cursor and the selection move to it; nothing moves in time. With nothing picked, the first press lands on the nearest end |
| Shift+Left / Shift+Right | Extend the selection one dot toward that side (the cursor moves with it) |
| Alt+Left / Alt+Right | Move the selection in time (a grid step with snap on, else a small step: four pixels in a lane; on a grid of explicit lines, such as the Filter shape window's Notes or Harmonics, the step goes to the next line that snaps, past the dotted out-of-key notes). Hold the key and it glides, one undo step; with snap on a hold steps at most 12 grid steps a second and the value tooltip stays on the dot ([`timeline/automation-points.md`](../timeline/automation-points.md#keyboard-moves)) |
| Alt+Up / Alt+Down | Move the selection's value (holding glides, like Alt+Left / Right) |
| Two arrows held | Diagonal: hold Alt+Right, then also Up, and time and value move at once; let one go and the other carries on alone. The whole hold, both keys, is one undo step (`KeyHold` glides each axis on its own) |
| Alt+Shift+arrows | The fine move: a fifth of the step in the curve editor, a quarter in a lane, never snapped. In a lane with two or more points selected, Alt+Shift+arrows stretch or scale the selection instead ([`timeline/automation.md`](../timeline/automation-points.md#stretching-a-selection)) |
| Ctrl+Alt+Left / Right, Ctrl+Alt+Up / Down | Scale the selection about its middle, a tenth per press: Right / Up spreads the dots apart, Left / Down pulls them together (Right / Left in time, Up / Down in value; Shift: a fiftieth). Needs two or more dots. In time the order is kept: the block stops at an unselected neighbour (never pushing it), at the start of the lane or curve, and before two dots would meet; in value each dot stops at its own range edge. One undo step per press; a press that changes nothing opens none. The Mac chord is Control+Option (plain Control+arrows belong to Mission Control); a Fixed curve scales values only |
| Up / Down | Move the selection's value (the same step as Alt+Up / Down). A lane also takes Shift+Up / Down for a coarse step. With nothing selected the key is left to the app |
| Cmd+A | Select every dot that can move |
| Cmd+C / Cmd+V | Copy the selection; paste it at the cursor (the curve editor) or at the transport position (a lane) |
| Cmd+D | Duplicate the selection; the copies become the selection (on the morph map: the focused dot's shape). Where they start is the Timeline preference Duplicate points: right after the last dot (default, one tick past it), one dot-spacing after, or the next grid line after the selection ([`timeline/automation-points.md`](../timeline/automation-points.md#duplicating)) |
| Ctrl+T | Insert a new dot halfway between the cursor dot and the next one (value: the midpoint of the two); the new dot becomes the selection and the cursor. A curve's last dot has no room after it (the range ends there) and a lane's last point has no next one, so the key does nothing there |
| Delete / Backspace | Remove the selection; the point after it (else the last one left) becomes the selection, in a curve and in a lane (the EQ curve keeps the switched-off band selected) |
| Cmd+Z | Undo. Every press above that changes something is exactly one undo step; a press that changes nothing (a clamped end, a pinned axis, a full curve) opens none |

**Ctrl+T is the physical Control key.** On the Mac it is distinct from Cmd, so it never collides with the app's
Cmd+T (Add Track). Off the Mac Cmd is Ctrl, so the same flag stands for both and Ctrl+T inserts while the editor has
focus (Add Track is Ctrl+Alt+T there). The focused editor sees a key before the app's shortcut table does, so a rebound
app shortcut on Ctrl+T loses to the insert while an editor has focus and wins everywhere else.

The shared test of the map is one typed suite run against both editors through a small adapter per editor:
`Tests/UI/Layout/PointEditing/PointEditorSharedKeyTests.cpp` (the pure model's own tests sit beside it in
`PointEditingModelTests.cpp`). The mouse gestures have the same kind of guard, `PointEditorSharedMouseTests.cpp`: box
select (plain, additive, bare click, marquee painted, move after it) runs on both editors, so a gesture added to one
editor and not the other fails there.

## The shared model

Both editors run on one pure model in `Source/UI/Layout/PointEditing/` (no components, no doc), so a fix or a new key
lands in both at once:

| Part | File | What it owns |
|---|---|---|
| Keys | `PointEditorKeys.h` | `pointKeyAction` decodes a key press into one action of the map above (pick, extend, value, move, scale, select all, copy, paste, duplicate, insert, remove, clear); `isInsertPointKey` holds the Ctrl+T rule |
| Selection | `PointSelectionModel.{h,cpp}` | The selected set (ascending), the cursor and the anchor, keyed by node index in the curve editor and by beat in a lane, and what Left/Right, Shift+Left/Right and Cmd+A do to them |
| Box select | `PointBoxSelect.{h,cpp}` | The press-drag-release rectangle on empty space, the additive flag, a bare click, and the marquee paint |
| Scale | `PointScale.{h,cpp}` | The Ctrl+Alt+arrows maths: the step (`pointScaleFactor`), values scaled about the middle with a clamp per dot, positions scaled as one block that keeps its order and its neighbours (`scalePositionsAbout`). The curve editor's glue is `CurveEditorScale.cpp`, a lane's `AutomationLaneEditor::scaleKey` |
| Group move | `PointGroupMove.{h,cpp}` | How far each moving dot may go (`roomWithin`, `roomBetweenNeighbours`) and the one delta the block moves by (`clampGroupDelta`) |
| Clipboard | `PointClipboard.h` | The copy/paste/duplicate payload: one entry per dot as its offset from the earliest, its value and the editor's extra data (a curve bend, a lane point's tension and curve) |
| Duplicate placement | `PointEditorDuplicate.{h,cpp}` (one level up, in `Layout/`) | Where Cmd+D puts the copies (the Duplicate points preference) |

The editors keep only their glue: the curve editor in `CurveEditorSelection.cpp`, `CurveEditorKeyboard.cpp` and
`CurveEditorClipboard.cpp` and `CurveEditorBoxSelect.cpp`; a lane in `LanePointSelection` (the model plus its box-select glue and repaints) and
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

Outside the rules: a lane's block move stops only at beat 0 and clamps each value on its own; it passes other dots
freely and never deletes one: a move (mouse drag, held keys or nudge) that would land on another dot's beat settles one
tick (1/960 beat) beside that dot on the side it came from, so both stay (paste, duplicate and a typed value still
replace a dot on the same beat). The curve's stops at unmoving neighbours and clamps the level as a group; a lane leaves
Cmd+A/C/V/D to the app's commands and Escape and Delete only act without modifiers.

## Mouse

| Gesture | Effect |
|---|---|
| Click a dot | Select only it |
| Shift- or Cmd-click | Add or remove it from the selection without moving it |
| Drag a selected dot | Move the whole selection |
| Double-click empty line | Add a dot; double-click a dot in the curve editor removes it |
| Drag a box on empty space (lanes and curve editor) | Select the dots inside it, replacing the selection; the box is drawn in the accent colour while you drag |
| Shift-, Cmd- or Ctrl-drag a box | Add the dots inside it to the selection |
| Click empty space without dragging | Clear the selection |

Alt+arrows then move the boxed dots together and Cmd+Z undoes that move in one step. The box is a mouse shortcut for
what Shift+Left/Right already do from the keyboard (extend the selection one dot at a time), so nothing needs a
mouse. In a Fixed (envelope-shaped) curve only dots that can move are boxed; a pinned dot never joins the selection.
The morph map is out of scope: it keeps a single selected dot and has no box. The gesture is the shared
`PointBoxSelect` (`Source/UI/Layout/PointEditing/`): it owns the rectangle, the pending click and the marquee, and each
editor only answers which dots lie inside it (a lane by beat, the curve editor by node index at the drawn position).

Everything drawn around a dot (its selection ring or halo, the keyboard focus ring, a morph dot's focus stop, a lane's
selection box) follows the dot where it is drawn: under the pointer during a drag, at the preview during a key hold,
and along the glide of a step, undo or redo, never at the old place. A lane takes both from one place
(`AutomationLaneEditorDrawnPoints.cpp`); the guard is `PointMarkersFollowTheDot` (`Tests/UI/Layout/PointMarkers/`),
which records every marker painted and fails one left off the dot.

### Draw tools in the shape windows

The Filter's and the LFO's shape windows add a pen to the curve editor (the Draw button,
[custom-filter.md](../modules/custom-filter.md#shape-editor-window)). With a pen chosen, a press on the curve is a
stroke (`CurveDrawGesture`), not a point drag: the double-click add and the hover handles are off, and everything else
(arrows, Ctrl+T, Delete, Cmd+C/V/D) still edits points from the keyboard. Escape during a drag cancels it; Enter applies
the pen to the selected stretch.

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
6. Add it to the behaviour matrix below, or the discovery test fails by name.

## The behaviour matrix

`PointEditorBehaviourMatrixTests.cpp` (`Tests/UI/Layout/PointEditing/`) drives every point editor through the same
script with real key and mouse events: box select, Shift+Left/Right extend, Alt+arrows move (one undo step) and
Up/Down value, held-key glide, two held arrows moving diagonally, Cmd+D duplicate, Delete moving the selection to the
next point, double-click on empty space adding a point, right-click on a point opening a menu at it, and edge scroll
(editors that scroll). A cell is a pass, listed `NotApplicable` with its reason, or listed in `kMissingBehaviours`
with its ticket; that list only shrinks (a listed cell that now passes fails the test until it is removed). The
grid is printed on every run.

`PointEditorDiscoveryTests.cpp` finds the editors itself: every component class under `Source/UI` that derives from
`juce::Component` and uses the shared model (`PointSelectionModel`, `LanePointSelection`, `PointBoxSelect`,
`PointClipboard`, `pointKeyAction`), calls `addBreakpoint`, `addPointAt`, `dragBandTo` or `onAddAt`, or overrides
`mouseDrag` and hit-tests dots, nodes, bands or points, must be a matrix row or be named in `kNotPointEditors` with the
reason it edits no points.
