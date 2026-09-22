# Timeline Playhead

`Source/UI/Timeline/TimelinePlayheadOverlay.h/.cpp` (`synth::ui::TimelinePlayheadOverlay`) — a
transparent, non-intercepting (`setInterceptsMouseClicks(false, false)`) overlay the panel adds
**last**, so it is topmost, and sizes to `getLanesBounds()`, i.e. the whole ruler plus lanes region.

Its local `x == 0` is `lanesBounds_.getX()`, which is also the ruler's origin and therefore exactly
`TimelineViewState`'s — no offset arithmetic anywhere in the overlay. It draws a `kLineWidth = 2 px`
vertical line in `theme.colors.accent` (literal cyan fallback with no themed LnF), full height.

**This is the second of the two exceptions to the no-unconditional-per-tick-repaint rule.** Its
confinement contract, the paint-count test pattern it introduces, and why a third exception is not
free are all in `docs/layout/animation.md` — read that before touching this component.

## Two timers

One of them is borrowed:

| Rate | Owner | What it does |
|---|---|---|
| 10 Hz | `MainComponent::timerCallback` (an existing timer, only while `timelinePanel.isVisible()`) | `TimelinePanelComponent::updateFromTransport(snapshot, outputLatencySeconds)` |
| 30 Hz | `TimelinePlayheadOverlay`'s own `juce::Timer` | Re-reads the transport and requests the movement strip — **only while playing** |

The low-rate poll is the **sole** owner of the 30 Hz timer's lifecycle: it sees the play/stop
transition and calls `startTimerHz` / `stopTimer`. The 30 Hz tick deliberately does *not* stop
itself when it notices a stopped transport — one owner is easier to reason about, and a tick after
playback stopped simply finds an unchanged x and requests nothing. Worst case the timer runs for
one extra poll interval, repainting nothing.

## What the low-rate poll also does

`TimelinePanelComponent::updateFromTransport` has a second job. The ruler paints the time signature
and the loop brace, and **nothing else repaints it** when those change from outside its own mouse
gestures — a bundle load, a host tempo map, the transport bar's controls.

So the poll diffs a small `RulerTransportState` (time signature plus the loop trio; the *position*
is deliberately excluded, since the playhead is the only thing that moves with it) and repaints the
ruler only on a change. A time-signature change also repaints the panel, whose lanes grid derives
its bar spacing from it. The first poll seeds the struct instead of counting as a change.

## Latency offset

The drawn beat is `ppq - outputLatencySeconds * (bpm / 60)`, clamped `>= 0`, so the line matches
what is being **heard** rather than the block currently being rendered.

`outputLatencySeconds` comes from `AudioEngine::getOutputLatencySamples()` — the open output
device's `getOutputLatencyInSamples()`, `0` in Hosted mode and `0` when no device is open, which is
every headless test. It is report-only, exactly like `getGraphLatencySamples()`.

**Why graph latency is not added in.** It is patch-dependent, mostly zero, and never compensated
anywhere, whereas the device buffer is the term that actually separates "rendered" from "heard".

## Zoom and scroll while playing

The old line position is remembered in **pixel** space (`getLastRequestedLineX()`), never
re-derived from the old beat.

**Why.** A zoom changes the mapping, so the stale pixels that must be repainted are where the line
*actually was*; remapping the old beat would repaint the wrong place and leave a smear. One zoom
therefore costs one wider-than-usual strip, which is the correct trade — repainting extra is safe,
missing pixels is not. A loop wrap costs the same: one strip spanning the jump.

**Stopped**, the overlay asks for nothing, but it still *draws*: `paint()` renders the line at the
current position whenever the panel paints for any other reason. Painting is not what the contract
restricts; asking for a repaint is.

## Follow playhead

