# Undo and redo glide

The rule, stated in [animation.md](animation.md#undo-and-redo-glide): **anything that glides forward glides on undo and
redo.** An undo that jumps looks like a different app from the edit it reverses. The two entry points are
`AppUndoManager::undo()` and `redo()` (the Cmd+Z / Cmd+Shift+Z path); a surface learns of a restore from
`isRestoring()` (true while it runs) and `getRestoreSerial()` (up by one after every finished step).

## Where it lives

- **Graph cards.** Both open a `CardGlideAnimator::Scope` around the restore. The scope captures every card's bounds
  before it, and after the restore's `updateComponents()` arms the usual slide from those rects to the restored ones,
  with the same geometry-final-at-once contract, 160 ms `easeOutCubic` and cable following as a forward move. A card
  matches its "before" by component, or by node id when the restore tore every card down and rebuilt it (any restore
  that frees a node does, e.g. the macro port a take-out created): so a module that moved between a macro and the root
  glides back and forth with its cables. A card for a node the restore creates grows in, and one it removes shrinks away
  ([delete and undo animation](animation.md#delete-and-undo-animation)); a project load never goes through `undo()` and so never glides
  (it comes to life instead: [project load reveal](animation.md#project-load-reveal)).
- **Edit Layout controls.** While the on-card layout editor is open, the card an undo or redo rebuilds gives the
  editor's controls the same treatment: `CardLayoutOnCardEditor` remembers each cell's rect before the re-sync and
  glides every control whose rect changed from there to the restored place (`startGlide`, 160 ms `easeOutCubic`, the
  geometry final at once). A control the restore brings back grows in (`fadeInControl`) and one it takes away shrinks
  out (`startShrinkGhostOf`, from a picture of the card taken at the previous re-sync). Nothing glides under Reduce
  Motion or off screen.
- **Macro borders.** `undo()`/`redo()` also snapshot the painted macro borders before the restore and hand them to
  `GraphEditor::glideHullsFrom` after it, so the border a take-out or join moved glides (220 ms, `MacroHullGlide`) back
  to where it was, like it glided out.
- **Timeline track rows.** `AppUndoManager::isRestoring()` is true during the restore. When the timeline panel rebuilds
  its header rows while it is set and the rebuild is a pure reorder (same tracks, new order),
  `TimelinePanelComponent::glideTrackRowsFrom` starts the rows at their old slots and `release()`s the shared
  `ReorderDragAnimator` to the new ones (140 ms settle, frames only while it runs). No row is lifted. A rebuild that
  drops or restores a track runs the delete and undo motion instead ([delete and undo animation](animation.md#delete-and-undo-animation));
  any other rebuild lands at once. A lane reorder within a track and a modulator row reorder glide back and forth the same way ([lane motion](../timeline/lane-motion.md#undo-and-redo-glide)).
- **An added or duplicated track's row** grows in through the delete motion's mirror ([delete and undo animation](animation.md#delete-and-undo-animation));
  undoing it shrinks the row away and the rows below close the gap.
- **Off-screen.** The timeline glide runs only while the panel is showing (`ReorderDragAnimator`'s `animate` flag), so
  headless tests land at once unless a test forces it.

## Points, dots and clips

One shared tween, `KeyedTween` (`Layout/KeyGlide/KeyedTween.h`: `KeyedPointTween` for points, `KeyedRectTween` for
rectangles), eases the DRAWN place of keyed things to where they now rest: the geometry is final at once, the drawn value
trails it by an offset that shrinks to zero over 160 ms (`easeOutCubic`), a retarget continues from the drawn place, and it
stops when settled. A surface that refreshes from its model asks a `RestoreWatch` (`Layout/KeyGlide/RestoreWatch.h`)
whether the refresh belongs to a restore (inside one, or the first after one finished) and, if so, retargets each moved key
from its old drawn place to the new one. An ordinary edit keeps landing at once.

- **Morph map.** `ShapeMapController::refresh()` and `syncCursor()` hand the restore flag to `MorphMap::setDots`/`setCursor`;
  a dot (keyed by index, same count) and the cursor glide in map units.
- **Curve editor.** `CurveEditorComponent::setModel(model, glideMoved)`: the same node count and mode glides each node dot;
  another shape lands at once. The shape window passes the flag when it reloads the selected curve after a restore.
- **Automation lane points.** `AutomationLaneEditor` matches old to new by order when a restore keeps the point count and
  changes beats or values; `LanePointGlide` still animates a pure add or remove.
- **Timeline clips.** `TimelineClipLaneArea` remembers each clip's start, length and row at every refresh; after a restore the
  clips that differ glide their rectangle (keyed by clip id), and the notes inside glide with it. It also remembers each clip's
  note ids and relative starts; `classifyClipNoteGlide` (`ClipEdgeSizing.cpp`, next to `clipEdgeResult`) compares the two syncs
  and `glidedNoteX` draws each note: a moved clip or a Carry edge drag **shifts** the notes with the clip's left edge, a Stretch
  drag **scales** each note from the old span to the new one through the gliding rectangle, and a Trim drag leaves the notes
  where they are so the edge slides over them. `UndoGlideSurfaces` registers a clip-contents surface for every sizing mode and
  both edges in which the notes change place, and a second test holds the rest still.

**Repainting.** `KeyedTween` repaints the WHOLE owner for every frame, retarget, clear, fade step and erase: the lane's
curve is drawn through the tweened points, so a box around the moving dot would leave the old dot and the segments to its
neighbours stale. An owner whose paint of a moving thing stays strictly inside that thing's box opts into the tight
repaint with `repaintOnlyAround(px)`; none does today (lane, curve editor, morph map and clip lane all take the default,
the clip lane because its selection outline, label and notes reach past the clip rectangle).

Under Reduce Motion a restored point, dot, cursor or clip is already at its new place on the first frame and fades in
there over `control_motion::kReducedMs` from low opacity (`KeyedTween::retarget(..., fadeWhenReduced = true)`, read with
`alphaFor`). Animations Off and an owner that is off screen land it plain (a test forces the tween with
`forceAnimateForTest`). The LFO card's Custom-wave editor takes the same path: its poll passes the restore flag to
`setModel`. Track header rows and automation lane rows land at once under Reduce Motion, with no fade.

## The checklist

`Tests/UI/Layout/UndoGlideSurfaces.h` lists every undoable move surface in one place (`surfaces()`): each entry builds the
surface, makes a move through its real edit path, calls the real `AppUndoManager::undo()` and `redo()`, and
`UndoGlideSurfaces.EveryMoveSurfaceGlidesOnUndoAndRedo` checks the moved thing is drawn between its two places on the first
frames and settles at the restored one. **A new move surface adds an entry there; that list is the checklist.**
`UndoGlideSurfaces.EveryGlideFrameRepaintsEverythingThatChanged` walks it too: each surface returns the component its
tween repaints (`paintedComponent()`; a surface drawn through a `KeyedTween` must return one), and after every 16 ms
glide frame of an undo and a redo (also under Reduce Motion) the test re-renders only the areas the tween asked to repaint
(`setKeyedTweenDirtyProbeForTest`) and compares with a full render.
The same list is walked under Reduce Motion (a fade for the point surfaces, an instant landing for the rows) and with Animations Off (instant).
