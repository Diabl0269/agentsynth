# Automation points: selecting, moving and stretching

How the points of an automation lane's curve are selected, moved from the mouse and the keyboard, duplicated and
stretched. The lane and its canvas are in [`automation.md`](automation.md#the-curve-canvas); the shared key map is
[`control/point-editing.md`](../control/point-editing.md).

## Selecting points

Points select the way clips and notes do (same modifiers, same Escape), through `LanePointSelection`
(`AutomationLanes/PointSelection/`, owned by the editor; `NoteSelectionModel`'s shape keyed on the point's
beat, since a lane has one point per beat). The selection is runtime view state, never saved. A SELECTED point is
a filled accent dot; unselected points keep the curve's colour.

| Gesture | Result |
|---|---|
| Click a point | Select just it; a click on an already selected point keeps the group so it can be dragged |
| Shift, Cmd or Ctrl + click a point | Toggle it in the selection (never starts a drag) |
| Drag a selected point | Move every selected point by the same beat delta (snapped like one point, the block stops with its first point at beat 0) and the same value delta (each value clamped on its own); one undo step |
| Plain drag on empty space | Box select, replacing the selection; Shift/Cmd/Ctrl + drag adds to it |
| Click empty space (no drag) | Clear the selection; double-click still adds a point |
| Escape | Clear the selection (the key is consumed only when something was selected) |
| Delete / Backspace | Remove the selected points, one undo step |
| Cmd+A, Cmd+C, Cmd+X, Cmd+V | The app's Select All / Copy / Cut / Paste commands, routed to the focused lane editor through `EditSurface::AutomationLane`; Duplicate and Repeat are inactive there |
| Left / Right | Pick the previous or next point; with nothing selected the first press selects the first or last point |
| Up / Down | Nudge every selected value by 1% of the lane's range (Shift: 10%) |
| Alt + Left / Right | Move the keyboard cursor to the previous or next point and select only it |

"On empty space" means away from every point, from the flat line of an empty lane and from the curve itself:
a press within a handle's reach of the curve still scrubs that segment's tension, so tension scrub and box select
do not overlap. A move, nudge, delete or paste is one `TimelineDoc::editBreakpoints` call under one
`recordTimelineChange` (`LanePointEdits.cpp`); the notification it fires prunes the selection to the points that
still exist, so the editor selects the moved or pasted beats after the edit returns. Paste lands at the transport
position, snapped, with the earliest copied point there and each value clamped to the lane's range; the pasted
points become the selection. The clipboard (`LanePointClipboard`) lives on `TimelineAutomationLanes`, so a copy
survives its editor being folded away and pastes into any lane. The selection survives a doc notification that keeps
the lane and the point, and clears when a point disappears or the editor is pointed at another lane
(`setActiveLane`, or the editor leaving the pool when its track folds).

Removal and return animate (`LanePointGlide`): when a doc change only removes points (a delete, a cut, a redo) they
shrink and fade out over 110 ms `easeInCubic` while the old curve cross-fades into the re-formed one; when it only
brings points back (an undo, a paste, an added point) they fade in over 160 ms `easeOutCubic` the same way. A move
is both a removal and an addition, so it lands at once, as does everything under Reduce Motion or off screen. The
editor follows the doc through `laneDocChanged()` (from `TimelineAutomationLanes::refreshPooled`) and lazily from
paint and the input entry points by `TimelineDoc::getRevision()`.

### Keyboard moves

A held Alt+Left/Right (time) or Up/Down and Alt+Up/Down (value) is a **keyboard drag**, not a repeat of the step. The press
steps once; if the key is physically down the hold then feeds the same preview state a mouse drag of the points uses
(`previewBeat_`, `previewValue_`, `dragMovedPoints()`), advanced every frame by `KeyGlide`
(`Layout/KeyGlide/`, the model the morph map's arrow hold shares: a cubic ease-in from a start speed to a cap, the tap
threshold and ramp length of the cursor glide, 150 ms and 2 s), frames from `KeyHold` on the frame pump. The doc is
not touched until the key goes up: release (or focus lost) commits ONCE through `commitPointMove`, so the tap and the
hold are one undo step and the committed points are exactly the last preview; Escape cancels with no edit. A press
whose key is not physically down (a synthesised event, an assistive tool) is one committed step, as before.

The hold reads all four arrows each frame and runs one glide per axis, so holding an arrow of the other axis as well
(Alt+Right, then Up) moves time and value together; that axis steps its tap at once and glides from its own start
speed, and letting either key go leaves the other moving alone. The hold, both keys included, ends and commits when
the last arrow goes up. Left/Right join a hold only with Alt down (a plain Left/Right picks a point). While a hold
runs, every arrow press is swallowed: the frames read the keys.

| | Tap | Hold starts at | Hold cap | Modifiers |
|---|---|---|---|---|
| Time | one grid step; four pixels at the current zoom with snap off | 1 beat/s | 16 beats/s | Alt+Shift (one point): a quarter |
| Value | 1% of the range | 0.15 of the range per second | 1.2 per second | Shift: a 10% tap, hold three times as fast; Alt+Shift: a quarter |

With snap off a hold is continuous. With snap on a time hold is the continuous distance cut into whole grid steps, so
the points jump from grid line to grid line; its start speed is one grid step per quarter second (a one-bar grid steps
about four times a second) and the cap grows by the same factor, but never below the free speeds above, so fine grids
are unchanged. The curve editor does the same with its grid cell. Alt+Shift with two or more points still stretches
([below](#stretching-a-selection)).

**Easing.** A discrete jump of the drawn points (the tap, a snapped step, a one-step key press) is final in the doc
or the preview at once; the drawn dots trail it by an offset that eases to zero over 160 ms `easeOutCubic`
(`KeyedPointTween`, keyed by the point's beat, an `AnimationDriver` that stops when settled). A new jump retargets
from where the dot is drawn, and the offsets carry across the commit at release. Under Reduce Motion or Animations
Off, or off screen, nothing eases (a hold still moves). The stretch box and the keyboard focus ring follow the drawn dots (during a drag, a hold and any glide, `AutomationLaneEditorDrawnPoints.cpp`), and so does the line (`AutomationLaneEditor::tweenedPoints` feeds the kernel the moved points, so it meets every dot). Undo and redo of a move use the same tween: when a restore keeps the point count and changes beats or values, `AutomationLaneEditor::glideRestoredPoints` matches old to new by order and glides each dot back ([undo-glide.md](../layout/undo-glide.md)); a pure add or remove stays `LanePointGlide`'s.

Each point is reachable from the keyboard, following the shared map in [`control/point-editing.md`](../control/point-editing.md): Tab into the editor, Left/Right walk the points
(nothing moves in time), Shift+Left/Right add the next point past the selection on that side (the cursor moves there),
Alt+Left/Right move the selection by a grid step (four pixels at the current zoom with snap off), Up/Down and Alt+Up/Down move its value by 1% of the range
(Shift+Up/Down: 10%), Alt+Shift+arrows move a lone point a quarter of that (two or more stretch instead, below), Ctrl+T
inserts a point halfway to the next one at the linear midpoint of the values (one `editBreakpoints`, one undo step, the
new point selected; nothing after the last point, so it does nothing there), Delete removes the one the cursor is on,
and Cmd+D copies the selected points as ONE undo step ([where the copies go](#duplicating) is a preference; the
copies become the selection; the clipboard is untouched, and with no point selected Cmd+D duplicates the lane). The focus ring (`paintFocusRing`) sits on the cursor point, or around the lane when there is
none. The editor's screen-reader description is rewritten on every selection change (point count, how many are
selected, and for a single point its bar and beat and its value in the parameter's own text; with two or more
selected it also names the stretch keys) and announced as a title change. The nudge and cursor keys are fixed, like Delete and Escape on the piano roll; Cmd+A/C/X/V are the
existing rebindable commands. Return stays free.

## Duplicating

Cmd+D copies the selected points as ONE undo step; the copies keep their spacing and become the selection. Where they
start is the Timeline preference **Duplicate points** (`synth::ui::duplicatePlacement()`, [`PointEditorDuplicate.h`](../../Source/UI/Layout/PointEditorDuplicate.h),
shared with the curve editor):

| Choice | First copy |
|---|---|
| Right after the last point (default) | One tick (1/960 beat, the timeline's finest resolution) past the last selected point |
| One point-spacing after | One gap past the last point, the gap being the one between the last two selected, so repeats run evenly |
| At the next grid line after the selection | The copies shifted by whole grid steps so the first copy sits at the first grid step past the last selected point; with snap off, as the default |

One selected point is copied one grid step on (a quarter of a beat with snap off) in every choice. A copy that lands on an
existing point's beat replaces it, as a move does.

## Stretching a selection

With two or more points selected under the Pointer tool, a thin accent box (`LanePointStretch`, padded 8 px so a handle
never sits on a point) surrounds them with a small square handle centred on each of its four edges; handles stay inside
the lane even when a point sits at its top or bottom. The box fades in over 160 ms and out over 110 ms on one
`AnimationDriver` (Reduce Motion or an off-screen editor lands at once; nothing repaints once settled).

**The box and moves.** The box is bounded by the points it frames, and it stays when the editor moves them: the doc
notification an own move, nudge, drop or key-hold release fires does not prune the selection (`OwnEditScope`), so the
box never fades out and back in around an edit. While a mouse drag or a key hold is under way the box is the bounds of
the PREVIEW points, with no lag. When the selection grows or shrinks, or a tap or an undo moves it, the box's
rectangle eases to its new one over 160 ms `easeOutCubic` (`LanePointStretch::retargetBox`, retargeting from where it
is drawn, plain under Reduce Motion, stopping when settled); Escape still fades it out.

| Gesture | Result |
|---|---|
| Drag the right handle | Scale the selected beats about the left edge (the leftmost selected beat stays), so four points spread out evenly when dragged right and squeeze when dragged left |
| Drag the left handle | The same about the right edge |
| Drag the top / bottom handle | Scale the selected VALUES about the box's opposite edge (lowest value for the top handle, highest for the bottom one), each clamped to the lane's range; dragging past the opposite edge flattens, never flips; a selection whose values are all equal has nothing to scale |
| Escape during the drag | Cancel: nothing changes and the editor consumes the key |
| Alt+Shift+Left / Right | Move the right edge one grid step (a sixteenth with snap off) left or right: squeeze or stretch, one undo step per press |
| Alt+Shift+Up / Down | Grow or shrink the selection's value range by 5% about its lowest value, one undo step per press |

The dragged EDGE snaps to the shared view-state snap (the others follow proportionally and are not snapped, so a
stretch keeps the points' relative spacing); no beat goes below 0, and the closest two selected points never come nearer
than 1/64 beat, so a squeeze cannot collapse points onto one beat. Growing is bounded and pushing: when the edge comes
within one grid step of the nearest unselected point beyond it, that point and every unselected point after it move
along, kept a grid step beyond the edge with their own spacing (a tiny gap with snap off); a left stretch stops when
the pushed block reaches beat 0, and squeezing never pushes. Unselected points between the selected ones stay put.

The drag previews in the editor's preview state like a move (curve and dots redraw from it; the doc is untouched) and
commits on mouse-up as ONE `editBreakpoints` through `commitPointEdit`, pushed points included, so one Cmd+Z restores
everything; a drag that ends where it began commits nothing. The selection afterwards is the same points at their new
beats. The value bubble is hidden for the whole stretch. A handle takes the press before any point, segment or box
select under it (reach 6 px). A press on a point inside the box selects and drags it as usual; a press anywhere else inside the box
(over the curve too) drags the whole selection like grabbing one of its points: the delta is the pointer's travel since
the press with the beat snapped at both ends, one `editBreakpoints`, one undo step, the same points stay selected. The
box is kept inside the lane, so a point at the far left or top does not cut an edge off. Handles show the horizontal or vertical resize cursor on hover and for the whole drag, and the editor's
tooltip reads "Drag to stretch the selected points in time" / "Drag to scale their values" over them. The math
(`stretchBeats`, `scaleValues`) is pure; the editor glue is `AutomationLaneEditorStretch.cpp`.

For later per-point features: `getPointSelection()` is the selection (`getSelected()` beats, `selectedPoints(lane)`
the breakpoints, `boundingBox(lane, mapper)` the box around them in editor coordinates, `getCursor()`), and the
editor's `onSelectionChanged` fires after the selection or cursor changed.
