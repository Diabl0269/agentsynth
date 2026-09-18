# Edit-Surface Focus Arbitration

Three independently-editable surfaces compete for the same physical keys: the graph editor, the
clip lanes ([clips](clips.md)) and the piano roll ([piano-roll](piano-roll.md)). Each already grabs
keyboard focus on its own `mouseDown`. This doc is the rule that decides which one Cmd+C/V/D/X/R
and Cmd+Shift+A act on.

Cmd+C/V/D are global `ApplicationCommandManager` commands owned by `MainComponent`, so — unlike
Delete and Escape, which each surface intercepts locally via its own `keyPressed` — something has
to decide *which* surface's selection and clipboard they mean.

## The one resolver

`MainComponent::resolveEditSurface() const` is the single focus-ownership rule:

```cpp
enum class EditSurface { Graph, TimelineClips, PianoRoll };
```

It returns `TimelineClips` / `PianoRoll` when the timeline panel is visible AND real keyboard focus
(`juce::Component::getCurrentlyFocusedComponent()`) sits inside the clip-lane area or piano roll
respectively, and `Graph` otherwise — including when the timeline panel is hidden outright,
regardless of what a stale focus pointer inside it might point at.

**Nothing new grabs focus for this.** Every surface already does it on `mouseDown`
(`GraphEditor::mouseDown` is the idiom's original; `TimelineClipLaneArea`, `PianoRollComponent` and
`AutomationLaneEditor` all copy it), so "the surface you last clicked owns the verbs" falls out of
ordinary JUCE focus tracking with no extra bookkeeping.

Headless tests cannot always create a real focus grab — `grabKeyboardFocus()` needs a native peer,
and `FocusArbitrationTest`'s `SurfaceResolverRealFocus` documents why this repo does not attempt
one — so `MainComponent::setEditSurfaceOverrideForTest()` is consulted FIRST and short-circuits the
real-focus check when set.

## Clipboard verbs route by surface

`MainComponent::getCommandInfo` / `perform` branch on `resolveEditSurface()` for
`AppCommands::copySelection` / `pasteSelection` / `duplicateSelection` / `cutSelection` /
`repeatSelection`.

**Graph** — Copy/Paste/Duplicate are `GraphEditor::copySelection()` / `pasteClipboard()` /
`duplicateSelection()` against its own `ModuleClipboard`. Cut is **composed** from the two that
already exist: `copySelection()` fills the clipboard without touching the graph or the undo stack,
then `deleteSelection()` removes the selection inside its own `recordStructuralChange`, so it costs
exactly one graph-undo step and the copy half survives the undo. Repeat is **inactive** here —
always — because "N copies, each one selection-span further along" is a time-axis idea and a
spatial canvas has no such axis; Duplicate is the graph's answer to "another one of these".

**TimelineClips** — the clip clipboard, owned by `TimelinePanelComponent`, which already owns the
clip selection:

- `copySelectedClips()` serialises the selected clips — notes (each with its own `muted` flag),
  name, length, `muted`, and every audio field (`assetRef`, `gainDb`, both fades,
  `sourceStartSeconds`) — with starts relative to the earliest selected clip.
