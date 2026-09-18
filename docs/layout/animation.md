# Animation System

`Source/UI/Layout/UIAnimation.h` (namespace `synth::ui`) holds the shared animation infrastructure.
All motion in the app uses it. The `juce_animation` module is linked into both the `Core` and the
`AgentSynth` app targets. What is allowed to repaint at all is [rendering](rendering.md).

## Easing functions

Pure, stateless helpers, no component state required:

| Function | Signature | Curve |
|---|---|---|
| `easeOutCubic` | `float easeOutCubic(float t)` | Fast-out deceleration |
| `easeInOutCubic` | `float easeInOutCubic(float t)` | Smooth in and out |
| `easeOutBack` | `float easeOutBack(float t)` | Overshoots slightly, then settles |

All take `t` in `[0, 1]` and return a mapped `[0, 1]` value, which may exceed 1 briefly for
`easeOutBack`.

## AnimationDriver

A VBlank-driven, **time-bounded** tween: it animates a progress value from `0` to `1` over a
caller-specified duration, then auto-stops. It never ticks beyond `t = 1.0`.

```cpp
// Members in the owning component:
juce::VBlankAnimatorUpdater vblankUpdater_{ this };
synth::ui::AnimationDriver driver_;

// Start a 200 ms transition:
driver_.start(0.20);   // seconds

// In timerCallback / VBlank callback:
float t = driver_.getValue();
auto bounds = AnimationDriver::lerpBounds(startRect, endRect, t);
setBounds(bounds);
if (!driver_.isRunning())
    ; // settled — no more repaints fired
```

- Callers must hold both a `juce::VBlankAnimatorUpdater` and an `AnimationDriver` as members; the
  VBlank updater keeps the driver alive and ticking.
- `isRunning()` returns `false` once `t` reaches `1.0` — the driver stops itself and fires no
  further repaints.
- `AnimationDriver::lerpBounds(from, to, t)` is a static helper interpolating a
  `juce::Rectangle<int>` linearly between two positions.

## PanelSlide

A sliding panel does **not** animate by tweening its `Rectangle`. It owns a
`synth::ui::PanelSlide` — a `[0..1]` open fraction plus the from/to snapshot of the tween moving it
— and the owner's `resized()` derives the panel's *size* from that fraction:

```cpp
// Members:
synth::ui::PanelSlide librarySlide_, aiPanelSlide_, timelineSlide_;
synth::ui::AnimationDriver panelSlideAnim_;          // ONE driver for all three
juce::VBlankAnimatorUpdater vblankUpdater{ this };

void resized() override {
    const int libW = librarySlide_.sizeBetween(collapsedW, fullW);   // fraction -> px
    const int aiW  = aiPanelSlide_.sizeBetween(0, aiPanelWidth);
    ...                                                              // carve, then setBounds
}
```

Two properties follow, and both are the point:

- **A layout pass is correct whenever it runs.** A window resize, a theme change, a timeline height
  drag or a `setSize()` *in the middle of a slide* re-derives the same proportions instead of
  snapping the panel to an endpoint. `resized()` is the single geometry authority — there is no
  second "compute the target bounds" function to keep in step with it.
- **A toggle only moves the fraction.** It flips the logical flag, persists it, refreshes the
  toolbar and calls one seam. The alternative shape — flip the flag, call `resized()` (which snapped
  every panel to its FINAL bounds), *then* start a tween from the pre-toggle bounds — is what made a
  panel appear fully open for one frame and yank back before easing. That jump is a property of the
  call order, not of the easing.

`MainComponent::beginPanelSlide()` is that seam, and the shape to copy:

```cpp
void MainComponent::beginPanelSlide() {
    if (isLibraryVisible) moduleLibrary.setVisible(true);      // opening: visible before frame 0
    ...                                                        // closing: hidden in finish...()

    const bool canAnimate = isShowing();                       // no VBlank reaches an off-screen
    const bool a = librarySlide_ .retarget(isLibraryVisible  ? 1.0f : 0.0f, canAnimate);
    const bool b = aiPanelSlide_ .retarget(isAiPanelVisible  ? 1.0f : 0.0f, canAnimate);
    const bool c = timelineSlide_.retarget(isTimelineVisible ? 1.0f : 0.0f, canAnimate);
    if (! (a || b || c)) { finishPanelSlide(); return; }        // landed synchronously

    resized();                                                 // frame 0, before the first VBlank
    panelSlideAnim_.start(vblankUpdater, kPanelSlideMs, synth::ui::easeInOutCubic,
                         [this](float t) { applyPanelSlideFrame(t); },
                         [this]          { finishPanelSlide(); });
}
```

