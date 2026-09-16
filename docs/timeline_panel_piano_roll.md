# Timeline Panel: Piano Roll

The per-clip MIDI note editor shown inside the timeline panel's lanes region (§2 below,
numbering kept as in the parent doc). Clip lanes, the automation strip, and the keyboard/focus
arbitration rule live in the companion document
[`docs/timeline_panel_clips_automation.md`](timeline_panel_clips_automation.md). The panel shell,
ruler/grid, track headers, playhead, transport bar, metronome/count-in and edit-tool strip live in
[`docs/timeline_panel_core.md`](timeline_panel_core.md),
[`docs/timeline_panel_tracks.md`](timeline_panel_tracks.md), and
[`docs/timeline_panel_transport.md`](timeline_panel_transport.md).

---

## 2. Piano Roll

`Source/UI/PianoRoll/PianoRollComponent/PianoRollComponent.h` (`synth::ui::PianoRollComponent`) is a minimal
per-clip note editor shown INSIDE the timeline panel's lanes region — no separate window. Backed by
`synth::ui::NoteSelectionModel` (`Source/UI/PianoRoll/NoteSelectionModel.h`), `ClipSelectionModel`'s sibling
keyed on `synth::NoteId` with the identical add/remove/toggle/setSelection/retainOnly contract,
plus a `noteHitTestMarquee` free function mirroring `clipHitTestMarquee`.

**Source layout** (`Source/UI/PianoRoll/PianoRollComponent/`, split by concern — FRO64, FRO75):
- `PianoRollComponent.h` — the class declaration, shared by every unit below. Each member's
  detailed contract lives as a doc comment next to its out-of-line definition in the matching
  `PianoRoll<Concern>.cpp` unit below, not in the header itself (kept under the file-size cap).
