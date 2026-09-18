# Repaint Discipline

The rules that keep the editor's frame rate smooth. **No unconditional per-tick repaint** is the
invariant the root and `Source/UI/` `CLAUDE.md` files both carry; this doc is the mechanism behind
it. Motion itself is [animation](animation.md).

## Module cards are buffered through a zoom-frozen cache

`ModuleComponent` is buffered to an image via `synth::ui::ZoomFrozenCachedImage`
(`Source/UI/Layout/ZoomFrozenCachedImage.h`), **not** raw `setBufferedToImage(true)`.

**Why a custom cache.** JUCE's standard cached image keys on the *accumulated* device scale
(`zoomLevel x deviceScale`), so every wheel tick re-rasterizes every visible card. During a zoom
gesture `GraphEditor` freezes each card's raster scale (`setModuleRasterFrozen(true)`) — the
existing image is resampled, not re-rendered — and thaws `kZoomSettleMs` (140 ms) after the last
zoom event with exactly one crisp re-render.

**Never call `setBufferedToImage()` on `ModuleComponent`** — JUCE asserts and silently deletes the
custom cache.

The `GraphEditor` 30 Hz connection animation blits these cached images rather than re-running JUCE
text layout and parameter reads on every animation frame. **Do not reintroduce unconditional
`repaint()` calls in `timerCallback` on module components or their always-visible children.**

## The canvas has one invalidation seam

`buildVisibleCables()` is memoized, and `GraphEditor::repaintCanvas()` is the single "canvas
changed" seam that drops the memo and repaints (`content.repaint()`). **Any new repaint of the
canvas content must go through `repaintCanvas()`, never `content.repaint()` directly.**

## Gated timers, not free-running ones

- **`ModuleComponent::timerCallback` runs at 15 Hz** (`startTimerHz(15)`) and repaints only when the
  display needs to change: when RMS level changes, when active modulation routing changes, or when
  the sequencer step index changes.
- **A theme switch is exactly one re-skin pass**: `AppLookAndFeel::applyTheme()` into
  `sendLookAndFeelChangeMessage()` into one `repaint()`. No timer is started and no continuous
  repaint is added during or after a theme switch.
- **`applyToolbarIcons()` is gated** to narrow-mode transitions in `MainComponent::resized()`, not
  run on every resize frame, because cloning `Drawable` objects is expensive — see
  [chrome](chrome.md#toolbar).
- **The status bar polls at 5 Hz** and repaints only itself. There are zero `writeToLog` calls in
  the status-polling path.
- **Automation-to-UI reflection adds no new timer.** `GraphEditor::timerCallback()`'s existing 30 Hz
  tick drains `AudioEngine::getAutomationUiFeed()` and calls
  `ModuleComponent::reflectParameterValue()`, which only ever calls
  `slider->setValue(..., juce::dontSendNotification)` — no direct `repaint()`. The card's own gated
  15 Hz timer and buffered-image cache pick the change up on their own schedule, the same as any
  other control change.
- **The timeline panel's transport poll adds no timer.** It rides `MainComponent`'s existing 10 Hz
  tick, only while the panel is visible, and repaints the ruler only when the ruler's own state —
  time signature, loop trio — changed. The playhead's 30 Hz strip repaint is a permitted exception
  and runs only while the transport is playing; see
  [animation](animation.md#the-time-bounded-animation-rule).
- **The track headers' channel-chip meters share ONE gated 15 Hz timer**, the same shape as
  `ModuleComponent`'s meter poll and not a new animation exception for the same reason: the tick is
  unconditional but the **repaint is not**. `TimelinePanelComponent` — not each chip, since up to
  `TimelineDoc::kMaxTracks` rows would mean 256 independent timers — ticks every header's
  `tickChannelMeter()`, and `ChannelChipComponent::setMeterLevel` repaints only when the drawn level
  crosses a coarse threshold, returning whether it did, so the gate itself is testable without
  driving a real timer. The timer starts when the panel gets a `TrackHeaderHost` and stops when it
  loses one, and `timerCallback` returns immediately while the panel is hidden. The per-tick read is
  `TrackChannelLinkSurface::getChannelMeterPeak` — two atomic reads off a cached strip id — never
  `getChannelInfo()`, which re-derives the link from the live graph (a node scan plus two
  connection-list BFS walks) and is a per-click cost, not a per-row-per-frame one.

## All UI animation is time-bounded

Animations run for a finite transition duration and stop when settled. Never add a continuous or
per-frame animation outside the **two** exceptions defined in
[animation](animation.md#the-time-bounded-animation-rule), each of which is bounded by an activity
rather than by a duration.