A toggle button sits immediately next to the snap toggle in the panel's snap/tool strip
(`followPlayheadButton_`, tinted via `Icon::FollowPlayhead` — see
[`layout/icons.md`](../layout/icons.md)). `kFollowPlayheadButtonWidth` is 30 px; the snap
toggle's `kSnapToggleButtonWidth` is **46 px**, wide enough for the word `"Snap"`.

**The button is labelled with the VERB, not with its key.** A button that spells its own letter
goes stale the moment a user rebinds it, so the live key is in the tooltip, resolved through
`synth::shortcutHintFor`.

Backed by the boolean preference `"timelineFollowPlayhead"`, loaded and persisted the same way the
snap-enabled flag is. Turning it on page-flips the panel's view horizontally so the playhead never
scrolls off screen while playing. The check rides the SAME 10 Hz poll every other transport-driven
repaint in `updateFromTransport` uses — no new timer — and is gated on **all four** of:

- the transport actually playing,
- the preference on,
- the piano roll **closed** (the roll's own follow wiring takes over while it is open), and
- no clip drag in progress (`!clipLaneArea_.isDragInProgress()` — a follow flip landing mid-drag
  would fight the gesture the user is mid-way through).

Any one of the four false costs zero work.

When the latency-compensated drawn playhead beat moves outside `[firstVisibleBeat, firstVisibleBeat
+ visibleBeats)`, the view re-centres so the beat lands **10% into the new page** rather than flush
against its edge — landing exactly on the edge would immediately re-trigger the same flip on the
very next poll once the beat advances one more sample.

**Roll-local variant.** `PianoRollComponent::setFollowPlayhead(bool)` mirrors the same feature
inside the roll's own horizontal view (`rollView_`), independent of the panel's — the roll has its
own zoom and scroll ([piano-roll](piano-roll.md#horizontal-mapping)) and therefore its own follow
decision. It runs from inside `setPlayheadBeat()` itself — the same seam the roll's local-playhead
delegation pushes a drawn beat through — gated on `followPlayhead_` on, no drag in flight
(`dragMode_ == DragMode::None`), and the edge-auto-scroll timer idle (the roll's own auto-scroll is
already steering `rollView_` on its own terms; a follow flip on top of it would fight that gesture
the same way it would fight a clip drag in the panel).

It runs BEFORE the strip-diff repaint calculation below it in the same function, so that diff is
computed against the mapping the line will actually draw at, and it costs nothing while the beat is
already inside the view — the common playing-and-visible case never calls `setHorizontalView` at
all, so the zero-repaint-while-unmoved contract is untouched.
`TimelinePanelComponent::setApplicationProperties()` pushes the shared preference into the roll via
`pianoRoll_.setFollowPlayhead(followPlayhead_)`: one preference, two independent view states.

**Suspension (FRO247).** A fifth gate, `!followSuspended_`, sits alongside the three above.
`followSuspended_` is set whenever the view moves for a reason OTHER than follow itself: opening a
clip (`openClip()` — a stale follow position from the PREVIOUS clip, or from before the roll was
even open, must not fight the fresh fit-to-clip framing `openClip()` computes from the clip's own
bounds) or a manual horizontal scroll/zoom (`mouseWheelMove`'s horizontal-scroll branch,
`zoomHorizontalAroundX` — covering the wheel, the trackpad pinch, and the zoom-in/out command). It
is cleared only by an explicit `setFollowPlayhead(true)`, the one gesture that means "snap to the
playhead now." Without it, the very next `setPlayheadBeat()` tick after either action — which the
panel's 10 Hz poll delivers unconditionally, whether or not the transport is playing — would
immediately undo it: the notes editor would open on an empty grid centred on wherever the playhead
happens to be instead of the pattern, and a manual scroll away from the playhead would snap straight
back.

Tests: `Tests/UI/Timeline/TimelinePlayheadTests.cpp`,
`Tests/UI/Timeline/TimelinePanel/TimelinePanelFollowPlayheadTests.cpp`,
`Tests/UI/PianoRoll/PianoRollMouseTests.cpp` (`PianoRollFollowPlayheadTest`).
