# Timeline Ruler and Markers

`Source/UI/Timeline/TimelineRulerComponent.h/.cpp` (`synth::ui::TimelineRulerComponent`) is a thin
strip (`Metrics::timelineRulerHeight` = 30) docked at the top of the lanes region: bar and beat
ticks, the loop brace, marker flags, and the two drag gestures that seek the playhead and set the
loop range.

It owns nothing. A `TimelineViewState&` (shared with the panel — see [view](view.md)), an optional
`synth::TransportService*`, an optional `synth::TimelineDoc*` (the markers it draws and edits) and
an optional `AppUndoManager*` are all non-owning setters that may stay null. The moving position
line is the separate `TimelinePlayheadOverlay` drawn over it — see [playhead](playhead.md).

## Tick density bands

`paint()` reads `getPositionSnapshot()` once per frame (message thread, cheap) for the time
signature and loop bounds, and draws three adaptive-density bands as you zoom in — Cubase's own
progression. **The SNAP division has no say in any of them**: a 1/16 snap on a zoomed-out
arrangement must not turn the strip into a grey smear.

1. **Far out** — bar lines plus bar numbers only; the labelled-bar stride itself doubles (by powers
   of two) until labelled bars are `>= 40 px` apart (`kMinLabelSpacingPx`, `.cpp`-local), so bar
   labels never overlap regardless of zoom.
2. **Mid** — short, half-height beat ticks appear between the bar lines once `pixelsPerBeat >= 8`
   (`kMinBeatTickSpacingPx`).
3. **Close in** — those beat ticks also earn a small, dim `"bar.beat"` sub-label (`"80.2"`, 1-based
   on both halves, faded to 65% alpha and two points smaller than the bar number) once
   `pixelsPerBeat >= 48` (`kMinBeatLabelSpacingPx`).

`rulerTickPlanFor(pixelsPerBeat, beatsPerBar)` (`TimelineRulerComponent.h`) is the ONE pure
decision behind bands 2 and 3 (`RulerTickPlan{drawBeatTicks, drawBeatLabels}`): a label implies a
tick by construction (a label with no tick to sit against would float), and a bar of one beat or
fewer draws no ticks at all (there is nothing non-bar to mark). `paint()` calls it once per frame
and never re-derives it inside the per-bar loop.

**Why the plan is font-independent.** The same helper drives `TimelineRulerTickPlanTest` directly
against the two thresholds. No measured text width appears anywhere in the decision, so the guard
means the same thing on every platform.

