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
- **A forward edit that adds or removes cards glides the same way.** The Poly click (the pill and the track header's
  switch) brackets its change with the same `Scope(restore=true)` and `retractCablesGoneSince(..., growAdded=true)`
  / `glideHullsFrom`, so the Poly MIDI card grows in and its cables grow out like Cmd+Z of it. The registry of such
  entry points is `structuralEdits()` in `Tests/UI/Layout/UndoGlideSurfacesTests.cpp`; a new structural click adds a row.
- **Edit Layout controls.** While the on-card layout editor is open, the card an undo or redo rebuilds gives the
  editor's controls the same treatment: `CardLayoutOnCardEditor` remembers each cell's rect before the re-sync and
  glides every control whose rect changed from there to the restored place (`startGlide`, 160 ms `easeOutCubic`, the
  geometry final at once). A control the restore brings back grows in (`fadeInControl`) and one it takes away shrinks
  out (`startShrinkGhostOf`, from a picture of the card taken at the previous re-sync). Nothing glides under Reduce
  Motion or off screen. The re-sync runs inside the restore (an `addRestoreSettledHook` the editor registers when it
  opens), so no frame shows the restored layout under outlines still at the old one. The card itself is exempt from the canvas's card slide (`CardGlideAnimator::setCardExempt`)
  while the editor is open: a restore that changes its height would otherwise hide it behind a snapshot drawn over the
  editor, showing the restored layout with no outlines. A second Cmd+Z or Cmd+Shift+Z
  mid-glide starts the new glide from where each control is drawn then (nothing is landed first), and a re-sync that
  arrives while a glide runs with no new rebuild is ignored (FRO721).
- **Macro folds.** A collapse or expand the step reverses folds the modules back ([macro fold motion](macro-fold-motion.md)).
  A second Cmd+Z or Cmd+Shift+Z while the first fold or glide runs starts the new fold from where every card is drawn
  then; the first fold keeps going ([interruption](macro-fold-motion.md)).
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
  a different count or mode (a drawn stretch undone or redone) morphs the whole line instead: the line as drawn before
  the restore and the new one are sampled at every x either has, and the drawn level eases from the old to the new over
  one `KeyedPointTween` key (`morphTween_`, so the glide time, Reduce Motion's land-and-fade-in and Animations Off are
  the shared ones); the fill follows the line and the dots and bend handles fade in as it lands. A restore during a
  morph starts from the line as drawn. The shape window and the card's curve view pass the flag when they reload the
  selected curve after a restore. A segment's bend glides too: `glideBendsFrom` keys each changed bend in `bendTween_` (a
  `KeyedPointTween`, the bend in x) and the line, fill and rings are drawn from `drawnModel()`, the model with the drawn
  bends. Guards: the `Curve editor drawn stretch` entry in `UndoGlideSurfaces` and `EveryUndoableCurvePropertyGlidesOnUndoAndRedo`.
- **Automation lane points.** `AutomationLaneEditor` matches old to new by order when a restore keeps the point count and
  changes beats or values; `LanePointGlide` still animates a pure add or remove.
