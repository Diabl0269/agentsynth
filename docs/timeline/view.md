# Timeline View State

`Source/UI/Timeline/TimelineViewState.h` (`synth::ui::TimelineViewState`) is the single pure,
headless-testable beat↔pixel mapping shared by every consumer of the timeline panel, plus the snap
division that decides where an edit lands. This doc also covers the lanes grid painted from it and
every wheel, pinch and keyboard gesture that moves it.

The panel shell is [timeline](timeline.md); the strip the mapping is drawn into is
[ruler](ruler.md). The piano roll has its own horizontal mapping and shares only the snap division
— see [piano-roll](piano-roll.md#horizontal-mapping).

## Beat and pixel mapping

No JUCE dependency; `TimelinePanelComponent` owns the one instance and exposes it via
`getViewState()`:

- `pixelsPerBeat` — zoom, clamped to `[kMinPixelsPerBeat = 1.5, kMaxPixelsPerBeat = 512]`
- `firstVisibleBeat` — horizontal scroll, clamped `>= 0`
- `beatToX()` / `xToBeat()`
- `zoomAroundX(factor, anchorX)` — keeps the beat under `anchorX` fixed on screen, clamps
  preserved. The anchor invariant yields to the `firstVisibleBeat >= 0` clamp at extreme zoom-out;
  the comment on the function states which one wins.
- `scrollBeats(delta)`
- `snapBeat(beat, beatsPerBar)`
- `trackScrollY` — vertical scroll of the track rows, shared with the header column
- `rowHeightScale` — vertical zoom, within `[0.5, 3.0]`

## Snap divisions

`TimelineViewState::Snap` is `Off, Bar, Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond,
SixtyFourth, HundredTwentyEighth`. Every non-`Bar`, non-`Off` value is a **note value** (a fraction
of a whole note), the DAW-conventional reading of the grid selector: `Whole` (combo label `"1"`) =
4 beats — a full 4/4 bar, `Half` = 2, `Quarter` (label `"1/4"`, the default) = 1 beat, `Eighth` =
0.5, `Sixteenth` = 0.25, then `"1/32"` = 0.125, `"1/64"` = 0.0625 and `"1/128"` = 0.03125. `Bar`
snaps to multiples of `beatsPerBar` (`tsNum * 4 / tsDen` off the transport's time signature — the
same formula `TransportService::getPosition()` uses). Ties round up, toward +infinity; beats are
never negative in practice.

**Why the three finest values sit at the end of the enum rather than in note-value order.** The
enum's int is persisted, so inserting in order would silently reinterpret every saved grid choice.

On top of the division sits a master switch, `TimelineViewState::snapEnabled` — the panel's `Snap`
button and the panel-wide **`J`** key toggle it. `J` is Cubase's snap key; `Q` is Cubase's
*quantise*, which is what the roll uses it for, so one letter never means two verbs depending on
which timeline surface has focus.

The snap choice lives in a `juce::ComboBox` docked in the transport bar's right-hand side, whose
items are `Off`, `Bar`, and then `1`, `1/2`, `1/4`, `1/8`, `1/16`, `1/32`, `1/64`, `1/128`
(`synth::ui::TimelineTransportBar` fills the rest of that strip, left-aligned — see
[transport](transport.md#layout-and-glyphs)). It persists under the `"timelineSnap"` int key, plus
`"timelineSnapEnabled"` for the switch, in `juce::ApplicationProperties`, set via
`TimelinePanelComponent::setApplicationProperties()` — a non-owning pointer setter, the same shape
as `AIChatComponent::setAccountService()`.

## Snap is magnetism, not visibility

**The switch governs MAGNETISM, not the grid.** `divisionBeats()` — what every magnetic edit reads
— returns 0.0 while the switch is off; `divisionBeatsRaw()` keeps returning the chosen division,
and **every grid PAINT site reads the raw one**.

**Why.** Turning snap off must not change which lines are drawn: it stops edits being pulled onto
the division, and a ruler that dropped its subdivision lines at the same moment would leave the
user eyeballing positions against nothing — exactly when free-hand editing needs the grid most. The
lanes-grid painter (`TimelinePanelComponent::paint()`) and the piano roll's gridlines both take
`divisionBeatsRaw()`; `TimelineClipLaneArea::floorSnappedBeatAt` / `ceilSnappedBeatAt` /
`minDrawLengthBeats` take `divisionBeats()`, because those ARE the magnetism.
`TimelinePanelGridDrawingTest.SnapOffKeepsTheSubdivisionLinesDrawn` pins the split. Picking a
division from the combo flips the switch back on.

## The lanes grid

`TimelinePanelComponent::paint()` draws the same bar/beat/subdivision hierarchy the ruler draws,
directly into the lanes region below it, from the shared three-level policy in
`Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h` (`GridLineLevel::{Subdivision,
Beat, Bar}`, `gridLineColourFor` / `gridLevelIsReadable`). `PianoRollComponent` paints its own grid
from the SAME policy ([piano-roll](piano-roll.md#gridlines)), so the two surfaces can never drift
apart in what is visible at a given zoom.

Per-level alpha is monotonic (`Subdivision` 0.28, `Beat` 0.50, `Bar` 0.85) and each line is lifted
halfway (`kGridLineContrastMix = 0.5`) from the theme's `border` token toward the background's
CONTRASTING colour (`background.contrasting(1.0f)` — black or white) before that alpha is applied.

**Why the contrast mix.** Raising alpha alone cannot fix a dark theme, where `border` is already
only a shade or two off `bg0`, so even alpha 1.0 reads as barely-there. Mixing toward the
contrasting extreme is what makes the line a LINE while keeping the theme's own hue in it, with no
new token to re-skin.

A whole level is DROPPED rather than drawn as a wall of touching pixels once its spacing falls
under `kMinGridLinePixels = 3.0` px (`gridLevelIsReadable`) — the same "no grid is better than a
smear" call the ruler's adaptive density makes, and Cubase's own rule. Beat lines gate at
`pixelsPerBeat >= 8` (`kMinBeatLinePixelsPerBeat`, `TimelinePanelLayout.cpp` — the same threshold
the ruler's beat ticks use). The subdivision level draws only when the current snap DIVISION is
finer than a beat, only when the beat level is ALSO drawn (the hierarchy stays monotonic — a
subdivision may never outlive its parent beat level, which the ~8 px/~3 px gap between the two
gates would otherwise allow), and only when it clears its own density guard.

## Wheel and trackpad bindings

`mouseWheelMove()` is implemented once, on the panel — JUCE bubbles an unhandled wheel event from
the ruler child up to it, so both regions share identical behaviour from one implementation.
The bindings follow Cubase:

| Gesture | Effect |
|---|---|
| Plain vertical wheel | Scrolls the track rows vertically (`TimelineViewState::trackScrollY`) |
| Shift+wheel, or a trackpad's own `deltaX` | Scrolls horizontally, converted to beats at the current zoom — a constant *pixel* distance per wheel unit, so the same physical gesture covers less musical time zoomed in |
| Cmd+wheel | Zooms horizontally around the cursor |
| Cmd+Shift+wheel | Zooms vertically, scaling `TimelineViewState::rowHeightScale` within `[0.5, 3.0]`, anchored so the row under the pointer stays put |
| Trackpad pinch (`mouseMagnify()`) | Horizontal zoom; Shift+pinch is vertical. Deliberate enough a gesture to need no modifier, on the panel and inside the piano roll alike |

`rowHeightScale` multiplies the themed row height in BOTH `TimelineClipLaneArea::getRowHeight()`
and the panel's `layoutTrackHeaders()`. `trackScrollY` is shared with the header column: a
scrollbar drag on the header viewport writes the same value back via `HeaderViewport::onScrolledY`,
and `syncTrackScroll()` is the one re-sync point.

**The two zoom-decided branches read `synth::ui::dominantWheelDelta(wheel)`
(`Source/UI/Timeline/ScrollPolicy.h`), never a single axis.** macOS folds a Shift-held wheel
gesture into `deltaX` (the OS's own axis swap), so a branch that is selected by its MODIFIERS and
reads only `deltaY` — Cmd+Shift+wheel here, and its twin in the piano roll — would receive exactly
`0.0` under that fold and silently do nothing. `dominantWheelDelta` (`deltaY != 0 ? deltaY :
deltaX`) is the one-line guard both zoom branches share, verified against
`juce_MouseEvent.h`/`juce_Viewport.cpp` rather than assumed.

A plain-scroll branch, by contrast, reads the axis the gesture actually ARRIVED on
(`std::abs(deltaX) > std::abs(deltaY) ? deltaX : deltaY`): a Shift+wheel the OS left on `deltaY`
and a trackpad's own sideways `deltaX` are both still "the amount to move by", so picking the
dominant one there would be wrong.

## Natural scrolling

`Settings → Preferences → "Natural scrolling"` (default ON) is the one plain-scroll preference
layered on top of the OS's own setting, and is where `ScrollPolicy.h`'s `scrollAmount(delta,
invertScroll)` matters.

**Why the OS flag is never re-read.** JUCE's wheel deltas are already OS-direction-adjusted:
`MouseWheelDetails::isReversed` only REPORTS whether the OS has "natural" scrolling on — the delta
itself is pre-flipped so `juce::Viewport` can always apply `viewPosition -= delta` and feel native
on every platform. "Natural" for every scrolling surface in this app therefore means copying that
Viewport convention, never re-reading `isReversed` to flip anything a second time.

`scrollAmount`'s `invertScroll` is the APP-LEVEL preference stacked on top:
`TimelinePanelComponent::setScrollInverted` / `PianoRollComponent::setScrollInverted`, both driven
by `MainComponent::applyNaturalScrollingPreference()`, installed as a `juce::ChangeListener` on
`appProperties.getUserSettings()` itself — a `juce::PropertiesFile` IS a `ChangeBroadcaster`, so a
settings-file write reaches both surfaces with no dedicated callback wired through the Settings
tab. One preference, not two: a user who wants the wheel flipped wants it flipped everywhere it
scrolls. It affects ONLY the timeline panel and the piano roll; the graph canvas pans rather than
scrolling and is unaffected.

The piano roll's pitch axis needs one more mapping on top of `scrollAmount` (screen-oriented: +y
means the view moves DOWN), since `firstVisiblePitch_` is the pitch at the TOP row and pitch grows
upward — a view moving down therefore DECREASES it, which is why that branch negates the result
(see [piano-roll](piano-roll.md#wheel-bindings)). The horizontal axis on both surfaces needs no
such remapping, since a view moving right IS a larger `firstVisibleBeat`.

## Zoom-scroll direction

`Settings → Preferences → "Scroll up to zoom in"` is a separate preference from Natural scrolling
(a checkbox, default ON, persisted key `zoomScrollUpZoomsIn`), **because zoom-on-wheel cares about
the FINGER rather than the content**. `ScrollPolicy.h`'s `wheelGestureIsUpward()` recovers the
physical gesture direction by XOR-ing the dominant delta's sign with `isReversed` — the one place
`isReversed` IS consulted, which a plain scroll must not do — so "scroll up enlarges" means the
same physical motion whether or not the OS has natural scrolling on.

Both editors' Cmd and Cmd+Shift wheel-zoom branches compute `zoomIn = wheelGestureIsUpward(wheel)
!= zoomScrollInverted_`, with magnitude still `|dominantWheelDelta|` through the existing
sensitivity curve. `MainComponent::applyZoomScrollPreference()` propagates over the same
settings-file `ChangeBroadcaster` path; the panel forwards both scroll preferences to its piano
roll.

## Keyboard zoom and grid commands

**Zoom** (Cmd+=/Cmd+- horizontal, Cmd+Shift+=/Cmd+Shift+- vertical) reaches the SAME
`zoomTimelineHorizontal` / `zoomTimelineVertical` (panel) or `zoomHorizontal` / `zoomVertical`
(roll) entry points the wheel and pinch gestures do, anchored at the visible centre rather than a
cursor position, and is routed per focused surface by `MainComponent::resolveEditSurface()` — see
[`shortcuts.md`](../shortcuts.md#zoom) for the full per-surface table and the
Graph-is-horizontal-only exception.

**The grid division** is likewise reachable from the keyboard, alongside the snap combo:
Ctrl+Shift+1..8 set it outright (`TimelinePanelComponent::setSnapValue`, `1` through `1/128`), and
Ctrl+Shift+Left/Right step it by one (`cycleSnapValue`), clamped coarsest↔finest and never wrapped
— holding the key parks on `Bar` or `1/128` rather than surprising the user by wrapping around.
From `Off`, both directions re-enter at the last musical division the user actually chose. Real
Ctrl, not Cmd, even on macOS — see [`shortcuts.md`](../shortcuts.md#timeline) for why that is
deliberate.

Every set and cycle call is a view-state-only change (nothing on the undo stack) that goes through
`setSnapValue`, the panel's ONE writer for the shared snap value, so the combo, these commands and
the cycle keys share one persist-and-repaint path.
