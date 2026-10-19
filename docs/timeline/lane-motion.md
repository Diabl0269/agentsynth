# Lane motion

How the automation lanes under an open track move when something in them comes, goes or is put back. The general
rules are in [animation](../layout/animation.md#motion-rules); the lanes themselves in [automation](automation.md).

## Lane blocks and modulator rows

A lane block (a lane row plus its modulator rows) never jumps in or out of an open track: `TimelineAutomationLanes`
runs one shared grow / shrink tween for them (`TimelineAutomationLanesBlockMotion.cpp`, one `AnimationDriver`, 160 ms
`easeOutCubic`, every tween in flight retargeted from where it stands when another starts).

- **Arriving.** A lane that appears in a track that was already open (Duplicate, Move to track, Add automation, an undo
  bringing one back) grows from no height to its full height; its header, rows and curve are cut at the block's
  growing bottom edge, never squashed. A new routing's modulator row (Add modulator, a row dropped on this lane) grows
  the same way while it fades in.
- **Leaving.** A lane that leaves an open track (Delete lane, Move to track, an undo taking it away) is replaced by a
  picture of its block taken just before it went (`BlockPicture`), which shrinks and fades in its old place and is
  dropped when the gap has closed. A modulator row whose routing went (Remove modulator, an undo) fades out in a gap
  that closes in its old slot; the rows below slide up with it.
- **A row dropped on another lane** (the modulator row drag) is both at once: it grows in at the slot it was dropped in,
  and its old lane's gap closes, so neither lane block and nothing below jumps. Its old row is not drawn fading in the
  old slot (`droppedRoutingKey_` hides it at once): it is the row growing in the new one. The undo moves it back the
  same way.
- Every height feeds `extraHeights()`, so the track headers and lanes below glide through the usual `onLayoutChanged`
  layout, frame by frame.
- Opening or folding a track, loading a project, a lane turning into a modulator's amount band, or a lane that comes and
  goes within one change (never drawn) lands at once. So does everything under Reduce Motion, Animations Off or with
  the timeline not showing (`canAnimateBlocks`).
- Tests force the path with `FadeAnimateGuard` and step it with `stepBlockMotionForTest`
  (`AutomationLanesBlockMotionTests.cpp`).
