# Timeline Panel: Clips & Automation

This document covers the timeline panel's clip lanes, the automation strip, and the
keyboard/focus arbitration rule that decides which surface Cmd+C/V/D/X/R and Cmd+Shift+A act on.
The piano roll note editor lives in the companion document
[`docs/timeline_panel_piano_roll.md`](timeline_panel_piano_roll.md). The panel shell, ruler/grid,
track headers, playhead, transport bar, metronome/count-in and edit-tool strip live in
[`docs/timeline_panel_core.md`](timeline_panel_core.md),
[`docs/timeline_panel_tracks.md`](timeline_panel_tracks.md), and
[`docs/timeline_panel_transport.md`](timeline_panel_transport.md).

- [1. Clip Lanes](#1-clip-lanes)
- [3. Automation Strip](#3-automation-strip)
- [4. Keyboard & Focus](#4-keyboard--focus)

---

## 1. Clip Lanes

`Source/UI/TimelineClipLaneArea/` (`synth::ui::TimelineClipLaneArea`, declared in
`TimelineClipLaneArea.h`) fills the lanes region below the ruler (`getLanesBounds()` minus the
ruler strip — the same rect the bar/beat grid is painted into) with per-track rows of
`synth::Clip` rects: drag to move, drag an edge to trim, a context menu to split/duplicate/delete,
and marquee (rubber-band) multi-select. Backed by `synth::ui::ClipSelectionModel`
(`Source/UI/ClipSelectionModel.h`), the clip analogue of `SelectionModel`
(`docs/layout_selection_canvas.md` §1.2) — a `std::set<synth::ClipId>` with the same add/remove/toggle/
setSelection/retainOnly contract, ordered ascending by id so a batched move or delete always walks
clips in a stable order regardless of click order.

Source layout (FRO66 split, one file per concern; the class itself is declared in
`TimelineClipLaneArea.h`, with constants shared by two or more units in `TimelineClipLaneInternal.h`):

| Unit | Concern |
|------|---------|
| `TimelineClipLaneArea.cpp` | Ctor, `TimelineDoc` wiring, row/rect geometry, hit-testing, core `paint()` |
| `TimelineClipLanePainting.cpp` | Tool affordances (drag/draw/split-preview ghosts), waveform painting, live-recording strip |
| `TimelineClipLaneMouse.cpp` | Mouse handling, drag-preview/auto-scroll, double-click clip creation, file drag/drop |
| `TimelineClipLaneSelection.cpp` | Selected-clip span query, panel-scoped `keyPressed`, marquee begin/update/end |
| `TimelineClipLaneEditTools.cpp` | Active tool/cursor/gestures and their previews, clip renaming, clip context menu |

**Ownership.** `TimelinePanelComponent` owns the `ClipSelectionModel` and the lane area
(`getClipSelection()` / `getClipLaneArea()`); the lane area holds the selection model and the
shared `TimelineViewState` by reference, exactly the relationship the ruler already has with the
view state. `MainComponent` forwards its one `AppUndoManager` in (`TimelinePanelComponent::
setUndoManager`), the same wiring block that installs the doc,
transport and track-header host (`docs/timeline_panel_tracks.md` §3).

**Z-order, and the one relocation this task makes.** `TimelinePanelComponent::paint()` still paints
the bar/beat grid directly, unchanged — that stays the ONE place the grid is painted. The lane area
is added as a child positioned over exactly that same rect, *before* the playhead overlay (added
last). Since JUCE always paints a parent before its children, the result is grid → clips →
playhead with no extra bookkeeping.

**Row geometry.** `Metrics::timelineTrackRowHeight` (56 px, code-only) is the single row height
both the track-header column and the clip-lane area lay out at — see `docs/timeline_panel_tracks.md` §3.
`TimelineTrackHeaderComponent::kRowHeight` is kept as the matching headless literal fallback rather
than deleted, read by both components' `dynamic_cast<AppLookAndFeel*>`-with-fallback pattern.
`TimelineClipLaneArea::computeClipRect(viewState, trackIndex, startBeat, lengthBeats, rowHeight)` is
a pure static function (no doc, no component) — `[beatToX(start), beatToX(start+length)]` × `[row *
rowHeight, rowHeight]` — so geometry is unit-testable with no component or LookAndFeel at all.

**Rendering.** Every track in doc order gets one row; every clip on it, a rounded rect filled with
`synth::ui::resolveTrackColour(track.colourArgb, trackIndex, track.muted || clip.muted)` (the
track-header colour resolver from `docs/timeline_panel_tracks.md` §3, reused rather than re-invented — dimmed when **either** flag is set, since the two are
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
applyBindingMenuChoice`/`applyContextMenuChoice` already establish (`docs/timeline_panel_tracks.md` §3). `ClipContextChoice` also
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
point the transport bar's own click handler uses — see `docs/timeline_panel_transport.md` §5 — so the button's visual state
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