- `pasteClipsAtPlayhead()` re-inserts them onto their ORIGINAL tracks (by `TrackId`), re-based so
  the earliest clip lands at the transport's CURRENT position (snapped via the shared
  `TimelineViewState`), in ONE `AppUndoManager::recordTimelineChange`. The track fallback is
  **kind-aware** (`TimelineDoc::moveClipToTrack`'s rule): a clip lands back only on a track that
  still plays its payload, else the doc's first track of the required kind, else it is skipped.
  Audio fields go back through `setClipAsset` / `setClipGainDb` / `setClipFades` rather than a raw
  struct write, **so a clipboard `assetRef` is re-validated exactly like a freshly-loaded file's —
  a clipboard is only as trustworthy as whatever filled it.**
- `duplicateSelectedClips()` calls `TimelineDoc::duplicateClip()` per selected clip, batched the
  same way.
- `cutSelectedClips()` is copy then delete the selection, as ONE `recordTimelineChange`, never
  wrapped a second time — that would make Cmd+Z a two-step undo for one gesture.
- `repeatSelectedClips(count)` makes `count` back-to-back copies of the selection's own span (`max
  end - min start`, not each clip's own length, so a multi-clip rhythm tiles intact), the first
  starting one span-length after the selection's start, batched into one undo step.

All four leave their result selected, mirroring `GraphEditor`'s own "the copies are what you
probably want next" convention.

**PianoRoll** — the roll's OWN note clipboard ([piano-roll](piano-roll.md#note-clipboard)):
Copy/Paste/Duplicate/Cut/Repeat all act on notes. Paste **primes** the roll's playhead from the
live transport (`timelinePanel.getPianoRoll().setPlayheadBeat(audioEngine.getTransport().
getPositionSnapshot().ppq)`) immediately before pasting. That is priming, not a side effect: the
roll only has a playhead position because the overlay pushes one while playing, and a stopped
transport never does.

**Paste is active only when the SURFACE-MATCHING clipboard has something in it** —
`GraphEditor::canPaste()` for Graph, `TimelinePanelComponent::canPasteClips()` for TimelineClips,
`PianoRollComponent::canPasteNotes()` for PianoRoll (both halves: a non-empty clipboard AND an open
clip) — so copying modules never makes Paste live on the clip lanes or the roll, or vice versa. Cut
shares Copy's enablement predicate on every surface, since a cut is a copy that also deletes.
Repeat's predicate is `hasClipSelection()` / `hasNoteSelection()` on the timeline surfaces and
unconditionally `setActive(false)` on Graph.

## Select All

`Cmd+Shift+A` (`AppCommands` / action id `selectAllModules`, kept for a persisted binding's sake
even though the verb widened) is routed by the SAME resolver:
`TimelinePanelComponent::selectAllClips()` on TimelineClips, `PianoRollComponent::selectAllNotes()`
on PianoRoll, `GraphEditor::selectAllModules()` on Graph.

Unlike the clipboard verbs it is **always active**, since each surface's own `selectAll*` just
returns `false` harmlessly when there is nothing to select. See
[`shortcuts.md`](../shortcuts.md#surface-routing-who-cmdcvdxr-and-cmda-act-on) for the user-facing
table.

## Space is global

`AppCommands::togglePlayback` (`ShortcutManager` action id `togglePlayback`, default binding: bare
spacebar, no modifiers) is deliberately NOT routed by `resolveEditSurface()` — it always toggles
the transport, from any surface, via
`TimelinePanelComponent::getTransportBar().getPlayStopButton().triggerClick()`. That is the SAME
choke point the transport bar's own click handler uses
([transport](transport.md#the-transport-is-the-truth)), so the button's visual state and a
Space-triggered toggle can never disagree.

**Why it is safe to claim app-wide**, for the same reason Cmd+C/V is: a focused `juce::TextEditor`
consumes the spacebar itself (types a space character) before `MainComponent::keyPressed`, the sole
dispatch point, ever sees it. Always active — the timeline is always available, so there is no
preference that can hide the transport out from under this command.

## Delete stays panel-local

Unlike C/V/D, Delete and Escape are not routed through `ShortcutManager` or
`ApplicationCommandManager` at all (see [`shortcuts.md`](../shortcuts.md) for the reasoning). Each
surface's own `keyPressed` handles its own selection and falls through (`return false`) on an empty
one, which is what makes an unmodified `Delete` binding surface-scoped for free.

`FocusArbitrationPlaybackDeleteTests.cpp`'s `DeletePerSurface` pins that a clips-focused Delete
never touches the graph, a graph-focused Delete never touches the clips, and an empty selection on
either falls through rather than eating the key.

## Tests

`Tests/App/FocusArbitration/` — one test per verb × surface, split by concern:
`FocusArbitrationClipboardTests.cpp` (Copy/Paste/Duplicate on the graph, clip copy,
paste-at-playhead and duplicate including the missing-track fallback, and the roll's own
enablement and primed-playhead paste), `FocusArbitrationSelectionTests.cpp` (Cut as ONE undo step
on clips and on notes, Select All routing per surface, Repeat's tiling and its count clamp, and
the empty-selection enablement), `FocusArbitrationPlaybackDeleteTests.cpp` (Space from every
surface, per-surface Delete, the resolver's real-focus fallback, and a bare arrow key falling
through untouched), `FocusArbitrationZoomGridTests.cpp` (snap and zoom commands per focused
surface, inactive while the panel is hidden, and both scroll preferences reaching the lanes and
the roll) and `FocusArbitrationShiftedKeysTests.cpp` (shifted symbol key codes reaching the grid
and vertical-zoom commands, and the locator jump keys from inside and outside the panel), with the
shared fixture in `FocusArbitrationTestFixture.h`.
