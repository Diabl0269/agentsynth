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

## Per-frame work does not grow with the patch

The 30 Hz tick rebuilds the cable list (the memo above is dropped every tick so cable activity stays live) and every
glide frame rebuilds and repaints it, so on a project with dozens of tracks anything per cable, per card or per macro
in those passes runs hundreds of times a frame. Three rules keep it linear:

- **Look nodes up through a map built once per pass**, never a scan per item. `rebuildVisibleCables()` and its
  re-anchor passes map node id -> card and uuid -> node once; a per-cable `moduleComponentForNode()` (or a per-member
  `resolveMemberNodeId()`, a whole-graph scan) made the tick O(cables x cards x nodes) and took over 100 ms at 80
  tracks.
- **A paint computes each macro border once.** The outline, chip, collapse and '+'/'-' buttons and every port-strip row
  all ask `paintedMacroHullBounds()`; `GraphContentComponent::paint` opens a `graph_editor_paint::HullMemoScope`
  (`GraphEditorPaintMemo.h`) so the first answer per macro serves the rest of that paint. Nothing moves a card during
  one paint, so the memo never goes stale; it does not outlive the paint.
- **A glide only snapshots what can be seen.** `CardGlideAnimator::arm` renders a snapshot (a full card paint) only for
  a card whose old-to-new path crosses the visible canvas; an off-screen card is neither hidden nor snapshotted but
  keeps its item, so its cables still slide. Snapshotting every moved card stalled an undo of a big Auto Arrange for
  most of a second.

`Tests/UI/Graph/GraphEditor/GraphEditorPaintWorkTests.cpp` holds these as work counts (`graph_editor_paint::
workCounters()`), not timings. `Tests/App/ManyTracksProfileTests.cpp` is a disabled bench that prints the real per-frame
costs for 10 to 80 instrument tracks (`--gtest_also_run_disabled_tests --gtest_filter='*ManyTracksProfile*'`).

## Gated timers, not free-running ones

- **`ModuleComponent::timerCallback` runs at 15 Hz** (`startTimerHz(15)`) and repaints only when the
  display needs to change: when RMS level changes, when active modulation routing changes, or when
  the sequencer step index changes.
- **A theme switch is exactly one re-skin pass**: `AppLookAndFeel::applyTheme()` into
  `sendLookAndFeelChangeMessage()` into one `repaint()`. No timer is started and no continuous
  repaint is added during or after a theme switch.
- **`applyToolbarIcons()` is gated** to narrow-mode transitions in `MainComponent::resized()`, not
  run on every resize frame, because rebuilding each button's recoloured `Drawable` art is
  expensive — see [chrome](chrome.md#toolbar-buttons).
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