- **Timeline clips.** `TimelineClipLaneArea` remembers each clip's start, length and row at every refresh; after a restore the
  clips that differ glide their rectangle (keyed by clip id), and the notes inside glide with it. It also remembers each clip's
  note ids and relative starts; `classifyClipNoteGlide` (`ClipEdgeSizing.cpp`, next to `clipEdgeResult`) compares the two syncs
  and `glidedNoteX` draws each note: a moved clip or a Carry edge drag **shifts** the notes with the clip's left edge, a Stretch
  drag **scales** each note from the old span to the new one through the gliding rectangle, and a Trim drag leaves the notes
  where they are so the edge slides over them. `UndoGlideSurfaces` registers a clip-contents surface for every sizing mode and
  both edges in which the notes change place, and a second test holds the rest still.
  A **snapped drag step** glides through the same tween: while an edge or body drag is live, each time the snapped preview
  jumps (a step of 1 px or more) `updateDragPreviewFromLastPointer` retargets `clipGlide_` from the clip's drawn place to the
  new line and sets the notes' `ClipNoteGlide` from the sizing mode, the preview staying final at once. The offsets carry
  across the drop. `UndoGlideSurfaces.EverySnappedDragStepEases` walks every surface that implements `Surface::dragStep`
  (the clip's left edge, right edge and body, and the notes of each sizing mode and edge): the first frame is at the old
  line, a later one between, it settles on the new line, and Reduce Motion lands it at once.
  A finished section edit and a repeat stamp glide clips too (a stamp only those in the placements it rewrote), and a
  clip that arrives on a known track grows from its left edge while one that leaves shrinks toward its centre where it
  was: `ClipLaneEnterExit` keeps a copy of every clip at each sync and draws the leaving ones with the real `paintClip`
  under a scale (180 ms; Reduce Motion an 80 ms fade; Off nothing), repainting through `repaintMotionArea` so the
  frame guard below sees those repaints too. Mechanism: [sections](../timeline/sections.md#motion).
- **Parametric EQ curves.** `EqBandGlide` (`UI/ModuleViews/EqBandGlide.h`) keeps the EQ's REAL band settings final at once and
  eases what is DRAWN. A band is two keys: place (log2 frequency, gain) and shape (Q, presence 0 to 1), plus one for the
  output trim, so frequency, gain, Q and on/off move in one 160 ms glide and the curve is re-summed from the drawn bands
  on every frame. An undo that switches a band off fades it out where it stood, and a redo that brings it back fades it
  in where it lands (it never flies to or from its hidden settings). A plain add or remove (a double-click, the + chip, a key) uses the
  same presence fade, so an added dot grows where it was parked and a removed one shrinks away where it stood; a changed type or
  slope runs a separate 220 ms morph (`Drawn::typeMorph`) that cross-fades the curve and the dot's kind colour. `EQCurveComponent` (the card's curve and the pop-out
  EQ window's, each with its own watch; `setUndoManager`) asks a `RestoreWatch` on EVERY 30 Hz tick, changed or not, so a
  restore that left this EQ alone cannot make a later drag glide; a drag, a key step or a CV change lands at once and
  ends any running glide. `MixerEqThumbnail` marks a restore when a parameter write arrives while
  `AppUndoManager::isRestoring()` (a flag instead of a watch, because its refresh is queued and unrelated restores never
  reach it). The card's knobs are plain `juce::Slider`s that take the restored values at once and repaint themselves; they do not glide.

**Repainting.** `KeyedTween` repaints the WHOLE owner for every frame, retarget, clear, fade step and erase: the lane's
curve is drawn through the tweened points, so a box around the moving dot would leave the old dot and the segments to its
neighbours stale. An owner whose paint of a moving thing stays strictly inside that thing's box opts into the tight
repaint with `repaintOnlyAround(px)`; none does today (lane, curve editor, morph map and clip lane all take the default,
the clip lane because its selection outline, label and notes reach past the clip rectangle).

Under Reduce Motion a restored point, dot (an EQ band's dot too), cursor or clip is already at its new place on the first frame and fades in
there over `control_motion::kReducedMs` from low opacity (`KeyedTween::retarget(..., fadeWhenReduced = true)`, read with
`alphaFor`). Animations Off and an owner that is off screen land it plain (a test forces the tween with
`forceAnimateForTest`). The LFO card's Custom-wave editor takes the same path: its poll passes the restore flag to
`setModel`. Track header rows and automation lane rows land at once under Reduce Motion, with no fade.
- **Sequencer pitch bars.** `StepGridComponent` glides its bars (160 ms `easeOutCubic`, a 4 ms stagger per column) to
  a pattern changed from outside a drag, and, since a scale change moves every played note without touching the pattern,
  to the notes a new Key or Scale plays: `syncSettings` starts the glide from the old scale on the card tick, so a Key
  pick, its undo and its redo all travel and the written-note outlines fade with them. Reduce Motion shortens it to an
  80 ms glide with no stagger (no fade: the bars still move); Animations Off lands at once. A second Cmd+Z or
  Cmd+Shift+Z mid-glide starts every bar from where it is drawn then (`captureDrawnAsGlideStart`). Guard: the `Sequencer key
  bars` entry in `UndoGlideSurfaces` (`shortGlideUnderReduceMotion`).
- **Drum Kit pad knobs.** `DrumPadControls` draws each of the four pad knobs at its parameter's value plus what is left of a
  `KeyedPointTween` glide (one key per knob, the value as a proportion of its range). A change the parameter makes while
  `AppUndoManager::isRestoring()` is true (an undo or redo of a knob change) and a pad pick both glide the drawn value from
  the one shown; a drag, a key press or automation takes the knob where it is, frame for frame. Guard: the `Drum Kit pad
  knob` entry in `UndoGlideSurfaces`; the pick itself is `DrumKitPadGlideTests`.

## The checklist

`Tests/UI/Layout/UndoGlideSurfaces.h` lists every undoable move surface in one place (`surfaces()`): each entry builds the
surface, makes a move through its real edit path, calls the real `AppUndoManager::undo()` and `redo()`, and
`UndoGlideSurfaces.EveryMoveSurfaceGlidesOnUndoAndRedo` checks the moved thing is drawn between its two places on the first
frames and settles at the restored one. **A new move surface adds an entry there; that list is the checklist.**
`UndoGlideSurfaces.EveryGlideFrameRepaintsEverythingThatChanged` walks it too: each surface returns the component its
tween repaints (`paintedComponent()`; a surface drawn through a `KeyedTween` must return one), and after every 16 ms
glide frame of an undo and a redo (also under Reduce Motion) the test re-renders only the areas the tween asked to repaint
(`setKeyedTweenDirtyProbeForTest`) and compares with a full render.
Two entries cover sections: `Section move: clips` (a cover move whose covered clip comes back on undo and shrinks
away again on redo) and `Section strip block` (a block dragged past another; it lands at once under Reduce Motion).
The same list is walked under Reduce Motion (a fade for the point surfaces, an instant landing for the rows) and with Animations Off (instant).
`UndoGlideSurfaces.ASecondStepMidGlideStartsFromTheDrawnPlace` (`UndoGlideSurfacesSecondStepTests.cpp`) presses a second
step mid-motion on every entry: an undo, then a redo 40 ms later while the undo still glides (and the mirror). Nothing may
jump at the second press, and the glide settles where the second step puts it. The canvas's macro fold has the same
check in `MacroFoldSecondStepMidMotion.*` (two undos, and two redos, 40 ms apart).

## Registration

`DragCancel.EscMidDragRestoresTheDocumentAndLeavesNoUndoStep` (`Tests/UI/Layout/DragCancelTests.cpp`) holds every clip-lane and piano-roll drag gesture to Esc: real press and drag, Esc through the key walk, release; the document and undo history must be unchanged and the preview gone. `DragCancel.EveryDragModeOfBothSurfacesIsListed` fails by name for a drag mode missing from its table.

`UndoGlideSurfaces` is an opt-in list, so a curve nobody registered snaps without any test failing (the EQ curves were
missed this way). There is no cheap general detector for "a module-window curve that has a restore path": the miss is the
ABSENCE of a tween, which nothing in the source marks. The rule stands in review: a surface that re-reads its model on a
timer or a listener registers here. `EqUndoGlide` (`Tests/UI/ModuleViews/EqUndoGlideTests.cpp`) covers what the list cannot
for the EQ: add/remove fades, the joint glide, a drag landing at once, and Cmd+Z / Cmd+Shift+Z from the curve and the window.

Two discovery tests keep the guards above from being skipped by a new surface. `DragSurfaces.EveryMouseDragOverrideIsRegistered`
(`Tests/UI/Layout/DragSurfaceRegistryTests.cpp`) scans `Source/` for every class that overrides `mouseDrag` and fails by
name for one that is not in its table. A class maps to the guard that checks its preview against its commit
(`clip-edge` = `dragEdgeExpectingPreviewEqualsCommit`, `point-markers:<name>` = `pointSurfaces()`, `undo-glide:<name>` =
`surfaces()` above, or the name of another test that checks it) or to `exempt: <reason>` for a drag with no preview/commit
split. The guard names are checked against the real lists, and a table entry whose class no longer overrides `mouseDrag`
fails as stale. A class with a real split and no guard goes in `kKnownGaps` with a ticket; that list may only shrink.
`PopupMotionUsers.EveryUserIsRegistered` (`Tests/UI/Layout/PopupMotionRegistryTests.cpp`) does the same for every file that
calls `PopupMotion::attach`, `dismiss`, `dismissModal` or `dismissCallOut` (`launchDialog` is registered by the theme sweep).
Both scan through `Tests/UI/SourceScan.h`, which the theme sweep uses too.