The ruler has no timer of its own: `repaint()` is called after one of its own interactions, or by
the panel's 10 Hz poll when the time signature or loop range changed from elsewhere
([playhead](playhead.md#what-the-low-rate-poll-also-does)).

## Interaction zones

The strip is split horizontally at `height / 2` (`TimelineRulerComponent::Zone`): the **top** half
owns the loop range, the **bottom** half owns the playhead. The boundary row belongs to the
playhead (`y < height * 0.5` is the loop zone). Both gestures are therefore reachable with no
modifier, which is the point — a plain drag anywhere in the bottom half scrubs the cursor, which is
what people expect a ruler to do.

**The zone is decided once, from the `mouseDown` y, and latched in `gestureZone_` for the whole
gesture.** A drag routinely leaves the band it started in (and often the strip entirely), and a
gesture must never change meaning mid-flight.

| Gesture | Effect |
|---|---|
| Press in the bottom (playhead) zone | `transport->locateBeat()` to the snapped beat under the cursor — on **press**, not release |
| Drag in the bottom zone | Keeps seeking as the pointer moves: the cursor follows the mouse (scrub) |
| Press-drag-release in the top (loop) zone | `transport->setLoop(min, max, true)` to the snapped `[start, end]` range (dragging leftwards normalises) |
| Click (no drag) in the top zone, **on a dimmed brace** | Re-arms that range (`setLoop(start, end, true)`), bounds untouched — the inverse of the Cmd+click below |
| Click (no drag) in the top zone, anywhere else | **Nothing.** Deliberate: the loop range is all this half owns, so a stray click cannot clear or collapse it |
| Cmd+click (no drag), either zone | Toggles looping off, keeping the existing bounds |

Both drag paths share the same throttle discipline: a command is posted only when the snapped value
changed since the last post, never once per pixel of movement, because `TransportService`'s command
FIFO dedupes nothing. `postSeekIfChanged` dedupes on the beat clamped to `>= 0` — the same clamp
`locateBeat` applies — so dragging left of beat 0 cannot re-post identical seeks; `postLoopIfChanged`
dedupes on the `[start, end]` pair. `mouseUp` calls its zone's poster again, so it is a no-op when
the last drag update already posted the final value.

## The loop brace

The brace is drawn whenever a RANGE exists (`loopEnd > loopStart`) — **not** only while looping is
armed. Disarming greys it out instead of hiding it, so a loop set earlier stays findable, and
re-armable, rather than disappearing the moment looping is switched off.

`TimelineRulerComponent::braceStateFor(looping, start, end)` is the pure rule (`BraceState::None /
Inactive / Active`) that `paint()` and the `getBraceStateForTest()` seam both call, so a drawn
brace and an asserted one cannot diverge. `braceColourFor()` picks the colour: `colors.accent`
armed, `colors.textMuted` at 70% alpha disarmed.

**Why muted text rather than a faded accent.** The hover band is already accent at 10% alpha, and a
dimmed accent brace over it would still read as lit.

The click target for the re-arm is the brace's whole x-span across the loop half, not the 4 px bar
it paints — 4 px is not a click target.

## Hover affordance

`mouseEnter` / `mouseMove` set the hovered zone AND the hovered marker; `mouseExit` clears both.
`applyHoverCursor()` is the ONE place the cursor is decided: a marker flag wins
(`DraggingHandCursor`), otherwise it is per zone (`PointingHandCursor` over the playhead half,
`LeftRightResizeCursor` over the loop half).

**Why it is split out.** Each hover setter only fires on *its own* change, so a zone change while
the pointer sits inside a flag would otherwise leave the zone's cursor showing over a draggable
marker.

`paint()` tints the hovered half with `accent` at 10% alpha, under the loop brace so the brace
stays legible. `repaint()` fires only when the hovered zone or the hovered marker actually
**changes** — never per mouse-move pixel — and no timer or animation is involved
(`docs/layout_visuals_animation.md` §3).

## Markers

A **marker** is a named position in the arrangement — a cue point, not a track. It has no clips, no
binding and no audible effect, so it lives on `TimelineDoc` itself (`std::vector<Marker>`, see
[`architecture_timeline.md` §3](../architecture_timeline.md#3-timelinedoc-the-timeline-document-model))
rather than in the track list.

**Why not a marker track.** It would have to be excluded from every place that iterates tracks to
make sound.

**Where they come from.** The `"+ Track"` menu's **Add Marker** entry (`kAddMarkerMenuId` — see
[add-track](add-track.md#add-marker)) calls `TimelinePanelComponent::addMarkerAtPlayhead()`: a
marker at the transport's CURRENT position — **unsnapped**, because a cue marks something the user
just heard, so it belongs exactly where the playhead is; a later drag *does* snap — named `"Marker
N"` from the existing marker count, coloured from `Theme::Colors::warning`. The amber family is
deliberately not `accent`, which the loop brace and the playhead already use in this same strip.
One `recordTimelineChange`. It needs no `TrackHeaderHost`, which is why `applyAddTrackMenuChoice`
handles it *before* the host null-check.

**How they draw.** `buildMarkerFlags()` is the single enumeration `paint()` and hit-testing both
walk — computing them separately is how a drawn flag drifts from the clickable one, the same rule
`GraphEditor::buildVisibleCables` exists to enforce for wires
(`docs/layout_selection_canvas.md` §3). Each flag is a filled tab in the strip's own marker band,
anchored at the marker's beat and running right, plus a full-height 1 px stem at the beat itself so
the exact position stays readable when the label is clipped. Culling is **per flag**, not per
marker: a flag whose anchor has scrolled off the left edge may still have most of its tab on
screen.

**Persistence** rides `TimelineDoc::toVar` / `fromVar`, so the project bundle and undo/redo carry
markers with no new code, and untrusted marker data is gated by `synth::validateTimeline` — see
[`ai/timeline-safety.md`](../ai/timeline-safety.md). The reserved-`"timeline"` refusal on the
untrusted PATCH path is untouched.

## The marker band

The numbers row and the marker band **tile** the strip's height rather than overlapping it.
`rulerLabelRowHeight(componentHeight)` is the height the bar and beat labels are centred in
(`height - kMarkerBandHeight`, falling back to the full height once that would leave the numbers
less than `kMinNumbersRowHeight`), and everything below it belongs to markers. That helper is the
ONE place the split lives — `paint()` centres its labels in it and `buildMarkerFlags()` starts the
band where it ends, so a number and a flag can never be handed overlapping rows.

**Why the strip is 30 px rather than 24.** At 24 the two rows had to share a strip sized for one,
which squeezed the flag to 9 px with a 9 pt label — barely visible. At 30 the numbers get 17 px
(comfortable for the 11 pt bar font) and the band gets 13. `Metrics::timelineRulerHeight` has
exactly one production consumer, `TimelinePanelComponent::resized`, which carries the same literal
as its headless fallback.

**Making a flag readable.** The label font is `kMarkerFlagFontHeight = 11` — the SAME size as the
bar numbers, because a marker label is a name the user typed and has to compete with the ruler's
own text, not whisper under it. The tab is filled at full opacity and outlined with a 1 px
`darker(0.5f)` edge, so a light flag on a light theme still has a shape instead of bleeding into
the strip. Label colour comes from `markerLabelColourFor(fill)` — black or white by the fill's own
`getPerceivedBrightness()`, i.e. **maximum** contrast, deliberately stronger than
`PianoRollComponent::labelColourFor`'s `contrasting(0.7f)`: a marker colour is arbitrary user data
and 11 pt inside a 13 px tab has no legibility to spare on the mid-tones.

The tab's WIDTH comes from the label's character COUNT (`markerFlagWidthFor(textLength)`, clamped
to `[kMarkerMinFlagWidth, kMarkerMaxFlagWidth]`), never from measured text — the clickable rect and
the painted rect are the same rect, and a font-measured width would make a hit-test assertion mean
something different on every platform, the same discipline as the bar-label stride.

**The stem, in two places.** `kMarkerStemWidth` is 2 px, not a hairline — at 1 px in the marker's
own colour it vanishes against the bar lines it crosses. In the RULER it runs the full height: full
opacity across the marker band and `kMarkerStemAlpha` above it, so it reads as belonging to the
flag without competing with the numbers it passes through. It is the one thing that deliberately
crosses the row boundary, because it is what names the exact beat.

`TimelinePanelComponent::paint()` then continues each stem **down through the lanes** at
`kMarkerLaneStemAlpha` (0.40), which is what makes a marker locatable against the clips rather than
only against the ruler. That is a static painted line on the panel's existing change-driven repaint
path — `timelineChanged()` repaints the ruler and `gridLanesBounds_` — so there is no timer and no
per-frame work, and `docs/layout_visuals_animation.md` §3's animation rules are untouched.
Off-screen markers are culled per marker, not clamped to an edge.

**Repaint discipline.** The ruler never listens to the doc and never polls it. A marker only moves
as the result of a mutation, and that mutation already notifies — so
`TimelinePanelComponent::timelineChanged()` calls `ruler_.repaint()`, and that is the whole
mechanism. No timer.

## Marker gestures

Marker flags are hit-tested BEFORE the zone split and before the transport null-check — a marker
lives on the doc, so it stays editable in a build with no transport wired in. Anything outside a
flag rect behaves exactly as the zone table above describes.

| Gesture | Effect |
|---|---|
| Drag a flag | Moves the marker, snapped through the shared `TimelineViewState` and clamped at beat 0. The drag is a **preview** (`markerDragBeat_`); the drop commits it as ONE `moveMarker` undo step. `buildMarkerFlags()` reports the preview beat, so the flag the user is looking at is the one the drop commits — and hit-testing follows it for free |
| Press a flag with no drag | **Nothing.** A stray click must not quietly re-snap the marker it landed on |
| Cmd+click a flag | Falls through to the zone gesture (switch looping off). A marker must not punch small dead holes in a strip-wide gesture — and Cmd is not `isPopupMenu()`, so a macOS Ctrl+click still opens the menu |
| Double-click a flag | Opens the inline rename editor directly — the discoverable path, next to the menu's `Rename…` rather than replacing it |
| Right-click a flag | **Rename… / Change colour… / Delete** (`MarkerContextChoice`) |

**The right button is inert apart from opening the menu**, and that gate is load-bearing rather
than tidiness. Without it the marker context menu appears for a split second and dismisses itself:

1. `mouseDown` on a right-click shows the menu and returns **before** latching `gestureZone_`,
   which therefore still holds whatever the previous gesture left in it (its initialiser is
   `Zone::Playhead`);
2. the ruler has captured the mouse on that press, so JUCE delivers the matching `mouseUp` to it
   even with the menu modal;
3. `mouseUp` finds no marker drag latched, falls through to the zone branch and — reading that
   stale `Zone::Playhead` — runs `postSeekIfChanged()`, which calls `TransportService::locateBeat()`
   **and** `repaint()` on the very component the menu was anchored to.

`TimelineClipLaneArea` never had the problem because its `mouseDown` gates the whole gesture path
behind `if (!e.mods.isLeftButtonDown()) return;`, so a right-click there latches nothing and its
`mouseDrag` / `mouseUp` are structurally inert. The ruler mirrors that: `mouseDown`, `mouseDrag`
and `mouseUp` all return early on `e.mods.isPopupMenu()`, and `openMarkerContextMenu` uses a plain
`juce::PopupMenu::Options()` exactly as `showClipContextMenu` does — no `withTargetComponent`,
which on a full-width 30 px strip also anchors the menu somewhere unrelated to the flag clicked.
Right-clicking the ruler therefore does **not** seek the playhead. Pinned by
`TimelineRulerMarkerTest.RightClickOnAFlagLatchesNothingAndPostsNothing` and
`RightClickOffAFlagDoesNotSeekOrLoop`.

**Double-click to rename** is scoped to flag hits only. Off a flag the gesture behaves as the two
ordinary clicks it always was — a seek in the playhead zone, the start of a loop drag in the loop
zone — and a right-button double-click stays the context menu's business.

One ordering detail is load-bearing. JUCE delivers a double-click as *down, up, down, doubleClick,
up*, so by the time `mouseDoubleClick` runs, the second press has already latched a marker drag.
The handler therefore **leaves the latch alone** and only clears `markerDragMoved_`: the trailing
`mouseUp` then takes the marker branch, commits nothing (a press that never dragged is a no-op) and
tidies up. Clearing `draggingMarker_` instead would send that `mouseUp` into the loop/playhead
branch under a STALE latched `gestureZone_` and seek the cursor out from under the editor being
opened.

`Rename…` opens an inline `juce::TextEditor` over the flag (`beginRenameMarker`), widened to at
least 96 px because an unlabelled flag's tab is 9 px and nobody can type into that, and pulled back
inside the strip. Return commits, Escape cancels, clicking away **commits** — the same reading
`TimelineClipLaneArea::beginRenameClip` has — and any press elsewhere in the strip commits an open
rename first. A label over `TimelineDoc::kMaxMarkerTextLength` is refused by the doc, which leaves
the existing one in place: a rejected edit is not an erase.

`Change colour…` reuses `synth::ui::ColourPickerPopup` with exactly the preview-writes-live /
commit-is-one-undo-step shape the track colour swatch uses
([tracks](tracks.md#colour-swatch)), sharing the same favourites shelf. Every mutation routes
through `performMarkerEdit()`, the one undo seam: `recordTimelineChange` with a manager installed,
a direct call without one.

`Rename` and `ChangeColour` are deliberately INERT as `MarkerContextChoice` values — they open UI
rather than mutating, and neither has a headless meaning. The enum exists so the menu's whole
vocabulary is enumerable and a test can assert those two mutate nothing; the commit paths are
`renameMarker()` and the picker's own `onCommit`. Same split as
`TimelineClipLaneArea::ClipContextChoice::Rename`.

## Opening a menu is a protected virtual

`TimelineRulerComponent::openMarkerContextMenu` is a `protected virtual`, and that is a CI
requirement rather than a style choice.

**Why.** A live `juce::PopupMenu` creates a real top-level window, and JUCE positions it against a
display it looks up from the mouse point. On a display-less runner — the Linux Debug coverage job —
that lookup returns null and `MenuWindow::calculateWindowPos` dereferences it: **SIGSEGV**, while
macOS and Windows stay green because they have a display.

Tests subclass the ruler and override this leaf to record `(menuRequests, lastMenuMarker)`,
asserting *which marker the menu was requested for* alongside the gesture-state invariants, and
drive the outcomes through `applyMarkerContextChoice`. Production behaviour is unchanged. The same
seam idiom appears in `PianoRollComponent::promptExtendClipToFitNotes`, `requestRepaintStrip`, and
`ModuleLibraryComponent::showHelpPopover` / `launchHelpCallOutBox`.

`TimelinePanelComponent::openAddTrackMenu` is the same seam for the `"+ Track"` menu. No test
reaches it today — they all drive `applyAddTrackMenuChoice`, the documented headless entry point —
but a test that clicked the button would crash identically, so the override point exists before
someone writes that test. Those two are the only real-window creations under the panel and the
ruler; the marker colour picker's `CallOutBox` is launched from inside `openMarkerContextMenu`'s
own menu item, so the override covers it too, and both inline rename editors are ordinary child
components with no window of their own.