Four rules live in there:

1. **`retarget()` starts from the fraction's CURRENT value.** Re-toggling mid-flight reverses from
   where the panel *is*, never from an extreme — the same contract
   `PianoRollComponent::setScalePanelVisible` states for the scale panel's slide.
2. **`canAnimate == false` lands immediately.** No VBlank reaches an off-screen component, so a
   headless toggle — and a persisted restore before the window exists, where `initialiseCommon()`
   snaps the fractions rather than sliding them — must be synchronous. That is the contract
   `Tests/UI/Layout/PanelAnimationAndLoadingTests.cpp` asserts with no message pump at all, and the
   reason the whole test suite sees final bounds the instant a `simulate*Click()` returns.
3. **Every slide is retargeted, not just the one whose flag moved.** The three panels share one
   `AnimationDriver` — they share a window, so they must share a clock — and restarting it has to
   carry any slide already in flight to *its* target. One animator per panel would leave whichever
   slide the next toggle did not mention frozen half-open.
4. **A close is a slide too.** The panel is hidden in `finishPanelSlide()`, not at toggle time;
   hiding it up front is what makes "close" not an animation at all.

`applyPanelSlideFrame(t)` advances all three fractions and calls `resized()`. It adds **no**
`repaint()` — moving a child's bounds already invalidates the region it left and the one it arrived
at, and a full-window repaint per frame is exactly what [rendering](rendering.md) forbids.
`finishPanelSlide()` stops the driver, `finish()`es each fraction on its exact endpoint (a driver's
last frame is not guaranteed to be `t == 1`), hides whatever finished closing, and lays out once
more.

`PanelSlide` holds no animator and no JUCE GUI state, so its math is unit-tested headlessly
(`Tests/UI/Layout/UIAnimationTests.cpp`) and it is reusable by any multi-panel surface.
`GraphEditor::toggleModMatrixVisibility` and `PianoRollComponent::setScalePanelVisible` implement
the same pattern by hand and are deliberately **not** ported to it: they are single-panel surfaces
and correct as they stand.

## formatShortcutHint

```cpp
juce::String formatShortcutHint(const juce::String& base,
                                const juce::String& shortcutDisplay);
```

Composes tooltip text with an optional keyboard shortcut hint appended in `[brackets]`. An empty
`shortcutDisplay` omits the hint. Used by feature controls to produce consistent `setTooltip()`
strings.

## What moves, and how

| Feature | Animation | Owner |
|---|---|---|
| **Module drop landing** | Eased tween from drop position to the snapped, anti-overlapped final position (`easeOutBack`); `computeDropFinalPosition` is a pure helper | `GraphEditor` |
| **Mod-matrix show/hide** | Open-fraction tween, `easeInOutCubic` | `GraphEditor` |
| **Library sidebar show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver | `MainComponent` |
| **Library section collapse/expand** | Band-height fold (150 ms), `easeInOutCubic` | `ModuleLibraryComponent` |
| **AI panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver | `MainComponent` |
| **Timeline panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver — same slide, bottom axis | `MainComponent` |
| **Empty-canvas first-run hint** | Static drawn text, no animation — drawn only when `isCanvasEmpty(nodeCount)` returns `true` | `GraphEditor` |
| **Library rows** | Hover highlight; grab / dragging-hand cursor on draggable rows; per-module descriptions via `descriptionFor(name)` surfaced as `setTooltip()`; search-query substring highlight on matching labels | `ModuleLibraryComponent` |
| **Preset-load feedback** | Status bar text updated during load; no spinner | `MainComponent` into `StatusBarComponent` |
| **AI request Cancel and spinner** | Cancel button visible while a request is in flight; pulsing "thinking" spinner, time-bounded — stops on completion or cancel, confined to its region | `AIChatComponent` |
| **Timeline playhead** | 30 Hz vertical position line, **playing only**, repainting only the strip between its old and new x | `TimelinePlayheadOverlay` |
| **Zoom settle debounce** | `zoomSettleAnim`: a DEBOUNCE `AnimationDriver` (140 ms, `kZoomSettleMs`) with a no-op `onUpdate` — zero repaints while running, all the work in `onComplete`, which thaws the frozen card rasters | `GraphEditor` |
| **Toolbar toggle pill** | Instant state change (accent pill when on), no timer or animation — driven by `applyToolbarIcons()`'s and `setLibraryVisible()`'s `setToggleState(dontSendNotification)` calls | `ToolbarComponent` |

