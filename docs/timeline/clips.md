# Timeline Clip Lanes

`Source/UI/Timeline/TimelineClipLaneArea/` (`synth::ui::TimelineClipLaneArea`, declared in
`TimelineClipLaneArea.h`) fills the lanes region below the ruler — `getLanesBounds()` minus the
ruler strip, the same rect the bar/beat grid is painted into — with per-track rows of `synth::Clip`
rects: drag to move, drag an edge to trim, a context menu to split/duplicate/delete, and marquee
multi-select.

Selection is backed by `synth::ui::ClipSelectionModel`
(`Source/UI/Timeline/ClipSelectionModel.h`), the clip analogue of `SelectionModel`
(`docs/layout/selection.md`) — a `std::set<synth::ClipId>` with the same
add/remove/toggle/setSelection/retainOnly contract, ordered ascending by id so a batched move or
delete always walks clips in a stable order regardless of click order.

## Source layout

One file per concern; the class is declared in `TimelineClipLaneArea.h`, with constants shared by
two or more units in `TimelineClipLaneInternal.h`:

| Unit | Concern |
|------|---------|
| `TimelineClipLaneArea.cpp` | Ctor, `TimelineDoc` wiring, row/rect geometry, hit-testing, core `paint()` |
| `TimelineClipLanePainting.cpp` | Tool affordances (drag/draw/split-preview ghosts), waveform painting, live-recording strip |
| `TimelineClipLaneMouse.cpp` | Mouse handling, drag-preview/auto-scroll, double-click clip creation, file drag/drop |
| `TimelineClipLaneSelection.cpp` | Selected-clip span query, panel-scoped `keyPressed`, marquee begin/update/end |
| `TimelineClipLaneEditTools.cpp` | Active tool/cursor/gestures and their previews, clip renaming, clip context menu |

## Ownership and z-order

`TimelinePanelComponent` owns the `ClipSelectionModel` and the lane area (`getClipSelection()` /
`getClipLaneArea()`); the lane area holds the selection model and the shared `TimelineViewState` by
reference, exactly the relationship the ruler has with the view state. `MainComponent` forwards its
one `AppUndoManager` in (`TimelinePanelComponent::setUndoManager`), the same wiring block that
installs the doc, transport and track-header host ([tracks](tracks.md)).

