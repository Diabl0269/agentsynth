# Duplicate motion: the copy comes out of the original

A duplicated track (Cmd+D, the row menu, the strip menu's "Duplicate Track", Paste Tracks) is not a new row growing out
of nothing: the copy visibly comes out of the track it was copied from, on all three surfaces at once.

| Surface | Forward | Undo |
|---|---|---|
| Timeline row | The copy starts on the original row (80% size, 75% opaque, lifted look: shadow, raised fill, accent edge) and travels to its slot under it, growing to full size and settling into a normal row, while the rows below make room **in the same movement**. | The copy goes back into the original while the rows close up. |
| Mixer strip | The same, sideways, out of the original strip. | Back into it. |
| Canvas | The copy's macro (a collapsed card, or an open macro's border) and its modules glide from the original's place to theirs and fade up; cables follow. | The copy glides back into the original's place and fades out. |

Redo plays the forward motion again. Several tracks at once (a multi-selection, Paste): each copy comes out of **its own**
original.

## Numbers

- Forward 240 ms, `easeOutCubic`. Undo 200 ms, `easeInCubic` (`ExitEnterTimeline::kOutOfSourceMs`, `kIntoSourceMs`).
  Rows, strips and the other rows read one eased progress (`ExitEnterTimeline::together`), so no frame shows them out of step.
  No outline: it marks what an undo brought back, and a copy is new.
- Canvas: the glide is 240 ms (`CardGlideAnimator`, `Item::fadeUp`: 35% to opaque by halfway); an open macro's border uses the
  macro hull glide (220 ms). The undo is the delete/undo exit phase (180 ms, ease in) with the ghost travelling to the
  original instead of shrinking in place.
- **Reduce Motion:** nothing travels or scales. The rows and strips are in place at once and the copy fades over 80 ms
  (`kTogetherFadeMs`); on the canvas the copy fades in where it is (`noteEnter`). **Animations off, an off-screen panel, or
  the piano roll open:** everything lands at once.

## How it is built

- **Plan variant, no new animator.** `ExitEnterListPlan::forInsertionFromSource` / `forRemovalIntoSource`
  (`Source/UI/Layout/ExitEnterList/`) mark the copy's slice `travels` with `originStart` (its original's start, as the
  motion begins on the way out, as it ends on the way back). `drawnAt` places it between the two; `ExitEnterListOverlay`
  draws the lifted look (`paintReorderLift`). Both return false, leaving the plain grow-in/shrink, when a copy has no
  source among the rows (scrolled out, pinned, hidden).
- **Who says "this is a copy".** `MainComponent::TrackGrowInScope::noteOrigin(copy, source)` tells the timeline panel and the
  mixer panel before the scope ends; each keeps copy -> source (mixer: the column carrying the source track) so the undo and
  redo, which go through `AppUndoManager::setTrackListHooks`, know it too. An entry is dropped when the user deletes the
  copy (its undo then grows it back where it stood, with the outline), and everything is cleared on a project load.
- **Canvas.** `duplicateTrack` and `duplicateTracksBelow` open a `CardGlideAnimator::Scope` around the undo record;
  `MacroGroupController::glideCopiesOutOfOriginals` calls `noteGlideOutOf` for each copied card and the copy's macro card,
  and `noteOrigin` records copy -> original (cards, macro cards, borders). The copy's open border starts from the original's
  hull (`GraphEditor::glideHullsFrom`). On undo/redo the exit/enter ghost gets `Item::via` (the original's rect) and
  travels instead of shrinking or growing.

## Tests

`Tests/UI/Layout/ExitEnterList/ExitEnterListFromSourceTests.cpp` (the plan and overlay at several moments, both
directions, Reduce Motion, several copies), `Tests/UI/Timeline/TimelineTrackDuplicateMotionTests.cpp` (the panel: start on
the original, rows below in step, undo, redo, multi-track, hand-deleted copy, off-screen),
`Tests/App/MainComponent/MainComponentDuplicateTrackMotionTests.cpp` (canvas glide at several moments, undo and redo
ghosts, Reduce Motion, off-screen).
