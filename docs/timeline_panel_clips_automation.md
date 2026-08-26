# Timeline Panel: Clips & Automation

This document covers the timeline panel's clip lanes, the piano roll note editor, the automation
strip, and the keyboard/focus arbitration rule that decides which surface Cmd+C/V/D/X/R and
Cmd+Shift+A act on. The panel shell, ruler/grid, track headers, playhead, transport bar,
metronome/count-in and edit-tool strip live in the companion document,
`docs/timeline_panel_core.md`.

- [1. Clip Lanes](#1-clip-lanes)
- [2. Piano Roll](#2-piano-roll)
- [3. Automation Strip](#3-automation-strip)
- [4. Keyboard & Focus](#4-keyboard--focus)

---

## 1. Clip Lanes

`Source/UI/TimelineClipLaneArea.h/.cpp` (`synth::ui::TimelineClipLaneArea`) fills the lanes region
below the ruler (`getLanesBounds()` minus the ruler strip — the same rect the bar/beat grid is
painted into) with per-track rows of `synth::Clip` rects: drag to move, drag an edge to trim, a
context menu to split/duplicate/delete, and marquee (rubber-band) multi-select. Backed by
`synth::ui::ClipSelectionModel` (`Source/UI/ClipSelectionModel.h`), the clip analogue of
`SelectionModel` (`docs/layout_selection_canvas.md` §1.2) — a `std::set<synth::ClipId>` with the same add/remove/toggle/
setSelection/retainOnly contract, ordered ascending by id so a batched move or delete always walks
clips in a stable order regardless of click order.

**Ownership.** `TimelinePanelComponent` owns the `ClipSelectionModel` and the lane area
(`getClipSelection()` / `getClipLaneArea()`); the lane area holds the selection model and the
shared `TimelineViewState` by reference, exactly the relationship the ruler already has with the
view state. `MainComponent` forwards its one `AppUndoManager` in (`TimelinePanelComponent::
setUndoManager`), the same wiring block that installs the doc,
transport and track-header host (`docs/timeline_panel_core.md` §3).

**Z-order, and the one relocation this task makes.** `TimelinePanelComponent::paint()` still paints
the bar/beat grid directly, unchanged — that stays the ONE place the grid is painted. The lane area
is added as a child positioned over exactly that same rect, *before* the playhead overlay (added
last). Since JUCE always paints a parent before its children, the result is grid → clips →
playhead with no extra bookkeeping.

**Row geometry.** `Metrics::timelineTrackRowHeight` (56 px, code-only) is the single row height
both the track-header column and the clip-lane area lay out at — see `docs/timeline_panel_core.md` §3.
`TimelineTrackHeaderComponent::kRowHeight` is kept as the matching headless literal fallback rather
than deleted, read by both components' `dynamic_cast<AppLookAndFeel*>`-with-fallback pattern.
`TimelineClipLaneArea::computeClipRect(viewState, trackIndex, startBeat, lengthBeats, rowHeight)` is
a pure static function (no doc, no component) — `[beatToX(start), beatToX(start+length)]` × `[row *
rowHeight, rowHeight]` — so geometry is unit-testable with no component or LookAndFeel at all.

**Rendering.** Every track in doc order gets one row; every clip on it, a rounded rect filled with
`synth::ui::resolveTrackColour(track.colourArgb, trackIndex, track.muted || clip.muted)` (the
track-header colour resolver from `docs/timeline_panel_core.md` §3, reused rather than re-invented — dimmed when **either** flag is set, since the two are
independent in the model: a clip the user muted individually dims exactly like one sitting on a
muted track), a name (width > ~40 px) and a thin pitch-mapped note preview (width > ~24 px,
clip-relative note beats offset by the clip's current — possibly mid-drag — start). A muted clip
keeps its shape, its selection border and its waveform/notes, and loses only the fill/border
brightness and its name label's alpha (`kMutedClipLabelAlpha`) — painted straight from
`synth::Clip::muted` on the doc, **never** from a `TimelineSnapshot`, which no longer contains a
muted clip at all (see `docs/architecture.md`'s TimelineSnapshot section). Selected clips get a
brighter border and a slight fill lift. Repaints happen only on: doc changes (`refreshFromDoc()`,
routed in from `TimelinePanelComponent::timelineChanged()`, which also calls
`ClipSelectionModel::retainOnly` so a clip removed by any path can never stay selected), view-state
changes (zoom/scroll/snap), and interactions — never a timer.

**Interactions (Select tool).** Everything below is `EditTool::Select`'s gesture table — the tool
this component grew up with, and the only one that drags, resizes, marquees or double-click-authors
(see **Edit tools** further below for what the other five tools do instead; switching away from
Select disables all of this):

| Gesture | Effect |
|---|---|
| Click a clip | Selects it (replacing the selection unless Shift/Cmd/Ctrl, which toggles just that clip) |
| Click empty lane space | Deferred: only a press that never becomes a drag clears the selection on mouse-up — the same `pendingEmptyCanvasClick` trick `GraphEditor::mouseDown/mouseUp` uses for the canvas, so a drag is never mistaken for a deselect |
| Drag from empty lane space | Marquee — intersection hit-test (`clipHitTestMarquee`), additive with Shift; there is no drag-to-pan here (scrolling is wheel-only, see `docs/timeline_panel_core.md` §2), so a plain drag also starts a (non-additive) marquee |
| Drag a clip's body | Moves it (and every other selected clip) by one Snap-quantised beat delta shared across the whole selection, computed from the clip that was grabbed and clamped so no clip's start goes below 0, **plus** one shared track-row delta (see **Cross-track drag** below) — no longer same-track-only |
| **Alt** + drag a clip's body | Copy-drag: the originals stay exactly where they are (doc and screen both); release commits a `duplicateClip` + `moveClipToTrack` per dragged clip at the destination, and the **copies** end up selected. There is no Alt-**click** action — copying a clip onto itself isn't something anyone asks for by clicking |
| Drag within 6 px of the right edge | Resizes (trims) the clip's length, Snap-quantised, floored at 1/16 beat |
| Drag within 6 px of the left edge | Moves the start and shrinks/grows the length so the **end** stays fixed; the clip's notes (clip-relative) travel with it — a deliberate divergence from per-note-anchored trimming, deferred to a later task |
| Right-click a clip | `PopupMenu`: **Split at pointer** (enabled only when the Snap-quantised pointer lands strictly inside the clip), **Glue with next** (enabled only when a legal join target exists — greyed out, not hidden, so the menu's items never move around), **Duplicate**, **Mute**/**Unmute** (one toggling item, labelled for what the click will do), **Rename…** (opens the inline editor — see below), **Delete**, and — for an audio clip only (non-empty `assetRef`), below a separator — **Relink audio…** — preserves the existing selection, the same rule `GraphEditor`'s cable/canvas menus follow |
| Double-click a clip | Opens the piano roll on it (`onClipDoubleClicked` → `TimelinePanelComponent::openPianoRoll`) |
| Double-click empty lane space | Authors content on the row under the pointer — see **Adding content** below (MIDI: a one-bar clip, or one spanning the loop locators when the click lands inside them; audio: a file chooser; automation: nothing) |
| Drop audio files from the OS | Imports the first readable one onto the audio row under the cursor — see **Adding content** |
| Delete / Backspace | Deletes every selected clip as ONE undo step; returns `false` (key falls through) when the selection is empty |
| Escape | Clears the selection; returns `false` when it is already empty |
| P | **Loop the selection** — sets the transport loop to the selected clips' `[min startBeat, max endBeat]` span and arms looping; returns `false` when nothing is selected |

**Cross-track drag.** A plain (non-copy) move drag previews **one shared track-row delta** for the
WHOLE dragged set, derived from the vertical drag distance (`round(dy / rowHeight)`), legal only if
**every** dragged clip's destination row exists and accepts its payload — `TimelineDoc::
moveClipToTrack`'s kind rule: an audio clip (non-empty `assetRef`) only onto a `TrackKind::Audio`
row, a MIDI clip only onto `TrackKind::Midi`, neither onto `Automation`. An illegal drop for **any**
clip in the set clamps the whole group's row delta back to 0 — a same-lane move, i.e. exactly what
this drag did before it could cross tracks — rather than dropping only the clips that would have fit
and silently tearing the selection apart. `getPreviewRowDeltaForTest()` is the row delta the
in-flight drag would apply; a copy-drag never moves the originals on EITHER axis
(`effectiveGeometryFor` and `effectiveRowFor` share the same `copyDrag_` guard — they must agree or
the original slides while its row holds), so the destinations paint as translucent ghosts
(`paintDragGhosts`: source-track colour at 0.4 alpha plus a 1 px outline, no name label — a real
blur would be per-frame image filtering, which `docs/layout_visuals_animation.md` §2–3 rules out) while the source clips keep
painting where they are. Ghost geometry comes from one helper (`dragGhostRectFor`) shared with
`getDragGhostRectsForTest()`, the same single-enumeration reasoning as `buildVisibleCables()`.

**Edge auto-scroll during a drag.** `Source/UI/EdgeAutoScroll.h` is a small pure helper — one
function, `edgeScrollVelocity(pos, lo, hi, zonePx, maxPerTick)`, plus the two shared constants
`kEdgeZonePx` (24 px edge-zone width) and `kEdgeScrollHz` (30 Hz gated-timer rate) — deliberately
holding no state, no timer and no component reference, so "how fast does an edge-drag scroll" is
one decision the clip lanes and the piano roll (§2 below) both read rather than two that could
silently drift apart. Velocity is 0 in the dead middle band, ramps linearly with penetration depth
from 0 at the zone's inner edge to `maxPerTick` right at the component edge, and clamps at
`maxPerTick` beyond it (a pointer JUCE still reports after being dragged off the component
entirely must not fly the view away). `TimelineClipLaneArea::updateAutoScrollArming()` starts/stops
a `juce::Timer` at `kEdgeScrollHz` gated on **both** a Move/Resize drag being active AND the last
known pointer sitting inside the edge zone — the timer never runs for any other reason, including
a marquee or a plain hover. Each `autoScrollTick()` re-checks both conditions (the drag can have
ended, or the pointer moved back to the dead zone, since the last check), scrolls
`TimelineViewState` by `velocityPxPerTick / pixelsPerBeat` beats, and — because no `MouseEvent`
fired this tick, only the view moved — calls `updateDragPreviewFromLastPointer()` to re-derive the
in-flight drag preview against the same last-known pointer position but the NEW scroll offset. That
re-derivation only works because the drag-delta maths is **beat-anchored, not pixel-anchored**:
`deltaBeats = viewState_.xToBeat(pointer.x) - mouseDownBeat_` reads the delta through `xToBeat`'s
own `firstVisibleBeat` term every time, rather than caching a pixel offset computed against the view
position at `mouseDown` — a scroll mid-drag (from an auto-scroll tick, or in principle any other
scroll) is absorbed by that term instead of silently invalidating a stale pixel delta. Every scroll
tick fires `onViewScrolledByDrag` so `TimelinePanelComponent` repaints the ruler and itself (the
ruler has no other way to learn the shared view state moved — it isn't a drag participant).
Headless seams: `tickAutoScrollForTest()` drives one tick without a real timer, and
`isAutoScrollTimerRunningForTest()` observes the gating.

**Inline rename.** `beginRenameClip(ClipId)` (reached from the context menu's **Rename…**) opens a
`juce::TextEditor` over the clip's name area, pre-filled and `selectAll()`ed: Return commits through
`renameClip()` (which calls `TimelineDoc::setClipName` — trims, rejects a blank result, one undo
step), Escape cancels, and losing focus **commits** (the same three outcomes every other in-place
rename in this app has — clicking away is a commit, not a cancel). Any rename already in flight
commits first if a second one opens, and the editor detaches itself **before** either outcome runs,
so the `onFocusLost` callback its own teardown fires re-enters to a null editor and stops rather than
double-committing. `getRenameEditorForTest()` is the test seam (a live `juce::TextEditor` is no more
testable than a `juce::PopupMenu`).

**One undo step per gesture.** Every drag/trim previews locally (a member offset or length, read
back by `paint()` through `effectiveGeometryFor()`) and commits to the doc exactly once on
mouse-up, through `AppUndoManager::recordTimelineChange` — a multi-clip move or a multi-clip Delete
is one `recordTimelineChange` call however many clips it touches, mirroring
`GraphEditor::dragSelectionBy()`/`finalizeSelectionDrag()` and `deleteSelection()`'s one-transaction
contract for modules. `showMenuAsync` never runs headlessly, so the context menu's Split/Glue/
Duplicate/Mute/Delete actions are exercised in tests through `applyClipContextChoice(ClipId, choice,
pointerBeat)` — the same menu-without-the-menu idiom `TimelineTrackHeaderComponent::
applyBindingMenuChoice`/`applyContextMenuChoice` already establish (`docs/timeline_panel_core.md` §3). `ClipContextChoice` also
carries `Rename`, which is deliberately **inert** in this function — renaming opens a
`juce::TextEditor` rather than mutating the doc, so its commit path is `renameClip()` and this enum
case exists only so the menu's whole vocabulary is enumerable (a test can assert the choice mutates
nothing).

**Edit tools (Split / Glue / Erase / Mute / Draw).** `handleToolMouseDown()` routes every non-Select
tool: Split/Glue/Erase/Mute act **immediately on press** (a DAW's tool click is expected to land
under the finger, not on release) and hit-test a clip first — a click on empty lane space with any
of these four held does nothing at all, deliberately, rather than falling through to a selection
change (which would make an Erase click look like it selected something). All four route straight
into `applyClipContextChoice` — **the tool and the menu item are the same code path**, which is what
keeps "the tools are an accelerator for the menu" literally true: `Split` → `SplitAtPointer` at the
Snap-quantised pointer beat, `Glue` → `GlueWithNext` against `findGlueTarget(id)` (the clip on the
SAME track with the smallest `startBeat` at or after `id`'s end — a gap is a legal join target,
since `TimelineDoc::joinClips` treats a gap as silence and only rejects an overlap; picking the
abutting clip only would make the tool silently inert on the very arrangement, clips with gaps,
where gluing is most useful), `Erase` → `Delete`, `Mute` → `ToggleMute`. None of the four ever starts
a drag, a marquee, a trim or the double-click authoring gestures — every mouse-move and mouse-up is
a no-op while one of them is active, so a mis-aimed drag can never silently move or trim a clip
instead of doing the click action.

**Draw** is the one exception with a drag: press anchors on the **floor-snapped** beat of the row
under the pointer (`floorSnappedBeatAt` — the grid line at or *before* the click, so the clip lands
in the cell it was aimed at) on a **Midi** row only (an Audio row's content is an imported asset and
an Automation row's is breakpoints — neither is something a pencil can draw); drag grows a ghost
rect to the **ceil-snapped** beat under the pointer (`ceilSnappedBeatAt`, floored at one snap
division — or `kMinClipLengthBeats` with Snap off — so a drag that has entered a cell always
includes the whole cell); release commits. A press that never dragged falls back to
`createMidiClipAt` — the SAME one-bar-clip authoring gesture the empty-lane double-click uses (see
**Adding content** below), so a plain pencil click and a plain double-click land in the same place.

**Split/Draw preview seams — repaint only on a state change.** Both previews are gated on the
previewed STATE changing, never on raw pointer movement, mirroring the paint-count discipline
`TimelinePlayheadOverlay::requestRepaintStrip` established: `updateSplitPreview(pos)` recomputes the
hovered clip + snapped beat, compares against the cached pair, and only then calls the virtual
`requestToolPreviewRepaint(region)` with the union of the old and new preview rects — pointer
movement inside one snap cell over the same clip costs zero repaints. `updateDrawGesture`/
`commitDrawGesture` follow the identical rule for the Draw ghost rect. `requestToolPreviewRepaint`
is the ONE seam both previews cost through (`getSplitPreviewForTest()` / `getDrawGhostRectForTest()`
expose the state itself, so a test can assert on it without decoding pixels) — a test subclass
overrides it and counts, the same pattern `PianoRollComponent::requestRepaintPreviewStrip` and the
playhead overlay's own seam use. `mouseEnter` re-applies the active tool's cursor and `mouseExit`
drops the Split preview, so a hover line never survives the pointer leaving the lanes.

**Relink audio….** Offered whenever the clicked clip's `assetRef` is non-empty, whether the
asset currently resolves (a plain re-point) or is missing (see `docs/architecture.md`'s
asset-management section for the missing-asset placeholder this same field drives). Unlike the
three actions above, this is a plain callback (`onRelinkAudioRequested`) rather than a
`ClipContextChoice` — it needs a host `juce::FileChooser` and a `synth::AssetManager` import,
neither of which `TimelineClipLaneArea` has. `MainComponent::promptRelinkClipAsset` opens the
dialog and calls `relinkClipAsset(id, chosenFile)`; the headless path,
`MainComponent::relinkClipAssetForTest(id, chosenFile)`, calls the same function directly and
never goes through the menu (or `showMenuAsync`) at all. The import lands in the current bundle's
`Audio/`, or — with no bundle yet — the app-data `Recordings/` convention takes use; every
other clip that shared the OLD ref is rewritten alongside the clicked one, as ONE undo step.

**Adding content.** Recording and the AI tools are not the only ways in: both gestures below author
content directly, and each is ONE undo step.

*Double-click empty lane space.* The row under the pointer decides what happens. A **Midi** row gets
a new clip at `floorSnappedBeatAt(x)` — the snap grid line at or *before* the click, never after it,
so the clip lands in the cell it was aimed at — one bar long (the transport's time signature, 4
beats with no transport), auto-named `"Clip N"` from the row's clip count, selected, and then fired
through the SAME `onClipDoubleClicked` hook a clip double-click uses, so the user lands straight in
the piano roll ready to draw notes.

**Except when it spans the loop locators.** `locatorSpanForDoubleClick(clickedBeat)` is the pure rule:
when the `timelineDoubleClickSpansLocators` preference is on (**default ON**), a transport is
installed, the locators define a real span (`loopEnd > loopStart`) AND the clicked beat falls inside
`[loopStart, loopEnd)`, the clip is authored at `startBeat = loopStart` with
`lengthBeats = loopEnd - loopStart` instead. Every other case keeps the one-bar behaviour exactly.
Four details that are decisions, not accidents:

- The beat tested is the **RAW, unsnapped** beat under the pointer. Snapping first could push a click
  that landed outside the span into it (or the reverse), and the question being asked is where the
  user actually clicked.
- The span is **half-open**: a click exactly ON the right locator is a click in the bar *after* the
  loop, and authoring a locator-length clip there would run past where the user pointed.
- Looping being switched **off** does not matter — the locators are a *range*, and a degenerate span
  (`end <= start`) is also what "no locators set yet" looks like, so both fall back to one bar.
- Only the double-click path asks. The **Draw tool** goes through the same `createMidiClipAt` but
  passes no length override, because a pencil drag states its own length (see `commitDrawGesture`).

The preference is read **at use time** through the duplicated-string-key idiom (the same one
`timelineLoopSelectionArms` uses), from a non-owning `juce::ApplicationProperties*` the panel forwards
in `setApplicationProperties`. Nothing is cached and nothing is pushed live: flipping the toggle in
`Settings → Preferences → "Timeline: double-click inside the locators spans them"` takes effect on the
very next double-click. A null properties pointer (or no user-settings file) means "take the default",
which is ON.

An **Audio** row asks for a file through `audioFileChooser_` — a
`std::function` seam defaulting to a real async `juce::FileChooser` filtered by
`juce::AudioFormatManager::getWildcardForAllFormats()`, which a test replaces with a lambda that
answers synchronously (`juce::FileChooser`, like `showMenuAsync`, never runs in a test process) — and
reports the choice through `onAudioFileDropped`, exactly as a file drop does. An **Automation** row,
and a double-click below the last row, do nothing.

*OS file drag-and-drop.* `TimelineClipLaneArea` is a `juce::FileDragAndDropTarget`.
`isInterestedInFileDrag` is EXTENSION-based (`AudioFormatManager::findFormatForFileExtension`) — no
dragged file is ever opened — and true when at least one file qualifies. `fileDragMove` highlights
the **audio** row under the cursor with an accent wash, repainting only the rows involved and only
when the row actually changes (`fileDragExit`/`filesDropped` clear it); a MIDI row, an automation row
and the space below the last row neither highlight nor accept a drop. `filesDropped` reports the
FIRST readable audio file (a multi-file drop makes one clip, not N).

*Who imports.* Neither gesture imports anything here: `onAudioFileDropped(TrackId, snappedBeat,
File)` hands the decision outwards and `MainComponent::importAudioFileToClip` does the work, for the
same reason **Relink audio…** does — the lane area owns no `AssetManager` and no bundle root. That
method reuses `relinkClipAsset`'s policy verbatim: a saved project imports into the bundle's `Audio/`
(`AssetManager::importAudioFile`), and an unsaved one into the app-data `Recordings/` convention
`chooseTakeFiles()` writes takes into, so `saveToFile`'s existing `adoptRecordingsAssets` sweep moves
it into the bundle on the first save. The clip's length is the file's own duration in beats
(`audioFileLengthInBeats`, at the transport's current bpm), `sourceStartSeconds` is 0, and the clip +
its asset binding are batched into ONE `recordTimelineChange`. A failed import (unreadable or
non-audio) reports through the status bar and mutates the document not at all. Headless seam:
`MainComponent::importAudioFileToClipForTest`.

*Empty-row hint.* A row with no clips paints one dim line, centred, straight from doc state — no
timer, no animation, `Theme::Colors::textMuted`: **"Double-click to add a clip — or arm (R) and
record"** on a Midi row, **"Drop an audio file — or arm (R) and record"** on an Audio row, and
nothing on an Automation row (its content is breakpoints, authored in the automation strip). The line
is dropped rather than truncated when the row is shorter than 24 px or narrower than the text plus
its padding.

**Panel-scoped Delete key.** The lane area grabs keyboard focus on `mouseDown` (same as
`GraphEditor::mouseDown`), so pressing Delete right after a click lands on `TimelineClipLaneArea::
keyPressed` rather than whichever panel had focus before. This is the *local* half of Delete-key
arbitration — the **Keyboard & Focus** section below (§4) formalises the cross-panel rule that
decides which of the graph editor / clip lanes / piano roll a given keypress belongs to in the
first place.

**P = loop the selection** (Cubase's locators-to-selection) rides on that same local half: an
unmodified `P` handled in `TimelineClipLaneArea::keyPressed`, **not** a `ShortcutManager` command,
for exactly the reason Delete/Escape aren't (see [`shortcuts.md`](shortcuts.md) and §4 below) —
a bare letter in the app-wide table would fire from any panel that doesn't consume it first. The
lane area owns no transport: `getSelectedClipSpan()` computes `[min startBeat, max endBeat]` across
the selection (any row, ignoring anything unselected) and hands it outwards through
`std::function<void(double startBeat, double endBeat)> onLoopRangeRequested`, which
`MainComponent` wires to `transport.setLoop(start, end, /*enabled=*/true)` — the same
"the lane decides what, the owner does it" division as `onAudioFileDropped`/`onRelinkAudioRequested`
above. Nothing selected (or no owner listening) returns `false` so the key keeps its meaning
elsewhere. The piano-roll surface has no equivalent yet.

Tests: `Tests/TimelineClipLaneTests.cpp` — `ClipSelectionModel`/`clipHitTestMarquee` unit coverage,
pure-geometry tests for `computeClipRect`, and interaction tests driven by hand-built
`juce::MouseEvent`s (same pattern as the ruler tests in `docs/timeline_panel_core.md` §2, in `Tests/TimelinePanelTests.cpp`) against
a bare `TimelineDoc` + `AppUndoManager` + `TimelineClipLaneArea`, no `MainComponent` needed. The
authoring gestures are split across three files, each covering the half it owns: the lane area's
(group 7 there — snapping, one-bar length, one undo step, the injected chooser, drag interest and the
row highlight, the hint text and its paint), the panel's (`Tests/TimelinePanelTests.cpp` group 6 — a
new clip really opens the piano roll), and the import's (`Tests/AssetManagerTests.cpp` group 2 —
saved-bundle vs `Recordings/` destination, length from the file, failure mutating nothing).
`Tests/TimelineClipEditingTests.cpp` covers the edit-tool layer added on top: each tool's
click-acts-immediately behaviour and its empty-space no-op, `findGlueTarget`'s gap-bridging,
Alt-copy vs plain move, the cross-track kind check (legal drop, illegal drop clamping the whole
group back to 0), the split/draw preview seams' repaint-only-on-change discipline (via a counting
subclass overriding `requestToolPreviewRepaint`), the inline rename's commit/cancel/focus-loss
paths, and the clip clipboard's audio-field round trip (see the clip clipboard subsection below).

## 2. Piano Roll

`Source/UI/PianoRollComponent.h/.cpp` (`synth::ui::PianoRollComponent`) is a minimal per-clip note
editor shown INSIDE the timeline panel's lanes region — no separate window. Backed by
`synth::ui::NoteSelectionModel` (`Source/UI/NoteSelectionModel.h`), `ClipSelectionModel`'s sibling
keyed on `synth::NoteId` with the identical add/remove/toggle/setSelection/retainOnly contract,
plus a `noteHitTestMarquee` free function mirroring `clipHitTestMarquee`.

**Entry/exit.** Double-clicking a clip in `TimelineClipLaneArea` fires its `onClipDoubleClicked(ClipId)`
callback, which `TimelinePanelComponent`'s constructor wires to `openPianoRoll(ClipId)`. That call:
hides `clipLaneArea_`, shows `pianoRoll_` (same rect — see Z-order below), and calls
`PianoRollComponent::openClip`, which frames the clip: the pitch scroll centres on its median note
pitch (60 for an empty clip), and the roll's own horizontal mapping is set so the clip's START sits
at the keys column's right edge, zoomed so the whole clip fits the grid width (clamped to
`TimelineViewState`'s pixels-per-beat bounds). A drawn "◀ Clips" back button (top-left of the header strip) and Escape (when
nothing is selected — a first Escape only clears the selection) both call the roll's own
`requestClose()`, which closes itself immediately (so `isOpen()` is accurate even with no owner
wired) and fires `onCloseRequested` — the panel wires this to `closePianoRoll()`, which re-shows
`clipLaneArea_`. If the edited clip disappears from the doc (any mutation, from any path —
`TimelinePanelComponent::timelineChanged()` calls `pianoRoll_.refreshFromDoc()` on every doc
notification, mirroring the clip-lane area's own refresh seam), `refreshFromDoc()` notices the clip
is gone and closes the roll the same way. Panel API: `openPianoRoll(ClipId)` / `closePianoRoll()` /
`isPianoRollOpen()`.

**Z-order.** `pianoRoll_` is added via `addChildComponent` (not `addAndMakeVisible`, so it starts
invisible) right after `clipLaneArea_` and before `playhead_` — so only one of
clip-lane-area/piano-roll is ever visible, and the playhead overlay stays topmost and untouched
either way (same bounds, same `viewState_.beatToX` mapping it always had).

**Vertical layout: toolbar row, then ruler, then note canvas.** While the roll is CLOSED nothing is
unusual — the ruler is the top row of the lanes region and the roll occupies exactly
`gridLanesBounds_`, the clip-lane rect. While it is **open**, the panel reserves
`PianoRollComponent::kToolbarHeight` from the top of the lanes region for the roll's chip toolbar
*before* placing the ruler, and gives the roll a rect spanning the whole unit — toolbar row, ruler
band, canvas. The roll's own `resized()` is the single carve-up:

| Band | Height | Owner |
|---|---|---|
| Chip toolbar | `kToolbarHeight` | the roll (its own chrome) |
| Ruler | `rulerBandHeight_` | **left blank** for `ruler_`, a sibling drawn on top of it |
| Note canvas | the remainder | the roll (keys-column gutter + grid) |

The middle band is what puts the chrome **above** the ruler rather than sandwiched between the ruler
and the notes — chrome wedged between two content rows was the layout bug. The owner pushes the
ruler's real height in via `setRulerBandHeight()` (0 by default, which collapses the layout back to
toolbar-then-canvas for a bare roll or a test), and the panel calls `ruler_.toFront(false)` while
open because the roll was added to the panel *after* the ruler and would otherwise paint over it.

**`canvasTop()` is the one seam** every grid/row/hit-test coordinate reads — `kToolbarHeight +
rulerBandHeight_`. Introducing it is what let the ruler move in between the chrome and the canvas
without a y-offset having to be found and corrected at twenty separate call sites, and it is why a
standalone roll (`rulerBandHeight_ == 0`) has bit-for-bit the pre-toolbar-row geometry.
`openPianoRoll`/`closePianoRoll` both re-run `resized()`, because `isOpen()` is what the panel's
carve-up branches on. The playhead overlay's skipped region is the union of the ruler and the **roll's
own rect** (not `gridLanesBounds_`), so the overlay does not draw its line across the chips. Adding a
second context toolbar later is one more `removeFromTop` in that one carve-up plus a constant — no
other y-coordinate in the file moves. Pinned by `PianoRollLayoutTest`.

**Coordinate system — the roll owns its own horizontal mapping.** The keys column is a real 44 px
GUTTER (`kKeysColumnWidth`): `PianoRollComponent::beatToX(absBeat)` is `leftGutterWidth() + (beat -
firstVisibleBeat) * pixelsPerBeat` against the roll's OWN `TimelineViewState` member (`rollView_`),
so `x == leftGutterWidth()` is the first visible beat and the clip's opening bar is reachable. (It
previously was not: the column was an opaque strip painted OVER the leftmost 44 px of the shared
mapping, with clicks there ignored — the first bar of a clip parked at x == 0 was unclickable and
half-hidden.) `leftGutterWidth()` — `kKeysColumnWidth` alone, or `kScalePanelWidth +
kKeysColumnWidth` (170 + 44 px) while the Scale Assist panel (below) is open — is now the single
function every place that used to hardcode `kKeysColumnWidth` as "the grid's left offset" goes
through instead, so the panel opening/closing can never leave one call site reading the old offset
while another reads the new one. The roll's zoom and scroll are its own; the panel-wide
`TimelineViewState` is still shared, but for exactly ONE thing — the **snap division**
(`snapBeat`/`divisionBeats`), so the roll's gridlines, its snapped edits and the panel's snap
selector can never disagree.

**The keys column is a virtual keyboard.** Pressing a key there auditions that pitch through the
SAME `onAuditionNote` path a note click uses (see **Note audition** below), so it reaches exactly the
destination modules the track plays through, with exactly the same no-stuck-note guarantees — round 1
deliberately left this a no-op ("no virtual-keyboard preview in v1") and this is that gap closed.
Mouse-down on a key is the note-on, dragging up or down the column re-articulates **once per key**
(gated on the pitch actually changing, so sliding inside one key costs no MIDI and no repaint), and
mouse-up is the note-off. A drag that strays sideways off the column is clamped to the column's own
y-range rather than abandoned: a finger sliding down a keyboard drifts horizontally all the time, and
dropping the gesture there would leave the note held with no way to release it. Velocity is a fixed
`kKeysColumnVelocity` (102, ~0.8 of full scale) — a virtual keyboard has no velocity sensor, and
previewing everything at 127 misrepresents how the patch sounds under the notes being written.

The held key paints in the theme's `toolActive` token — the same "this control is switched on" colour
the edit-tool strip, the follow-playhead button and the roll's own Snap chip use — over the key's OWN
rect, so a black key lights up across just its narrower flush-left area and the white showing through
beside it stays white. `keyFill` itself is reassigned rather than only the drawn colour, so
`labelColourFor` still contrasts the note name against what is actually underneath it. Repaints are
confined to the one or two key rows involved (`keyRowRect`), never the column.

A keys press starts **no document gesture**: no drag mode, no selection change, no undo step. It is
tracked by its own pair of members (`keysColumnPressing_`/`keysColumnPitch_`) rather than a
`DragMode`, precisely so none of the note-gesture machinery can mistake it for an edit — `mouseDrag`
handles it and returns before any of that runs. `endKeysColumnPress()` only drops the *pressed paint*;
the note-off comes from `stopAudition()`, which `mouseUp` and every cancel path
(`openClip`/`closeRoll`/`visibilityChanged`/a tool switch/the destructor) already call
unconditionally, so there is exactly one owner of the release. Pinned by `PianoRollKeysColumnTest`
plus two `PianoRollAuditionIntegrationTest` cases that drive the real panel down to the host recorder.

**Piano-style keys column + key labels.** The keys column paints alternating white/black-key row
tints from `colors.pianoKeyWhite`/`pianoKeyBlack` (see [`theming.md` §2](theming.md#2-token-reference))
rather than the earlier plain `bg1`/`surfaceHi` alternation, so the column reads as an actual
keyboard. `PianoRollComponent::KeyLabelMode` (`AllNotes` default, `OctavesOnly`) controls label
density: `AllNotes` labels every visible key row (subject to the readability floor below);
`OctavesOnly` labels only the C rows, matching the octave-label-only behaviour the column always
had. `keyLabelFor(pitch, mode, rowHeightPx)` is the single, static, headless-testable decision
point both the paint call site and tests assert on directly — an empty string means "draw no label
at all". **Below a 9 px row height, every mode collapses to C-only labels** — `OctavesOnly`
behaviour, in effect, regardless of what the toggle says — since a name doesn't fit a row that
short and a half-drawn label is worse than none. The mode is set via
`setKeyLabelMode()`/`getKeyLabelMode()`, persisted under the preference described just below.

**"pianoRollKeyLabels" preference.** `PreferencesSettingsTab` gets one new toggle ("On labels every
key in the piano roll's keys column. Off labels only the Cs."), backed by the string key
`"pianoRollKeyLabels"` (`"all"` default / `"c"`), read by
`TimelinePanelComponent::reloadPianoRollAppearancePrefs()` — called once from
`setApplicationProperties()` and again from `MainComponent::changeListenerCallback` on every
settings-file write (the same "re-read on notify" treatment `applyNaturalScrollingPreference`/
`applyZoomScrollPreference` already get), so an Appearance/Preferences-tab edit shows up in an
already-open roll immediately, no restart needed. `"all"` matches `KeyLabelMode::AllNotes`'s own
default, so an install that never opens the tab is unaffected.

**Playhead delegation** is the consequence, and the rule. `TimelinePlayheadOverlay` maps beats
through the SHARED view state, so inside the roll's rect its x is simply wrong. Instead of adding a
second timer, the overlay grew a delegation seam: `TimelinePlayheadOverlay::LocalPlayheadClient`
(`isLocalPlayheadActive()` + `setPlayheadBeat(absBeat)`), plus `setLocalPlayheadRegion(rect)` — the
client's rect in the overlay's own coordinates, set by `TimelinePanelComponent::resized()` (the
sibling offset is only known there). While a client is active the overlay:

- confines `paint()` and every repaint strip to `getSharedRegion()` — everything **above** the
  client's rows, i.e. the ruler strip only; and
- hands the client the DRAWN beat (position minus output latency, clamped ≥ 0) on **every**
  `refreshLine`, *before* its own "did my x move?" gate — a differently-zoomed mapping can move on a
  frame where the shared one didn't.

`PianoRollComponent` then runs the identical confinement contract on its own side: its
`requestRepaintStrip(Rectangle<int>)` is the paint-count seam (same pattern, same test style as
`TimelinePlayheadTests.cpp`), a beat whose rounded x is unchanged requests **nothing** (so a stopped
transport costs zero repaints), a moved beat requests the union of the old and new strips clipped to
the grid rect, and the FIRST beat after an open costs exactly one strip (there was no line on screen
yet). The line is drawn in `paint()` before the keys column and the header, so both clip it exactly
the way they clip a note that has scrolled off to the left. There is still exactly ONE playhead
timer in the whole panel — the overlay's, playing-only.

Vertically, pitch maps at `pixelsPerSemitone_` px/semitone (default `kPixelsPerSemitone` = 10,
Cmd+Shift+wheel scales it within `[kMinPixelsPerSemitone, kMaxPixelsPerSemitone]` = `[4, 40]`);
`firstVisiblePitch_` names the HIGHEST pitch drawn at the grid's top row, clamped to `[0, 127]`. A
20 px header strip sits above both the keys column and the grid, housing the back button and a "Q"
(quantise) button — both plain `juce::Path`/text shapes, never a Unicode glyph through a themed font
(the same "draw it, don't asset it" rule `TimelineTransportBar`'s `GlyphButton` follows, and the
reason `TimelineViewState::divisionBeats` below and every label here go through `AppLookAndFeel` —
see the CLAUDE.md font-swap invariant).

**Gridlines** are drawn from state alone, faintest level first so a bar line always wins a shared
pixel — and from the SAME `GridLineLevel`/`gridLineColourFor`/`gridLevelIsReadable` policy
(`Source/UI/TimelineClipLaneArea.h`, see `docs/timeline_panel_core.md` §2) the clip lanes paint their own grid from, so the
two surfaces can never disagree on what's visible or how dark it is at a given zoom: the current
snap division (`GridLineLevel::Subdivision`, alpha 0.28 — only when it is finer than a beat,
`Snap::Off` has no division at all), beats (`Beat`, 0.50), bars (`Bar`, 0.85), each lifted halfway
toward the background's contrasting colour before that alpha applies. Each level is dropped
entirely when its spacing falls under `kMinGridLinePixels` (3 px, `gridLevelIsReadable`), the same
adaptive-density idea the panel's own grid uses. `visibleLineRange(spacingBeats)` is the ONE range
computation `paintGridLines` and the `getGridLineCountForTest` seam both walk, so the assertion can
never drift from the paint. Changing the snap selector repaints the roll (`TimelinePanelComponent`'s
`snapCombo_.onChange`); no timer.

**Rendering.** Grid-row backgrounds alternate white/black-key tint (`colors.bg1` / `colors.
surfaceHi` — the keys column itself uses `colors.pianoKeyWhite`/`pianoKeyBlack`, see above), a
per-key or C-only label in the keys column per `KeyLabelMode` (mono font via `juce::Font::
getDefaultMonospacedFontName()`, resolved to the theme's mono family by `AppLookAndFeel::
getTypefaceForFont` — never a raw family string), and the clip's span outside `[clipStart,
clipEnd)` dimmed. Notes are rounded rects whose fill/border colours come from the single resolver
`synth::ui::resolveNoteColour()` (`Source/UI/NoteColour.h` — see [`theming.md` §12](theming.md#12-note-colours)):
velocity brightens the fill, a note the active scale (Scale Assist, below) flags as outside it
gets `noteOutOfScale` regardless of any per-pitch-class override, and selected notes get a
`noteSelected`-coloured border. Repaints happen only on doc/listener refresh, interaction, and
view-state changes — no timer.

**Gestures (Select tool)** — everything below is `EditTool::Select`'s table; the other five tools replace it entirely (see **Edit tools** further below) (each previews locally — a member delta/length/velocity read back by `paint()` through
`effectiveGeometryFor()` — and commits ONCE via `AppUndoManager::recordTimelineChange` on mouse-up,
so a multi-note move/scrub/delete is one undo step):

| Gesture | Effect |
|---|---|
| **Single click on empty grid** | DESELECTS (click-through). Creates nothing, writes no undo step. |
| **Double-click on empty grid** | Creates ONE note: pitch from the row, start snapped, length exactly **one snap division** (1 bar quantise → a 1-bar note; 1/4 → a quarter; 1/16 beat when Snap is Off), velocity 100, channel 1. Selected, one undo step. Snapping up past the clip's end steps back one division rather than creating nothing. |
| **Drag from empty grid** | Marquee (intersection hit-test) — a plain drag REPLACES the selection; Shift or Cmd/Ctrl makes it additive. A press that never crosses the drag threshold is still just the deselect click above (same deferred-click promotion the clip lanes use), and there is nothing to retrain because the roll has no drag-to-pan (scrolling is wheel-only) |
| Click a note | Selects it; **Shift** toggles it in/out of the selection, **Cmd** adds it (never removes — the drag that may follow scrubs the whole selection) |
| Drag a note's body | Moves it + every other selected note, by one shared snapped beat delta and one shared semitone delta |
| Drag within 5 px of a note's right edge | Resizes (trims) **every selected note** by one shared length delta, Snap-quantised. The grabbed note takes the pointer's own (snapped, division-floored) length; every other snapshotted note gets its OWN original length plus that delta, floored individually at `kMinNoteLengthBeats` — see **Multi-note resize** below. Grabbing a note that is not in the selection replaces the selection with it, so it resizes alone |
| **Cmd**+drag on a note's right edge | The same resize with the grid BYPASSED: the note's end follows the RAW beat under the pointer and the floor drops to `kMinNoteLengthBeats`, for continuous sub-division trimming. Latched at mouse-down (`resizeUnquantized_`), never re-read from the live modifiers — a gesture must not change meaning half way through because Cmd was released. Composes with the multi-note rule: the delta the group takes is unquantized too. Tested BEFORE the Cmd velocity-scrub row below, since a right-edge hit is the more specific of the two Cmd gestures |
| Mouse-DOWN on any note | **Auditions it** — see **Note audition** below. Every note-hit branch sounds the note (select, move, resize, velocity scrub, even a Shift+click that deselects it), so "clicking a note plays it" never depends on which modifier is down |
| **Double-click a note** | Deletes it, one step (standard DAW idiom — the mirror of double-click-to-create) |
| Delete / Backspace | Deletes the selection, one step; returns `false` when the selection is empty |
| Escape | Clears the selection; closes the roll when nothing is selected |
| **Cmd**+drag on a note's BODY | Moves it (and the rest of the selection) with the grid **BYPASSED** — the note follows the raw beat under the pointer. One modifier, one meaning: Cmd on a note says "do this smoothly", whichever part of it you grabbed (body -> unsnapped move, right edge -> unsnapped resize). Latched at mouse-down (`moveUnquantized_`), never re-read from the live modifiers |
| **Cmd**+CLICK on a note (no drag) | Additive-select **toggle**: adds an unselected note, removes an already-selected one. Cmd+click and Cmd+drag are indistinguishable at mouse-down, so the note is ADDED immediately (the move needs it in the selection) and mouse-up completes the toggle *only if nothing moved* — the same deferred-classification trick `pendingEmptyClick_` uses for the empty-grid press. A Cmd+drag therefore never deselects what it is moving |
| **Option**+vertical-drag on a note | Scrubs velocity, ~1/px, clamped to `[1, 127]` independently per note (multi-selection scrubs all by the same delta). **Moved here off Cmd**, which now means "unsnapped" on both halves of a note; one modifier meaning "smooth" on the right edge and "change the volume" two pixels to its left was the thing worth fixing. Option is free for a mouse drag on this surface — the roll's other Option bindings are KEY chords, and a modifier may mean different things to the keyboard and the mouse without ambiguity |
| **Quantise** chip (or the bare `Q` key) | **One-shot quantise**: snaps the SELECTED notes' starts to the chosen division (per-note `moveNote`, one mutation lambda — `TimelineDoc::quantiseNotes` has no note-subset overload); with **nothing selected it quantises every note in the clip** via `quantiseNotes` directly. Reads `divisionBeatsRaw()`, so it works even while snap is off (cleaning up free-hand notes is its whole point). Flashes on every press, and writes NO undo step when the clip is already quantised (`recordTimelineChange` drops no-op mutations) |
| **Snap** chip (or the `J` key) | **Toggles grid magnetism** — flips the shared `TimelineViewState::snapEnabled`, so it switches off/on everywhere (roll, clip lanes, ruler) while the chosen division survives underneath. The key is the SHARED `timelineSnapToggle` (the timeline's own snap key moved off Q to J too), not a piano-roll duplicate of it. The only chip here that paints lit for snap. A view-state toggle, never a document edit — no undo step. Fires `onSnapToggled` so the panel persists the choice and repaints the other grid painters. **Magnetism only — the grid stays drawn** (see *Snap is magnetism, not visibility* below) |
| **Quantise Pitches** chip (or `Option+Shift+Q`) | **Quantise pitches into the scale**: `quantisePitchesToActiveScale()` snaps the selected notes' PITCHES (or every note in the clip when nothing is selected) via `MusicalScale::snapPitch`, leaving starts and the selection untouched. An ACTION chip, never lit — but painted dimmed (`isPitchQuantiseEnabled()`) when it would do nothing: no scale chosen for this clip, or an empty clip. A click with no scale is silently inert; the KEY falls THROUGH (`keyPressed` returns `false`) in the same case. **This chip is its ONLY entry point** besides the key — the duplicate button inside the Scale Assist panel is gone |
| **Show Only Scale Notes** chip (or `Option+S`) | Toggles the pitch-ROW filter for the open clip (`toggleScaleFilter()`): out-of-scale rows collapse out of the grid, and ↑/↓ start stepping by scale degree. A toggle, so it paints lit; dimmed with no scale chosen (the flag is still remembered — arm it first, pick the scale second). Shares ONE piece of state with the Scale Assist panel's checkbox |

**Header chips.** **Six** drawn chips (not child `juce::Button`s — they are painted shapes hit-tested
by position, `HeaderButtonId`), left to right: **"Clips"** (back), **Snap**, **Quantise**, **Quantise
Pitches**, **"Scale"**, **Show Only Scale Notes**. Each is a `juce::Rectangle<int>` member carved in
`resized()` and resolved through the single seam `headerButtonBoundsFor(which)`, which
`updateHeaderButtonHover()` and `paintHeader()`'s hover wash BOTH read — compute them separately and
the lit rect drifts from the clickable one. Every chip does exactly ONE thing on a plain click; there
are no modifier variants left in the header at all (snap and quantise used to share one chip that
way, which is the ambiguity the split removes). The GAPS carry meaning: 4 px between groups, 2 px
within one, so "snap + the two quantise verbs" reads as a cluster and "scale + its row filter" as
another. Only **Snap** and **Show Only Scale Notes** are toggles, so they are the only two that ever
paint lit; the rest are actions and merely dim when they would be a no-op.

**Four of them carry drawn vector glyphs rather than letters**, because letters were the problem:
"Q" for the grid toggle was the *same letter the timeline binds to snap*, and a second "Q" beside it
for pitch-quantize told the user nothing about which was which. All four are pure `juce::Path` /
`fillRect` drawing against the chip's rect, in a colour the caller derives from the fill it actually
painted (so they stay legible on resting, hover and lit fills in every theme), and no font or
`IconLibrary` entry is involved — the same "draw it, don't asset it" rule the back arrow follows:

| Chip | Glyph | Why it reads |
|---|---|---|
| Snap | A **magnet** (half-annulus horseshoe + two poles) | The universal magnetic-snap mark (Cubase, Blender, CAD). A distinct silhouette at 16 px, which a letter sharing its shape with the timeline's own snap key was not |
| Quantise | Two small blocks landed flush on two faint **vertical** gridlines, vertically staggered | The axis IS the meaning: this verb moves notes horizontally in time, so the grid it snaps to is vertical. Staggering the pair reads as two notes rather than one bar |
| Quantise Pitches | A **note head** on the lowest of three faint **horizontal** rows, with a down arrow pushing it there | The same "snapped onto the grid" idea rotated 90°, which is exactly the difference between the two verbs — so they are tellable apart without the tooltip |
| Show Only Scale Notes | A **funnel** | The one mark that reads as "filter" everywhere. Deliberately not an eye (the rows are *removed from the row mapping*, not merely hidden) and not a keyboard (indistinguishable from the keys column two pixels below) |

"Scale" keeps its word: it is the one label here naming a NOUN (a panel) rather than a verb, and a
glyph for "the scale picker" would be a guess. Every tooltip is rebuilt per query through
`synth::shortcutHintFor` (`snapTooltipText()` / `quantiseTooltipText()` /
`quantisePitchTooltipText()` / `scaleTooltipText()` / `scaleFilterTooltipText()`), so a rebind shows
up the very next time it is asked for, with no cache and no listener.

**Snap is magnetism, not visibility.** `currentGridBeats()` (snap-aware, `0.0` while the switch is
off) is read ONLY by code that snaps an edit; `drawnGridBeats()` (`divisionBeatsRaw` — the chosen
division regardless of the switch) is what `paintGridLines` draws. They used to be the same function,
so turning snap off *erased the sub-beat gridlines* — backwards, since free-hand editing is exactly
when the user needs to see the grid they are placing notes against. Only `Snap::Off` (no division
chosen at all) genuinely has no sub-beat level; the beat and bar levels are unconditional either way.
Pinned by `PianoRollGridVisibilityTest`.

**Show Only Scale Notes has ONE writer.** The header chip, `Option+S` and the Scale Assist panel's
checkbox are three views of one flag, and all three route through `PianoRollComponent::
toggleScaleFilter()`: it writes the open clip's `ClipScaleMemory`, pushes the scale context, and
reflects the new value back into the panel via `setSelection()` (which fires no callback by
contract — it is a reflection, not an edit — so it cannot loop back in). The flag is remembered per
clip alongside the scale, and is deliberately NOT gated on a scale being chosen: arming it first and
picking the scale second is a real order of operations, and `isRowFilterActive()` (flag AND a real
scale) is the separate question anything behavioural asks.

**↑/↓ step by ROW, which makes them scale-aware for free.** `transposeSelectedNotesByRow(±1)` walks
`visiblePitches_` and resolves each note through the same `rowShiftedPitch` seam a Move drag's
vertical half uses, with ONE shared row delta clamped so the group stays in range (never per-note
clamping, which would reshape a chord). With the filter ON the row set IS the scale, so a step is the
next **scale degree** — C→D is two semitones, E→F is one, and an arrow key can no longer strand a
note on a hidden out-of-scale row. With the filter OFF `visiblePitches_` is all 128 and a row step
*is* a semitone step, so chromatic behaviour is preserved **by construction** rather than by a
parallel code path that could drift. The octave actions stay on `transposeSelectedNotes(±12)`: an
octave is twelve semitones by definition, not twelve degrees.

**Note audition — "clicking a note plays it."** `PianoRollComponent::onAuditionNote(int pitch, float
velocity01, bool on)` fires `true` on a mouse-down that hits a note (Select tool only — an Erase /
Mute / Split / Glue click is not a request to hear the note it is about to change), `false` on the
release, and a **noteOff/noteOn PAIR** whenever a Move drag carries the grabbed note onto a different
pitch, so dragging up a scale sounds like dragging up a scale. Only the grabbed note sounds, never
the whole multi-selection, and the retrigger is gated on the pitch actually changing (resolved
through the same `rowShiftedPitch` the drawn preview uses), so a horizontal drag inside one row costs
nothing.

The roll deliberately knows nothing about the graph, the transport, or which modules a track plays
through — it emits a pitch, a normalised velocity and an on/off edge, which is what keeps the
surface headless-testable (every audition test is a callback count). The wiring is three hops, and
each one only adds what it alone knows:

1. `TimelinePanelComponent` resolves WHICH TRACK from the edited clip (`TimelineDoc::
   getTrackForClip`) — the roll's callback carries no clip/track on purpose — and calls
   `TrackHeaderHost::auditionTrackNote(trackId, pitch, velocity, on)`.

   **The two edges are handled asymmetrically, and that asymmetry IS the correctness argument.** A
   note-ON is disposable: no host, no doc, roll closed, or an unresolvable track all just mean
   silence. A note-OFF is not — an audition note is exempt from every positional flush downstream (see
   below), so a dropped off hangs the note until the node is bypassed. So the track resolved for the
   ON is **latched** (`auditionTrackLatch_`), and the matching OFF is routed to that latched track
   **unconditionally**, never re-resolved. Between the two edges the edited clip can be deleted, the
   roll can close, or a *different* clip can open — re-resolving would drop the off in the first two
   cases and send it to the **wrong track** in the third. The latch holds at most one note (the roll
   sounds one at a time and always emits its own off before a retrigger's on) and is cleared on the
   off; an unmatched off — one whose ON was refused because the roll was closed — forwards nothing,
   since a stray off could cut a timeline note of the same pitch short.

   The same hazard is why `PianoRollComponent::closeRoll()` calls `stopAudition()` **before** clearing
   `clipId_`, matching `openClip()`'s order: clearing first made `isOpen()` false while the teardown
   note-off was still in flight, and the panel dropped it. Both halves are pinned by
   `PianoRollAuditionIntegrationTest`, which drives the whole chain through a real
   `TimelinePanelComponent` and a recording `TrackHeaderHost`. That distinction matters — the
   roll-only audition tests wire `onAuditionNote` straight to a recorder, so they cannot see this half
   of the contract at all, which is exactly how the bug got in.
2. `MainComponent` (the only object that owns the doc and the graph at once) resolves
   `track->bindingUuid` → the live node via `findNodeByUuid`, downcasts to
   `TimelineMidiSourceModule`, and pushes.
3. `TimelineMidiSourceModule::pushAuditionNote()` (Core) hands the event to the audio thread.

**That last hop is the whole point of the design.** A track's MIDI destinations are graph connections
whose SOURCE is its Track In node's MIDI output (`MainComponent::setMidiDestinationConnected`), so a
note emitted from that node reaches exactly the modules the clip's notes reach — by construction,
with no second copy of the destination list to keep in sync. Injecting anywhere else (e.g.
`AudioEngine`'s own `midiMessageCollector`) would reach the global MIDI-in path instead, and the
preview would play the wrong instrument or nothing.

The hand-off is a fixed-capacity `juce::AbstractFifo` of POD events plus a slot array —
`TransportService`'s command-FIFO idiom — drained once per block at the top of `processBlock`: no
lock, no allocation and no logging on the audio thread, and a full FIFO **drops** the event rather
than blocking. An auditioned note is parked in the module's existing held-note table with an
**infinite end beat**, which is what keeps `emitRange`'s end-beat release scan from ever touching it
and what distinguishes it from a timeline note. Two consequences worth stating:

- **A preview survives a stop, a locate and a loop wrap.** Those flushes went through
  `flushTimelineNotes()` (non-audition notes only): they say something about where the transport is,
  and audition is not on the transport's clock. It is not gated on track mute/solo either — a preview
  is a MONITOR path, like clicking a key on a MIDI Keyboard module.
- **Bypass is the one thing that does take it with it**, because a bypassed source emits nothing and a
  queued note-off would never be delivered. The first bypassed block releases whatever is held
  (`flushActiveNotes()`, which releases everything) and anything pushed while bypassed is discarded
  rather than replayed on resume.

On the UI side the contract is the one a stuck note would violate: **exactly one `false` follows
every `true`**, emitted from mouse-up, from a gesture cancelled by a tool switch, from
`openClip`/`closeRoll`, from `visibilityChanged` (a component hidden mid-drag never gets a mouse-up —
the sharpest case) and from the destructor. `stopAudition()` is a no-op when nothing is sounding,
which is what lets every one of those paths call it unconditionally.

**Multi-note resize, and notes past the clip end.** The resize gesture snapshots `resizeNotes_` at
mouse-down exactly the way the Move and velocity-scrub previews snapshot `dragNotes_`, and previews
through one shared `previewLengthDelta_` (see the gesture table above for the per-note floor rule).
`resizePreviewLengthFor(origin)` is the single function the live preview and the mouse-up commit both
go through, so what is written can never disagree with what was drawn; the commit is ONE
`recordTimelineChange` however many notes it touches.

The resize has **no clip-length clamp** — that is what makes dragging a note out past the clip's end
possible at all. On mouse-up, if any resized note now ends past `clip->lengthBeats`, the roll raises
`promptExtendClipToFitNotes(clipId, maxEnd)`: an **async** `juce::AlertWindow` (never a modal loop —
the mouse-up is still unwinding) guarded by a `Component::SafePointer`, offering **Extend** /
**Keep**. It is a `protected virtual`, the same test seam `requestRepaintStrip` is, because a headless
run has no message loop to answer a real alert with; the answer itself is handled by
`applyExtendPromptAnswer(clipId, length, extend)`, factored out of the alert's callback so that the
real answer path is one line there and fully covered by tests here.

**The overrunning CLIP ID is captured at prompt time and carried through the answer** — the answer
never reads the live `clipId_`, which is why `extendClipTo` takes the id as a parameter. A modal
window blocks user *input*, not the message thread: an AI action, an undo/redo or a timer can
`openClip()` a different clip while the alert is up, so re-deriving the target at answer time silently
grew *whichever clip happened to be open* — a direct violation of the "the clip is not grown behind
the user's back" guarantee. A captured id that no longer resolves (the clip was deleted while the
alert was up) is a silent no-op, not a crash and not a resurrection. Pinned by
`PianoRollResizeTest.ExtendAnswerActsOnTheCapturedClipNotWhicheverIsOpenNow` and
`…ExtendAnswerForADeletedClipIsANoOp`.

- **Extend** → `extendClipTo(capturedClipId, maxEnd)` → `TimelineDoc::resizeClip` in its **own** undo
  step, deliberately not merged with the resize that provoked it: the user answered a second
  question, and undo should take them back one answer at a time.
- **Keep** → the notes stay overrunning, and that is safe rather than merely tolerated:
  `TimelineSnapshot::buildFrom` clamps every emitted event's end to the clip's end and drops any note
  whose start is at or past it, so the overrun is **inaudible**. Pinned by
  `PianoRollResizeTest.OverrunNotesAreTruncatedByTheSnapshotSoKeepingThemIsInaudible`.

**Edit tools (Split / Glue / Erase / Mute / Draw).** `handleToolMouseDown(pos)` routes every
non-Select tool exactly the way `TimelineClipLaneArea::handleToolMouseDown` does for clips: Split/
Glue/Erase/Mute act on a single click and hit-test a note first (a click on empty grid with one of
these four held does nothing — no deselect, no marquee). `performSplit(id, pos)` cuts at the
snapped, CLIP-relative beat under the pointer via `splitBeatFor`, which returns `nullopt` (no-op)
unless the cut leaves at least `kMinNoteLengthBeats` on BOTH sides — a split that would leave a
sliver on either side would silently mean "resize to nothing" instead of "split". `performGlue(id)`
absorbs the next note of the **same pitch** via `glueCandidateFor` — the smallest `startBeat` at or
after the clicked note's own end; gaps ARE bridged (the glued note runs from the clicked note's
start to the absorbed note's end), which is what makes gluing a staccato pair into one sustained
note possible at all. `performErase(id)` deletes it. `performMuteToggle(id)` flips `MidiNote::
muted` (`TimelineDoc::setNoteMuted`) — a muted note paints **outline-only** (no fill, dimmed border)
straight from the doc's flag, the note-level twin of the clip lane's dimmed-fill treatment; a note
inside a muted clip already contributes nothing to the run (`TimelineSnapshot` excludes the whole
clip), so a note's own mute only matters inside an unmuted clip. **Draw** anchors on the
floor-snapped beat of a mousedown, grows a length preview on drag (`getDrawPreviewLengthForTest()`),
and on release either creates the dragged-length note or — for a press that never dragged — falls
back to the SAME one-note-per-division gesture the Select tool's double-click-on-empty-grid
performs. The Split tool's hover preview (the clip-relative cut beat under the pointer) repaints
only through its own seam, `requestRepaintPreviewStrip` (see below), never the playhead's
`requestRepaintStrip` — a hover that repainted the playhead's strip would be a bug, not a rounding
difference, which is why the two are separate virtuals a test can count independently.

**Note clipboard.** `PianoRollComponent` owns its OWN clipboard (`ClipboardNote`, distinct from
`TimelineClipLaneArea`'s clip clipboard) as a MEMBER — it deliberately outlives `openClip()`, so
"copy here, open another clip, paste there" works. Each entry stores its offset **relative to the
earliest selected note** (not an absolute or clip-relative beat), which is what lets a copied block
survive being pasted into a different clip at a different position: the block keeps its internal
shape and only its anchor moves. Every field a note carries is captured, `muted` included — a muted
note pastes back muted, the same way a split or a duplicate carries the flag.

- `copySelectedNotes()` captures the selection; `canPasteNotes()` is true only with a non-empty
  clipboard AND an open clip (a roll with nothing open has nowhere to put the block).
- `pasteNotesAtPlayhead()` anchors the block at the snapped, CLIP-relative playhead position when
  it lands inside `[0, clip length)`, else at `0.0` — a playhead parked outside the edited clip
  still pastes something visible rather than nothing. `MainComponent::perform` **primes** the
  playhead immediately before pasting (`setPlayheadBeat(transport.getPositionSnapshot().ppq)`): the
  roll only has a playhead position because the overlay PUSHES one via `setPlayheadBeat` while
  playing, so a stopped transport would otherwise paste at whatever beat playback last stopped
  pushing rather than under the position the user can actually see. `buildPastedNotes` applies the
  same clip-window policy every edit here does: a note landing at/after the clip's end is skipped,
  one that would overrun it is clamped, and one with less than `kMinNoteLengthBeats` of room left is
  skipped rather than shrunk below the editor's floor.
- `duplicateSelectedNotes()` copies the selection to immediately after its own span (`max end - min
  start`, same pitches), one undo step, selects the copies — and does **not** touch the clipboard
  (duplicating isn't copying; stomping a clipboard the user filled deliberately would be a
  surprise).
- `cutSelectedNotes()` is copy then delete, one undo step (the clipboard is filled first, so a cut
  is always paste-able).
- `selectAllNotes()` selects every note in the open clip.
- `repeatSelectedNotes(count)` places `count` copies of the selection block, each one span further
  along, **clipped at the clip's end**: placement stops at the first block that would fall entirely
  outside the clip rather than piling every remaining copy onto the last beat. One undo step; every
  created note ends up selected.

All six mirror `TimelineClipLaneArea`'s clip clipboard verbs one-for-one (see the §1 subsection
above) — the two clipboards are entirely separate stores, so copying clips never makes Paste live
on the roll or vice versa; see [`shortcuts.md`](shortcuts.md) for the surface-routing table that
picks which one Cmd+C/V/D/X act on.

**Arrow-key editing.** `PianoRollComponent::keyPressed()` handles Left/Right/Up/Down directly
(matched on key CODE, since `juce::KeyPress::operator==(int)` also requires no modifiers, which
would miss Shift+Up) and returns `false` — falls through — when the selection is empty, so the keys
keep whatever meaning they have elsewhere with nothing selected. `nudgeSelectedNotes(direction)`
moves the WHOLE selection by one shared grid-division delta (the current snap division, or
`kMinNoteLengthBeats` with Snap off) — never per-note, which would silently reshape a chord —
clamped so the group's earliest start never goes below 0 and its latest end never crosses the
clip's length, the same "clamp the group together" rule the drag gestures use.
`transposeSelectedNotes(semitones)` moves every selected note by one shared semitone delta, clamped
into `[0, 127]` as a group so a chord transposes as a chord and never collapses at the pitch
extremes: Up/Down is one semitone, **Shift**+Up/Down is a full octave (12 semitones), the same
octave-jump convention every DAW uses. Both return `true` (consumed) even when the clamp left
nothing to move — the key WAS applicable, it just had nowhere left to go. **Alt+Left/Right
navigates BETWEEN notes** instead of moving them: `selectAdjacentNote(forward)` walks
`Clip::notes`' canonical (startBeat, pitch, id) order by index — no second ordering is defined that
could drift from the doc's comparator — anchoring forward on the selection's LAST selected note and
backward on its FIRST, and collapses to a single-note selection on the neighbour just outside the
block, so repeated presses sweep the clip. At either end the selection is kept and the key still
consumed (the same rule as a fully-clamped nudge); selection-only, so no undo step ever; an
off-screen target scrolls into view minimally via `setHorizontalView` (horizontal only — Alt+Up/Down
stays unhandled/reserved, and yanking the vertical view on a horizontal walk would lose the user's
place). Alt was chosen over Cmd/Shift: plain arrows already nudge, Shift+Up/Down is the octave, and
Cmd+arrows carry OS-level jump-to-boundary semantics. Tool-switching digit keys
are deliberately **not** handled here at all (see the edit-tool strip subsection above) — that
binding belongs to the panel, so the roll and the panel can never disagree about which tool is
active.

**No pencil-by-default any more.** Creating a note is the double-click; a single click on empty grid
is a plain deselect, and a plain drag from empty grid does nothing at all. Marquee is reached only
through Shift, decided entirely at `mouseDown`. JUCE dispatches `mouseDoubleClick` from
`internalMouseUp` — i.e. AFTER the second `mouseDown`/`mouseUp` pair — so the deselect (or the
select-a-note) has already happened by the time the create/delete runs; the double-click is the last
word either way.

The Q button's flash is a **one-shot** `juce::Timer` (`kQuantiseFlashMs` = 120; `timerCallback()`
stops the timer on its first call) repainting only the button's own rect — bounded, never a running
animation, per the CLAUDE.md repaint invariant. The tooltip is served by `juce::TooltipClient`
resolved by position (`getTooltipFor(Point<int>)`), since the header's buttons are drawn shapes, not
child `juce::Button`s.

**Edits stay inside the clip window.** `TimelineDoc` itself only clamps a note's `startBeat >= 0`
finite — "notes can only exist inside the clip" is this editor's own policy, enforced before every
write: draw/resize clamp `[start, start+length)` into `[0, clipLength)`; a multi-note move clamps
its shared delta so the group's earliest start never goes below 0 and its latest end never crosses
`clipLength` (the same clamp-the-group-together reasoning as `TimelineClipLaneArea::mouseDrag`'s
Move branch, extended with an upper bound because notes, unlike clips, live inside one). A note
whose available room shrinks to zero-length is rejected by `TimelineDoc::addNote`'s own validation.

**Scale Assist panel.** `Source/UI/ScaleAssistPanel.h` (`synth::ui::ScaleAssistPanel`) is a
deliberately dumb, `kScalePanelWidth` = 170 px wide sibling docked left of the keys column, shown
by a header "Scale" button and hidden by default (persisted, see below). It holds no reference to
the roll, the doc, or any undo manager — every user action travels OUT through a
`std::function` callback (`onScaleChanged`, `onPitchVisibilityChanged`, `onQuantizePitches`,
`onGenerate`) and every piece of state the roll needs to push back IN (switching clips, restoring a
persisted scale) travels IN through `setSelection()` — the same "panel owns presentation, owner
owns the doc" split `PianoRollComponent` itself follows for `TimelineViewState`. Its contents, top
to bottom: a Root combo (C…B) and a Scale combo ("No scale" first, then every entry from
`synth::builtInScalePresets()` in order — Major, Natural/Harmonic/Melodic Minor, Dorian, Phrygian,
Lydian, Mixolydian, Locrian, Major/Minor Pentatonic, Blues, Whole Tone, Chromatic — then the user's
saved scales, then "Edit custom scales..." which reveals a 12-toggle pitch-class + name + Save
editor rather than being itself a scale choice); a "Show only scale notes" toggle (the same flag the
header's funnel chip drives — see **Show Only Scale Notes has ONE writer** above); and a Min/Max note
range pair plus a "Generate" button with an **"Add to existing"** toggle under it (see **Random generation** below —
the toggle sits under the button rather than beside it because at `kScalePanelWidth` a
button-plus-checkbox row would truncate the label the button's meaning depends on).

**The scale engine** (`Source/Timeline/MusicalScale.h`) is deliberately header-only and free of any
UI or `TimelineDoc`-mutation dependency: `synth::MusicalScale` is a root pitch class (0=C…11=B)
plus a 12-bit, root-RELATIVE interval mask (bit *i* set means "root + i semitones, mod 12, is in
the scale" — the same mask value describes a scale's shape at any root). `contains(midiPitch)` and
`snapPitch(midiPitch)` are the two queries every caller needs; `snapPitch` walks outward from the
pitch by increasing semitone distance and, on an exact tie between an equidistant in-scale pitch
above and below, resolves to the **lower** one. A degenerate mask of 0 ("nothing is in scale") is
handled explicitly everywhere rather than left to loop or crash: `contains` returns `false` for
every pitch and `snapPitch` passes its input straight through. `builtInScalePresets()` is the
fixed, indexed preset list above; user scales (`synth::UserScale { name, mask }`) round-trip through
`parseUserScales`/`serializeUserScales` (tolerant JSON — a malformed entry is skipped individually,
a whole-string parse failure yields an empty list) under the single properties key
`"pianoRollUserScales"` — read/written by `ScaleAssistPanel::setPropertiesFile()`, the same
one-key idiom `NoteColour.h`'s own persistence (§ theming.md §12) follows. `setPropertiesFile
(nullptr)` is a legal, permanent state: Save still works for the session, it just never reaches
disk — the same null-degrades-gracefully contract every other timeline sub-component's setter
follows.

**Per-clip, session-only scale memory.** `PianoRollComponent` keeps `clipScaleMemory_`, a
`std::map<ClipId, {scale, pitchVisibilityOn}>` populated only as clips get their scale touched —
NOT persisted to disk, and deliberately so: a scale choice is a per-editing-session aid, not
document state that would need its own undo/redo or bundle-format entry. `openClip()` calls
`restoreScaleMemoryForOpenClip()`, which pushes whatever this clip remembers (or "No scale" for a
clip never opened before) back into the panel and the roll's own scale context, rebuilding
`visiblePitches_` against it.

**Pitch-visibility row collapse.** `visiblePitches_` — the sorted, ascending list of every pitch
that currently gets a drawn row — is normally all 128 pitches, but "Show only scale notes" (with
a real scale selected) collapses it to exactly the scale's in-scale pitches **plus every pitch any
note in the open clip already uses** — the "notes stay visible" rule: turning the toggle on must
never hide a note that is already there, only change which *additional*, currently-empty rows are
offered for new ones. Every row-index-based operation (vertical wheel-scroll, vertical zoom, the
Up/Down/Shift+Up/Down transpose-by-row deltas used while dragging a note) walks `visiblePitches_`
by INDEX rather than raw semitone arithmetic for exactly this reason — with rows collapsed, a
one-row move can be a multi-semitone jump, and indexing by semitone would silently walk through
hidden rows instead of the next drawn one. `firstVisiblePitch_` is re-clamped to the nearest member
of `visiblePitches_` immediately on any rebuild, so it is always a real, currently-drawn row.

**Quantize-to-scale.** Reached from the header's **Quantise Pitches** chip or `Option+Shift+Q` — and
from nowhere else. The panel used to carry a duplicate "Quantize pitches" button; it is **gone**,
because a second door to the same room inside a panel the user has to open first could only ever
disagree with the chip about its own enabled state. Both entry points go through
`quantisePitchesToActiveScale()`, which resolves the clip's scale via `activeScaleForOpenClip()` and
defers to `quantisePitchesToScale(scale)` — that snaps every note's pitch via `MusicalScale::snapPitch` and
writes back only the notes that actually moved (`TimelineDoc::moveNote`, one
`recordTimelineChange` — a mutation that changes nothing pushes no undo step, so an already-quantised
clip costs zero history entries). The selection is deliberately left untouched: this only ever
moves pitches, never adds/removes/reselects notes.

**Random generation: replace or add.** "Generate" (`onGenerate(minPitch, maxPitch, addToExisting)`)
calls `generateRandomNotesIntoClip`, which reads the RAW snap division regardless of the snap on/off
toggle (`TimelineViewState::divisionBeatsRaw` — the same "clean up notes drawn free-hand" reasoning
`isQuantiseEnabled` documents elsewhere), falling back to a sixteenth (0.25 beats) when that
division is Off/0, since generation always needs a concrete grid step to place notes on. `synth::
generateRandomNotes` (`MusicalScale.h`) then places one note per grid step from beat 0 until the
step's start would fall at or past the clip's length, picking a pitch uniformly among the in-scale
pitches inside `[minPitch, maxPitch]` (every pitch in range when the scale is null or chromatic);
if no candidate pitch exists in that range at all, it returns an empty note list rather than
falling back to an out-of-range or out-of-scale pitch. The RNG is caller-owned and default-seeded
here — the panel's Generate button always wants a fresh draw, never a reproducible one.

An **"Add to existing"** `juce::ToggleButton` under the Generate button picks between two modes; it
is session-only and starts **OFF**, so an embedding that never touches it sees exactly the
pre-existing behaviour. Its state travels OUT with the callback by value rather than being read back
off the panel, since the owner's handler mutates the doc and must not depend on the panel still
being in the same state — or alive — afterwards. Either mode is ONE mutation and ONE undo step, and
only the notes actually ADDED become the selection (the diff, not the whole clip), which is what
makes "generate again" reviewable:

- **OFF — replace** (the default): the single mutation clears every existing note in the clip and adds
  the fresh batch, so undo restores the old contents in one step rather than unwinding a
  clear-then-paste.
- **ON — add**: nothing is cleared, and a generated note that exactly duplicates an existing
  **(pitch, startBeat)** is SKIPPED. That is precisely the key a re-run collides on — generation walks
  the same grid steps every time, so without the check a second Generate would stack unison notes at
  the same offsets, invisible in the roll (one rect drawn over another) and unclickable apart. A note
  at the same start but a DIFFERENT pitch is a chord and is kept. A run in which everything was a
  duplicate adds nothing, selects nothing and pushes no undo step.

**Panel visibility persistence.** The panel's own open/closed state persists under the boolean key
`"pianoRollScalePanelVisible"` (default `false`) — distinct from the user-scales key above, and
owned by the roll/panel rather than `ScaleAssistPanel` itself, since visibility is a roll-chrome
decision, not scale data.

**Edge auto-scroll (roll side).** The piano roll runs the identical gated-timer contract
`TimelineClipLaneArea` established for the clip lanes (see **Edge auto-scroll during a drag** in
§1 above) via the same `Source/UI/EdgeAutoScroll.h` helper and constants, extended to BOTH axes:
`updateAutoScrollArming()`/`autoScrollTick()` check `edgeScrollVelocity` against the grid rect's
horizontal AND vertical edges (`kEdgeAutoScrollMaxPxPerTick` horizontal, matching the clip lanes'
value exactly so a drag that crosses from one editor to the other feels identical; a separate
`kEdgeAutoScrollMaxRowsPerTick` = 1.0 for the vertical axis, since pitch scroll moves by rows, not
pixels). A drag mid-scroll re-derives its preview from the same beat-anchored maths §1
describes, plus the equivalent row-anchored logic vertically. `openClip()`, `closeRoll()` and a
cancelled drag all stop the timer explicitly — a drag from the PREVIOUS clip, or from before a
cancel, must never keep scrolling the current one. Headless seams mirror the clip lanes':
`tickAutoScrollForTest()` and `isAutoScrollTimerRunningForTest()`.

**Wheel.** All four bindings are handled here and NOTHING bubbles to the panel — the roll's
zoom/scroll are its own, so the shared `TimelineViewState` must not move when the wheel lands inside
the roll:

| Binding | Effect |
|---|---|
| Cmd+wheel | Horizontal zoom around the beat under the cursor (`rollView_.zoomAroundX`, anchor = `x - kKeysColumnWidth`), same `exp(dominantWheelDelta(wheel) * 2.0)` factor as the panel's own zoom so the two feel identical |
| Cmd+Shift+wheel | Vertical zoom — scales `pixelsPerSemitone_` within `[4, 40]`, keeping the pitch under the cursor put |
| Shift+wheel, or a trackpad's `deltaX` | Horizontal scroll (`kScrollPixelsPerWheelUnit` px per unit, converted to beats at the roll's own zoom), inverted by the same `scrollInverted_`/**Natural scrolling** preference as the panel (see `docs/timeline_panel_core.md` §2) |
| Plain wheel | Vertical (pitch) scroll, `kPitchScrollSemitonesPerWheelUnit`, clamped to `[0, 127]`, likewise inverted by `scrollInverted_` — and the one axis that needs its own sign flip on top of `scrollAmount()`, since `firstVisiblePitch_` is the pitch at the TOP row and pitch grows upward while the screen convention is "+y moves the view down" (the Natural-scrolling paragraph in `docs/timeline_panel_core.md` §2 has the full reasoning) |

Both zoom rows read `dominantWheelDelta` rather than `wheel.deltaY` directly, for the identical
reason the panel's own Cmd+Shift+wheel branch does (see `docs/timeline_panel_core.md` §2): macOS folds a Shift-held wheel
gesture into `deltaX`, so a branch chosen by its modifiers must never assume the OS parked the
gesture on `deltaY`. Cmd+=/Cmd+- and Cmd+Shift+=/Cmd+Shift+- reach the same `zoomHorizontal`/
`zoomVertical` entry points (anchored at the grid's visible centre rather than the cursor) when the
roll is the focused surface — see [`shortcuts.md`](shortcuts.md#zoom). Zoom is not persisted across
opens — `openClip` reframes to the clip every time.

`TimelineViewState::divisionBeats(beatsPerBar)` (factored out of `snapBeat` for this task) exposes
the EFFECTIVE snap grid as a plain beat value (0.0 while the snap toggle is off);
`divisionBeatsRaw()` is the chosen division regardless of the toggle — what `performQuantise()`
feeds `quantiseNotes`/`moveNote`, since neither takes a `TimelineViewState::Snap` directly.

**The ruler above the roll shows the clip's REAL timeline position.** While the roll is open,
`TimelinePanelComponent::openPianoRoll` installs the roll's own view state into the ruler
(`TimelineRulerComponent::setMappingOverride(&roll.getRollViewState(), kKeysColumnWidth)` — the
offset is the keys gutter, which sits right of the ruler's x == 0). Labels, ticks, the loop brace
AND the ruler's drag-to-loop / drag-to-scrub gestures all map through the override (the snap
division still comes from the shared state), so opening a clip parked at bar 6 shows "6" at the
gutter's edge rather than wherever the lanes were scrolled. The roll fires
`onHorizontalViewChanged` on every zoom/scroll so the panel can repaint the ruler; close restores
the shared mapping. While the override is live the playhead overlay's shared-mapping line would be
a lie in the ruler strip too, so the overlay's local-client region covers the ruler rows as well as
the roll's (`TimelinePanelComponent::resized`) — the roll draws the only playhead line.

Tests: `Tests/PianoRollTests.cpp` — `NoteSelectionModel`/`noteHitTestMarquee` unit coverage (mirrors
`TimelineClipLaneTests.cpp`'s groups 1–2) and `PianoRollComponent` interaction tests driven by
hand-built `juce::MouseEvent`s against a bare `TimelineDoc` + `AppUndoManager` +
`PianoRollComponent`, no `TimelinePanelComponent` needed. The gesture table is pinned test by test
(single click deselects and creates nothing; double-click creates one note per snap division, at two
divisions; double-click deletes; click-select-then-drag moves, one step), as are the first-bar
reachability fix, zoom-around-cursor's fixed point (plus "the shared view state never moves"), the
gridline-density seam, and the local playhead — the last through `CountingRoll`, a subclass
overriding `requestRepaintStrip`, exactly as `TimelinePlayheadTests.cpp` does for the overlay.
Note: `juce::MouseWheelDetails` has no default member initialisers, so wheel tests construct it
`{}`-initialised or a garbage `deltaX` decides the branch.
The edit-tool/clipboard/arrow-key layer is covered in the same file: each tool's click-acts-immediately behaviour, `splitBeatFor`'s both-sides-must-fit rule, `glueCandidateFor`'s gap-bridging, the outline-only muted-note paint, the Draw tool's drag-vs-click fallback, the note clipboard's cross-clip survival and its clip-window clamps on paste/repeat, and the arrow keys' shared-delta clamp with an empty selection falling through.

## 3. Automation Strip

A horizontal strip docked at the BOTTOM of the lanes region (`gridLanesBounds_`), toggled open by
selecting a lane — from the lane picker inside the strip itself, or from ANY generic auto-UI knob's
right-click menu (`ModuleComponent` → `GraphEditor::onAutomateParameterRequested` →
`MainComponent::automateParameter`). While open it takes exactly `Metrics::
timelineAutomationStripHeight` (72, code-only) off the bottom of `gridLanesBounds_`, so
`TimelineClipLaneArea`/`PianoRollComponent` (and the playhead overlay, trimmed the same amount)
shrink by that much — never the other way around, and the ruler/track-header column are untouched.

**Strip chrome** (`TimelinePanelComponent`'s own members, laid out in `resized()`): a header row —
four tool `juce::TextButton`s (glyphs `P` / `✎` / `╱` / `⌫`, radio-grouped so exactly one is down;
`kAutomationToolButtonWidth` is 28 px, up from 24, from the timeline-panel button-size sweep),
a lane-picker `juce::ComboBox` (every doc lane, labelled `"NodeName · paramId"` via
`TrackHeaderHost::getNodeDisplayName(lane.nodeUuid)` — falling back to the uuid's first 8
characters when it doesn't resolve — the SAME interface the track-header binding chip already
uses, so no second graph-aware seam was added), a record-mode `juce::ComboBox` (Off/Read/Touch/
Latch/Write, 1-based combo id = `LaneRecordMode` + 1) bound to `TimelineDoc::setLaneRecordMode`
through `AppUndoManager::recordTimelineChange` (a manual selector change IS a user gesture, unlike
`AutomationRecorder`'s own programmatic Write-drops-to-Touch-on-stop call — see that setter's
header comment), and a close `✕` button — above `synth::ui::AutomationLaneEditor`, the curve
canvas. Panel API: `showAutomationLane(LaneId)` / `closeAutomationStrip()` /
`isAutomationStripVisible()`; headless hooks `applyAutomationLaneMenuChoice(int)` /
`applyAutomationRecordModeChoice(int)` (juce::PopupMenu/ComboBox don't run in a test — the same
"headless hook" idiom every other timeline sub-component's context menu already follows).

**`Source/UI/AutomationLaneEditor.h/.cpp`** (`synth::ui::AutomationLaneEditor`) is the curve canvas,
editing ONE `synth::AutomationLane` at a time. X is the SAME shared `TimelineViewState` the clip
lanes use (so it lines up with the playhead pixel-for-pixel — the piano roll is the one surface that
maps beats through its own zoom/scroll instead; see §2); Y maps the lane's own
`RangeSnapshot [min..max]` linearly onto the component's height, top = max
(`valueToY`/`yToValue`). The curve is sampled every ~2 px by building a local
`TimelineSnapshot::Point[]` from the lane's breakpoints and calling `AutomationKernel::evaluate`
with a fresh `AutomationCursor` — paint is not hot, so re-deriving this on every repaint (rather
than caching it) is deliberate: it is the SAME evaluator the audio thread uses, so the canvas can
never show a shape real playback wouldn't produce.

**Tools** (`AutomationLaneEditor::Tool`, set by the strip's header buttons):

| Tool | Gesture |
|---|---|
| Pointer | Drag a HANDLE moves it — beat snapped via the shared view-state snap, value clamped to the lane's range; tension/curve carry over untouched. Drag a SEGMENT (not a handle — hit-tested first) scrubs the segment's LEFT point's tension, ±0.01 per vertical pixel, clamped to `[-1, 1]` (`AutomationKernel`'s own "shape comes from the LEFT point" contract). Double-click empty space adds a point at that (beat, value), Linear/tension 0. |
| Pencil | Freehand drag collects raw (beat, value) samples (no snapping — that's the point of freehand); on mouse-up they are thinned by `synth::AutomationRecorder::thinPoints` (reused, not re-implemented — its RDP helper is `public static` precisely so a second caller can reach it) at the SAME `kThinningEpsilonFraction` scaled to the lane's own range, and replace whatever existed inside the dragged beat span. |
| Line | Drag previews a straight line from press to release; mouse-up replaces the dragged span with exactly the two (snapped) endpoints, Linear. |
| Eraser | Drag removes every handle it touches — collected into a set as the pointer passes over them (dimmed in the preview), deleted on mouse-up. |

Right-click a SEGMENT shows Hold/Linear (ticking the current one), routed through the headless
`applySegmentCurveChoice(beat, curve)` hook. Right-click a HANDLE shows `{Delete point}`. Escape
clears in-flight tool-drag state and returns `true`; when idle it returns `false` so the key falls
through to `TimelinePanelComponent`'s own `keyPressed` (added for this task), which closes the
strip — the same ancestor-chain fallthrough `TimelineClipLaneArea`/`PianoRollComponent`'s own
panel-scoped Delete/Escape already relies on, one level further up.

**One gesture, one mutation.** Every preview above is strictly component-local (a handful of
`preview*_` members, read back by `paint()`) and NEVER touches the doc during `mouseDrag` — commit
happens exactly once, on mouse-up. The subtlety: `TimelineDoc`'s own single-point mutators
(`addBreakpoint`/`removeBreakpoint`) each bump the revision counter independently, so a gesture that
touches several points (Pencil's thin-and-replace, Line's remove-span-then-add-two-endpoints, a
Pointer move that lands on a different beat, Eraser's multi-point sweep) calling them in a loop
would cost one revision bump — one audio-thread republish — PER POINT instead of per gesture. This
section therefore adds one new batched primitive, `TimelineDoc::editBreakpoints(laneId, removeBeats,
addPoints)`: removes every existing point at a beat in `removeBeats`, then inserts every point in
`addPoints` (validated/clamped exactly like `addBreakpoint`), as ONE `applyMutation` call however
many points move either way. Every multi-point gesture above routes through it; only the genuinely
single-point ones (tension scrub, curve toggle, double-click-add, record-mode select) still call a
plain single mutator, because those already cost exactly one bump on their own.

**Knob entry point.** `ModuleComponent`'s generic auto-UI slider branches (`createControls()`'s
float/int cases) attach `this` as a `MouseListener` on the slider (`addMouseListener(this, false)`
— safe because `this` outlives every child slider, both being torn down together in
`~ModuleComponent()`). `ModuleComponent::mouseDown` checks `e.eventComponent != this` FIRST (a hit
on a child fires the SAME override, in the CHILD's local coordinate space, which the body-click
geometry further down must never see) and, on a right-click, shows `"Automate '<Param>'"` via a
`juce::Component::SafePointer<ModuleComponent>` (the popup's callback is async — the module can be
gone by the time it fires) that calls `owner.onAutomateParameterRequested(nodeId, paramId)` — a new
`GraphEditor` host seam mirroring `onSaveSnippetRequested` exactly (`GraphEditor` owns no
`TimelineDoc`, so it hands the pair back to the one component that owns both the doc and the
graph). `MainComponent::automateParameter(nodeId, paramId)` (public — also the test's headless
hook) resolves the node's uuid (ensure-uuid, mirrored into the processor, the same idiom
`createTrackInNode()`/`AIStateMapper` use at every uuid writer site), finds the first
`TrackKind::Automation` track or creates one, binds a lane with the parameter's real
`NormalisableRange` (`addLane` dedupes doc-wide — a repeat call for an already-automated parameter
is a no-op that returns the existing lane), opens the timeline panel via the SAME toggle-button
click path `simulateToggleTimelineClick()` uses if it's hidden, and opens the strip on that lane.

Tests: `Tests/AutomationEditorTests.cpp` — `AutomationLaneEditor` gesture/publish-discipline
coverage (mirrors the `TimelineClipLaneArea`/`PianoRollComponent` hand-built-`juce::MouseEvent`
idiom against a bare `TimelineDoc` + `AppUndoManager`), the panel's strip open/close/record-mode
selector, and a `MainComponent` integration test for the knob
entry point.

## 4. Keyboard & Focus

By this point the panel has THREE independently-editable surfaces competing for the same physical
keys: the graph editor, the clip lanes, and the piano roll (each already grabbing keyboard focus on
its own `mouseDown` — §1/§2 above). Cmd+C/V/D are global
`ApplicationCommandManager` commands owned by `MainComponent`, so — unlike Delete/Escape, which
each surface already intercepts locally via its own `keyPressed` — nothing decided *which*
surface's selection and clipboard they should act on until this task.

**The one resolver.** `MainComponent::resolveEditSurface() const` is the single focus-ownership
rule:

```cpp
enum class EditSurface { Graph, TimelineClips, PianoRoll };
```

It returns `TimelineClips`/`PianoRoll` when the timeline panel is visible AND real keyboard focus
(`juce::Component::getCurrentlyFocusedComponent()`) sits inside the clip-lane area / piano roll
respectively, and `Graph` otherwise — including when the timeline panel is hidden outright,
regardless of what a stale focus pointer inside it might point at. Nothing new grabs focus for
this: every surface already does it on `mouseDown` (`GraphEditor::mouseDown` is the idiom's
original; `TimelineClipLaneArea`, `PianoRollComponent` and `AutomationLaneEditor` all copy it), so
"the surface you last clicked owns the verbs" falls out of ordinary JUCE focus tracking with no
extra bookkeeping. Headless tests can't always create a real focus grab (`grabKeyboardFocus()`
needs a native peer — see `Tests/FocusArbitrationTests.cpp`'s `SurfaceResolverRealFocus`, which
documents why this repo doesn't attempt one), so `MainComponent::setEditSurfaceOverrideForTest()`
is consulted FIRST and short-circuits the real-focus check when set.

**Cmd+C/V/D/X and Cmd+R route by surface** — `MainComponent::getCommandInfo`/`perform` branch on
`resolveEditSurface()` for `AppCommands::copySelection/pasteSelection/duplicateSelection/
cutSelection/repeatSelection`:

- **Graph** — Copy/Paste/Duplicate unchanged: `GraphEditor::copySelection()/pasteClipboard()/
  duplicateSelection()` against its own `ModuleClipboard`. Cut is **composed** from the two that
  already exist (`copySelection()` fills the clipboard without touching the graph or the undo
  stack, then `deleteSelection()` removes the selection inside its own `recordStructuralChange`),
  so it costs exactly one graph-undo step and the copy half survives the undo. Repeat is
  **inactive** here — always — because "N copies, each one selection-span further along" is a
  time-axis idea and a spatial canvas has no such axis; Duplicate is the graph's answer to "another
  one of these".
- **TimelineClips** — the clip clipboard, owned by `TimelinePanelComponent` (it already owns the
  clip selection — see §1): `copySelectedClips()` serialises the selected clips — notes (each
  with its own `muted` flag), name, length, `muted`, and every audio field (`assetRef`, `gainDb`,
  both fades, `sourceStartSeconds`) — starts relative to the earliest selected clip, into it;
  `pasteClipsAtPlayhead()` re-inserts them onto their ORIGINAL tracks (by `TrackId`), re-based so
  the earliest clip lands at the transport's CURRENT position (snapped via the shared
  `TimelineViewState`), in ONE `AppUndoManager::recordTimelineChange`; the track fallback is
  **kind-aware** (`TimelineDoc::moveClipToTrack`'s rule) — a clip lands back only on a track that
  still plays its payload, else the doc's first track of the required kind, else it is skipped.
  Audio fields go back through `setClipAsset`/`setClipGainDb`/`setClipFades` rather than a raw
  struct write, so a clipboard `assetRef` is re-validated exactly like a freshly-loaded file's — a
  clipboard is only as trustworthy as whatever filled it (this closes a bug: the clipboard used to
  silently drop a copied audio clip's asset). `duplicateSelectedClips()` calls
  `TimelineDoc::duplicateClip()` per selected clip, batched the same way. `cutSelectedClips()` is
  copy then delete the selection, as ONE `recordTimelineChange` (never wrapped a second time — that
  would make Cmd+Z a two-step undo for one gesture). `repeatSelectedClips(count)` makes `count`
  back-to-back copies of the selection's own span (`max end - min start`, not each clip's own
  length, so a multi-clip rhythm tiles intact), the first starting one span-length after the
  selection's start, batched into one undo step. All four leave their result selected, mirroring
  `GraphEditor`'s own "the copies are what you probably want next" convention.
- **PianoRoll** — the roll's OWN note clipboard (see the §2 subsection's **Note clipboard**
  above) closes what was previously a deliberate v1 gap: Copy/Paste/Duplicate/Cut/Repeat all act on
  notes now. Paste **primes** the roll's playhead from the live transport
  (`timelinePanel.getPianoRoll().setPlayheadBeat(audioEngine.getTransport().
  getPositionSnapshot().ppq)`) immediately before pasting — priming, not a side effect, since the
  roll only has a playhead position because the overlay pushes one while playing, and a stopped
  transport never does.

`getCommandInfo`'s Paste case is active only when the SURFACE-MATCHING clipboard has something in
it — `GraphEditor::canPaste()` for Graph, `TimelinePanelComponent::canPasteClips()` for
TimelineClips, `PianoRollComponent::canPasteNotes()` for PianoRoll (both halves: a non-empty
clipboard AND an open clip) — so copying modules never makes Paste live on the clip lanes or the
roll, or vice versa. Cut shares Copy's enablement predicate on every surface (a cut is a copy that
also deletes). Repeat's predicate is `hasClipSelection()`/`hasNoteSelection()` on the timeline
surfaces and unconditionally `setActive(false)` on Graph. `Cmd+Shift+A` (`AppCommands`/actionId
`selectAllModules`, kept for a persisted binding's sake even though the verb widened) is routed by
the SAME resolver: `TimelinePanelComponent::selectAllClips()` on TimelineClips,
`PianoRollComponent::selectAllNotes()` on PianoRoll, `GraphEditor::selectAllModules()` on Graph —
unlike the clipboard verbs it is **always active**, since each surface's own `selectAll*` just
returns `false` harmlessly when there's nothing to select. See
[`shortcuts.md`](shortcuts.md#surface-routing-who-cmdcvdxr-and-cmda-act-on) for the
user-facing table.

**Space is global.** `AppCommands::togglePlayback` (`ShortcutManager` action id `togglePlayback`,
default binding: bare spacebar, no modifiers) is deliberately NOT routed by `resolveEditSurface()`
— it always toggles the transport, from any surface, via
`TimelinePanelComponent::getTransportBar().getPlayStopButton().triggerClick()` (the SAME choke
point the transport bar's own click handler uses — see `docs/timeline_panel_core.md` §5 — so the button's visual state
and a Space-triggered toggle can never disagree). Safe to claim app-wide for the same reason
Cmd+C/V is (see `shortcuts.md`): a focused `juce::TextEditor` consumes the spacebar itself (types a
space character) before `MainComponent::keyPressed`, the sole dispatch point, ever sees it. Always
active — the timeline is GA, so there is no preference that can hide the transport out from under
this command.

**Delete stays panel-local.** Unlike C/V/D, Delete/Escape were never routed through
`ShortcutManager`/`ApplicationCommandManager` at all (see `shortcuts.md`'s reasoning) — each
surface's own `keyPressed` already handles its own selection and falls through (`return false`) on
an empty one, which is what let an unmodified `Delete` binding be surface-scoped for free since
before this task. This section adds no new production code here, only
`Tests/FocusArbitrationTests.cpp`'s `DeletePerSurface`, which pins that a clips-focused Delete never
touches the graph, a graph-focused Delete never touches the clips, and an empty selection on either
falls through rather than eating the key.

Tests: `Tests/FocusArbitrationTests.cpp` — one test per verb x surface (`resolveEditSurface()`
override coverage, clip copy/paste-at-playhead/duplicate incl. the missing-track fallback, the
piano-roll inactive gap, Space from every surface, per-surface Delete, and the resolver's real-focus
fallback behaviour).