`TimelinePanelComponent::paint()` paints the bar/beat grid directly — that stays the ONE place the
grid is painted ([view](view.md#the-lanes-grid)). The lane area is added as a child positioned over
exactly that same rect, *before* the playhead overlay (added last). Since JUCE always paints a
parent before its children, the result is grid → clips → playhead with no extra bookkeeping.

## Row geometry

`Metrics::timelineTrackRowHeight` (56 px, code-only) is the single row height both the track-header
column and the clip-lane area lay out at — see [tracks](tracks.md#the-track-header-column).
`TimelineTrackHeaderComponent::kRowHeight` is kept as the matching headless literal fallback rather
than deleted, read by both components' `dynamic_cast<AppLookAndFeel*>`-with-fallback pattern.

`TimelineClipLaneArea::computeClipRect(viewState, trackIndex, startBeat, lengthBeats, rowHeight)`
is a pure static function — no doc, no component — computing `[beatToX(start), beatToX(start +
length)]` × `[row * rowHeight, rowHeight]`, so geometry is unit-testable with no component or
LookAndFeel at all.

## Rendering

Every track in doc order gets one row; every clip on it, a rounded rect filled with
`synth::ui::resolveTrackColour(track.colourArgb, trackIndex, track.muted || clip.muted)` — the
track-header colour resolver ([tracks](tracks.md#colour)), reused rather than re-invented, dimmed
when **either** flag is set, since the two are independent in the model: a clip the user muted
individually dims exactly like one sitting on a muted track. Plus a name (width > ~40 px) and a
thin pitch-mapped note preview (width > ~24 px, clip-relative note beats offset by the clip's
current — possibly mid-drag — start).

A muted clip keeps its shape, its selection border and its waveform or notes, and loses only the
fill/border brightness and its name label's alpha (`kMutedClipLabelAlpha`) — painted straight from
`synth::Clip::muted` on the doc, **never** from a `TimelineSnapshot`, which does not contain a
muted clip at all (see [`architecture/timeline.md`](../architecture/timeline.md#timelinesnapshot-the-audio-threads-view-of-the-timeline)).
Selected clips get a brighter border and a slight fill lift.

Repaints happen only on doc changes (`refreshFromDoc()`, routed in from
`TimelinePanelComponent::timelineChanged()`, which also calls `ClipSelectionModel::retainOnly` so a
clip removed by any path can never stay selected), view-state changes (zoom/scroll/snap), and
interactions — never a timer.

## Select-tool gestures

Everything below is `EditTool::Select`'s gesture table — the only tool that drags, resizes,
marquees or double-click-authors. Switching away from Select disables all of it; see
[edit-tools](edit-tools.md) for what the other five do instead.

| Gesture | Effect |
|---|---|
| Click a clip | Selects it, replacing the selection unless Shift/Cmd/Ctrl, which toggles just that clip |
| Click empty lane space | Deferred: only a press that never becomes a drag clears the selection on mouse-up — the same `pendingEmptyCanvasClick` trick `GraphEditor::mouseDown/mouseUp` uses for the canvas, so a drag is never mistaken for a deselect |
| Drag from empty lane space | Marquee — intersection hit-test (`clipHitTestMarquee`), additive with Shift. There is no drag-to-pan here (scrolling is wheel-only, see [view](view.md#wheel-and-trackpad-bindings)), so a plain drag also starts a non-additive marquee |
| Drag a clip's body | Moves it, and every other selected clip, by one Snap-quantised beat delta shared across the whole selection, computed from the clip that was grabbed and clamped so no clip's start goes below 0, **plus** one shared track-row delta (see **Cross-track drag**) |
| **Alt** + drag a clip's body | Copy-drag: the originals stay exactly where they are, doc and screen both; release commits a `duplicateClip` + `moveClipToTrack` per dragged clip at the destination, and the **copies** end up selected. There is no Alt-**click** action — copying a clip onto itself is not something anyone asks for by clicking |
| Drag within 6 px of the right edge | Resizes (trims) the clip's length, Snap-quantised, floored at 1/16 beat |
| Drag within 6 px of the left edge | Moves the start and shrinks or grows the length so the **end** stays fixed; the clip's notes, being clip-relative, travel with it |
| Right-click a clip | `PopupMenu`: **Split at pointer** (enabled only when the Snap-quantised pointer lands strictly inside the clip), **Glue with next** (enabled only when a legal join target exists — greyed out, not hidden, so the menu's items never move around), **Duplicate**, **Mute**/**Unmute** (one toggling item, labelled for what the click will do), **Rename…**, **Delete**, and — for an audio clip only (non-empty `assetRef`), below a separator — **Relink audio…**. It preserves the existing selection, the same rule `GraphEditor`'s cable and canvas menus follow |
| Double-click a clip | Opens the piano roll on it (`onClipDoubleClicked` → `TimelinePanelComponent::openPianoRoll`) |
| Double-click empty lane space | Authors content on the row under the pointer — see **Adding content** |
| Drop audio files from the OS | Imports the first readable one onto the audio row under the cursor — see **Adding content** |
| Delete / Backspace | Deletes every selected clip as ONE undo step; returns `false` (key falls through) when the selection is empty |
| Escape | Clears the selection; returns `false` when it is already empty |
| P | **Loop the selection** — see **Loop the selection** below |

## Cross-track drag

A plain (non-copy) move drag previews **one shared track-row delta** for the WHOLE dragged set,
derived from the vertical drag distance (`round(dy / rowHeight)`), legal only if **every** dragged
clip's destination row exists and accepts its payload. `TimelineDoc::moveClipToTrack`'s kind rule:
an audio clip (non-empty `assetRef`) only onto a `TrackKind::Audio` row, a MIDI clip only onto
`TrackKind::Midi`, neither onto `Automation`.

An illegal drop for **any** clip in the set clamps the whole group's row delta back to 0 — a
same-lane move — rather than dropping only the clips that would have fit and silently tearing the
selection apart. `getPreviewRowDeltaForTest()` is the row delta the in-flight drag would apply.

A copy-drag never moves the originals on EITHER axis: `effectiveGeometryFor` and `effectiveRowFor`
share the same `copyDrag_` guard — they must agree, or the original slides while its row holds — so
the destinations paint as translucent ghosts (`paintDragGhosts`: source-track colour at 0.4 alpha
plus a 1 px outline, no name label; a real blur would be per-frame image filtering, which
`docs/layout/rendering.md` rules out) while the source clips keep painting where they
are. Ghost geometry comes from one helper (`dragGhostRectFor`) shared with
`getDragGhostRectsForTest()`, the same single-enumeration reasoning as `buildVisibleCables()`.

## Edge auto-scroll

`Source/UI/Timeline/EdgeAutoScroll.h` is a small pure helper — one function,
`edgeScrollVelocity(pos, lo, hi, zonePx, maxPerTick)`, plus the two shared constants `kEdgeZonePx`
(24 px edge-zone width) and `kEdgeScrollHz` (30 Hz gated-timer rate). It deliberately holds no
state, no timer and no component reference, so "how fast does an edge-drag scroll" is one decision
the clip lanes and the piano roll both read rather than two that could silently drift apart.

Velocity is 0 in the dead middle band, ramps linearly with penetration depth from 0 at the zone's
inner edge to `maxPerTick` right at the component edge, and clamps at `maxPerTick` beyond it — a
pointer JUCE still reports after being dragged off the component entirely must not fly the view
away.

`TimelineClipLaneArea::updateAutoScrollArming()` starts and stops a `juce::Timer` at
`kEdgeScrollHz` gated on **both** a Move or Resize drag being active AND the last known pointer
sitting inside the edge zone — the timer never runs for any other reason, including a marquee or a
plain hover. Each `autoScrollTick()` re-checks both conditions (the drag can have ended, or the
pointer moved back to the dead zone, since the last check), scrolls `TimelineViewState` by
`velocityPxPerTick / pixelsPerBeat` beats, and — because no `MouseEvent` fired this tick, only the
view moved — calls `updateDragPreviewFromLastPointer()` to re-derive the in-flight drag preview
against the same last-known pointer position but the NEW scroll offset.

**That re-derivation only works because the drag-delta maths is beat-anchored, not pixel-anchored.**
`deltaBeats = viewState_.xToBeat(pointer.x) - mouseDownBeat_` reads the delta through `xToBeat`'s
own `firstVisibleBeat` term every time, rather than caching a pixel offset computed against the
view position at `mouseDown`, so a scroll mid-drag — from an auto-scroll tick, or in principle any
other scroll — is absorbed by that term instead of silently invalidating a stale pixel delta.

Every scroll tick fires `onViewScrolledByDrag` so `TimelinePanelComponent` repaints the ruler and
itself; the ruler has no other way to learn the shared view state moved, since it is not a drag
participant. Headless seams: `tickAutoScrollForTest()` drives one tick without a real timer, and
`isAutoScrollTimerRunningForTest()` observes the gating.

## Inline rename

`beginRenameClip(ClipId)`, reached from the context menu's **Rename…**, opens a `juce::TextEditor`
over the clip's name area, pre-filled and `selectAll()`ed: Return commits through `renameClip()`
(which calls `TimelineDoc::setClipName` — trims, rejects a blank result, one undo step), Escape
cancels, and losing focus **commits**. Clicking away is a commit, not a cancel, the same as every
other in-place rename in this app.

Any rename already in flight commits first if a second one opens, and the editor detaches itself
**before** either outcome runs, so the `onFocusLost` callback its own teardown fires re-enters to a
null editor and stops rather than double-committing. `getRenameEditorForTest()` is the test seam: a
live `juce::TextEditor` is no more testable than a `juce::PopupMenu`.

## One undo step per gesture

Every drag and trim previews locally — a member offset or length, read back by `paint()` through
`effectiveGeometryFor()` — and commits to the doc exactly once on mouse-up, through
`AppUndoManager::recordTimelineChange`. A multi-clip move or a multi-clip Delete is one
`recordTimelineChange` call however many clips it touches, mirroring
`GraphEditor::dragSelectionBy()` / `finalizeSelectionDrag()` and `deleteSelection()`'s
one-transaction contract for modules.

`showMenuAsync` never runs headlessly, so the context menu's Split/Glue/Duplicate/Mute/Delete
actions are exercised in tests through `applyClipContextChoice(ClipId, choice, pointerBeat)` — the
same menu-without-the-menu idiom `TimelineTrackHeaderComponent::applyBindingMenuChoice` /
`applyContextMenuChoice` establish ([tracks](tracks.md#test-seams)). `ClipContextChoice` also
carries `Rename`, which is deliberately **inert** in that function: renaming opens a
`juce::TextEditor` rather than mutating the doc, so its commit path is `renameClip()` and this enum
case exists only so the menu's whole vocabulary is enumerable.

## Edit tools

`handleToolMouseDown()` routes every non-Select tool. Split, Glue, Erase and Mute act **immediately
on press** — a DAW's tool click is expected to land under the finger, not on release — and hit-test
a clip first: a click on empty lane space with any of these four held does nothing at all,
deliberately, rather than falling through to a selection change, which would make an Erase click
look like it selected something.

All four route straight into `applyClipContextChoice` — **the tool and the menu item are the same
code path**, which is what keeps "the tools are an accelerator for the menu" literally true:

- `Split` → `SplitAtPointer` at the Snap-quantised pointer beat
- `Glue` → `GlueWithNext` against `findGlueTarget(id)` — the clip on the SAME track with the
  smallest `startBeat` at or after `id`'s end. **A gap is a legal join target**, since
  `TimelineDoc::joinClips` treats a gap as silence and only rejects an overlap; picking the
  abutting clip only would make the tool silently inert on the very arrangement — clips with gaps
  — where gluing is most useful
- `Erase` → `Delete`
- `Mute` → `ToggleMute`

None of the four ever starts a drag, a marquee, a trim or the double-click authoring gestures:
every mouse-move and mouse-up is a no-op while one of them is active, so a mis-aimed drag can never
silently move or trim a clip instead of doing the click action.

**Draw** is the one exception with a drag. Press anchors on the **floor-snapped** beat of the row
under the pointer (`floorSnappedBeatAt` — the grid line at or *before* the click, so the clip lands
in the cell it was aimed at) on a **Midi** row only: an Audio row's content is an imported asset and
an Automation row's is breakpoints, neither of which a pencil can draw. Drag grows a ghost rect to
the **ceil-snapped** beat under the pointer (`ceilSnappedBeatAt`, floored at one snap division — or
`kMinClipLengthBeats` with Snap off — so a drag that has entered a cell always includes the whole
cell); release commits. A press that never dragged falls back to `createMidiClipAt`, the SAME
one-bar-clip authoring gesture the empty-lane double-click uses, so a plain pencil click and a
plain double-click land in the same place.

## Split and draw previews

Both previews are gated on the previewed STATE changing, never on raw pointer movement, mirroring
the paint-count discipline `TimelinePlayheadOverlay::requestRepaintStrip` establishes.
`updateSplitPreview(pos)` recomputes the hovered clip plus snapped beat, compares against the
cached pair, and only then calls the virtual `requestToolPreviewRepaint(region)` with the union of
the old and new preview rects — pointer movement inside one snap cell over the same clip costs zero
repaints. `updateDrawGesture` / `commitDrawGesture` follow the identical rule for the Draw ghost
rect.

`requestToolPreviewRepaint` is the ONE seam both previews cost through — a test subclass overrides
it and counts, the same pattern `PianoRollComponent::requestRepaintPreviewStrip` and the playhead
overlay's own seam use; `getSplitPreviewForTest()` / `getDrawGhostRectForTest()` expose the state
itself, so a test can assert on it without decoding pixels. `mouseEnter` re-applies the active
tool's cursor and `mouseExit` drops the Split preview, so a hover line never survives the pointer
leaving the lanes.

## Relink audio

Offered whenever the clicked clip's `assetRef` is non-empty, whether the asset currently resolves
(a plain re-point) or is missing — see
[`architecture/app-wiring.md`](../architecture/app-wiring.md#asset-management)'s asset-management section for the
missing-asset placeholder this same field drives.

Unlike the three tool actions above, this is a plain callback (`onRelinkAudioRequested`) rather
than a `ClipContextChoice`: it needs a host `juce::FileChooser` and a `synth::AssetManager` import,
neither of which `TimelineClipLaneArea` has. `MainComponent::promptRelinkClipAsset` opens the
dialog and calls `relinkClipAsset(id, chosenFile)`; the headless path,
`MainComponent::relinkClipAssetForTest(id, chosenFile)`, calls the same function directly and never
goes through the menu (or `showMenuAsync`) at all.

The import lands in the current bundle's `Audio/`, or — with no bundle yet — the app-data
`Recordings/` convention. Every other clip that shared the OLD ref is rewritten alongside the
clicked one, as ONE undo step.

## Adding content

Recording and the AI tools are not the only ways in: both gestures below author content directly,
and each is ONE undo step.

**Double-click empty lane space.** The row under the pointer decides what happens. A **Midi** row
gets a new clip at `floorSnappedBeatAt(x)` — the snap grid line at or *before* the click, never
after it, so the clip lands in the cell it was aimed at — one bar long (the transport's time
signature, 4 beats with no transport), auto-named `"Clip N"` from the row's clip count, selected,
and then fired through the SAME `onClipDoubleClicked` hook a clip double-click uses, so the user
lands straight in the piano roll ready to draw notes.

**Except when it spans the loop locators.** `locatorSpanForDoubleClick(clickedBeat)` is the pure
rule: when the `timelineDoubleClickSpansLocators` preference is on (**default ON**), a transport is
installed, the locators define a real span (`loopEnd > loopStart`) AND the clicked beat falls inside
`[loopStart, loopEnd)`, the clip is authored at `startBeat = loopStart` with `lengthBeats = loopEnd
- loopStart` instead. Every other case keeps the one-bar behaviour exactly. Four details that are
decisions, not accidents:

- The beat tested is the **RAW, unsnapped** beat under the pointer. Snapping first could push a
  click that landed outside the span into it, or the reverse, and the question being asked is where
  the user actually clicked.
- The span is **half-open**: a click exactly ON the right locator is a click in the bar *after* the
  loop, and authoring a locator-length clip there would run past where the user pointed.
- Looping being switched **off** does not matter — the locators are a *range*, and a degenerate
  span (`end <= start`) is also what "no locators set yet" looks like, so both fall back to one bar.
- Only the double-click path asks. The **Draw tool** goes through the same `createMidiClipAt` but
  passes no length override, because a pencil drag states its own length.

The preference is read **at use time** through the duplicated-string-key idiom (the same one
`timelineLoopSelectionArms` uses), from a non-owning `juce::ApplicationProperties*` the panel
forwards in `setApplicationProperties`. Nothing is cached and nothing is pushed live: flipping the
toggle in `Settings → Preferences → "Timeline: double-click inside the locators spans them"` takes
effect on the very next double-click. A null properties pointer, or no user-settings file, means
"take the default", which is ON.

An **Audio** row asks for a file through `audioFileChooser_` — a `std::function` seam defaulting to
a real async `juce::FileChooser` filtered by
`juce::AudioFormatManager::getWildcardForAllFormats()`, which a test replaces with a lambda that
answers synchronously (`juce::FileChooser`, like `showMenuAsync`, never runs in a test process) —
and reports the choice through `onAudioFileDropped`, exactly as a file drop does. An **Automation**
row, and a double-click below the last row, do nothing.

**OS file drag-and-drop.** `TimelineClipLaneArea` is a `juce::FileDragAndDropTarget`.
`isInterestedInFileDrag` is EXTENSION-based
(`AudioFormatManager::findFormatForFileExtension`) — no dragged file is ever opened — and true when
at least one file qualifies. `fileDragMove` highlights the **audio** row under the cursor with an
accent wash, repainting only the rows involved and only when the row actually changes
(`fileDragExit` / `filesDropped` clear it); a MIDI row, an automation row and the space below the
last row neither highlight nor accept a drop. `filesDropped` reports the FIRST readable audio file
— a multi-file drop makes one clip, not N.

**Who imports.** Neither gesture imports anything here: `onAudioFileDropped(TrackId, snappedBeat,
File)` hands the decision outwards and `MainComponent::importAudioFileToClip` does the work, for
the same reason **Relink audio…** does — the lane area owns no `AssetManager` and no bundle root.
That method reuses `relinkClipAsset`'s policy verbatim: a saved project imports into the bundle's
`Audio/` (`AssetManager::importAudioFile`), and an unsaved one into the app-data `Recordings/`
convention `chooseTakeFiles()` writes takes into, so `saveToFile`'s `adoptRecordingsAssets` sweep
moves it into the bundle on the first save. The clip's length is the file's own duration in beats
(`audioFileLengthInBeats`, at the transport's current bpm), `sourceStartSeconds` is 0, and the clip
plus its asset binding are batched into ONE `recordTimelineChange`. A failed import — unreadable or
non-audio — reports through the status bar and mutates the document not at all. Headless seam:
`MainComponent::importAudioFileToClipForTest`.

**Empty-row hint.** A row with no clips paints one dim line, centred, straight from doc state — no
timer, no animation, `Theme::Colors::textMuted`: **"Double-click to add a clip — or arm (R) and
record"** on a Midi row, **"Drop an audio file — or arm (R) and record"** on an Audio row, and
nothing on an Automation row, whose content is breakpoints authored in the automation strip. The
line is dropped rather than truncated when the row is shorter than 24 px or narrower than the text
plus its padding.

## Panel-scoped Delete

The lane area grabs keyboard focus on `mouseDown` (same as `GraphEditor::mouseDown`), so pressing
Delete right after a click lands on `TimelineClipLaneArea::keyPressed` rather than whichever panel
had focus before. This is the *local* half of Delete-key arbitration; [focus](focus.md) formalises
the cross-panel rule that decides which of the graph editor, clip lanes or piano roll a given
keypress belongs to in the first place.

## Loop the selection

**P = loop the selection** (Cubase's locators-to-selection) rides on that same local half: an
unmodified `P` handled in `TimelineClipLaneArea::keyPressed`, **not** a `ShortcutManager` command,
for exactly the reason Delete and Escape are not (see [`shortcuts.md`](../shortcuts.md) and
[focus](focus.md#delete-stays-panel-local)) — a bare letter in the app-wide table would fire from
any panel that does not consume it first.

The lane area owns no transport: `getSelectedClipSpan()` computes `[min startBeat, max endBeat]`
across the selection (any row, ignoring anything unselected) and hands it outwards through
`std::function<void(double startBeat, double endBeat)> onLoopRangeRequested`, which `MainComponent`
wires to `transport.setLoop(start, end, /*enabled=*/true)` — the same "the lane decides what, the
owner does it" division as `onAudioFileDropped` / `onRelinkAudioRequested`. Nothing selected, or no
owner listening, returns `false` so the key keeps its meaning elsewhere.

## Tests

`Tests/UI/Timeline/TimelineClipLane/` — `ClipSelectionModel` and `clipHitTestMarquee` unit coverage
(`TimelineClipLaneSelectionTests.cpp`), pure-geometry tests for `computeClipRect`
(`TimelineClipLaneAreaTests.cpp`), mouse interaction driven by hand-built `juce::MouseEvent`s
against a bare `TimelineDoc` + `AppUndoManager` + `TimelineClipLaneArea`, no `MainComponent` needed
(`TimelineClipLaneMouseTests.cpp`), painting (`TimelineClipLanePaintingTests.cpp`) and the tool
layer (`TimelineClipLaneEditToolsTests.cpp`).

The authoring gestures are split across three places, each covering the half it owns: the lane
area's (`TimelineClipLaneAuthoringTests.cpp` — snapping, one-bar length, one undo step, the
injected chooser, drag interest and the row highlight, the hint text and its paint), the panel's
(`Tests/UI/Timeline/TimelinePanel/TimelinePanelAuthoringGestureTests.cpp` — a new clip really opens
the piano roll), and the import's (`Tests/Project/AssetManagerTests.cpp` — saved-bundle vs
`Recordings/` destination, length from the file, failure mutating nothing).

`Tests/Timeline/TimelineClipEditing/` covers the document-level edit layer the tools sit on: each
tool's click-acts-immediately behaviour and its empty-space no-op, `findGlueTarget`'s gap-bridging,
Alt-copy vs plain move, the cross-track kind check (legal drop, illegal drop clamping the whole
group back to 0), the split and draw preview seams' repaint-only-on-change discipline via a
counting subclass overriding `requestToolPreviewRepaint`, the inline rename's commit/cancel/
focus-loss paths, and the clip clipboard's audio-field round trip
([focus](focus.md#clipboard-verbs-route-by-surface)).
