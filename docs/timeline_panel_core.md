# Timeline Panel: Core

This document covers the timeline panel's shell, the ruler/grid/zoom/scroll/snap and loop-brace
behaviour, track headers and binding chips, the playhead, the transport bar, the metronome and
count-in, and the edit-tool strip. Clip lanes, the piano roll, the automation strip, and the
keyboard/focus arbitration rule live in the companion document,
`docs/timeline_panel_clips_automation.md`.

- [1. Timeline Panel](#1-timeline-panel)
- [2. Ruler, Grid, Zoom/Scroll, Snap, Loop Brace](#2-ruler-grid-zoomscroll-snap-loop-brace)
- [3. Track Headers, Binding Chips, Add-Track](#3-track-headers-binding-chips-add-track)
- [4. The Playhead](#4-the-playhead)
- [5. The Transport Bar](#5-the-transport-bar)
- [6. Metronome + Count-In](#6-metronome--count-in)
- [7. Edit-Tool Strip](#7-edit-tool-strip)

---

## 1. Timeline Panel

`Source/UI/TimelinePanelComponent.h/.cpp` — `synth::ui::TimelinePanelComponent`, a bottom-docked
panel owned by `MainComponent`. This section was written incrementally, one piece at a time — the
shell came first, then the ruler, track headers, playhead and transport bar filled it with real
content, and finally the keyboard/focus rule that arbitrates between it and the graph editor — each
subsection below (or in the companion clips/automation document) is still the reference for its own
piece, and this intro is only a map of how they compose.

Region layout, low-rate transport poll aside (see §4 below), everything here is pure
layout-plus-paint with no timer or animation of its own — one region diagram for the whole panel:

```
+====================================================================+  <- resize grab strip
| Transport bar strip  (play/stop/record/loop, BPM, time-sig, ruler   |  §5 (+ §2's
| readout, metronome/count-in .......................... snap combo) |   snap selector)
+---------------------+----------------------------------------------+
| "+ Track"            | Ruler  (bar/beat ticks, loop brace)          |  §3 (+ §2)
| Track header column  +----------------------------------------------+
| (name/colour/M/S/R/  | Clip lanes  <-or->  Piano roll               |  §3 / clips §1 <-> roll §2
|  binding chip),      | (one of the two, same rect, playhead overlay |
|  scrolls              | drawn on top of either)                     |  §4 (playhead)
|                       +----------------------------------------------+
|                       | Automation strip (opens by shrinking the     |  automation §3 (docked,
|                       |  region above by its own fixed height)       |   optional)
+---------------------+----------------------------------------------+
```

(§ numbers in the diagram above refer to this document except *clips*/*roll*/*automation*, which
point to §1/§2/§3 of `docs/timeline_panel_clips_automation.md`.)

Keyboard focus is orthogonal to this diagram, not another region: whichever of the graph editor /
clip lanes / piano roll the user last clicked owns Cmd+C/V/D, per the **Keyboard & focus**
subsection in `docs/timeline_panel_clips_automation.md` §4.

The timeline is GA: the toolbar button (`MainComponent::toggleTimelineButton`), the Cmd+T command,
and the Space play/stop transport key are always available. (An earlier "Show timeline
(experimental)" Preferences kill switch has been removed; a stale `timelineFeatureEnabled` key left
over from it in an existing install's `ApplicationProperties` is simply ignored.)

### What it is

The always-present scaffold: a themed background (`theme.colors.bg0`-family, same fallback
pattern as the toolbar/status-bar/sidebar panels), a thin top border separating it from the graph
editor above, and three child regions laid out in `resized()` — the diagram above is what each one
now holds; the panel initially left the lanes/ruler area an unfilled centred "Timeline" placeholder, all
since replaced by the ruler/grid/clips/piano-roll/playhead content the subsections below describe:

- **Transport bar** (top strip, `Metrics::timelineTransportBarHeight`)
- **Track-header column** (left, `Metrics::timelineTrackHeaderWidth`)
- **Lanes/ruler area** (remainder)

All three are exposed as public rect getters (`getTransportBarBounds()`, `getTrackHeaderBounds()`,
`getLanesBounds()`) so every later task and its tests build on the same arithmetic instead of
re-deriving it. The component owns no timer and no animation of its own (the playhead overlay's,
§4 below, and the automation strip's knob-entry-point, `docs/timeline_panel_clips_automation.md`
§3, aside).

### Docking, toggle, shortcut

`MainComponent` carves the panel full-width, directly above the status bar: `resized()` — the one
geometry authority, see `docs/layout_visuals_animation.md` §3's `PanelSlide` subsection — removes it from the bottom AFTER the status
bar and BEFORE the AI-panel/library removals, so it spans the whole window width regardless of
which side panels are open. The height it removes is `timelineSlide_.sizeBetween(0,
timelinePanelHeight_)`, i.e. the user's height scaled by the panel's open fraction, so the same
carve serves both the docked panel and every frame of its slide.

A toolbar toggle (`ToolbarComponent::Slot::ToggleTimeline`, right-hand group, immediately before
`ToggleTheme`) and the **Cmd+T** shortcut (action id `toggleTimelinePanel`; see
[`shortcuts.md`](shortcuts.md)) both flip `MainComponent::isTimelineVisible`. Visibility persists
under the `timelinePanelVisible` key in `juce::ApplicationProperties`, default `false`.

### Height: user-resizable, persisted

The panel's height is **not** fixed. `Metrics::timelinePanelHeight` (220) is the **default and the
minimum**, no longer the law:

- **`MainComponent` owns the value** (`timelinePanelHeight_`), and it is the only thing that lays
  the panel out. `resized()` — and therefore **every frame of the show/hide slide**, which is just
  `resized()` at a moving fraction — reads the member, never the metric directly. A height drag
  landing mid-slide needs no special case for the same reason.
- **Clamp rule**, applied in `MainComponent::clampTimelinePanelHeight()` on every layout pass, not
  only when the user drags: `[Metrics::timelinePanelHeight, max(metric, 75% of the window height)]`.
  Re-clamping per pass is what stops a height saved on a large window from swallowing a smaller
  window's canvas; on a window so short that 75% falls under the metric, the floor wins. Before the
  first layout (window height still 0) only the floor applies — otherwise construction would clamp a
  persisted height away against a window that does not exist yet.
- **Persistence**: the `timelinePanelHeight` int key (same name as the metric) in
  `juce::ApplicationProperties`, absent by default — absence is what makes the metric the default.
  Written **once per gesture**, on drag end, never per pixel.
- **The grab strip** (`TimelinePanelComponent::kResizeHandleHeight = 5`, `MouseCursor::
  UpDownResizeCursor`) spans the panel's full width along its top edge, *overlapping* the transport
  bar strip: `getTransportBarBounds()` still starts at `y == 0` (the three regions tile exactly, as
  before), but the transport controls inside it are laid out below the strip, so a resize grab never
  lands on a transport button. Idle it paints exactly the hairline the panel already drew there;
  hovered or dragging it brightens to the accent colour with a faint wash, and it repaints **only on
  a hover-state change** (`docs/layout_visuals_animation.md` §2–3's repaint discipline).
- **The panel never resizes itself.** Dragging reports the *desired* height — measured absolutely,
  from the panel's pinned bottom edge in screen coordinates, so the owner moving the top edge under
  the cursor can't make the gesture chase itself — through `onResizeHeight` (every drag step,
  unclamped) and `onResizeHeightCommitted` (once, on mouse-up: the cue to persist). The owner clamps,
  stores and re-runs its layout on each step, which is what makes the drag live. That is one
  user-driven layout pass per mouse event, not a free-running animation.

### Animation

The slide in/out is the **same** `PanelSlide` + one shared `AnimationDriver` the library and AI
panels use (`MainComponent::beginPanelSlide()`, ~190 ms ease-in-out-cubic, one shared
`VBlankAnimatorUpdater`) — no animator or timer of its own, and no timeline-specific animation code
at all: only the axis differs, and that lives in `resized()`'s carve order. `timelineSlide_`'s
fraction drives the height against a pinned bottom edge, so the panel grows upward into place and
shrinks back down the same way; `setVisible(false)` happens in `finishPanelSlide()`, once the slide
is actually done, same as the sibling panels. See `docs/layout_visuals_animation.md` §3 for the full contract (mid-flight reversal,
the synchronous off-screen path, why one driver serves all three).

### No build-time (or runtime) gating

Everything past the always-present `TimelinePanelComponent` member and the `resized()` carve — the
toolbar button and the `toggleTimelinePanel` command included — is ordinary, always-compiled,
always-active code: there is no build configuration and no runtime preference that omits the
button, the shortcut, or the panel carve.

Contents — ruler/grid/snap, track headers, the playhead and the transport bar are covered in the
subsections below; clip lanes, the piano roll, the automation strip, and finally the keyboard/focus
rule tying it to the graph editor are covered in `docs/timeline_panel_clips_automation.md`.

## 2. Ruler, Grid, Zoom/Scroll, Snap, Loop Brace

`Source/UI/TimelineViewState.h` (`synth::ui::TimelineViewState`) is the single pure,
headless-testable beat<->pixel mapping shared by every consumer: `pixelsPerBeat` (zoom, clamped to
`[kMinPixelsPerBeat=1.5, kMaxPixelsPerBeat=512]`), `firstVisibleBeat` (horizontal scroll, clamped
`>= 0`), `beatToX()`/`xToBeat()`, `zoomAroundX(factor, anchorX)` (keeps the beat under `anchorX`
fixed on screen, clamps preserved — see the comment on why the anchor invariant yields to the
`firstVisibleBeat >= 0` clamp at extreme zoom-out), `scrollBeats(delta)`, and `snapBeat(beat,
beatsPerBar)`. No JUCE dependency; `TimelinePanelComponent` owns the one instance and exposes it
via `getViewState()`.

**Snap** (`TimelineViewState::Snap`) — `Off, Bar, Whole, Half, Quarter, Eighth, Sixteenth,
ThirtySecond, SixtyFourth, HundredTwentyEighth`. Every
non-`Bar`/non-`Off` value is a **note value** (a fraction of a whole note), the DAW-conventional
reading of the grid selector: `Whole` (combo label `"1"`) = 4 beats — a full 4/4 bar, `Half` = 2,
`Quarter` (label `"1/4"`, the default) = 1 beat, `Eighth` = 0.5, `Sixteenth` = 0.25, down through
`"1/32"` = 0.125, `"1/64"` = 0.0625 and `"1/128"` = 0.03125 (the three finest were APPENDED after
`Sixteenth` — the enum's int is persisted, so inserting in note-value order would silently
reinterpret every saved grid choice). `Bar` snaps to
multiples of `beatsPerBar` (`tsNum * 4 / tsDen` off the transport's time signature — same formula
`TransportService::getPosition()` already uses). Ties round up (toward +infinity, beats are never
negative in practice). On top of the division sits a master switch, `TimelineViewState::snapEnabled`
— the panel's `Snap` button and the panel-wide **`J`** key toggle it (Cubase's snap key; `Q` is
Cubase's *quantise*, which is what the roll uses it for, so one letter no longer means two verbs
depending on which timeline surface has focus).

**The switch governs MAGNETISM, not the grid.** `divisionBeats()` — what every magnetic edit reads —
returns 0.0 while the switch is off; `divisionBeatsRaw()` keeps returning the chosen division, and
**every grid PAINT site reads the raw one**. Turning snap off must not change which lines are drawn:
it stops edits being pulled onto the division, and a ruler that dropped its subdivision lines at the
same moment left the user eyeballing positions against nothing. The lanes-grid painter
(`TimelinePanelComponent::paint()`) and the piano roll's gridlines both take `divisionBeatsRaw()`
for that reason; `TimelineClipLaneArea::floorSnappedBeatAt`/`ceilSnappedBeatAt`/`minDrawLengthBeats`
take `divisionBeats()`, because those ARE the magnetism. `TimelinePanelGridDrawingTest.SnapOffKeeps-
TheSubdivisionLinesDrawn` pins the split.
Picking a division from the combo flips the switch back on. The snap choice lives in a
`juce::ComboBox` docked in the transport bar's right-hand side (items `Off/Bar/1/1⁄2/1⁄4/1⁄8/1⁄16`;
`synth::ui::TimelineTransportBar` fills the rest of that strip, left-aligned — see §5 below) and
persists under the `"timelineSnap"` int key (plus `"timelineSnapEnabled"` for the switch) in
`juce::ApplicationProperties`, set via
`TimelinePanelComponent::setApplicationProperties()` (non-owning pointer setter, same shape as
`AIChatComponent::setAccountService()`).

`Source/UI/TimelineRulerComponent.h/.cpp` (`synth::ui::TimelineRulerComponent`) is a thin strip
(`Metrics::timelineRulerHeight = 24`) docked at the top of the lanes region. It owns nothing: a
`TimelineViewState&` (shared with the panel), an optional `synth::TransportService*`, an optional
`synth::TimelineDoc*` (the **markers** it draws and edits — see *Markers* below) and an optional
`AppUndoManager*`, all four non-owning setters that may stay null. `paint()` reads `getPositionSnapshot()` once per frame (message thread,
cheap) for the time signature and loop bounds, and draws three adaptive-density bands as you zoom
in — Cubase's own progression, and the SNAP division has no say in any of them (a 1/16 snap on a
zoomed-out arrangement must not turn the strip into a grey smear):

1. **far out** — bar lines + bar numbers only; the labelled-bar stride itself doubles (by powers of
   two) until labelled bars are `>= 40px` (`kMinLabelSpacingPx`, `.cpp`-local) apart, so bar labels
   never overlap regardless of zoom;
2. **mid** — short, half-height beat ticks appear between the bar lines once `pixelsPerBeat >= 8`
   (`kMinBeatTickSpacingPx`);
3. **close in** — those beat ticks also earn a small, dim `"bar.beat"` sub-label (`"80.2"`, 1-based
   on both halves, faded to 65% alpha and two points smaller than the bar number) once
   `pixelsPerBeat >= 48` (`kMinBeatLabelSpacingPx`).

`rulerTickPlanFor(pixelsPerBeat, beatsPerBar)` (`Source/UI/TimelineRulerComponent.h`) is the ONE
pure decision behind bands 2–3 (`RulerTickPlan{drawBeatTicks, drawBeatLabels}`) — a label implies a
tick by construction (a label with no tick to sit against would float), and a bar of one beat or
fewer draws no ticks at all (there is nothing non-bar to mark). `paint()` calls it once per frame,
never re-derives it inside the per-bar loop, and the same helper drives
`Tests/TimelinePanelTests.cpp`'s `TimelineRulerTickPlanTest` suite directly against the two
thresholds — deterministic and font-independent by design (no measured text width anywhere in the
decision), so the guard means the same thing on every platform. A bracket over `[loopStartPpq,
loopEndPpq]` completes the strip. No timer of its own: `repaint()` is called after one of its own
interactions, or by the panel's 10 Hz poll when the time signature or loop range changed from
elsewhere (§4, below). The moving position line is the separate `TimelinePlayheadOverlay` drawn
over it.

**Two interaction zones.** The strip is split horizontally at `height / 2`
(`TimelineRulerComponent::Zone`): the **top** half owns the loop range, the **bottom** half owns the
playhead. The boundary row belongs to the playhead (`y < height * 0.5` is the loop zone). Both
gestures are therefore reachable with no modifier, which is the point — a plain drag anywhere in the
bottom half scrubs the cursor, which is what people expect a ruler to do.

The zone is decided **once**, from the `mouseDown` y, and latched in `gestureZone_` for the whole
gesture. A drag routinely leaves the band it started in (and often the strip entirely), and a
gesture must never change meaning mid-flight.

| Gesture | Effect |
|---|---|
| Press in the bottom (playhead) zone | `transport->locateBeat()` to the snapped beat under the cursor — on **press**, not release |
| Drag in the bottom zone | Keeps seeking as the pointer moves: the cursor follows the mouse (scrub) |
| Press-drag-release in the top (loop) zone | `transport->setLoop(min, max, true)` to the snapped `[start,end]` range (dragging leftwards normalises) |
| Click (no drag) in the top zone, **on a dimmed brace** | Re-arms that range (`setLoop(start, end, true)`), bounds untouched — the inverse of the Cmd+click below |
| Click (no drag) in the top zone, anywhere else | **Nothing.** Deliberate: the loop range is all this half owns, so a stray click can't clear or collapse it |
| Cmd+click (no drag), either zone | Toggles looping off, keeping the existing bounds |

**Locator visibility.** The brace is drawn whenever a RANGE exists (`loopEnd > loopStart`) —
**not** only while looping is armed. Disarming greys it out instead of hiding it, so a loop you set
earlier stays findable (and re-armable) rather than disappearing the moment you switch looping off.
`TimelineRulerComponent::braceStateFor(looping, start, end)` is the pure rule
(`BraceState::None / Inactive / Active`) that `paint()` and the `getBraceStateForTest()` seam both
call, so a drawn brace and an asserted one can't diverge; `braceColourFor()` picks the colour —
`colors.accent` armed, `colors.textMuted` at 70% alpha disarmed. Muted *text*, not a faded accent:
the hover band is already accent at 10% alpha, and a dimmed accent brace over it would still read as
lit. The click target for the re-arm is the brace's whole x-span across the loop half, not the 4 px
bar it paints (4 px is not a click target).

Both drag paths share the same throttle discipline: a command is posted only when the snapped value
changed since the last post, never once per pixel of movement (`TransportService`'s command FIFO
dedupes nothing). `postSeekIfChanged` dedupes on the beat clamped to `>= 0` — the same clamp
`locateBeat` applies — so dragging left of beat 0 can't re-post identical seeks; `postLoopIfChanged`
dedupes on the `[start,end]` pair. `mouseUp` calls its zone's poster again, so it is a no-op when the
last drag update already posted the final value.

**Hover affordance.** `mouseEnter`/`mouseMove` set the hovered zone AND the hovered marker,
`mouseExit` clears both. `applyHoverCursor()` is the ONE place the cursor is decided — a marker flag
wins (`DraggingHandCursor`), otherwise it is per zone (`PointingHandCursor` over the playhead half,
`LeftRightResizeCursor` over the loop half). It is split out precisely because each hover setter only
fires on *its own* change: a zone change while the pointer sits inside a flag would otherwise leave
the zone's cursor showing over a draggable marker. `paint()` tints the hovered half with `accent` at
10% alpha, under the loop brace so the brace stays legible. `repaint()` fires only when the hovered
zone or the hovered marker actually **changes** — never per mouse-move pixel — and there is no timer
or animation involved (`docs/layout_visuals_animation.md` §3).

### Markers (position + text + colour)

A **marker** is a named position in the arrangement — a cue point, not a track. It has no clips, no
binding and no audible effect, so it lives on `TimelineDoc` itself (`std::vector<Marker>`, see
[`architecture.md`](architecture.md)) rather than in the track list: a "marker track" would have to be
excluded from every place that iterates tracks to make sound.

**Where they come from.** The `"+ Track"` button's menu gained an **Add Marker** entry below a
separator (`kAddMarkerMenuId` — see *Track headers* below). It calls
`TimelinePanelComponent::addMarkerAtPlayhead()`: a marker at the transport's CURRENT position
(**unsnapped** — a cue marks something the user just heard, so it belongs exactly where the playhead
is; a later drag *does* snap), named `"Marker N"` from the existing marker count, coloured from
`Theme::Colors::warning` (the amber family — deliberately not `accent`, which the loop brace and the
playhead already use in this same 24 px strip). One `recordTimelineChange`. It needs no
`TrackHeaderHost`, which is why `applyAddTrackMenuChoice` handles it *before* the host null-check.

**How they draw.** `buildMarkerFlags()` is the single enumeration `paint()` and hit-testing both walk
— computing them separately is how a drawn flag drifts from the clickable one, the same rule
`GraphEditor::buildVisibleCables` exists to enforce for wires (`docs/layout_selection_canvas.md` §3). Each flag is a filled tab in the
strip's own **marker band** — a dedicated sub-band along the bottom edge — anchored at the marker's
beat and running right, plus a full-height 1 px stem at the beat itself so the exact position stays
readable when the label is clipped. Culling is **per flag**, not per marker: a flag whose anchor has
scrolled off the left edge may still have most of its tab on screen.

**The band, and why it exists.** Flags were first drawn in the strip's lower HALF — which is where
the bar numbers are vertically centred, so a marker at beat 4 was painted straight over the ruler's
own "4". The numbers row and the marker band now **tile** the strip's height instead of overlapping
it: `rulerLabelRowHeight(componentHeight)` is the height the bar/beat labels are centred in
(`height - kMarkerBandHeight`, falling back to the full height once that would leave the numbers less
than `kMinNumbersRowHeight`), and everything below it belongs to markers. That helper is the ONE
place the split lives — `paint()` centres its labels in it and `buildMarkerFlags()` starts the band
where it ends, so a number and a flag can never be handed overlapping rows.

**`Metrics::timelineRulerHeight` is 30, up from 24, to pay for that split.** At 24 the two rows had
to share a strip sized for one, which squeezed the flag to 9 px with a 9 pt label — that is where
"the flag is barely visible" came from. At 30 the numbers get 17 px (comfortable for the 11 pt bar
font) and the band gets 13. The metric has exactly one production consumer
(`TimelinePanelComponent::resized`, which carries the same literal as its headless fallback).

**Making a flag actually readable.** The label font is `kMarkerFlagFontHeight = 11` — the SAME size
as the bar numbers, because a marker label is a name the user typed and has to compete with the
ruler's own text, not whisper under it. The tab is filled at full opacity and outlined with a 1 px
`darker(0.5f)` edge, so a light flag on a light theme still has a shape instead of bleeding into the
strip. Label colour comes from `markerLabelColourFor(fill)` — black or white by the fill's own
`getPerceivedBrightness()`, i.e. **maximum** contrast, deliberately stronger than
`PianoRollComponent::labelColourFor`'s `contrasting(0.7f)`: a marker colour is arbitrary user data
and 11 pt inside a 13 px tab has no legibility to spare on the mid-tones.

**The stem, in two places.** `kMarkerStemWidth` is 2 px, not a hairline — at 1 px in the marker's own
colour it vanished against the bar lines it crosses. In the RULER it runs the full height: full
opacity across the marker band and `kMarkerStemAlpha` above it, so it reads as belonging to the flag
without competing with the numbers it passes through. It is the one thing that deliberately crosses
the row boundary, because it is what names the exact beat.

`TimelinePanelComponent::paint()` then continues each stem **down through the lanes** at
`kMarkerLaneStemAlpha` (0.40), which is what makes a marker locatable against the clips rather than
only against the ruler. That is a static painted line on the panel's existing change-driven repaint
path — `timelineChanged()` repaints the ruler and `gridLanesBounds_` — so there is no timer and no
per-frame work; the [§3](layout_visuals_animation.md) animation rules are untouched. Off-screen
markers are culled per marker, not clamped to an edge.

The tab's WIDTH comes from the label's character COUNT (`markerFlagWidthFor(textLength)`, clamped to
`[kMarkerMinFlagWidth, kMarkerMaxFlagWidth]`), never from measured text — the clickable rect and the
painted rect are the same rect, and a font-measured width would make a hit-test assertion mean
something different on every platform (the same discipline as the bar-label stride). Label text is
drawn in black or white by `getPerceivedBrightness()` against the marker's OWN colour: a marker colour
is arbitrary, so a fixed theme token would go invisible over half the palette.

**Repaint discipline.** The ruler never listens to the doc and never polls it. A marker only moves as
the result of a mutation, and that mutation already notifies — so
`TimelinePanelComponent::timelineChanged()` calls `ruler_.repaint()`, and that is the whole
mechanism. No timer (`docs/layout_visuals_animation.md` §3).

**Gestures.** Marker flags are hit-tested BEFORE the zone split and before the transport null-check
(a marker lives on the doc, so it stays editable in a build with no transport wired in). Anything
outside a flag rect behaves exactly as it did.

**The right button is inert apart from opening the menu** — and that gate is a bug fix, not tidiness.
The marker context menu appeared for a split second and then dismissed itself, because:

1. `mouseDown` on a right-click showed the menu and returned **before** latching `gestureZone_`,
   which therefore still held whatever the previous gesture left in it (its initialiser is
   `Zone::Playhead`);
2. the ruler had captured the mouse on that press, so JUCE delivered the matching `mouseUp` to it
   even with the menu modal;
3. `mouseUp` found no marker drag latched, fell through to the zone branch, and — reading that stale
   `Zone::Playhead` — ran `postSeekIfChanged()`, which calls `TransportService::locateBeat()` **and**
   `repaint()` on the very component the menu had been anchored to via `withTargetComponent(this)`.

`TimelineClipLaneArea` never had the problem because its `mouseDown` gates the whole gesture path
behind `if (!e.mods.isLeftButtonDown()) return;`, so a right-click there latches nothing and its
`mouseDrag`/`mouseUp` are structurally inert. The ruler now mirrors that: `mouseDown`, `mouseDrag`
and `mouseUp` all return early on `e.mods.isPopupMenu()`, and `openMarkerContextMenu` uses a plain
`juce::PopupMenu::Options()` exactly as `showClipContextMenu` does — no `withTargetComponent`, which
on a full-width 24-to-30 px strip also anchored the menu somewhere unrelated to the flag clicked.
A side effect worth naming: right-clicking the ruler used to **seek the playhead**, for the same
reason. It no longer does. Pinned by `TimelineRulerMarkerTest.RightClickOnAFlagLatchesNothingAnd-
PostsNothing` and `RightClickOffAFlagDoesNotSeekOrLoop`.

**Opening the menu is a protected virtual (`openMarkerContextMenu`), and that is a CI requirement,
not a style choice.** A live `juce::PopupMenu` creates a real top-level window, and JUCE positions it
against a display it looks up from the mouse point. On a display-less runner — the Linux Debug
coverage job — that lookup returns null and `MenuWindow::calculateWindowPos` dereferences it:
**SIGSEGV**, while macOS and Windows stay green because they have a display. The first version of the
right-click tests reached the real path and crashed CI for exactly this reason (round 2's tests never
exercised it, which is why the trap only sprang once the gesture was covered). The tests now
subclass the ruler and override this to record `(menuRequests, lastMenuMarker)`, asserting *which
marker the menu was requested for* alongside the gesture-state invariants, and drive the outcomes
through `applyMarkerContextChoice`. Production behaviour is unchanged. Same seam idiom as
`PianoRollComponent::promptExtendClipToFitNotes` and `requestRepaintStrip`.

`TimelinePanelComponent::openAddTrackMenu` is the same seam for the `"+ Track"` menu. No test reaches
it today (they all drive `applyAddTrackMenuChoice`, the documented headless entry point), but a test
that clicked the button would crash identically — the override point exists before someone writes
that test. Those two are the only real-window creations under the panel and the ruler; the marker
colour picker's `CallOutBox` is launched from inside `openMarkerContextMenu`'s own menu item, so the
override covers it too, and both inline rename editors are ordinary child components with no window
of their own.

| Gesture | Effect |
|---|---|
| Drag a flag | Moves the marker, snapped through the shared `TimelineViewState` and clamped at beat 0. The drag is a **preview** (`markerDragBeat_`); the drop commits it as ONE `moveMarker` undo step. `buildMarkerFlags()` reports the preview beat, so the flag the user is looking at is the one the drop commits — and hit-testing follows it for free |
| Press a flag with no drag | **Nothing.** A stray click must not quietly re-snap the marker it landed on |
| Cmd+click a flag | Falls through to the zone gesture (switch looping off). A marker must not punch small dead holes in a strip-wide gesture — and Cmd is not `isPopupMenu()`, so a macOS Ctrl+click still opens the menu below |
| **Double-click a flag** | Opens the inline rename editor directly — the discoverable path, next to the menu's `Rename…` rather than replacing it |
| Right-click a flag | **Rename… / Change colour… / Delete** (`MarkerContextChoice`) |

**Double-click to rename** is scoped to flag hits only. The ruler had **no** `mouseDoubleClick`
override at all before this, so nothing was taken: off a flag the gesture still behaves as the two
ordinary clicks it always was (a seek in the playhead zone, the start of a loop drag in the loop
zone), and a right-button double-click stays the context menu's business.

One ordering detail is load-bearing. JUCE delivers a double-click as *down, up, down, doubleClick,
up* — so by the time `mouseDoubleClick` runs, the second press has already latched a marker drag.
The handler therefore **leaves the latch alone** and only clears `markerDragMoved_`: the trailing
`mouseUp` then takes the marker branch, commits nothing (a press that never dragged is a no-op) and
tidies up. Clearing `draggingMarker_` instead would send that `mouseUp` into the loop/playhead branch
under a STALE latched `gestureZone_` and seek the cursor out from under the editor being opened.

`Rename…` opens an inline `juce::TextEditor` over the flag (`beginRenameMarker`), widened to at least
96 px because an unlabelled flag's tab is 9 px and nobody can type into that, and pulled back inside
the strip. Return commits, Escape cancels, clicking away **commits** — the same reading
`TimelineClipLaneArea::beginRenameClip` has, and any press elsewhere in the strip commits an open
rename first. A label over `TimelineDoc::kMaxMarkerTextLength` is refused by the doc, which leaves the
existing one in place: a rejected edit is not an erase. `Change colour…` reuses
`synth::ui::ColourPickerPopup` with exactly the preview-writes-live / commit-is-one-undo-step shape
`TimelineTrackHeaderComponent`'s track swatch uses, sharing the same favourites shelf. Every mutation
routes through `performMarkerEdit()`, the one undo seam (`recordTimelineChange` with a manager
installed, a direct call without one).

`Rename` and `ChangeColour` are deliberately INERT as `MarkerContextChoice` values — they open UI
rather than mutating, and neither has a headless meaning. The enum exists so the menu's whole
vocabulary is enumerable and a test can assert those two mutate nothing; the commit paths are
`renameMarker()` and the picker's own `onCommit`. Same split as
`TimelineClipLaneArea::ClipContextChoice::Rename`.

**Persistence** rides `TimelineDoc::toVar`/`fromVar` (so the project bundle and undo/redo carry
markers with no new code), and untrusted marker data is gated by `synth::validateTimeline` — see
[`AI_Engine.md`](AI_Engine.md). The reserved-`"timeline"` refusal on the untrusted PATCH path is
untouched.

**Grid.** `TimelinePanelComponent::paint()` draws the SAME bar/beat/subdivision hierarchy directly
into the lanes region below the ruler, from the shared three-level policy in
`Source/UI/TimelineClipLaneArea.h` (`GridLineLevel::{Subdivision, Beat, Bar}`,
`gridLineColourFor`/`gridLevelIsReadable`) — `PianoRollComponent` paints its own grid from the SAME
policy (`docs/timeline_panel_clips_automation.md` §2), so the two surfaces can never drift apart in what's visible at a given zoom.
Per-level alpha is monotonic (`Subdivision` 0.28, `Beat` 0.50, `Bar` 0.85) and each line is lifted
halfway (`kGridLineContrastMix = 0.5`) from the theme's `border` token toward the background's
CONTRASTING colour (`background.contrasting(1.0f)` — black or white) before that alpha is applied:
raising alpha alone can't fix a dark theme, where `border` is already only a shade or two off `bg0`,
so even alpha 1.0 would read as barely-there; mixing toward the contrasting extreme is what makes
the line a LINE while keeping the theme's own hue in it, with no new token to re-skin. A whole level
is DROPPED rather than drawn as a wall of touching pixels once its spacing falls under
`kMinGridLinePixels = 3.0` px (`gridLevelIsReadable`) — the same "no grid is better than a smear"
call the ruler's own adaptive density makes above, and Cubase's own rule. Beat lines gate at
`pixelsPerBeat >= 8` (`kMinBeatLinePixelsPerBeat`, `TimelinePanelComponent.cpp` — the same threshold
the ruler's beat ticks use); the subdivision level draws only when the current snap DIVISION is
finer than a beat, the beat level is ALSO drawn (the hierarchy stays monotonic — a subdivision may
never outlive its parent beat level, which the ~8px/~3px gap between the two gates would otherwise
allow), and it clears its own density guard.

**Wheel, scroll direction, and keyboard zoom/grid commands.** `mouseWheelMove()` is implemented once
on the panel (JUCE bubbles an unhandled wheel event from the ruler child up to it, so both regions
share identical behaviour from one implementation) with Cubase-style bindings: **plain vertical
wheel scrolls the track rows vertically** (`TimelineViewState::trackScrollY`, shared with the header
column — a scrollbar drag on the header viewport writes the same value back via
`HeaderViewport::onScrolledY`, and `syncTrackScroll()` is the one re-sync point); **Shift+wheel or a
trackpad's own `deltaX` scrolls horizontally** (converted to beats at the current zoom — a constant
*pixel* distance per wheel unit, so the same physical gesture covers less musical time zoomed in);
**Cmd+wheel** zooms horizontally around the cursor; **Cmd+Shift+wheel** zooms vertically, scaling
`TimelineViewState::rowHeightScale` within `[0.5, 3.0]` (multiplies the themed row height in BOTH
`TimelineClipLaneArea::getRowHeight()` and the panel's `layoutTrackHeaders()`, anchored so the row
under the pointer stays put). `mouseMagnify()` (trackpad pinch — deliberate enough to need no
modifier) maps plain pinch to horizontal zoom and Shift+pinch to vertical, on the panel and inside
the piano roll alike.

**The two zoom-decided branches read `synth::ui::dominantWheelDelta(wheel)`
(`Source/UI/ScrollPolicy.h`), never a single axis.** macOS folds a Shift-held wheel gesture into
`deltaX` (the OS's own axis swap), so a branch that is selected by its MODIFIERS and reads only
`deltaY` — Cmd+Shift+wheel here, and its Cmd+Shift+wheel twin in the piano roll below — would
receive exactly `0.0` under that fold and silently do nothing; `dominantWheelDelta` (`deltaY != 0 ?
deltaY : deltaX`) is the one-line guard both zoom branches share, verified against
`juce_MouseEvent.h`/`juce_Viewport.cpp` rather than assumed. A plain-scroll branch, by contrast,
reads the axis the gesture actually ARRIVED on (`std::abs(deltaX) > std::abs(deltaY) ? deltaX :
deltaY`) — a Shift+wheel the OS left on `deltaY` and a trackpad's own sideways `deltaX` are both
still "the amount to move by," so picking the dominant one there instead would be wrong.

**Natural scrolling** (`Settings → Preferences → "Natural scrolling"`, default ON) is the one
plain-scroll preference layered on top of the OS's own setting, and is where
`Source/UI/ScrollPolicy.h`'s `scrollAmount(delta, invertScroll)` matters: JUCE's wheel deltas are
already OS-direction-adjusted (`MouseWheelDetails::isReversed` only REPORTS whether the OS has
"natural" scrolling on — the delta itself is pre-flipped so `juce::Viewport` can always apply
`viewPosition -= delta` and feel native on every platform), so "natural" for every scrolling surface
in this app means copying that Viewport convention, never re-reading `isReversed` to flip anything a
second time. `scrollAmount`'s `invertScroll` is the APP-LEVEL preference stacked on top:
`TimelinePanelComponent::setScrollInverted`/`PianoRollComponent::setScrollInverted` (both driven by
`MainComponent::applyNaturalScrollingPreference()`, installed as a `juce::ChangeListener` on
`appProperties.getUserSettings()` itself — a `juce::PropertiesFile` IS a `ChangeBroadcaster`, so a
settings-file write reaches both surfaces with no dedicated callback wired through the Settings
tab). One preference, not two — a user who wants the wheel flipped wants it flipped everywhere it
scrolls. It affects ONLY the timeline panel and the piano roll; the graph canvas pans rather than
scrolling and is unaffected. The piano roll's pitch axis needs one more mapping on top of
`scrollAmount` (screen-oriented: +y means the view moves DOWN), since `firstVisiblePitch_` is the
pitch at the TOP row and pitch grows upward — a view moving down therefore DECREASES it, which is
why that branch negates the result (see `docs/timeline_panel_clips_automation.md` §2); the horizontal axis on both surfaces needs no
such remapping, since a view moving right IS a larger `firstVisibleBeat`.

**Zoom-scroll direction** (`Settings → Preferences → "Scroll up to zoom in"`, a checkbox — briefly
a "Zoom direction" two-option dropdown in round 5, reverted back to a checkbox in round 6 after
user pushback on two-value selects; itself relabelled from "Scroll up zooms in" in round 3;
persisted key and its boolean semantics unchanged throughout, default ON, key
`zoomScrollUpZoomsIn`) is a separate preference from Natural scrolling, because zoom-on-wheel cares
about the FINGER rather than the content: `ScrollPolicy.h`'s `wheelGestureIsUpward()` recovers the
physical gesture direction by XOR-ing the dominant delta's sign with `isReversed` (the one place
`isReversed` IS consulted — a plain scroll must not do this, per the paragraph above), so "scroll up
enlarges" means the same physical motion whether or not the OS has natural scrolling on. Both
editors' Cmd/Cmd+Shift wheel-zoom branches compute `zoomIn = wheelGestureIsUpward(wheel) !=
zoomScrollInverted_`, with magnitude still `|dominantWheelDelta|` through the existing sensitivity
curve. `MainComponent::applyZoomScrollPreference()` propagates over the same settings-file
`ChangeBroadcaster` path; the panel forwards both scroll preferences to its piano roll.

**Keyboard zoom** (Cmd+=/Cmd+- horizontal, Cmd+Shift+=/Cmd+Shift+- vertical) reaches the SAME
`zoomTimelineHorizontal`/`zoomTimelineVertical` (panel) or `zoomHorizontal`/`zoomVertical` (roll)
entry points the wheel/pinch gestures do, anchored at the visible centre rather than a cursor
position, and is routed per focused surface by `MainComponent::resolveEditSurface()` — see
[`shortcuts.md`](shortcuts.md#zoom) for the full per-surface table and the Graph-is-horizontal-only
exception.

**The timeline's grid division** is likewise reachable from the keyboard, alongside the mouse's
snap combo (above): Ctrl+Shift+1..8 set it outright (`TimelinePanelComponent::setSnapValue`, 1
through 1/128), Ctrl+Shift+Left/Right step it by one (`cycleSnapValue`, clamped coarsest↔finest,
never wrapped — holding the key parks on `Bar` or `1/128` rather than surprising the user by wrapping around;
from `Off` both directions re-enter at the last musical division the user actually chose). Real
Ctrl, not Cmd, even on macOS — see [`shortcuts.md`](shortcuts.md#timeline) for why that's
deliberate. Every set/cycle call is a view-state-only change (nothing on the undo stack) that goes
through `setSnapValue`, the panel's ONE writer for the shared snap value, so the combo, these
commands and the cycle keys share one persist-and-repaint path.

## 3. Track Headers, Binding Chips, Add-Track

`Source/UI/TimelineTrackHeaderComponent.h/.cpp` (`synth::ui::TimelineTrackHeaderComponent`) — one
row per `synth::Track` (`Metrics::timelineTrackRowHeight`, 56 px — shared with the clip-lane area,
see `docs/timeline_panel_clips_automation.md` §1, so header rows and clip rows always line up), living in the panel's track-header
column. The column is a fixed
`"+ Track"` strip (22 px) at the top plus a `juce::Viewport` below it, so a project with more
tracks than fit **scrolls**; rows are never compressed. Both live inside `getTrackHeaderBounds()`,
so the panel's three regions still tile exactly.

**The column divider.** Because the regions tile, the header column's right edge and the lanes'
left edge are the same pixel — and with nothing drawn on it the track list ran straight into
whatever sat to its right. With the piano roll open that neighbour is the roll's own right-hand
utility sidebar (the scale panel), so the track list and that sidebar read as one undifferentiated
block, which is what the report was about. `TimelinePanelComponent::paint()` now draws a 1 px
`Colors::border` line on the seam, from the header column's top to the panel's bottom.

Two decisions in that one line. It is drawn by the **panel**, not by either neighbour: the seam is a
property of the panel's layout, so every future right-side sidebar inherits the divider instead of
having to remember to draw its own edge (and two neighbours both drawing one would double it). And
it starts at `trackHeaderBounds_.getY()`, i.e. **below the transport strip**, because that strip is
one continuous row of chrome across the full width — cutting it in half would imply a column
boundary its own controls do not respect.

**Toggle sizing.** The `M`/`S`/`R`/`A` toggles are `kToggleWidth` (24 px, up from 20) with an
explicit `kToggleGap` (4 px) between adjacent buttons — laid out edge-to-edge with no gap read as
one fused block, the worst offender in the timeline-panel button-size sweep.
`Metrics::timelineTrackHeaderWidth` grew alongside them (190 px, up from 160) so the wider,
gapped toggle group doesn't crush the name label down to single-digit pixel widths when a track's
`A` button is visible (4 toggles showing rather than 3).

**The document is the truth.** A header stores no state of its own: it re-reads name, colour,
mute/solo/arm and binding from the doc in `refreshFromDoc()`, and every edit is written back
through the doc. Headers are rebuilt/refreshed **only** from `TimelineDoc::Listener::timelineChanged`
(`TimelinePanelComponent` is the listener) — no timer, no polling. A notification whose track *set*
is unchanged refreshes the existing rows in place; only an added/removed/reordered track rebuilds
them, so a mute click doesn't destroy the row the user is typing a name into.

**Row contents:** colour swatch (click opens a full colour picker — see **Colour swatch** below), a
track-kind badge (`"MIDI"` / `"AUD"` / `"AUTO"`, fixed per `TrackKind` — identity chrome, not a
control), name label (double-click to edit), an `A` button (visible only when `Track::lanes` is
non-empty), `M` / `S` / `R` toggles, and the binding chip. `R` flips `Track::armed` in the document
and nothing else — arming is not recording; the record button and `MidiRecorder::startRecording`
live on the transport bar (§5, below), which looks for the first `armed` track when it starts a
take.

**Kind-badge icon.** The `"MIDI"`/`"AUD"`/`"AUTO"` text is the fallback: when a themed
`AppLookAndFeel` is installed and the corresponding asset is linked in, `paint()` draws
`Icon::TrackMidi`/`TrackAudio`/`TrackAutomation` (`kindBadgeIcon(TrackKind)`) instead — the same
"draw the glyph when the library has it, fall back to text otherwise" contract every other icon
consumer in the app follows. `getKindBadgeIconForTest()` mirrors `getKindBadgeTextForTest()`'s
"value or empty" idiom (returning `-1` for "fell back to text") so a test can assert on which path
ran without decoding pixels. A track's kind never changes after creation, so this is a one-time
paint decision, not something `refreshFromDoc()` has to re-derive.

**M/S/R active-state colours and the binding-chip theme fix.** `applyThemeDerivedColours()` is the
one place every colour this component bakes via `setColour` — the binding chip's warning/normal
fill and the `M`/`S`/`R` buttons' active-state colours (`theme.colors.trackMuteOn`/`trackSoloOn`/
`trackArmOn`) — gets (re-)derived from the currently installed `LookAndFeel`. It runs from three
call sites: the end of the constructor (so the very first paint isn't relying on a later call),
`refreshFromDoc()` (so a doc-driven repaint always shows the right colours), and the component's
own `lookAndFeelChanged()` override (new in this change) — without that last call site, a theme
switch alone, with no accompanying doc change, left the chip and the M/S/R active colours frozen
on whatever theme was active the last time `refreshFromDoc()` ran.

**Colour swatch.** Clicking it builds a `synth::ui::ColourPickerPopup` (`Source/UI/
ColourPickerPopup.h` — see [`theming.md` §13](theming.md#13-colour-picker-popup)) via
`buildColourPicker()` and launches it in a `juce::CallOutBox` anchored on the swatch, replacing the
old palette-cycle click. Its favourites shelf persists through `TrackHeaderHost::
getAppProperties()` (a new, non-pure `TrackHeaderHost` method defaulting to `nullptr` so every
existing implementer keeps compiling) — `nullptr` degrades to an in-memory-only picker, same as a
headless test gets. Preview writes the doc directly with no undo step on every drag/favourite
click; closing the popup either restores the original colour with no undo step (no net change) or
performs the real edit as ONE undo step whose undo target is the original colour (see
`theming.md` §13 for the exact preview/commit contract). `createColourPickerForTest()` exposes
`buildColourPicker()`'s exact wiring without ever launching the `CallOutBox`.

**"MIDI destinations..." menu entry.** The chip's context menu (see below) gains one more item,
`kMidiDestinationsMenuId`, offered only when `offersMidiDestinationsMenuEntryForTest()` says the
track's binding resolves to something with MIDI to send. Choosing it (or a test calling
`applyBindingMenuChoice(kMidiDestinationsMenuId)`) opens a `synth::ui::MidiDestinationPicker`
(`Source/UI/MidiDestinationPicker.h`) — a searchable, multi-select list of every live
MIDI-instrument node the track's bound Track In node could send MIDI to — in a `juce::CallOutBox`
anchored on the chip, via `openMidiDestinationsPicker()`/`buildMidiDestinationPicker()` (mirroring
the colour swatch's build/launch split, including `createMidiDestinationPickerForTest()` and a
`setOpenMidiDestinationsPickerHookForTest()` seam so the menu choice is exercisable without a live
callout). The candidate list is DYNAMIC ground truth, not an allowlist: every live graph node whose
`ModuleBase`-level `acceptsMidi()` is true (the per-module flags now reflect what each
`processBlock` actually consumes — see the module MIDI-flag audit in
`Tests/ModuleMidiFlagsTests.cpp`'s expected table), which automatically excludes MIDI *sources*
(Track In, External MIDI, MIDI Keyboard: they generate notes, they don't consume them). Rows render
in two sections: **Instruments** first (`isMidiInstrumentType()`, `Source/Modules/ModuleBase.h` —
the same set the add-track auto-wire target search uses) and **Other** for the remaining real MIDI
consumers (e.g. an ADSR's note-gate input); the section headers only appear when both groups are
present. **The graph is the truth**: toggling a row calls `TrackHeaderHost::
setMidiDestinationConnected(TrackId, nodeUid, connect)` — `MainComponent`'s implementation performs
one `recordStructuralChange` (add/remove the MIDI connection) then always calls
`reconcileTimelineAfterGraphChange()` — and immediately re-pulls `getMidiDestinationOptions(TrackId)`
to rebuild every row from what the graph now actually reports, rather than trusting the click. Both
host methods are non-pure with inert defaults (empty list / no-op) for the same
keep-every-implementer-compiling reason `getAppProperties()` is, and both no-op cleanly on a stale
popup (the track's binding or the target node no longer resolves) rather than crashing.

> **Known divergence, left alone deliberately.** `AIStateMapper` keeps its own, separately
> name-keyed `midiAcceptingTypes` list for the AI auto-wire path, and that list omits `Wavetable`.
> Unifying it with `isMidiInstrumentType()` would change AI auto-wire behaviour, which is out of
> scope for this change — the destination picker and the add-track auto-wire search both go
> through `isMidiInstrumentType()`; `AIStateMapper` does not.

**The `A` button** toggles this track's automation lane in the (single, doc-wide) automation strip.
The header only reports the click via `onAutomationToggleRequested` — it never tracks open/closed
state itself, since it can't see whether another track's lane is the one currently shown.
`TimelinePanelComponent::toggleAutomationForTrack` decides: already open on one of this track's own
lanes closes the strip; anything else (closed, or open on a different track) opens the track's first
lane.

**Colour** resolves *only* through `synth::ui::resolveTrackColour` (`Source/UI/TrackColour.h`): the
track's stored `colourArgb` when non-zero, otherwise a deterministic 8-entry palette indexed by the
track's position; a muted track comes back desaturated and dimmed (same hue). The palette is fixed
rather than theme-derived because the add-track flow *writes* the resolved colour into the document —
a theme-dependent value would mean a project opened under another theme came back recoloured. Unlike
`CableColour.h` there is no persisted override layer: the doc already stores the choice.

**Binding chip semantics** — three states, two of them amber (`theme.colors.warning`):

| Track state | Chip | Meaning |
|---|---|---|
| `bindingUuid` resolves | the node's plain display name (e.g. `"Track In"`) | plays through that node |
| `bindingUuid` empty | `"Unbound"` (amber) | never pointed anywhere; the track plays nowhere |
| `orphaned` | `"Missing"` (amber) | it WAS bound and the node is gone — retained, never auto-deleted |

An `Automation`-kind track shows **no chip at all** (`setVisible(false)`, decided in
`refreshFromDoc()`) — that track hosts lanes, and a node binding is meaningless for it; the bottom
half-row is simply empty. `Midi`/`Audio` tracks are unaffected.

The chip always carries a tooltip explaining what it shows and, when amber, how to fix it; the
`"#id"` suffix a bound name used to carry unconditionally is now added only in the re-bind menu, and
only to an option whose display name collides with another live candidate
(`MainComponent::getAvailableTrackInNodes`) — a lone node's name, on the chip or in the menu, always
stays plain.

Clicking the chip does two things: it **selects** the bound node in the GraphEditor's
`SelectionModel` (a highlight only — no canvas scroll, no focus change) and opens a menu listing
every live node of **the type this track's kind can be fed by** — `Track In` for a MIDI track,
`Track Audio` for an Audio track — **not claimed by another track**, plus
`"New Track In node"` (which likewise creates whichever type the track's kind needs). Offering the
wrong type would let a user bind a track to a node that structurally cannot play it: both modules
match on **kind as well as uuid**, so the result would be a track that silently plays nothing.
Picking one calls `TimelineDoc::setTrackBinding` as one undoable step, then reconciles.

> **A binding is NEVER re-established automatically — least of all by name.** An orphaned track
> stays orphaned until the user picks a node from that menu. Two nodes can carry the same display
> name, and a silent re-bind would quietly play a track through someone else's instrument.

**"+ Track"** (it used to read `"+ MIDI Track"` and added one outright until audio tracks existed,
and carries the tooltip *"Add a MIDI or Audio track"* so the two-item menu isn't a surprise)
opens a menu — **MIDI Track** / **Audio Track**, then a separator and **Add Marker** — whose ids are
`TimelinePanelComponent::kAddMidiTrackMenuId` / `kAddAudioTrackMenuId` / `kAddMarkerMenuId`. The two
track entries land on `TrackHeaderHost` (`addMidiTrack()` / `addAudioTrack()`), and
`TimelinePanelComponent::applyAddTrackMenuChoice(id)` is the headless seam — the same split the
binding and context menus use, since a `juce::PopupMenu` never runs in the test binary.
`MainComponent::simulateAddMidiTrackClick()` / `simulateAddAudioTrackClick()` call straight into it.

**Add Marker** sits below a separator because it is **not a track**: it adds no header row and no
graph node, it drops a flag on the ruler (see *Markers* under §2 above). It shares this menu
because `"+ Track"` is where a user reaches for "add something to the arrangement", and a second
button for one item would not earn its pixels. It is handled *before* `applyAddTrackMenuChoice`'s
`TrackHeaderHost` null-check — a marker is document data with nothing behind it to wire — and calls
`addMarkerAtPlayhead()`.

**The MIDI entry** is ONE compound undo step (`AppUndoManager::recordCombinedChange`, graph +
timeline in a single transaction, so one Cmd+Z removes all of it and redo restores it with the same
node uuid):

1. add a `Midi` track named `Track N` — **the doc side goes first**. `TimelineDoc::addTrack` refuses
   past `kMaxTracks`, and a node created before that refusal is known stays in the graph with no
   track to play through: `recordCombinedChange` *records* a mutation, it does not roll one back. A
   track with no binding yet is never flagged orphaned, so the intermediate state is inert;
2. create a `Track In` node through `AIStateMapper::createModule` (so it round-trips through
   `graphToJSON`/`applyJSONToGraph` — that is how undo, redo and `.agsproj` reproduce it), assign a
   fresh uuid and mirror it into the processor with `ModuleBase::setNodeUuid`, and place it at the
   canvas' left edge below every existing module (`GraphEditor::findLeftEdgeSlotBelowModules`);
3. **auto-wire only when unambiguous** — if the patch contains **exactly one** MIDI-driven
   instrument (module type `Poly MIDI`, `Oscillator`, `Wavetable`, `Sampler`, `Sequencer` or
   `Poly Sequencer`; MIDI *sources* — `Track In`, `External MIDI`, `MIDI Keyboard` — are excluded),
   connect `Track In -> that node` on the MIDI channel. With none or several, no wire is drawn: a
   chip that reads "bound" over an unwired node is fine, the cable is the user's to draw, whereas
   guessing wrong plays the track through the wrong instrument. Note `acceptsMidi()` cannot be the
   rule — `ModuleBase` returns `true` for every module in the app;
4. bind the track to the new node's uuid and give it the palette colour for its index.

**The Audio entry** mirrors it exactly (one compound step, track added first, factory-created node,
uuid minted and mirrored, placed at the left edge, an `Audio` track named `Audio N` bound to it with
its palette colour), with one difference in step 3: the auto-wire target is never ambiguous,
because the master
bus is a singleton — but there are **two** possible sinks and the order matters. If a
`Rec Tap` has already been spliced in front of Audio Output, wiring straight to the output would
route the track **around** the tap and quietly leave it out of every subsequent take, so
`createTrackAudioNode()` prefers the tap when one exists and falls back to Audio Output otherwise.
That makes both orderings compose: a track added *before* the first take is re-spliced by
`ensureMasterRecordTap()`, and one added *after* it lands on the tap directly. Both channels are
wired (`0 → 0`, `1 → 1`).

**Delete track** (right-click a header) is the same compound step in reverse: the track and its
bound `Track In` / `Track Audio` node go together, and come back together.

Headless test seams (a `juce::PopupMenu` never runs in the test binary): `collectBindingOptions()` /
`applyBindingMenuChoice(id)` and `applyContextMenuChoice(id)` are the menus' semantics without the
menu, and `handleChipClick(showMenu=false)` exercises the selection affordance on its own. The row
talks to the app exclusively through `synth::ui::TrackHeaderHost` (implemented by `MainComponent`),
so it is fully testable against a stub with no graph — see `Tests/TimelineTrackHeaderTests.cpp`.

## 4. The Playhead

`Source/UI/TimelinePlayheadOverlay.h/.cpp` (`synth::ui::TimelinePlayheadOverlay`) — a transparent,
non-intercepting (`setInterceptsMouseClicks(false, false)`) overlay the panel adds **last** (so it is
topmost) and sizes to `getLanesBounds()`, i.e. the whole ruler + lanes region. Its local `x == 0` is
`lanesBounds_.getX()`, which is also the ruler's origin and therefore exactly `TimelineViewState`'s —
no offset arithmetic anywhere in the overlay. It draws a `kLineWidth = 2 px` vertical line in
`theme.colors.accent` (literal cyan fallback with no themed LnF), full height.

**This is the second of the two exceptions to the no-unconditional-per-tick-repaint rule.** Its
confinement contract, the paint-count test pattern it introduces, and why a third exception is not
free are all in `docs/layout_visuals_animation.md` §3 — read that before touching this component.

**Two timers, one of them borrowed:**

| Rate | Owner | What it does |
|---|---|---|
| 10 Hz | `MainComponent::timerCallback` (**existing** timer, only while `timelinePanel.isVisible()`) | `TimelinePanelComponent::updateFromTransport(snapshot, outputLatencySeconds)` |
| 30 Hz | `TimelinePlayheadOverlay`'s own `juce::Timer` | re-reads the transport and requests the movement strip — **only while playing** |

The low-rate poll is the **sole** owner of the 30 Hz timer's lifecycle: it sees the play/stop
transition and calls `startTimerHz`/`stopTimer`. The 30 Hz tick deliberately does *not* stop itself
when it notices a stopped transport — one owner is easier to reason about, and a tick after playback
stopped simply finds an unchanged x and requests nothing. Worst case the timer runs for one extra
poll interval, repainting nothing.

`TimelinePanelComponent::updateFromTransport` has a second job: the ruler paints the time signature
and the loop brace, and **nothing else repaints it** when those change from outside its own mouse
gestures (a bundle load, a host tempo map, the transport bar's controls, §5). So the poll diffs a small
`RulerTransportState` (time signature + loop trio — the *position* is deliberately excluded, since
the playhead is the only thing that moves with it) and repaints the ruler only on a change; a time
signature change also repaints the panel, whose lanes grid derives its bar spacing from it. The first
poll seeds the struct instead of counting as a change.

**Latency offset.** The drawn beat is `ppq - outputLatencySeconds * (bpm / 60)`, clamped `>= 0`, so
the line matches what is being **heard** rather than the block currently being rendered.
`outputLatencySeconds` comes from the new `AudioEngine::getOutputLatencySamples()` — the open output
device's `getOutputLatencyInSamples()`, `0` in Hosted mode and `0` when no device is open (every
headless test). It is report-only, exactly like `getGraphLatencySamples()`. Graph latency is
deliberately *not* added in: it is patch-dependent, mostly zero, and never compensated anywhere,
whereas the device buffer is the term that actually separates "rendered" from "heard".

**Zoom/scroll while playing.** The old line position is remembered in **pixel** space
(`getLastRequestedLineX()`), never re-derived from the old beat. A zoom changes the mapping, so the
stale pixels that must be repainted are where the line *actually was* — remapping the old beat would
repaint the wrong place and leave a smear. One zoom therefore costs one wider-than-usual strip, which
is the correct trade (repainting extra is safe; missing pixels is not). A loop wrap costs the same:
one strip spanning the jump.

**Stopped** the overlay asks for nothing, but it still *draws*: `paint()` renders the line at the
current position whenever the panel paints for any other reason. Painting is not what the contract
restricts; asking for a repaint is.

**Follow playhead.** A toggle button sits immediately next to the snap toggle in the panel's
snap/tool strip (`followPlayheadButton_`, tinted via `Icon::FollowPlayhead` — see [`theming.md`
§3](theming.md#3-icon-tinting)). `kFollowPlayheadButtonWidth` is 30 px (up from 26, from the
timeline-panel button-size sweep); the snap toggle's `kSnapToggleButtonWidth` is **46 px**, wide
enough for the word `"Snap"`. The button is labelled with the VERB, not with its key: the key moved
from Q to J, and a button that spells its own letter goes stale the moment a user rebinds it — the
live key is in the tooltip, resolved through `synth::shortcutHintFor`. Backed by the boolean
preference `"timelineFollowPlayhead"`
(loaded/persisted the same way the snap-enabled flag is). Turning it on page-flips the panel's
view horizontally so the playhead never scrolls off screen while playing. The check rides the
SAME 10 Hz poll every other transport-driven repaint in `updateFromTransport` already uses — no
new timer — and is gated on **all four** of: the transport actually playing, the preference on,
the piano roll **closed** (the roll's own follow wiring, below, takes over while it's open), and
no clip drag in progress (`!clipLaneArea_.isDragInProgress()` — a follow flip landing mid-drag
would fight the gesture the user is mid-way through). Any one of the four false costs zero work.
When the (latency-compensated) drawn playhead beat moves outside `[firstVisibleBeat,
firstVisibleBeat + visibleBeats)`, the view re-centres so the beat lands **10% into the new page**
rather than flush against its edge — landing exactly on the edge would immediately re-trigger the
same flip on the very next poll once the beat advances one more sample.

**Roll-local variant.** `PianoRollComponent::setFollowPlayhead(bool)` mirrors the same feature
inside the roll's own horizontal view (`rollView_`), independent of the panel's — the roll has its
own zoom/scroll (see `docs/timeline_panel_clips_automation.md` §2) and therefore its own follow decision. It runs from inside
`setPlayheadBeat()` itself (the same seam the roll's local-playhead delegation, above, already
pushes a drawn beat through), gated on `followPlayhead_` on, no drag in flight
(`dragMode_ == DragMode::None`), and the edge-auto-scroll timer idle (the piano roll's own auto-scroll, `docs/timeline_panel_clips_automation.md` §2, is
already steering `rollView_` on its own terms — a follow flip on top of it would fight that
gesture the same way it would fight a clip drag in the panel). It runs BEFORE the strip-diff repaint
calculation below it in the same function, so that diff is computed against the mapping the line
will actually draw at, and it costs nothing while the beat is already inside the view — the common,
playing-and-visible case never calls `setHorizontalView` at all, so the zero-repaint-while-unmoved
contract above is untouched. `TimelinePanelComponent::setApplicationProperties()` pushes the shared
preference into the roll via `pianoRoll_.setFollowPlayhead(followPlayhead_)` — one preference,
two independent view states.

## 5. The Transport Bar

`Source/UI/TimelineTransportBar.h/.cpp` (`synth::ui::TimelineTransportBar`) — play/stop, record,
loop, BPM and time-signature editors, and the bar:beat readout, left-aligned in the transport-bar
strip (the snap combo stays docked right, §2 above). Buttons are **square** — `min(26 px, the
strip height after padding)`, centred in their slot — with `kGap = 7px` between them and `kGap * 2`
between groups; the two editable labels and the readout follow, in that order. `kButtonSize` (26,
up from 22) and `Metrics::timelineTransportBarHeight` (34, up from 28) were grown together in the
timeline-panel button-size sweep, so the glyphs actually render larger instead of being clamped
back down by `min(kButtonSize, bounds.getHeight())`.

**No SVG assets.** All three buttons are one `GlyphButton` (a `juce::Button` subclass) drawing a
plain `juce::Path` per glyph in `paintButton` — a triangle/square for play-stop, a circle for
record, an open arc with an arrowhead for loop. This mirrors the CLAUDE.md rule that themes never
swap typefaces: a one-off shape for a single caller doesn't earn a new icon asset either.

**Glyph geometry: one centred square, always.** Every glyph is drawn inside the button's shorter
side, inset by `kGlyphInsetRatio` (24%) on each edge — never a fraction of the *width* applied to
both axes, which is what flattened all four glyphs once the panel's 5 px resize grab strip left the
bar ~19 px tall, and is exactly the "really dense" the founder reported. Everything scales off
that square (the loop arc's stroke and arrowhead, the note's head/stem), so the row stays legible at
any strip height. `Tests/TimelineTransportBarTests.cpp::GlyphButtonsAreSquareAndSpaced` pins
squareness and the gaps at both the full and the trimmed strip height.

> **Record-red is deliberately theme-independent.** An engaged record button is
> `TimelineTransportBar::kRecordRedArgb` (`0xFFE53935`) — a filled red circle, a red border and a
> faint red wash behind it — **whatever the theme's accent is**, and themes may not override it. A
> hardware record LED is red on every desk; drawn in a cyan (or green) accent, "armed" stops reading
> as armed at all. It is the one colour on this bar that is not a theme token — every other lit
> glyph (play/stop, loop, metronome) still uses `colors.accent`. Idle record is a neutral outline
> (`colors.textPrimary` at 75%), not a dim red one. `GlyphButton::glyphColour()` is the single
> source for both the paint and the `getRecordGlyphColourForTest()` seam.

**The transport is the truth, read fresh, not cached.** Every button click and every editor commit
reads `TransportService::getPositionSnapshot()` **at the moment of the action**, rather than from a
value this bar remembers between polls:

- **Play/Stop** — one `GlyphButton` whose glyph flips between the two icons on `getToggleState()`.
  The click reads `getPositionSnapshot().playing` to decide `play()` vs `stop()`, so it works
  correctly even if nothing has polled `updateFromTransport()` since the last click (no dependency
  on visual resync happening in between).
- **Loop** — the click reads the CURRENT `loopStartPpq`/`loopEndPpq` off the snapshot and re-posts
  `setLoop(start, end, !looping)`. `TransportService`'s own construction default is `[0, 4)`, so "no
  bounds ever set" and "preserve existing bounds" fall out of the same one-line handler — there is
  no separate "default bounds" case to maintain.
- **BPM label** — a `juce::Label` (`setEditable(false, true, false)`, the same double-click-to-edit
  idiom as the track-name label), whose `onTextChange` calls `transport->setBpm()` — always accepted
  (clamped to `[TransportService::kMinBpm, kMaxBpm]` inside the service), so there is no revert case.
  It is also **draggable**: a nested `BpmDragLabel` overrides `mouseDown`/`mouseDrag` to turn
  vertical movement into a live `setBpm()` call, ±1.0 BPM per 4 px (±0.1 with Cmd held, for fine
  adjustment), anchored to the snapshot's BPM at `mouseDown` so the gesture is reproducible from the
  anchor + total delta regardless of how many `mouseDrag` calls land in between. Double-click and
  drag are independent gestures: JUCE dispatches `mouseDoubleClick` separately from
  `mouseDown`/`mouseDrag`/`mouseUp`, so overriding the latter three does not disturb `Label`'s own
  `editDoubleClick` handling.
- **Time-sig label** — same double-click idiom, parses `"N/D"` and calls `setTimeSignature(n, d)`,
  which validates numerator `1..64` and a fixed denominator set (`1/2/4/8/16/32`) and returns `false`
  **without posting anything** on rejection. The label then reverts to whatever the snapshot is
  CURRENTLY reporting (not a remembered value) — a rejected edit never touched the transport, so the
  snapshot is already the last known-good time signature.

> **An editable `juce::Label`'s editor does NOT inherit the app's `TextEditor` colours.** Both fields
> above typed **white on white** on every light theme until this was fixed, and the mechanism is
> worth knowing before adding a third one. `Label::createEditorComponent` copies
> `Label::textWhenEditingColourId` / `backgroundWhenEditingColourId` / `outlineWhenEditingColourId`
> onto the new editor's `TextEditor::textColourId` / `backgroundColourId` /
> `focusedOutlineColourId` — but only for ids that `isColourSpecified()`, and `LookAndFeel_V4`
> **does** specify `textWhenEditingColourId` from its own default-scheme white. So
> `AppLookAndFeel`'s themed `TextEditor::textColourId` was set correctly and then clobbered by V4's
> white on the way in. `AppLookAndFeel::applyTheme` now sets all three Label editing ids (plus
> `CaretComponent::caretColourId`, which V4 leaves black and therefore invisible on a dark theme),
> which fixes every editable label at once — these two and the track-name label in §3. A raw
> `juce::TextEditor` (the clip-rename and marker-rename editors) was never affected: it reads
> `TextEditor::textColourId` straight off the LookAndFeel. Pinned by
> `TimelineTransportBarTest.InlineFieldEditorsTakeTheirColoursFromTheTheme`, which asserts the
> TOKENS in a light and a dark theme so the fix cannot regress into a second hardcoded colour.
- **`updateFromTransport()`** (the drive seam, called from the panel's existing 10 Hz poll) resyncs
  the play/loop button visuals and the two labels' text from the snapshot — so a Space-bar play
  triggered elsewhere reflects here within one tick — but skips a label mid-edit
  (`Label::isBeingEdited()`): `Label::setText()` unconditionally discards an open editor's contents,
  so a poll landing mid-keystroke would otherwise fight the user's own typing.

**Bar:beat readout** — `TimelineTransportBar::formatBarBeat(ppq, tsNumerator, tsDenominator)` is a
**static, pure** helper (no `Component`, headless-testable on its own): `"BAR.BEAT.TICKS"`, 1-based
bar (zero-padded to 3 digits), 1-based beat (unpadded), ticks = 1/960 of a beat (zero-padded to 3
digits) — `beatsPerBar = tsNum * 4 / tsDen`, the same formula used throughout the timeline panel.
Pinned examples (see `Tests/TimelineTransportBarTests.cpp::FormatBarBeatTable`): `(0.0, 4/4)` ->
`"001.1.000"`, `(5.5, 4/4)` -> `"002.2.480"`, `(3.0, 3/4)` -> `"002.1.000"`. Painted in JetBrains
Mono via `juce::Font(juce::Font::getDefaultMonospacedFontName(), theme.type.value + 1, plain)` —
`AppLookAndFeel::getTypefaceForFont` resolves the default monospaced font name to
`theme.type.monoFamily` (JetBrains Mono in every built-in theme), the same indirection
`AIChatComponent`'s debug console and `SignInDialog`'s code label already use. Repainted **only
when the formatted string changes** — a plain string-diff cache, not a strip-confinement contract
like the playhead's (`docs/layout_visuals_animation.md` §3): the readout has no timer of its own and moves only when its owner polls
it, so there is nothing to bound beyond "don't repaint an unchanged tick".
`getReadoutRepaintCountForTest()` is the test seam, the same counting idiom
`TimelinePanelComponent::getTransportUpdateCountForTest()` uses.

**Recording is the one control the bar is not authoritative over.** Whether a take actually
captures anything, and onto which track, is something only `MainComponent` can see (it owns the
`TimelineDoc`). The record button's click computes `!getToggleState()` and reports that as *intent*
through `std::function<void(bool)> onRecordToggled` — it never flips its own toggle state.
`setRecordingState(bool)` is the ONE thing that ever does, called back by the owner with the real
outcome, and it is set the same way **regardless of whether anything is armed** — the indicator
reflects record-ON, not "a take is capturing". `MainComponent`'s implementation (installed in
`initialiseCommon()`):

- **ON does NOT require an armed track.** It iterates `timelineDoc.getTracks()` for the first
  `armed && (kind == TrackKind::Midi || kind == TrackKind::Audio)` track (first-armed-wins — there
  is deliberately no "record both at once"), but either way the transport rolls: **record implies
  roll** (a DAW convention — the record button starts the transport if it isn't already playing) and
  `setRecordingState(true)` fire unconditionally. An armed MIDI track additionally calls
  `midiRecorder.startRecording(track, currentPpq)`; an armed Audio track resolves a
  `RecordTapModule` tap and take files *before* the transport moves (a request that cannot be
  honoured — no Audio Output in the patch, or the take file can't be created/opened — must not
  leave the transport rolling) and then starts its capture. **With nothing armed**, the transport
  still rolls and the indicator still lights — identical to Play plus a lit record indicator — no
  take of either kind starts, and `statusBar.showMessage("Recording started - no track is armed")`
  explains the silence. Arming a track *mid-roll* does not retroactively start a take either:
  `TimelineDoc::setTrackArmed` has no listener watching for this, so the user has to stop and press
  Record again once something is armed.
- **OFF** (button click, or the 10 Hz poll noticing `playing -> stopped` while
  `midiRecorder.isRecording()` — the user hit Space/Stop instead of the record button) — both routes
  go through one `MainComponent::commitMidiRecording()`: `midiRecorder.stopAndCommit(doc, undo)`,
  `midiRecorder.hadOverrun()` -> `statusBar.showMessage("Dropped MIDI events during recording")`,
  then `setRecordingState(false)`. One choke point means the explicit and the auto-commit paths can
  never diverge — see `docs/architecture.md`'s MidiRecorder wiring entry (hook 5) for the full
  ordering. With nothing armed there was never a take to commit, so OFF just turns the indicator
  back off.

`AudioEngine::setMidiCaptureSink(&midiRecorder)` is the other half of the app-level wiring (feeds
`MidiRecorder::captureBlock` from `AudioEngine::renderNextBlock`'s one collector-merged buffer) —
see `Tests/MidiRecorderTests.cpp` for the model-level coverage and
`Tests/TimelineTransportBarTests.cpp` for the button-to-commit path.

## 6. Metronome + Count-In

Two more controls join the transport-bar strip, right after the loop button and before the BPM
label: a metronome toggle and a 3-item count-in selector. Both are `TimelineTransportBar` members —
not routed through `TimelinePanelComponent`, which has no other reason to know either setting
exists — and both persist themselves via the bar's own `setApplicationProperties(props)`, restoring
`"timelineMetronomeEnabled"` (bool, default off) and `"timelineCountInBars"` (int 0–2, default 0)
and re-persisting on every change, the same restore-then-persist-on-change idiom
`TimelinePanelComponent` uses for its snap combo. `TimelinePanelComponent::setApplicationProperties`
is a pure forward to the bar's version for these two keys; `TimelinePanelComponent::setMetronome`
forwards a `synth::Metronome*` the same way `setTransport` forwards a `TransportService*`.

**Metronome toggle** — one more `GlyphButton` (`Glyph::Metronome`), drawing a plain "quarter note"
(a filled ellipse notehead + a `juce::Rectangle` stem) rather than a `juce::Path` like its three
siblings — asset-free for the same CLAUDE.md reason the others are. Unlike Record, there is no
owner-side veto: the click directly flips both the button's own visual state and
`synth::Metronome::setEnabled` (via the bar's non-owning `synth::Metronome*`, set through
`setMetronome()`, mirroring `setTransport`'s null-safe contract) — no intent/outcome split is
needed. `setMetronome()` and `setApplicationProperties()` may run in either order: whichever runs
SECOND is what makes the persisted enabled value real, since each applies the last-known value to
the metronome pointer if the other has already been supplied.

**Count-in selector** — a `juce::ComboBox` ("Off" / "1 bar" / "2 bars", `getCountInBars()` returning
0/1/2), read by `MainComponent`'s record flow at the moment Record is clicked — never cached
elsewhere. See `docs/architecture.md`'s Metronome subsection for the full count-in choreography
(locate-back, forced-on click, the punch-in filter) this selector feeds.

Layout: `metronomeButton_` (a square button, like its three siblings) + `kGap` + `countInCombo_`
(64 px) + `kGap * 2`, inserted between the loop button and the BPM label — the bar's fixed-width
strip grows by ~98 px, well inside the timeline panel's normal width.

## 7. Edit-Tool Strip

`Source/UI/EditTool.h` declares the Cubase-style tool row shared by the clip lanes and the
piano roll (`docs/timeline_panel_clips_automation.md` §§1–2): `enum class EditTool { Select, Split, Glue, Erase, Mute, Draw }`, plus
`kAllEditTools` (an `std::array<EditTool, 6>` in that order), `editToolKeyDigit(tool)` and
`editToolName(tool)`. It is deliberately JUCE-free — gesture routing in both editors and the
strip's button wiring all switch on it, and `editToolForKeyChar(int keyChar)` is what
`TimelinePanelComponent::keyPressed()` consults for the number-key mapping, so tool switching is
testable with no UI at all.

**One active tool, owned by `TimelinePanelComponent`.** The clip lanes and the piano roll share the
same lanes rect and only one is ever visible, so a tool row that changed meaning depending on which
editor happened to be showing would be a trap. `setActiveTool(EditTool)` pushes the tool into
**both** `TimelineClipLaneArea::setActiveTool` and `PianoRollComponent::setActiveTool` unconditionally
(dontSendNotification on every strip button, set explicitly rather than via the radio group, since
this method is also reached from a number key or from a test — no button was necessarily clicked)
and lights the matching strip button; number keys and clicking a button are the only two ways a
user reaches it. Switching tools cancels whatever gesture/preview is already in flight in either
editor rather than trying to reinterpret it under the new tool — a half-finished drag has no
meaning under a different tool.

**Numbering follows Cubase**, so the muscle memory transfers: **1** Select, **3** Split, **4** Glue,
**5** Erase, **7** Mute, **8** Draw. **2** (Range Selection), **6** (Zoom) and **9** (Play/Scrub) are
Cubase tools this app doesn't ship yet, and the gaps are reserved **on purpose**:
`editToolForKeyChar` returns `std::nullopt` for them rather than clamping to a shipped tool, so
`TimelinePanelComponent::keyPressed()` leaves those three digits unconsumed and whatever they mean
elsewhere (nothing, today) is untouched. Shipping one of the missing three later costs no rebind —
the digit is already reserved for exactly that tool.

**All six tool digits are rebindable**, unlike the reserved 2/6/9 gaps above, which aren't bindings
at all. Each digit is a `ShortcutManager` action (`timelineToolSelect`, `timelineToolSplit`, …,
Timeline category) resolved directly by `TimelinePanelComponent::keyPressed()` — a *surface* action,
never dispatched through `ApplicationCommandManager` — so with a manager installed, an unbound tool
digit has no key at all, and a rebind takes effect immediately with no risk of colliding with the
Ctrl+Shift+digit grid-set commands (§2 above): the two live in the same category, but modifier
equality is exact, so a bare digit can never match a Ctrl+Shift one. Only a build with NO manager
installed (headless tests, an embedding with no settings store) falls back to the hardcoded digits
above via `editToolForKeyChar`. See [`shortcuts.md`](shortcuts.md#command-vs-surface-actions) for
the full command-vs-surface split and the tripwire test that guards it.

**The strip itself** is six `juce::DrawableButton`s (`ImageOnButtonBackground`), built unconditionally
in `TimelinePanelComponent`'s constructor (a headless build simply has no icon to draw in them —
`getToolButton(tool)` is never null once the panel exists), one shared radio group id so clicking one
un-toggles the rest, each with a tooltip that carries the digit (`"Split (3)"`, etc. —
`editToolName(tool) + " (" + editToolKeyDigit(tool) + ")"`). `kEditToolButtonWidth` is 28 px (up
from 24, from the timeline-panel button-size sweep). Laid out left-to-right in `kAllEditTools`
order (1, 3, 4, 5, 7, 8) immediately left of the snap combo/toggle in the transport bar — both are
"how the next edit behaves" chrome, so they read as one group without pushing the transport controls
off their left-aligned home. `applyToolStripTheme()` (constructor + `lookAndFeelChanged()`) re-applies
each icon from `AppLookAndFeel::getIcon` and sets the active-tool highlight as a **background colour**
(`colors.toolActive`, not a different icon tint — the glyph reads the same lit or not; see
[`theming.md`](theming.md)), null-guarded on both a headless LnF and a headless icon library.

**Custom per-tool cursors** (`Source/UI/ToolCursors.h`, `makeToolCursor(EditTool, const
juce::Drawable*)`) render the SAME already-tinted `Icon::Tool*` drawable the strip button paints
into a 24×24 `juce::Image` and wrap it in a `juce::MouseCursor`, so the cursor can never drift out of
sync with whatever theme is active — there is no separate cursor-only asset or tint step to go stale.
Hotspots are not uniform: **Select** hotspots at the arrow's tip `(4, 2)` and **Draw** at the pencil's
tip `(3, 21)` (both icons have an obvious off-centre working point, the way every DAW places a click
point there); **Split/Glue/Erase/Mute** hotspot at the icon's geometric centre `(12, 12)` (a
scissors' cut happens where the blades cross — the centre — and glue/erase/mute act on whatever is
directly under the pointer, so there is no other candidate point). Headless-safe by construction: a
null icon (asset library not linked in) falls back to a stock cursor per tool — `NormalCursor` for
Select, `CrosshairCursor` for Draw and for the remaining four (no single stock cursor reads as
"split" or "mute", so the crosshair at least telegraphs "a non-Select tool is active"). Both
`TimelineClipLaneArea` and `PianoRollComponent` cache their own six cursors
(`rebuildToolCursors()`), rebuilt only on a theme change — never per mouse-move, since building one
renders an icon into an `Image`.