- `PianoRollComponent.cpp` — construction/teardown, clip open/close entry points, horizontal geometry.
- `PianoRollScaleAssist.cpp` — the scale-assist panel and its Generate action.
- `PianoRollPainting.cpp` — `paint()`, header chip glyphs, the local playhead line.
- `PianoRollEditTools.cpp` — editing gestures, the edit-tool verbs, split-tool hover preview, tool cursors.
- `PianoRollAudition.cpp` — keys-column audition, note audition, clip-overrun after resize, tooltips.
- `PianoRollClipboardAndKeys.cpp` — the note clipboard and arrow-key editing (nudge/transpose/navigate).
- `PianoRollMouse.cpp` — mouse handling and edge-auto-scroll.
- `PianoRollZoom.cpp` — anchored zoom and `keyPressed` dispatch.
- `PianoRollInternal.h` — private shared constants/helpers (not a CMake source file).
- `PianoRollTypes.h` — a self-contained header (own `#pragma once` + includes) defining `NoteHit`,
  `NoteOrigin`, `ClipboardNote`, `NoteGeometry`, `LineRange` and `ClipScaleMemory` at namespace
  scope inside a nested `synth::ui::pianoroll` namespace (so generic names like `LineRange` can't
  collide). `PianoRollComponent.h` re-exposes each as a nested-type alias
  (`using NoteHit = pianoroll::NoteHit;`, at the same access-section spot each used to be defined
  inline), so every existing `PianoRollComponent::NoteHit`-style qualified reference elsewhere keeps
  compiling unchanged. `AutoScrollTimer` stays defined inline in the class itself (it is
  constructed with a reference to the owning `PianoRollComponent` and calls its protected
  `autoScrollTick()`, so it has no reason to live outside the class). Not a CMake source file, same
  as `PianoRollInternal.h`.

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
the edit-tool strip and the follow-playhead button use — over the key's OWN
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
(`Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h`, see `docs/timeline_panel_core.md` §2) the clip lanes paint their own grid from, so the
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
`synth::ui::resolveNoteColour()` (`Source/UI/PianoRoll/NoteColour.h` — see [`theming.md` §12](theming.md#12-note-colours)):
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
| **Quantise Length** chip (or `Alt+Q`) | **Quantise Selected Note LENGTHS to the grid** (FRO107): the length twin of bare `Q` above — same selection-vs-all branching (per-note `resizeNote` in one mutation lambda when something is selected; `TimelineDoc::quantiseNoteLengths` directly when nothing is), same `divisionBeatsRaw()` grid source, same `isQuantiseEnabled()` gate. Rounds each note's `lengthBeats` to the nearest positive multiple of the grid, **floored at one grid unit so a note can never become zero-length**. The chip flashes on every press (mirroring the Quantise chip, since both share the same gate and never silently no-op the way Quantise Pitches does) — see *Header chips* below |
| `J` key (no chip — see below) | **Toggles grid magnetism** — flips the shared `TimelineViewState::snapEnabled`, so it switches off/on everywhere (roll, clip lanes, ruler) while the chosen division survives underneath. The key is the SHARED `timelineSnapToggle` (the timeline's own snap key moved off Q to J too), not a piano-roll duplicate of it. A view-state toggle, never a document edit — no undo step. Fires `onSnapToggled` so the panel persists the choice and repaints the other grid painters. **Magnetism only — the grid stays drawn** (see *Snap is magnetism, not visibility* below). The roll's own Snap chip was removed (FRO108): it duplicated the timeline toolbar's own Snap button, which reads/writes this SAME shared flag by reference — the timeline chip and the `J` key are full replacement coverage, since the roll is always shown as a child of `TimelinePanelComponent` and never detached from its toolbar |
| **Quantise Pitches** chip (or `Option+Shift+Q`) | **Quantise pitches into the scale**: `quantisePitchesToActiveScale()` snaps the selected notes' PITCHES (or every note in the clip when nothing is selected) via `MusicalScale::snapPitch`, leaving starts and the selection untouched. An ACTION chip, never lit — but painted dimmed (`isPitchQuantiseEnabled()`) when it would do nothing: no scale chosen for this clip, or an empty clip. A click with no scale is silently inert; the KEY falls THROUGH (`keyPressed` returns `false`) in the same case. **This chip is its ONLY entry point** besides the key — the duplicate button inside the Scale Assist panel is gone |
| **Show Only Scale Notes** chip (or `Option+S`) | Toggles the pitch-ROW filter for the open clip (`toggleScaleFilter()`): out-of-scale rows collapse out of the grid, and ↑/↓ start stepping by scale degree. A toggle, so it paints lit; dimmed with no scale chosen (the flag is still remembered — arm it first, pick the scale second). Shares ONE piece of state with the Scale Assist panel's checkbox |

**Header chips.** **Six** drawn chips (not child `juce::Button`s — they are painted shapes hit-tested
by position, `HeaderButtonId`), left to right: **"Clips"** (back), **Quantise**, **Quantise
Length**, **Quantise Pitches**, **"Scale"**, **Show Only Scale Notes** — the three quantise verbs
(position, length, pitch) grouped together in that order, the order the feature was designed in.
Each is a `juce::Rectangle<int>` member carved in `resized()` and resolved through the single seam
`headerButtonBoundsFor(which)`, which `updateHeaderButtonHover()` and `paintHeader()`'s hover wash
BOTH read — compute them separately and the lit rect drifts from the clickable one. Every chip does
exactly ONE thing on a plain click; there are no modifier variants left in the header at all (snap
and quantise used to share one chip that way, which is the ambiguity the split removes). The GAPS
carry meaning: 4 px between groups, 2 px within one, so "the three quantise verbs" reads as a
cluster and "scale + its row filter" as another. Only **Show Only Scale Notes** is a toggle, so it
is the only one that ever paints lit; the rest are actions and merely dim when they would be a
no-op. **Quantise** and **Quantise Length** additionally flash on every press (they share the same
`isQuantiseEnabled()` gate); **Quantise Pitches** does not — it is silently a no-op with no scale
chosen, matching its own dim.

There USED to be a chip, **Snap** — removed by FRO108, alongside its glyph, because it
duplicated the timeline toolbar's own Snap button: both read/write the SAME shared
`TimelineViewState::snapEnabled` by reference (`PianoRollComponent.h`'s `viewState_` member is
commented "shared: SNAP ONLY"), not two synced copies, and the roll is always shown as a child of
`TimelinePanelComponent` with no standalone mode that would leave that toolbar behind. The `J` key
(unchanged) and the timeline's own chip are full replacement coverage. FRO107's **Quantise Length**
action (`Alt+Q`) now has its own chip in the strip instead — the same visible affordance `Q` and
`Alt+Shift+Q` already had.

**Four of them carry drawn vector glyphs rather than letters**, because letters were the problem:
a bare "Q" for length- or pitch-quantize would have told the user nothing about which quantise verb
it was. All four are pure `juce::Path` / `fillRect` drawing against the chip's rect, in a colour the
caller derives from the fill it actually painted (so they stay legible on resting, hover and lit
fills in every theme), and no font or `IconLibrary` entry is involved — the same "draw it, don't
asset it" rule the back arrow follows:

| Chip | Glyph | Why it reads |
|---|---|---|
| Quantise | Two small blocks landed flush on two faint **vertical** gridlines, vertically staggered | The axis IS the meaning: this verb moves notes horizontally in time, so the grid it snaps to is vertical. Staggering the pair reads as two notes rather than one bar |
| Quantise Length | ONE wider block whose **trailing (right)** edge snaps onto a single faint vertical gridline, with a short arrow pushing that edge onto the line | Deliberately not the Quantise glyph rotated or restyled: one block instead of two, and the marked edge is the block's END rather than its START, since this verb resizes a note rather than moving it |
| Quantise Pitches | A **note head** on the lowest of three faint **horizontal** rows, with a down arrow pushing it there | The same "snapped onto the grid" idea rotated 90°, which is exactly the difference between the two verbs — so they are tellable apart without the tooltip |
| Show Only Scale Notes | A **funnel** | The one mark that reads as "filter" everywhere. Deliberately not an eye (the rows are *removed from the row mapping*, not merely hidden) and not a keyboard (indistinguishable from the keys column two pixels below) |

"Scale" keeps its word: it is the one label here naming a NOUN (a panel) rather than a verb, and a
glyph for "the scale picker" would be a guess. Every tooltip is rebuilt per query through
`synth::shortcutHintFor` (`quantiseTooltipText()` / `quantiseLengthTooltipText()` /
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

**Scale Assist panel.** `Source/UI/PianoRoll/ScaleAssistPanel.h` (`synth::ui::ScaleAssistPanel`) is a
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
§1 above) via the same `Source/UI/Timeline/EdgeAutoScroll.h` helper and constants, extended to BOTH axes:
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
`TimelineClipLaneSelectionTests.cpp`'s `ClipSelectionModel`/`ClipMarqueeHitTest` coverage) and `PianoRollComponent` interaction tests driven by
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

