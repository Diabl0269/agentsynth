# Layout: Visuals & Animation

This document covers the in-module visualizer components (scope, EQ curve, frequency response,
wavetable display), the UI rendering-performance rules that keep the graph editor smooth, the
shared animation infrastructure (`AnimationDriver`, `PanelSlide`, the two time-bounded-animation
exceptions), and the Figma-style alignment guides shown while dragging a module.

- [1. Visualizer Components](#1-visualizer-components)
- [2. UI Rendering Performance](#2-ui-rendering-performance)
- [3. Animation System (Phase 5)](#3-animation-system-phase-5)
- [4. Alignment Guides (UI Phase 7 - Item 4)](#4-alignment-guides-ui-phase-7---item-4)

---

## 1. Visualizer Components

Several in-module visualizer components provide real-time signal display inside module cards; one of them (`EQCurveComponent`) is also an editor.

### FrequencyGrid (`Source/UI/FrequencyGrid.h`)

Not a component — the pure log-frequency / dB coordinate maths shared by the two frequency-domain views (`FrequencyResponseComponent` for Filter, `EQCurveComponent` for Parametric EQ). Both plot over the same 20 Hz – 20 kHz log axis, so `freqToX` / `xToFreq` / `indexToFreq` / `formatHzLabel` / `findPeakBin` live here once. The dB axis is *not* fixed — `dbToY` / `yToDb` take `minDb` and `maxDb` per call, because the filter view needs an asymmetric −40…+50 dB window (resonance peaks overshoot a long way) while the EQ view uses a symmetric ±30 dB one. Each component still paints its own grid: they differ in dB step, label set, and whether the 0 dB line is emphasised.

### FrequencyResponseComponent (`Source/UI/FrequencyResponseComponent.h`)

Serum-style frequency-response curve with an optional FFT spectrum overlay, used by `FilterModule` cards. **Hidden by default** on the card — a "Show Response" toggle reveals it (same opt-in pattern as "Show Scope"); the nested "Show Spectrum" control only appears while the response view is open. The component's 30 Hz timer starts in `visibilityChanged` only while visible, so a closed Filter card pays no animation cost.

The stroked path is **not** clamped to the bottom of the view: magnitudes may fall below `minDb` (`plotDb` only caps peaks at `maxDb`), and `paint` lets those points map to `y > height` so a steep LPF roll-off exits the clip region instead of drawing a horizontal floor along the right edge.

**Phase 4 paint additions:**
- Hz axis labels at 100 Hz, 1 kHz, 10 kHz — drawn 3 px right of each vertical frequency gridline (10 kHz label is right-justified to stay within bounds at narrow widths)
- dB axis labels at −20, 0, +20 dB — drawn at the left edge of each horizontal dB gridline
- Resonance peak marker: filled dot (accent cyan `0xff00b4d8`) with a dark outline ring, plus a small text callout label using `formatHzLabel` at the magnitude peak; callout is skipped when component width < 44 px

**Public static helpers** (headless-testable, no component state needed):

| Helper | Signature | Notes |
|---|---|---|
| `findPeakBin` | `static int findPeakBin(const float* mags, int numBins)` | Returns index of maximum value; returns -1 for null/empty |
| `formatHzLabel` | `static juce::String formatHzLabel(float hz)` | `100 → "100Hz"`, `1000 → "1kHz"`, `10000 → "10kHz"` |
| `freqToXStatic` | `static float freqToXStatic(float freq, float width)` | Log-scaled freq → x pixel; mirrors private `freqToX` (minFreq=20, maxFreq=20000) |
| `dbToYStatic` | `static float dbToYStatic(float db, float height)` | dB → y pixel; mirrors private `dbToY` (minDb=−40, maxDb=50) |
| `plotDb` | `static float plotDb(float db)` | Caps at `maxDb`; leaves values below `minDb` unclamped so the stroke can leave the view |

All axis helpers forward to `synth::ui::FrequencyGrid`; they stay as the component's public API so existing callers and tests are unaffected.

### EQCurveComponent (`Source/UI/EQCurveComponent.h`)

Interactive response curve for `ParametricEQModule`, in the traditional DAW idiom. Used twice over the same module: inline on the card and inside the pop-out `EQWindow`. Both views stay in sync automatically — each one's timer picks the other's edits up on its next tick.

On the card the curve is 150 px tall and laid out *beside* the port labels (`x` from 88 to `width − 88`, starting at `y = 60`) rather than below them. The labels only occupy a narrow gutter down each edge, so on a six-input module this reclaims ~125 px of otherwise dead space at the top of the card.

**Gestures.** All four bands start disabled, so the curve starts empty with a "Double-click to add an EQ point" hint.

| Gesture | Effect |
|---|---|
| Double-click empty space | Adds a point, enabling the slot that best fits that frequency (`findBandForNewPoint`) |
| Double-click a point | Removes it (disables that band; its settings are kept) |
| Drag a point | Sets frequency (x) and gain (y) |
| Scroll over a point | Widens / narrows it (Q), multiplicatively |
| Hover | Halos the handle and shows a `freq / gain / Q` readout bottom-right |

The mouse handlers are deliberately thin wrappers over public `addPointAt` / `removeBand` / `dragBandTo` / `nudgeBandQ` / `hitTestBand`, so the interaction is unit-tested without synthesising `juce::MouseEvent`s (`EQCurveInteraction.*`).

**Undo.** `onGestureStart` / `onGestureEnd` bracket every parameter-changing gesture; `ModuleComponent::wireEqGestureCallbacks` binds them to `AppUndoManager::captureBeforeState` / `pushSnapshotFromCapture`, so one drag is one undo step. Deliberately *not* opened on `mouseDown` — a click that never becomes a drag would otherwise push an empty undo entry. `dragBandTo` itself does no bracketing since it is called repeatedly during a drag. Both are wired through a `Component::SafePointer`, so a pop-out window that outlives its card becomes a no-op rather than a dangling call.

- **dB window**: symmetric ±30 dB, so 0 dB sits at the exact vertical centre and boosts read as the mirror of cuts. dB gridlines at ±12 and ±24; the 0 dB line is drawn brighter as the reference the curve is read against. `gainAtY` clamps to the parameter's ±24 dB, since the view is deliberately wider than the range.
- **Curve source**: `ParametricEQModule::responseDb()` — the analytic prototypes the module's biquad coefficients are derived from, so the drawing and the DSP cannot drift apart (locked by `ParametricEQAudio.MeasuredResponseTracksTheAnalyticCurve`). Sampled at 512 log-spaced points; the gradient fill hangs off the 0 dB line rather than the bottom edge, so a cut fills downward and a boost upward.
- **Band handles**: one numbered ring per *enabled* band at (centre freq, band gain) — the parameter pair, so vertical drag maps 1:1 to gain. As in Cubase/Pro-Q, a shelf's handle sits at its corner frequency at the full shelf gain, which is above the curve there (a shelf reaches its full gain only well past the corner).
- **Spectrum overlay**: 1024-point FFT of the module's `VisualBuffer`, drawn *behind* the curve over its own −80…0 dB window, smoothed with an exponential moving average. **On by default** here (unlike the Filter card) because the curve is meant to be read against it.
- **Repaint discipline**: 30 Hz timer that repaints when a band setting or the output trim changed, when hover/selection changed, or while the spectrum has actual signal. The analyser gates on peak < 1e-5 and repaints one final frame on the transition to silence, so a default-on spectrum still settles to **zero repaints on an idle patch** — that gate is what keeps this compliant with §2–3. Never make it unconditional.

### EQWindow (`Source/UI/EQWindow.h`)

Pop-out editor for a Parametric EQ, opened from the card's "Open EQ Window" button. Content-only `juce::Component` (same pattern as `SettingsWindow`); the caller wraps it in a `juce::DialogWindow` via `LaunchOptions::launchAsync()`. Hosts a second `EQCurveComponent` over the same module at 720×420 (resizable), plus a spectrum toggle and a gesture hint.

`ModuleComponent` holds the dialog as a `Component::SafePointer` and **deletes it in `detachFromProcessor()`** — the window references the module, so leaving it open across a graph rebuild would dangle. Re-clicking the button brings the existing window to front rather than opening a second one.

### ScopeComponent (`Source/UI/ScopeComponent.h`)

Oscilloscope waveform display used by all modules that have a `VisualBuffer`.

**Phase 4 paint additions:**
- Horizontal amplitude grid lines at ±0.5 and ±1.0 (drawn in `theme.colors.border` at 0.6 alpha)
- Centre (0.0) grid line — slightly brighter (`border.brighter(0.3f)`) to serve as the waveform baseline
- Centred "No Signal" empty-state text (in `textDisabled` colour) when the buffer peak ≤ 0.02 — replaces the waveform path draw entirely; `return` is early so no waveform is drawn on silence

**Public static helpers** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `isNoSignal` | `static bool isNoSignal(float peak) noexcept` | Returns `true` when `peak <= 0.02f` |
| `amplitudeToY` | `static float amplitudeToY(float amp, juce::Rectangle<float> bounds) noexcept` | Maps amplitude [-1,1] → y pixel; amp=+1 → top, amp=-1 → bottom, amp=0 → centre; uses 45% height per side |

**Themed colours**: resolved via `dynamic_cast<AppLookAndFeel*>` — `border` for grid, `textDisabled` for no-signal label, `accent` for the waveform. When the cast fails (headless), hardcoded fallbacks are used (`0xff2A2F38`, `0xff5C6470`, limegreen).

### ThresholdControlComponent (`Source/UI/ThresholdControlComponent.h`)

Live threshold readout used by Sample & Hold (meter-only, bipolar), ADSR (slider + unipolar meter)
and Comparator (slider + bipolar meter). The Decibels scale is ready for Compressor / Limiter to
adopt later without a new widget.

`TriggerMeterComponent` is a compatibility alias for the same type.

Two presentations:

- **Meter-only**: a thin bar (18 px) matching the original Sample & Hold trigger meter. The module
  keeps its own rotary for Threshold.
- **Slider+meter**: a caption, a level bar, and a linear slider whose thumb is the threshold
  "slice". The bar fills to the current input so the slice can be set by eye.

The bar switches to the `gateWire` colour while the detector is armed. A marker sits at the
**effective** threshold (knob + Threshold CV). Reads `ThresholdMeterSource`
(`getMeterLevel()`, `getEffectiveThreshold()`, `isOverThreshold()`, `getTriggerCount()`).

**Why it is a separate component, not something `ModuleComponent::paint` draws:** `ModuleComponent`
is `setBufferedToImage(true)`, so painting a live meter inside its `paint()` would invalidate that
cached image and re-run the module's text layout on every meter tick — exactly the repaint storm
§2 prohibits. Instead this owns a 20 Hz timer and repaints *itself* only when a displayed value
moves past a visible amount (see `needsRepaint`), leaving the parent's cached image untouched.

**Public static helpers** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `valueToNormalized` | `static float valueToNormalized(float, ThresholdScale, float minDb)` | Maps native units → [0, 1] |
| `valueToX` | `static float valueToX(float, float x, float width, ThresholdScale, float minDb)` | Maps native units → x offset; bipolar default matches the original meter |
| `needsRepaint` | `static bool needsRepaint(...) noexcept` | Mirrors the `timerCallback` repaint gate |
| `getFlashFrames` | `static constexpr int getFlashFrames() noexcept` | Ticks the fired-flash stays lit |

### WavetableDisplayComponent (`Source/UI/WavetableDisplayComponent.h`)

Wavetable frame view used by the `Wavetable` module card. Draws the frame currently under the scan position as a solid trace, with `kGhostFrames` (3) receding low-alpha traces sampled slightly further along the stack so the scan direction and the table's depth read as three-dimensional. Captioned with the table name and `frame/total`.

The trace is drawn **warped** — `getDisplayWaveformAt()` runs the same `readWarped()` the audio path does. A Warp knob whose effect is invisible is a knob users do not trust.

**Repaints are gated** — the 15 Hz timer compares a change signature (quantised scan position, table name, frame count, and `getWarpSignature()` — warp mode + quantised amount + interpolation mode) and calls `repaint()` only when it differs. This is required by §2: the display is always visible inside a `setBufferedToImage(true)` module card, so an unconditional per-tick repaint would invalidate that cache 15 times a second.

#### Wavetable card chrome

`ModuleComponent` builds the following for `Wavetable` modules:

- **"Load Wavetable..."** `TextButton` — opens an async `juce::FileChooser` (starting in the module's browser folder), and on success calls `loadWavetableFile()` and switches the module's `table` choice to `Loaded File`.
- **Folder browser row** — `[Folder...] [<] [>] [caption]`. `Folder...` opens an async directory chooser and calls `setWavetableFolder()`; `<` / `>` step the folder cursor; the caption shows the loaded file name and `index/total`.

Both completion lambdas hold a `Component::SafePointer` and re-derive the module via `dynamic_cast`, since the card can be destroyed while the dialog is open. The card also implements `FileDragAndDropTarget` for audio files, routing drops through the same import path as the Load button.

The card is **double-width** (issue #180): 15 knobs, 8 combos and a 16-jack port stack. Laid out flat, that came to 560×869 — technically correct and genuinely unusable, a wall of identical knobs with no hierarchy. Three mechanisms bring it to **560×554** and, more importantly, give it a reading order:

**1. Two-column jack gutter, both columns on the LEFT.** `getInputPortColumns()` returns 2 for a card with more than 10 visible input jacks at double width; `getPortCenter()` then lays the inputs out **column-major** (jack 0 top-left, running down then over) at `kPortColumnStride` (100 px) apart. Sixteen jacks in one column set a ~390 px floor on the card height before a single control is placed.

**Inputs stay on the left, outputs on the right — that convention is not negotiable for a saving in height.** It is what makes signal flow read left to right across a patch. Spilling the overflow down the right edge was tried and reverted: it reads as an output, and inputs and outputs are drawn identically, so the side is the only cue there is.

The real objection to an interior column — the module covers the lower half of a cable you are dragging towards it — is answered by not aiming at the gutter at all. Release the cable **on the destination knob** instead; see [`modulation.md` § Drag-to-Knob Modulation](modulation.md).

Everything that touches jack geometry — wire drawing, hit-testing (`getPortForPoint`), painting — reads `getPortCenter`, so this is the only place that changes. One consequence worth knowing: `getContentTopY()` takes the **maximum** y over all inputs rather than the last one's, because with more than one column an odd jack count leaves the second column a row short, so the last jack is not the lowest.

**2. Tabbed control body.** The 23 controls are grouped into five pages — **Tune / Unison / Phase / Sub / File** — by `kWavetablePages` in `ModuleComponent.cpp`, which maps parameter *display names* to pages. Three controls sit outside the strip: `Position` and `Warp` / `Warp Amt` are **pinned** above it (they are what you actually perform with), and `Table` is laid out in the chrome band beside the display it selects.

- **Jacks are never tabbed.** All 16 CV inputs stay on the card at all times, so a cable can never point at a hidden port. Only knobs and combos page.
- **Anything that paints from a knob's bounds must check visibility.** A hidden knob keeps the bounds it had when its page was last laid out. Modulation rings go through `getModRingSliderIndex()` and drop targeting through `getModTargetPortForPoint()`; both return "none" for a knob whose page is hidden, so a ring cannot paint over empty card and a hidden knob cannot swallow a cable drop.
- The card is sized to the **tallest** page, not the active one. A card that grew and shrank would shove its neighbours around the canvas on every tab click.
- Page knob rows are **centred**, and page combos run three across — most pages carry fewer than the 6 available knob columns, and left-aligning them stranded half the card's width.
- `createWavetableTabs()` must run **after** `createControls()` (it groups the controls that call creates) and must end by calling `updateLayout()` — `createControls()` already sized the card, so without a second pass the card keeps its flat-grid height and the tabbed layout never applies.

**3. Chrome beside the ports.** The display / Table / button row / caption sit **beside** the jack stack, starting just under the header — the same reclaim the Parametric EQ card makes for its response curve (see §1). The body still starts below the lowest jack.

The final size is pinned by `EstimatedModuleSizesMatchTheRealComponents`; `WavetableCardSplitsItsJackGutterIntoTwoColumns` and `WavetableTabsSwitchContentWithoutResizingTheCard` guard the two mechanisms above (including that every knob is reachable from some page — a control on no page is unusable).

**Public static helper** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `quantisePosition` | `static int quantisePosition(float position, int steps = 200)` | Clamps to [0,1] and quantises the scan position into `steps` buckets — the repaint gate's change signature |

**Themed colours**: resolved via `dynamic_cast<AppLookAndFeel*>` — `bg1` for the panel, `border` for the frame and zero line, `accent` for the traces, `textMuted` for the caption. Falls back to hardcoded colours when the cast fails (headless).

---

## 2. UI Rendering Performance

Guidelines to preserve smooth frame rates:

- **`ModuleComponent` is buffered to an image via `synth::ui::ZoomFrozenCachedImage`** (`Source/UI/ZoomFrozenCachedImage.h`), NOT raw `setBufferedToImage(true)`. JUCE's standard cached image keys on the *accumulated* device scale (`zoomLevel × deviceScale`), so every wheel tick used to re-rasterize every visible card. During a zoom gesture, `GraphEditor` freezes each card's raster scale (`setModuleRasterFrozen(true)`) — the existing image is resampled, not re-rendered — and thaws `kZoomSettleMs` (140 ms) after the last zoom event with exactly one crisp re-render. Never call `setBufferedToImage()` on `ModuleComponent` — JUCE asserts and silently deletes the custom cache. The `GraphEditor` 30 Hz connection animation blits these cached images rather than re-running JUCE text layout and parameter read on every animation frame. **Do not reintroduce unconditional `repaint()` calls in `timerCallback` on module components or their always-visible children.**
- **`buildVisibleCables()` is memoized.** `GraphEditor::repaintCanvas()` is the single "canvas changed" seam that drops the memo and repaints (`content.repaint()`); any new repaint of the canvas content must go through `repaintCanvas()`, never `content.repaint()` directly.
- **Gated 15 Hz repaint**: `ModuleComponent::timerCallback` repaints only when the display needs to change — specifically when RMS level changes, active modulation routing changes, or the sequencer step index changes. The timer runs at 15 Hz (`startTimerHz(15)`).
- **Single re-skin pass on theme switch**: `AppLookAndFeel::applyTheme()` → `sendLookAndFeelChangeMessage()` → one `repaint()`. No timer is started and no continuous repaint is added during or after a theme switch.
- **`applyToolbarIcons()` is gated**: cloning `Drawable` objects is expensive. The call is restricted to narrow-mode transitions in `MainComponent::resized()` — not executed on every resize frame. See `docs/layout.md` §5 for the gate logic.
- **Status bar polls at 5 Hz** and repaints only itself (`repaint()` on `StatusBarComponent` only). There are zero `writeToLog` calls in the status-polling path.
- **Automation → UI reflection adds no new timer.** `GraphEditor::timerCallback()`'s existing 30 Hz tick drains `AudioEngine::getAutomationUiFeed()` and calls `ModuleComponent::reflectParameterValue()`, which only ever calls `slider->setValue(..., juce::dontSendNotification)` — no direct `repaint()`; the card's own gated 15 Hz timer and buffered-image cache pick the change up on their own schedule, same as any other control change.
- **All UI animations are time-bounded** (see §3). They run for a finite transition duration and stop when settled. Never add continuous / per-frame animations outside the **two** exceptions defined in §3 (the AI thinking spinner and the timeline playhead), each of which is bounded by an activity, not by a duration.
- **The timeline panel adds no timer.** Its transport poll rides MainComponent's existing 10 Hz tick, only while the panel is visible, and repaints the ruler only when the ruler's own state (time signature, loop trio) changed. The playhead's 30 Hz strip repaint is the §3 exception and runs only while the transport is playing.

---

## 3. Animation System (Phase 5)

Phase 5 introduces a shared animation infrastructure in `Source/UI/UIAnimation.h` (namespace `synth::ui`). All motion in the app uses this helper. The `juce_animation` module is linked into both `Core` and the `AgentSynth` app target.

### Easing functions

Pure, stateless helpers — no component state required:

| Function | Signature | Curve |
|---|---|---|
| `easeOutCubic` | `float easeOutCubic(float t)` | Fast-out deceleration |
| `easeInOutCubic` | `float easeInOutCubic(float t)` | Smooth in + out |
| `easeOutBack` | `float easeOutBack(float t)` | Overshoots slightly then settles |

All take `t` in `[0, 1]` and return a mapped `[0, 1]` value (may exceed 1 briefly for `easeOutBack`).

### `AnimationDriver`

VBlank-driven, **time-bounded** tween: animates a progress value from `0` to `1` over a caller-specified duration, then auto-stops. It never ticks beyond `t = 1.0`.

**Usage contract:**

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

**Key properties:**
- Callers must hold both a `juce::VBlankAnimatorUpdater` and an `AnimationDriver` as members (VBlank updater keeps the driver alive and ticking).
- `isRunning()` returns `false` once `t` reaches `1.0` — the driver stops itself and fires no further repaints.
- `AnimationDriver::lerpBounds(from, to, t)` is a static helper that interpolates a `juce::Rectangle<int>` linearly between two positions.

### `PanelSlide` — fraction-driven panel layout

A sliding panel does **not** animate by tweening its `Rectangle`. It owns a `synth::ui::PanelSlide`
— a `[0..1]` open fraction plus the from/to snapshot of the tween moving it — and the owner's
`resized()` derives the panel's *size* from that fraction:

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

- **A layout pass is correct whenever it runs.** A window resize, a theme change, a timeline
  height drag or a `setSize()` *in the middle of a slide* re-derives the same proportions instead
  of snapping the panel to an endpoint. `resized()` is the single geometry authority — there is no
  second "compute the target bounds" function to keep in step with it.
- **A toggle only moves the fraction.** It flips the logical flag, persists it, refreshes the
  toolbar, and calls one seam. The old shape — flip the flag, call `resized()` (which snapped
  every panel to its FINAL bounds), *then* start a tween from the pre-toggle bounds — is what made
  a panel appear fully open for one frame and yank back before easing. That jump is a property of
  the call order, not of the easing.

`MainComponent::beginPanelSlide()` is that seam, and the shape to copy:

```cpp
void MainComponent::beginPanelSlide() {
    if (isLibraryVisible) moduleLibrary.setVisible(true);      // opening: visible before frame 0
    ...                                                        // closing: hidden in finish…()

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
   headless toggle (and a persisted restore before the window exists — `initialiseCommon()` snaps
   the fractions rather than sliding them) must be synchronous. That is the contract
   `Tests/PanelAnimationAndLoadingTests.cpp` asserts with no message pump at all, and the reason
   the whole test suite sees final bounds the instant a `simulate*Click()` returns.
3. **Every slide is retargeted, not just the one whose flag moved.** The three panels share one
   `AnimationDriver` (they share a window, so they must share a clock), and restarting it has to
   carry any slide already in flight to *its* target. One animator per panel would leave whichever
   slide the next toggle didn't mention frozen half-open.
4. **A close is a slide too.** The panel is hidden in `finishPanelSlide()`, not at toggle time —
   hiding it up front is what used to make "close" not an animation at all.

`applyPanelSlideFrame(t)` advances all three fractions and calls `resized()`; it adds **no**
`repaint()` (moving a child's bounds already invalidates the region it left and the one it arrived
at — a full-window repaint per frame is exactly what §2 forbids). `finishPanelSlide()` stops the
driver, `finish()`es each fraction on its exact endpoint (a driver's last frame is not guaranteed
to be `t == 1`), hides whatever finished closing, and lays out once more.

`PanelSlide` holds no animator and no JUCE GUI state, so its math is unit-tested headlessly
(`Tests/UIAnimationTests.cpp`) and it is reusable by any multi-panel surface.
`GraphEditor::toggleModMatrixVisibility` and `PianoRollComponent::setScalePanelVisible` already
implement the same pattern by hand and are **not** ported to it — they are single-panel surfaces
and correct as they stand.

### `formatShortcutHint`

```cpp
juce::String formatShortcutHint(const juce::String& base,
                                const juce::String& shortcutDisplay);
```

Composes tooltip text with an optional keyboard shortcut hint appended in `[brackets]`. Pass an empty `shortcutDisplay` to omit the hint. Used by feature controls to produce consistent `setTooltip()` strings.

### Phase 5 micro-interactions

| Feature | Animation | Note |
|---|---|---|
| **Module drop landing** | Eased tween from drop position → snapped + anti-overlapped final position (`easeOutBack`); `computeDropFinalPosition` is a pure helper | `GraphEditor` |
| **Mod-matrix show/hide** | Open-fraction tween, `easeInOutCubic` | `GraphEditor` |
| **Library sidebar show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver | `MainComponent` |
| **Library section collapse/expand** | Band-height fold (150 ms), `easeInOutCubic` | `ModuleLibraryComponent` |
| **AI panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver | `MainComponent` |
| **Timeline panel show/hide** | `PanelSlide` fraction tween (190 ms, `easeInOutCubic`), shared driver — same slide, bottom axis | `MainComponent` |
| **Empty-canvas first-run hint** | Static drawn text; no animation — drawn only when `isCanvasEmpty(nodeCount)` returns `true` | `GraphEditor` |
| **ModuleLibraryComponent rows** | Row hover-highlight; grab/dragging-hand cursor on draggable rows; per-module descriptions via `descriptionFor(name)` surfaced as `setTooltip()`; search-query substring highlight on matching labels | `ModuleLibraryComponent` |
| **Preset-load feedback** | Status bar text updated during load; no spinner | `MainComponent` → `StatusBarComponent` |
| **AI request Cancel + spinner** | Cancel button visible while a request is in flight; pulsing "thinking" spinner (time-bounded — stops on completion or cancel, confined to its region) | `AIChatComponent` |
| **Timeline playhead** | 30 Hz vertical position line, **playing only**, repainting only the strip between its old and new x | `TimelinePlayheadOverlay` (see §3's confinement contract below) |
| **Zoom settle debounce** | `zoomSettleAnim`: a DEBOUNCE `AnimationDriver` (140 ms, `kZoomSettleMs`), no-op `onUpdate` — zero repaints while running, all the work happens in `onComplete` (thaws the frozen card rasters) | `GraphEditor` |
| **Toolbar toggle pill** | Instant state change (accent pill when on), no timer/animation — driven by `applyToolbarIcons()`'s / `setLibraryVisible()`'s `setToggleState(dontSendNotification)` calls | `ToolbarComponent` |

### Time-bounded animation rule

**All animations MUST be time-bounded.** An `AnimationDriver` runs for a finite duration and stops at `t = 1.0`.

There are exactly **two** permitted exceptions, and both are bounded by an *activity* rather than by a duration:

| Exception | Runs while | Confined to | Stops |
|---|---|---|---|
| AI thinking spinner (`AIChatComponent::SpinnerDot`) | a network request is in flight | its own 8×8 component | on completion or cancel |
| Timeline playhead (`TimelinePlayheadOverlay`) | the transport is PLAYING | a strip a few px wide, spanning the panel's ruler + lanes | on stop/pause, with one final strip |

`zoomSettleAnim` (§2) is **not** a third exception: it is a normal time-bounded `AnimationDriver`
that restarts on every zoom event (the restart-on-event IS the debounce) and its `onUpdate` is a
no-op, so it fires zero repaints of its own — only `onComplete` does one-time work (thawing the
raster freeze).

Never add:
- Continuous `timerCallback` repaints on `ModuleComponent` or its children outside the existing gated 15 Hz gate.
- A free-running `AnimationDriver` (no duration, or duration far longer than the visible transition).
- Per-frame `repaint()` calls in any path that is always active (not gated to an active transition).

#### The playhead's confinement contract

A third exception is not granted just because the second was. The playhead earned it by satisfying three clauses, all enforced in code and asserted in `Tests/TimelinePlayheadTests.cpp`:

1. **Playing only.** The 30 Hz `juce::Timer` is started on the play transition and stopped on the stop/pause transition — it never runs while the transport is stopped, so an idle app repaints nothing (`ZeroRepaintsOver100IdleFrames`).
2. **Strip only.** A frame never repaints the component. It repaints the *union of the old and new line strips* (`kStripHalfWidth` px either side of the line), clipped to the bounds; a frame whose rounded x did not move requests nothing at all (`PlayingRequestsConfinedStrips`).
3. **Explicit stop.** Stopping emits exactly **one** final strip — so the line settles on the position playback ended at — and then goes silent (`StopEmitsOneFinalStripThenSilence`).

#### The paint-count pattern

`TimelinePlayheadOverlay` routes **every** repaint it asks for through one protected virtual:

```cpp
protected:
    virtual void requestRepaintStrip (juce::Rectangle<int> strip);   // default: repaint (strip)
```

A test subclasses the component and overrides that seam to count calls and record rects, which turns "how many repaints does an idle app cost?" into an ordinary headless assertion — no peer, no message loop, no screenshot diffing:

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

**Reuse this pattern for any future timed repaint.** A repaint budget that cannot be asserted is a repaint budget that will regress.

`PianoRollComponent` is the first reuse, and it is a *delegation*, not a fourth exception: the roll maps beats through its own zoom/scroll, so while it is open the overlay confines itself to `getSharedRegion()` (empty while the roll is open — the ruler rows follow the roll's mapping override too, see `docs/timeline_panel_clips_automation.md` §2) and hands the roll the drawn beat through `TimelinePlayheadOverlay::LocalPlayheadClient`. The roll draws the line at its own x under an identical `requestRepaintStrip` seam and the identical no-move-no-repaint gate — still one timer for the whole panel, still zero repaints while stopped. See `docs/timeline_panel_clips_automation.md` §2 (the piano roll section).

---

## 4. Alignment Guides (UI Phase 7 - Item 4)

Agent Synth provides **Figma-style smart alignment guides** while dragging modules in the graph editor. These visual cues help you align new modules with existing ones at edges and centers.

### What they are

- **Edge-to-edge snap lines** — appear when a module edge gets within 8 px of another module's edge (left/right/top/bottom)
- **Center alignment lines** — appear when dragging module centers align vertically or horizontally
- **Visual style** — muted-colour lines at ~70% opacity, ~1.5 px stroke width

### How they work

1. When you begin dragging a module, `GraphEditor::updateDragPreview()` scans all existing module rectangles in canvas space.
2. For each edge of the dragged ghost and each neighbor, it computes:
   - Edge-to-edge snap (distance < `Metrics.gridSize` = 8 px)
   - Center alignment (module centerX/centerY matches neighbor's centerX/centerY)
3. Matching positions are stored as `AlignmentGuide` structs and rendered in `paintOverChildren()` using the current theme's `textMuted` colour.
4. Guides are **visual-only** — they do not alter snapping behavior (`findFreeSlot()` still uses the soft 8 px grid).

### Configuration

A toggle is available in `Settings → Preferences → Show Alignment Guides`. It persists as `alignmentGuidesEnabled` in `juce::ApplicationProperties`.

| Toggle State | Effect |
|---|---|
| **ON** (default) | Guide lines appear during drag when alignment candidates are detected |
| **OFF** | No guide lines appear; only the module ghost is drawn |

### Implementation details

- **Grid threshold**: `Metrics.gridSize = 8` px — snap detection window radius
- **Guide opacity**: `Metrics.guideAlpha = 0.7` (70%)
- **Stroke width**: `Metrics.guideLineWidth = 1.5` px
- **Deduplication**: Only the closest guide per type (left/right/top/bottom.centerX/centerY) is shown

### Visual examples

**When dragging:**

- Move module left edge near another's right edge → vertical grey line appears
- Move module top edge near another's bottom edge → horizontal grey line appears  
- Align centerX with neighbor's centerX → vertical centre line appears
- Align centerY with neighbor's centerY → horizontal centre line appears

**Theme switching:**

When you change themes, alignment guides update to the new `textMuted` colour automatically.

---
