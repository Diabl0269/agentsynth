# Undo and redo glide

The rule, stated in [animation.md](animation.md#undo-and-redo-glide): **anything that glides forward glides on undo and
redo.** An undo that jumps looks like a different app from the edit it reverses. The two entry points are
`AppUndoManager::undo()` and `redo()` (the Cmd+Z / Cmd+Shift+Z path); a surface learns of a restore from
`isRestoring()` (true while it runs) and `getRestoreSerial()` (up by one after every finished step).

## Colours glide too

An undo or redo moves every changed visual property (position, size, colour, count) with the shared glide; colours ease through `KeyedColourTween`.

`KeyedColourTween` (`Layout/KeyGlide/KeyedColourTween.h`) is `KeyedTween` over `TweenColour` (linear 0..1 channels), so it has the same 160 ms `easeOutCubic`, the same Animations Off, hidden-owner and Reduce Motion rules (a colour lands at once under Reduce Motion: `fadeWhenReduced` is false) and the default whole-owner repaint. The morph map keys each dot's RESOLVED colour (the picked one, else its palette slot) in `colourTween_` and starts a glide only on a restore with the same dot count; the live colour picker preview stays instant. The shape window's chips glide the same way (each chip owns a tween), triggered by the same `RestoreWatch`. A track's colour glides on every surface that shows it, each on its own `RestoredColourGlide` (`Layout/KeyGlide/RestoredColourGlide.h`: a `KeyedColourTween` plus the app's `RestoreWatch`), so the header stripe and swatch, the clips, the mixer strip's dot and stripe, the automation lane stripe and curve, and the modulator row's tag move in step without shared state; a section's colour (its strip blocks and the lane edge lines), a marker's (its flag and stem), the By hand key strip's shape dots and the routing pane's track stripe glide the same way; a macro's colour (its open border and name chip, its collapsed card, and the card of a macro its track's colour picker recolours with the track) and each port's jack (card and docked widget) glide on one `MacroColourGlide` (`Graph/MacroGroupController/MacroColourGlide.h`, owned by `MacroGroupController`, synced from `syncMacroCards`); every fill derives from the drawn colour, the model keeps the resting one. Guards: the `Morph map dot colour`, `Shape window chip colour` and `Track colour (...)`, `Automation lane colour (header)` and `Modulator row colour`, `Section colour (strip)`, `Ruler marker colour`, `Per-note key strip colour` and `Track colour (routing pane)`, `Module card colour`, `Macro card colour`, `Macro port colour` and `Linked macro colour` entries in `UndoGlideSurfaces`; the shrink-only `colourSurfacesNotYetCovered()` is empty, and a new colour surface needs an entry, not a line there.

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
- **Add and delete in the curve editor.** A dot added (double-click on empty space) or deleted (Delete key, double-click on
  the dot, the right-click menu's Delete Point, a multi-select delete) eases the line into its new shape the way undo does,
  through one helper: `CurveEditorComponent::lineBeforeEdit()` captures the line as drawn before the edit and
  `glideLineFrom()` hands it to the morph (`morphFrom`, so Reduce Motion and Animations Off are the shared ones, and a
  second edit mid-glide continues from the line as drawn). `setModel` uses the same pair for a restore. A refresh from the
  model (`pushToTarget`, `refreshFromTarget`, the card's and the shape window's reload) never restarts or clears it.
  Guard: the edit-glide tests in `ModuleComponentFilterCurveViewTests.cpp` walk every surface of `CurveEditorSurfaces.h` and every way to add and delete.
  An added dot also **grows in**: `dotMorphScale` takes its radius from nothing to full on the morph's clock (an add's
  `EditMorph::newNode` only; Reduce Motion lands the line and fades the dot in at full size, Animations Off lands both).
  In the mod curve box and window a change that arrives through the target's watchers (the other view of the same
  curve, a preset chip) eases too: `ModCurveEditorView::refreshFromTarget` glides by default, and a view's own edit
  never comes back through it. Guards: the `ADotAddedToTheCurveBox...` cases in `ModCurveBoxTests.cpp` (a partly grown dot and an in-between line in Full,
  Reduce Motion and Off, both views, a preset) and the `Mod curve box added dot` entry in `UndoGlideSurfaces` (undo
  and redo of the add).
  **Undo and redo of one dot.** A restore whose count differs by exactly one dot, with every other dot and every other
  segment's bend the same (the undo of a delete, the redo of an add, and the reverse), builds the same `EditMorph` the
  add or delete would (`CurveEditorComponent::singleDotEdit`): the dots that exist on both sides stay at fade 1.0 and in
  place on every frame, and only the dot that comes or goes (and the handle of the segment that splits or merges) grows
  or fades. Any other change of the count keeps the hide-all morph. Guard: `UndoGlideSurfaces.EveryCurveSurfaceKeeps
  TheOtherDotsSteadyOnUndoAndRedo` (`Surface::dots()`: every registered curve surface, the `Mod curve box deleted dot`
  entry included, every 20 ms frame of an undo and a redo) and `CurveEditorEditMorph` in `CurveEditorPaintTests.cpp`.
