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
canvas content must go through `repaintCanvas()`, never `content.repaint()` directly.** The exceptions are animation
frames that change no cable or move cables themselves: the card glide's `Hooks::repaintArea` (it moves the cables of
gliding cards inside the memo, see below) and the cable retract, which repaint only their own area with
`content.repaint(area)` and keep the memo.

## Per-frame work does not grow with the patch

The 30 Hz tick rebuilds the cable list (the memo above is dropped every tick so cable activity stays live) and
repaints the whole canvas, so on a project with dozens of tracks anything per cable, per card or per macro in those
passes runs hundreds of times a frame. These rules keep it linear, and keep animation frames off that path:

- **Look nodes up through a map built once per pass**, never a scan per item. `rebuildVisibleCables()` and its
  re-anchor passes map node id -> card and uuid -> node once; a per-cable `moduleComponentForNode()` (or a per-member
  `resolveMemberNodeId()`, a whole-graph scan) made the tick O(cables x cards x nodes) and took over 100 ms at 80
  tracks.
- **A paint computes each macro border once.** The outline, chip, collapse and '+'/'-' buttons and every port-strip row
  all ask `paintedMacroHullBounds()`; `GraphContentComponent::paint` opens a `graph_editor_paint::HullMemoScope`
  (`GraphEditorPaintMemo.h`) so the first answer per macro serves the rest of that paint. Nothing moves a card during
  one paint, so the memo never goes stale; it does not outlive the paint.
- **An animation frame repaints only what it moves.** A card glide or delete/undo ghost frame
  (`CardGlideAnimator::requestFrameRepaint`) repaints the ghosts' and snapshots' paths plus the cables touching a
  gliding card, before and after the step, and moves those cables in the memo by the change in their card's offset
  (`followCables`) instead of rebuilding every cable; a cable retract frame repaints only where its ghosts are drawn.
  A partial paint then skips every cable (`synth::ui::cablePaintBounds`, `CableCurve.h`) and macro border whose box
  misses the clip, before building its path. Repainting the whole canvas and rebuilding every cable per frame made
  a one-card delete or undo cost as much as the idle tick's full paint at every frame (about 16 ms on the Load test
  project, 49 ms at 80 tracks).
- **A glide only snapshots what can be seen.** `CardGlideAnimator::arm` renders a snapshot (a full card paint) only for
  a card whose old-to-new path crosses the visible canvas; an off-screen card is neither hidden nor snapshotted but
  keeps its item, so its cables still slide. Snapshotting every moved card stalled an undo of a big Auto Arrange for
  most of a second.

- **Walk cables through one index per pass.** `AudioProcessorGraph::getConnections()` copies and sorts every cable on
  each call, so a loop over routings that asks it per routing is O(routings x cables). Build one
  `synth::ui::ConnectionIndex` (`ModMatrixEndpoints.h`) and pass it to `resolveRouting` / `realEndpointBehindPorts`.
  The mod dots go further: the tick counts the sources on every knob once when the routing set changes
  (`ModDotController::recountIfRoutingsChanged`) and each card's `syncModDotButtons` only looks its knobs up. Each card
  resolving every routing on its own made one duplicated track freeze the app for seconds at 20 tracks.
- **A closed panel does no work.** The Mod Matrix's 10 Hz tick and its graph-change refresh skip a closed matrix
  (`ModMatrixComponent::updateRowsIfOpen`); opening it catches up.

`Tests/UI/Graph/GraphEditor/GraphEditorPaintWorkTests.cpp` and `Tests/UI/Graph/CardGlide/CardGlideFrameCostTests.cpp`
hold these as work counts (`graph_editor_paint::
workCounters()`), not timings. `Tests/App/ManyTracksProfileTests.cpp` is a disabled bench that prints the real per-frame
costs for 10 to 80 instrument tracks (`--gtest_also_run_disabled_tests --gtest_filter='*ManyTracksProfile*'`).

### The Load test project

Performance work reproduces on one saved project: `~/Music/AgentSynth/Load test.agsproj` (about 270 KB; 20 instrument
tracks, 20 macros, 264 modules, 412 cables, 39 modulation routings, no samples or hosted plugins). It is the project
whose track duplicates froze the app. It lives on the developer's machine, not in the repository. The same bench file's
`DISABLED_LoadTestProjectProfile` opens it (or `PROFILE_PROJECT=<bundle>`) and duplicates its first track
`PROFILE_DUPLICATE` times (default 10) through the real Cmd+D path, printing each duplicate's own time, the message-loop
work it leaves behind and the next paint:
`PROFILE_DUPLICATE=10 ./Tests --gtest_also_run_disabled_tests --gtest_filter='*LoadTestProjectProfile*'`.
`DISABLED_LoadTestDeleteUndoProfile` opens a temporary copy of it (autosaves left out, the copy deleted afterwards)
and prints the cost of deleting the on-screen card with the most cables, undoing it, and a parameter-only undo: each
call, the one full repaint it asks for, and the average ghost frame (`profile()` prints the same at N tracks):
`./Tests --gtest_also_run_disabled_tests --gtest_filter='*LoadTestDeleteUndoProfile*'`.
Its audio-side counterpart, the per-block render cost of the same project, is `Tests/App/ModuleCpuProfileTests.cpp`
([audio-engine.md](../architecture/audio-engine.md#measuring-render-cost)).

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
