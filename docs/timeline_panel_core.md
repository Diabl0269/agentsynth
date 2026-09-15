# Timeline Panel: Core

This document covers the timeline panel's shell and the ruler/grid/zoom/scroll/snap and
loop-brace behaviour. Track headers, binding chips and Add-Track live in the companion document
[`docs/timeline_panel_tracks.md`](timeline_panel_tracks.md); the playhead, transport bar,
metronome/count-in and edit-tool strip live in
[`docs/timeline_panel_transport.md`](timeline_panel_transport.md); clip lanes, the piano roll, the
automation strip, and the keyboard/focus arbitration rule live in
[`docs/timeline_panel_clips_automation.md`](timeline_panel_clips_automation.md) and
[`docs/timeline_panel_piano_roll.md`](timeline_panel_piano_roll.md).

- [1. Timeline Panel](#1-timeline-panel)
- [2. Ruler, Grid, Zoom/Scroll, Snap, Loop Brace](#2-ruler-grid-zoomscroll-snap-loop-brace)

---


## 1. Timeline Panel

`Source/UI/Timeline/TimelinePanelComponent/` — `synth::ui::TimelinePanelComponent`, a bottom-docked
panel owned by `MainComponent`. This section was written incrementally, one piece at a time — the
shell came first, then the ruler, track headers, playhead and transport bar filled it with real
content, and finally the keyboard/focus rule that arbitrates between it and the graph editor — each
subsection below (or in the companion clips/automation document) is still the reference for its own
piece, and this intro is only a map of how they compose.

Source layout (FRO66 split, one file per concern; the class itself is declared in
`TimelinePanelComponent.h`):

| Unit | Concern |
|------|---------|
| `TimelinePanelComponent.cpp` | Ctor/dtor, shortcut-manager wiring, transport/doc/undo-manager setters |
| `TimelinePanelStrips.cpp` | Edit-tool strip, piano-roll open/close, automation strip |
| `TimelinePanelClipClipboard.cpp` | Clip clipboard: copy/paste/cut/duplicate/repeat/select-all |
| `TimelinePanelShortcuts.cpp` | Panel-scoped keyboard shortcut dispatch (`matchesAction`/`keyPressed`) |
| `TimelinePanelTrackHeaders.cpp` | Add-track menu, `timelineChanged`, header sync/layout, focus movement, drag-to-reorder |
| `TimelinePanelLayout.cpp` | Preferences (snap/follow-playhead/scroll-invert), zoom/scroll helpers, `resized()`/`paint()`, the `ResizeHandle` child component |

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

> **FRO11 (P9-5) note — this key/flag now gates the whole bottom dock, not just this panel.**
> `TimelinePanelComponent` is nested inside `MixerDockComponent` (the Timeline/Mixer tab strip;
> see [`mixer_implementation.md`](mixer_implementation.md) §8), and `mixerDock.setVisible(isTimelineVisible)`
> is what the toggle/shortcut/persisted key actually drive. `isTimelineVisible` /
> `timelinePanelVisible` mean "the dock is open", regardless of which tab is active; which of the
> two panels is *showing* inside an open dock is the separate, independently persisted
> `bottomDockActiveTab` key (`MixerDockComponent::kActiveTabKey`, default `"timeline"` — see
> `mixer_implementation.md` §8). Names kept for compatibility with existing persisted settings
> files; a component-local `TimelinePanelComponent::isVisible()` check no longer tells you whether
> the dock is open (it only reflects "the Timeline tab is selected") — callers that need "is the
> panel actually on screen" now compose `timelinePanel.isVisible() && mixerDock.isVisible()`
> (`MainComponent::timerCallback()`'s 10 Hz poll gate is the reference call site).

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
- **FRO11 (P9-5) note**: this value is `MainComponent`'s own **total dock-carve height**, i.e. it
  includes `MixerDockComponent::kTabStripHeight` (22px) on top of the timeline panel's own content
  height. `TimelinePanelComponent::ResizeHandle::desiredHeightFor()` stays agnostic of whatever
  chrome it sits inside and reports only its own desired *content* height; the
  `onResizeHeight`/`onResizeHeightCommitted` wiring in `MainComponentSetupTimeline.cpp` is the one
  seam that knows about both and adds the tab-strip height before calling
  `setTimelinePanelHeight()`. A pre-FRO11 persisted height (written before the dock existed) is
  therefore honoured as a total-carve value unchanged; only the panel's own *content* area is 22px
  shorter than it used to be at the same persisted number, in exchange for the tab strip.
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

`Source/UI/Timeline/TimelineViewState.h` (`synth::ui::TimelineViewState`) is the single pure,
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

`Source/UI/Timeline/TimelineRulerComponent.h/.cpp` (`synth::ui::TimelineRulerComponent`) is a thin strip
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

`rulerTickPlanFor(pixelsPerBeat, beatsPerBar)` (`Source/UI/Timeline/TimelineRulerComponent.h`) is the ONE
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
[`AI_Engine_patch_safety.md`](AI_Engine_patch_safety.md). The reserved-`"timeline"` refusal on the untrusted PATCH path is
untouched.

**Grid.** `TimelinePanelComponent::paint()` draws the SAME bar/beat/subdivision hierarchy directly
into the lanes region below the ruler, from the shared three-level policy in
`Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h` (`GridLineLevel::{Subdivision, Beat, Bar}`,
`gridLineColourFor`/`gridLevelIsReadable`) — `PianoRollComponent` paints its own grid from the SAME
policy (`docs/timeline_panel_piano_roll.md` §2), so the two surfaces can never drift apart in what's visible at a given zoom.
Per-level alpha is monotonic (`Subdivision` 0.28, `Beat` 0.50, `Bar` 0.85) and each line is lifted
halfway (`kGridLineContrastMix = 0.5`) from the theme's `border` token toward the background's
CONTRASTING colour (`background.contrasting(1.0f)` — black or white) before that alpha is applied:
raising alpha alone can't fix a dark theme, where `border` is already only a shade or two off `bg0`,
so even alpha 1.0 would read as barely-there; mixing toward the contrasting extreme is what makes
the line a LINE while keeping the theme's own hue in it, with no new token to re-skin. A whole level
is DROPPED rather than drawn as a wall of touching pixels once its spacing falls under
`kMinGridLinePixels = 3.0` px (`gridLevelIsReadable`) — the same "no grid is better than a smear"
call the ruler's own adaptive density makes above, and Cubase's own rule. Beat lines gate at
`pixelsPerBeat >= 8` (`kMinBeatLinePixelsPerBeat`, `TimelinePanelLayout.cpp` — the same threshold
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
(`Source/UI/Timeline/ScrollPolicy.h`), never a single axis.** macOS folds a Shift-held wheel gesture into
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
`Source/UI/Timeline/ScrollPolicy.h`'s `scrollAmount(delta, invertScroll)` matters: JUCE's wheel deltas are
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
why that branch negates the result (see `docs/timeline_panel_piano_roll.md` §2); the horizontal axis on both surfaces needs no
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