- **Automation lane points.** `AutomationLaneEditor` matches old to new by order when a restore keeps the point count and
  changes beats or values; `LanePointGlide` animates a pure add or remove: the dot grows or shrinks while the line
  glides between its two shapes on the same clock (the `Automation lane add` and `Automation lane delete` entries in
  `UndoGlideSurfaces`; `LaneLineGlide.*` checks every x of every frame and that the dots that stay never fade).
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
  clip that arrives on a known track or leaves grows from and shrinks toward its section block's origin (a clip with
  no arriving or leaving block: its own left edge in, its centre out;
  [sections](../timeline/sections.md#motion)): `ClipLaneEnterExit` keeps a copy of every clip at each sync and draws the leaving ones with the real `paintClip`
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
  pick, its undo and its redo all travel and the written-note outlines fade with them, and so does the Pitch lane's row
  shading (in-scale rows, the root's wash and its label colour blend to the new key with the bars' eased progress, and a
  second change mid-glide starts from the shade drawn then, `fromRowShade_`; Reduce Motion lands the shading at once). Reduce Motion shortens it to an
  80 ms glide with no stagger (no fade: the bars still move); Animations Off lands at once. A second Cmd+Z or
  Cmd+Shift+Z mid-glide starts every bar from where it is drawn then (`captureDrawnAsGlideStart`). Guard: the `Sequencer key
  bars` and `Sequencer step bars` (a note dragged in the Pitch lane) entries in `UndoGlideSurfaces`
  (`shortGlideUnderReduceMotion`); the grid repaints itself whole each frame, so the step bars entry sets
  `repaintsWholeComponent` and the frame check counts the whole grid as repainted. Steps never changes the pitch range, so changing it moves no bar.
- **Mixer insert and send rows, and the strip name.** A rebuild while `AppUndoManager::isRestoring()` is true takes the
  old column's drawn row looks (`MixerRowsMotion::Snapshot`: each row's top, dim and name) and the new column's lists
  ease from them (`MixerRowLookGlide`, one `KeyedRectTween` per list): rows that moved (a move, a reorder, a send move)
  slide, rows that were bypassed or brought back dim or brighten, a retargeted send or renamed module cross-fades its
  name, and the strip name in the column header cross-fades. Master's insert list does the same in `setColumn`. Rows
  that come or go keep the delete and undo motion; a module that joins or leaves the chain keeps its kind flip. Under
  Reduce Motion the rows are at their places on the first frame and fade in there; Animations Off and a mixer that is
  not showing land at once. A second restore mid-glide starts from the drawn looks. Guards: the `Mixer insert row`,
  `Mixer send row` and `Mixer insert bypass dim` entries in `UndoGlideSurfaces`, and `Tests/UI/Mixer/MixerRowsMotionTests.cpp`.
- **Drum Kit pad knobs.** `DrumPadControls` draws each of the four pad knobs at its parameter's value plus what is left of a
  `KeyedPointTween` glide (one key per knob, the value as a proportion of its range). A change the parameter makes while
  `AppUndoManager::isRestoring()` is true (an undo or redo of a knob change) and a pad pick both glide the drawn value from
  the one shown; a drag, a key press or automation takes the knob where it is, frame for frame. Guard: the `Drum Kit pad
  knob` entry in `UndoGlideSurfaces`; the pick itself is `DrumKitPadGlideTests`.
- **A toggle that changes the jack layout.** Dual I/O on a card re-lays its jacks (one Audio jack, or Left and Right). `DualIOJackGlide` records each raw channel's drawn jack before the change and eases it to the new layout, the cables with it, on the click, its undo and its redo alike: the parameter callback runs while the old cables are still in the graph on every path. Guard: the `Dual I/O jack split` entry in `UndoGlideSurfaces` (lands at once under Reduce Motion); the frames and the cable ends are `DualIOJackGlideTests`.
- **Controller surface cells.** `ControllerSurfaceComponent::setControls` recreates every cell at its final grid bounds, so it
  records where each cell is drawn (bounds plus the glide's offset) before the rebuild and, after it, eases every cell that
  stayed but sits on another grid place from there to its place (`KeyedRectTween` over the cells' parent, 160 ms `easeOutCubic`,
  a translation on the cell; Reduce Motion lands it and fades it in). A second undo or redo mid-glide starts from the drawn
  place. A live drag stays direct: its drop rebuilds the cells where they already are, so nothing glides, and a drag that starts
  on a gliding cell ends the glide first. Guard: the `Controller surface cell move` entry in `UndoGlideSurfaces`, and
  `ControllerSurfaceMoveGlideTests` for the group move and the drag rules.

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

### Edit Layout writes glide

Every write of the on-card editor (`CardLayoutOnCardEditor::writeLayout`) runs in the card glide's restore
`CardGlideAnimator::Scope`, exactly as the right-click quick path and an undo do, so the cards a bigger or smaller card
pushes slide and its jack rows fade. The editor's own card is exempt from that slide (`setCardExempt`), so its controls
glide through the editor instead: `writeLayout` keeps the cells, group titles and views as they were drawn and, once the
card has rebuilt, `animateRestore` glides each from there to its new rect (160 ms, `easeOutCubic`). That covers a size
change (Show as Fader, Size, Direction: the control grows or shrinks in place and the row it repacks follows, one glide
for the one undo step), a Preset, the Shared | Separate switch, a control added from the strip and every write that
moves a neighbour. A write whose caller glides itself (a drop's settle and push, a group move, Hide's close-the-gap, a
drop from the Add panel) holds `GlideHold` so the glide is not started twice. Under Reduce Motion, Animations Off or a
canvas that is not on screen the write lands at once. `OnCardWriteGlide` (`OnCardControlMotionTests.cpp`) pins one
write kind per case, with motion (first frame is where it was, a glide is in flight, the end is where it landed) and
under Reduce Motion (landed at once).