## The time-bounded animation rule

**All animations MUST be time-bounded.** An `AnimationDriver` runs for a finite duration and stops
at `t = 1.0`.

There are exactly **two** permitted exceptions, and both are bounded by an *activity* rather than by
a duration:

| Exception | Runs while | Confined to | Stops |
|---|---|---|---|
| AI thinking spinner (`AIChatComponent::SpinnerDot`) | a network request is in flight | its own 8x8 component | on completion or cancel |
| Timeline playhead (`TimelinePlayheadOverlay`) | the transport is PLAYING | a strip a few px wide, spanning the panel's ruler and lanes | on stop or pause, with one final strip |

`zoomSettleAnim` is **not** a third exception: it is a normal time-bounded `AnimationDriver` that
restarts on every zoom event — the restart-on-event IS the debounce — and its `onUpdate` is a no-op,
so it fires zero repaints of its own. Only `onComplete` does one-time work, thawing the raster
freeze.

Never add:

- Continuous `timerCallback` repaints on `ModuleComponent` or its children outside the existing
  gated 15 Hz gate.
- A free-running `AnimationDriver` — no duration, or a duration far longer than the visible
  transition.
- Per-frame `repaint()` calls in any path that is always active rather than gated to an active
  transition.

### The playhead's confinement contract

A third exception is not granted just because the second was. The playhead earned it by satisfying
three clauses, all enforced in code and asserted in `Tests/UI/Timeline/TimelinePlayheadTests.cpp`:

1. **Playing only.** The 30 Hz `juce::Timer` is started on the play transition and stopped on the
   stop or pause transition — it never runs while the transport is stopped, so an idle app repaints
   nothing (`ZeroRepaintsOver100IdleFrames`).
2. **Strip only.** A frame never repaints the component. It repaints the *union of the old and new
   line strips* (`kStripHalfWidth` px either side of the line), clipped to the bounds; a frame whose
   rounded x did not move requests nothing at all (`PlayingRequestsConfinedStrips`).
3. **Explicit stop.** Stopping emits exactly **one** final strip, so the line settles on the
   position playback ended at, and then goes silent (`StopEmitsOneFinalStripThenSilence`).

### The paint-count pattern

`TimelinePlayheadOverlay` routes **every** repaint it asks for through one protected virtual:

```cpp
protected:
    virtual void requestRepaintStrip (juce::Rectangle<int> strip);   // default: repaint (strip)
```

A test subclasses the component and overrides that seam to count calls and record rects, which turns
"how many repaints does an idle app cost?" into an ordinary headless assertion — no peer, no message
loop, no screenshot diffing:

```cpp
struct CountingPlayhead : synth::ui::TimelinePlayheadOverlay {
    using TimelinePlayheadOverlay::TimelinePlayheadOverlay;
    int requests = 0;
    void requestRepaintStrip (juce::Rectangle<int> r) override {
        ++requests;
        TimelinePlayheadOverlay::requestRepaintStrip (r);
    }
    void tick() { timerCallback(); }   // the protected timer callback, driven by hand
};
```

**Reuse this pattern for any future timed repaint.** A repaint budget that cannot be asserted is a
repaint budget that will regress.

`PianoRollComponent` is the first reuse, and it is a *delegation*, not a fourth exception. The roll
maps beats through its own zoom and scroll, so while it is open the overlay confines itself to
`getSharedRegion()` — empty while the roll is open, since the ruler rows follow the roll's mapping
override too, see [piano-roll](../timeline/piano-roll.md#the-ruler-above-the-roll) — and hands the
roll the drawn beat through `TimelinePlayheadOverlay::LocalPlayheadClient`. The roll draws the line
at its own x under an identical `requestRepaintStrip` seam and the identical no-move-no-repaint
gate: still one timer for the whole panel, still zero repaints while stopped. See
[piano-roll](../timeline/piano-roll.md#playhead-delegation).
