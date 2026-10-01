# Edit-Surface Focus Arbitration

Four independently-editable surfaces compete for the same physical keys: the graph editor, the
clip lanes ([clips](clips.md)), the piano roll ([piano-roll](piano-roll.md)) and the mixer
([mixer](../mixer/mixer.md)). Each of the first three already grabs keyboard focus on its own
`mouseDown`; the mixer panel wants keyboard focus outright (FRO18). This doc is the rule that
decides which one Cmd+C/V/D/X/R and Cmd+Shift+A act on.

Cmd+C/V/D are global `ApplicationCommandManager` commands owned by `MainComponent`, so — unlike
Delete and Escape, which each surface intercepts locally via its own `keyPressed` — something has
to decide *which* surface's selection and clipboard they mean.

## The one resolver

`MainComponent::resolveEditSurface() const` is the single focus-ownership rule:

```cpp
enum class EditSurface { Graph, TimelineClips, PianoRoll, Mixer };
```

It returns `TimelineClips` / `PianoRoll` when the timeline panel is visible AND real keyboard focus
(`juce::Component::getCurrentlyFocusedComponent()`) sits inside the clip-lane area or piano roll
respectively; `Mixer` when the mixer panel is actually showing (`BottomDockComponent::
isMixerShowing()` — covers docked-and-on-the-Mixer-tab and detached-to-a-window — OR
`MixerPlacementController::isOwnPanelShowing()` for the "Own panel" placement) AND real keyboard
focus sits inside `MixerPanelComponent` (FRO18: the mixer's single focusable leaf — every column's
own controls are `setWantsKeyboardFocus(false)`, so a column control's focus resolves here too);
and `Graph` otherwise — including when every one of those panels is hidden outright, regardless of
what a stale focus pointer inside one of them might point at.

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
- `duplicateSelectedClips()` is `repeatSelectedClips(1)` (below): the copies land as one block
  starting where the selection ends, so duplicating adjacent clips never overlaps an original.
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

**Mixer** — inactive on every one of these five, unconditionally. The mixer has no clipboard or
repeat model of its own: its own keyboard verbs (Left/Right column walk, Up/Down fader nudge,
Enter select-on-canvas, the rebindable M/S/R) are resolved directly by
`MixerPanelComponent::keyPressed`, never routed through `resolveEditSurface()`. Each `perform*()`
body still carries a `Mixer` case that returns `true`/`false` without touching the graph — belt-
and-suspenders for a direct/scripted `perform()` call, since `isEditSurfaceCommandActive` already
reports every one of these ids inactive, so `ApplicationCommandTarget::tryToInvoke` refuses them
before a keypress or menu click ever reaches `perform()`.

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
on PianoRoll, `GraphEditor::selectAllModules()` on Graph, and a deliberate no-op (a status-bar
message, the graph selection left untouched) on Mixer — there is no multi-column selection model to
select all of.

Unlike the clipboard verbs it is **always active** on every surface, including Mixer — since each
surface's own `selectAll*` (or the Mixer no-op) just returns `false`/does nothing harmlessly when
there is nothing to select. See
[`shortcuts.md`](../control/shortcuts.md#surface-routing-who-cmdcvdxr-and-cmda-act-on) for the user-facing
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
`ApplicationCommandManager` at all (see [`shortcuts.md`](../control/shortcuts.md) for the reasoning). Each
surface's own `keyPressed` handles its own selection and falls through (`return false`) on an empty
one, which is what makes an unmodified `Delete` binding surface-scoped for free.

`FocusArbitrationPlaybackDeleteTests.cpp`'s `DeletePerSurface` pins that a clips-focused Delete
never touches the graph, a graph-focused Delete never touches the clips, and an empty selection on
either falls through rather than eating the key.

## Clip keyboard mode

A keyboard-only user reaches a clip through the track header: **Right** on a focused header (or on the panel root, where Cmd+Shift+T and Tab leave focus: it enters the focused track, else the first track with clips; `handleRootFocusKey`)
(`timelineClipNext`) calls `TimelinePanelComponent::enterTrackClips`, which picks the first clip
starting at or after the playhead (else the track's first), makes it the lane area's **keyboard
clip** and grabs lane focus. A track with no clips ignores the key. Keys, defaults and the verbs
they run are in [`shortcuts.md`](../control/shortcuts.md#timeline); this section is the model.

- **The keyboard clip is the selection.** `TimelineClipLaneArea::setKeyboardClip` replaces the clip
  selection with that one clip and remembers its id; `getKeyboardClip()` only answers while that
  id is still the sole selected clip. A marquee, a click elsewhere, Delete or an undo that removes
  it ends keyboard mode without any extra bookkeeping, and every selection-based verb (copy, split,
  mute, loop the selection) acts on it unchanged. A clip selected with the pointer is stepped from
  by the same keys.
- **Stepping** uses `Source/UI/Timeline/ClipKeyboardNav.h`: Left/Right follow the track's start
  order; Up/Down pick the clip with the nearest start on the closest track above/below that has
  clips (ties go to the earlier one). A step with nowhere to go is consumed and changes nothing.
- **Enter** fires `onClipDoubleClicked`, the hook a double-click uses, so the piano roll opens on
  the clip. Closing the roll returns focus to the lane with the clip still the keyboard clip.
- **Alt+Left/Right** run `TimelineDoc::moveClip` inside one `recordTimelineChange` (one undo step)
  by one grid division: `TimelineViewState::divisionBeatsRaw` (the chosen Snap division even with
  the snap switch off), one beat when Snap is Off; the start clamps at beat 0.
- **Escape** is a fixed key: the lane clears keyboard mode and fires `onReturnToTrackHeaderRequested`;
  the panel moves focus back to that track's header. The clip stays selected.
- **Staying visible.** Every change fires `onKeyboardClipChanged`; the lane scrolls
  `firstVisibleBeat` until the clip is on screen (a little in from the edge it came from), and the
  panel moves `focusedTrackIndex_` to the clip's row and runs `ensureTrackVisible`.
- **Focus ring.** The keyboard clip is outlined with the theme accent while the lane holds focus.
- **Screen reader.** The lane area has title "Clips" and a group-role accessibility handler whose
  text value is the keyboard clip, from `describeClipForAccessibility`
  (`Source/UI/Timeline/ClipAccessibilityText.h`), e.g. "MIDI clip Bassline, track Bass, bars 5 to
  9" (bar numbers are 1-based; the end is the boundary where the next bar begins; a clip off the
  bar lines reads "from bar 2 beat 3.5 to bar 3 beat 1.5"; audio clips say "Audio clip", muted
  ones append ", muted"). A value-changed event is posted only when the description changes.

Tests: `Tests/UI/Timeline/TimelineClipKeyboardTests.cpp` drives real `KeyPress` events from a
focused header; `TimelineClipKeyboardNavTests.cpp` covers the pure helpers. Real OS focus needs a
native peer, so they assert the model the focus calls mirror (keyboard clip, selection,
`focusedTrackIndex_`, open roll).

## Piano roll focus

Return in clip mode opens the roll and `TimelinePanelComponent::openPianoRoll` hands keyboard focus
to its grid; **Escape** (nothing selected; with notes selected the first Escape clears the
selection) closes it and `closePianoRoll` hands focus back to the lane, the clip still the keyboard
clip. Tab and Shift+Tab inside the roll walk, in screen order, the header chips, the scale-assist
panel's controls, the velocity Set box and the velocity strip. The header chips are painted shapes,
so each has a transparent `PianoRollHeaderChip` button over it: the Tab stop, accent focus ring,
screen-reader name and role (a toggle chip reports its on state), tooltip, and the **Return**
target (it runs the chip's one action, the same function a click runs).

- **Focused note.** The one note the selection holds (`PianoRollComponent::getFocusedNote`); the
  Alt+Left/Right keys (`pianoRollNavNextNote`/`pianoRollNavPrevNote`) move the selection, so they
  move it. It is outlined with the accent focus ring while the grid holds focus, repainted on
  focus gained/lost and on every selection change.
- **Screen reader.** The grid has title "Piano roll" and a group-role handler whose text value is
  the focused note from `describeNoteForAccessibility` (`Source/UI/PianoRoll/NoteAccessibilityText.h`),
  e.g. "C4, bar 2 beat 1, length 1/8, velocity 100" (bar and beat are 1-based timeline positions;
  length is a fraction of a whole note when exact, else "0.3 beats"). With no single note focused it
  reads the clip name and note count ("Lead, 12 notes", plus ", 3 selected" for a multi-selection). A
  value-changed event is posted only when the text changes.

Tests: `Tests/UI/PianoRoll/PianoRollFocusedNoteTests.cpp` (real Alt+Arrow `KeyPress`es, the spoken
value, the ring rendered to a software image), `PianoRollKeyboardReachTests.cpp` (chips, tab order,
Return-opens/Escape-returns through the panel) and `NoteAccessibilityTextTests.cpp`. Real OS focus
needs a native peer, so the ring is forced with `setFocusRingForcedForTest`.

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
the roll), `FocusArbitrationShiftedKeysTests.cpp` (shifted symbol key codes reaching the grid
and vertical-zoom commands, and the locator jump keys from inside and outside the panel) and
`FocusArbitrationMixerSurfaceTests.cpp` (FRO227: the resolver's override round trip plus its
dock-visibility gate, every clipboard/repeat verb and both zoom axes inactive, and Select All's
no-op on the graph selection), with the shared fixture in `FocusArbitrationTestFixture.h`.
