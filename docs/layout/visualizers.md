# In-Card Visualizers

The real-time signal displays that live inside module cards — and, in two cases, edit the signal
too. Every one of them is subject to [rendering](rendering.md): a module card is buffered to an
image, so a visualizer that repainted unconditionally would invalidate that cache on every tick. The
card geometry they sit in is [module-card](module-card.md).

## FrequencyGrid

`Source/UI/ModuleViews/FrequencyGrid.h`. Not a component — the pure log-frequency and dB coordinate
maths shared by the two frequency-domain views, `FrequencyResponseComponent` for Filter and
`EQCurveComponent` for Parametric EQ. Both plot over the same 20 Hz to 20 kHz log axis, so
`freqToX` / `xToFreq` / `indexToFreq` / `formatHzLabel` / `findPeakBin` live here once.

**The dB axis is not fixed.** `dbToY` / `yToDb` take `minDb` and `maxDb` per call, because the
filter view needs an asymmetric -40 to +50 dB window (resonance peaks overshoot a long way) while
the EQ view uses a symmetric +/-30 dB one. Each component still paints its own grid: they differ in
dB step, label set, and whether the 0 dB line is emphasised.

## FrequencyResponseComponent

`Source/UI/ModuleViews/FrequencyResponseComponent.h`. A frequency-response curve with an optional
FFT spectrum overlay, used by `FilterModule` cards. **Hidden by default** on the card — a "Show
Response" toggle reveals it, the same opt-in pattern as "Show Scope" — and the nested "Show
Spectrum" control appears only while the response view is open. Its 30 Hz timer starts in
`visibilityChanged` only while visible, so a closed Filter card pays no animation cost.

**The stroked path is not clamped to the bottom of the view.** Magnitudes may fall below `minDb`
(`plotDb` only caps peaks at `maxDb`), and `paint` lets those points map to `y > height`, so a steep
low-pass roll-off exits the clip region instead of drawing a horizontal floor along the right edge.

Paint elements: Hz axis labels at 100 Hz, 1 kHz and 10 kHz, drawn 3 px right of each vertical
frequency gridline, with the 10 kHz label right-justified to stay within bounds at narrow widths; dB
axis labels at -20, 0 and +20 dB at the left edge of each horizontal gridline; and a resonance peak
marker — a filled accent-cyan dot (`0xff00b4d8`) with a dark outline ring, plus a small text callout
using `formatHzLabel` at the magnitude peak, skipped when the component is narrower than 44 px.

**Public static helpers** (headless-testable, no component state needed):

| Helper | Signature | Notes |
|---|---|---|
| `findPeakBin` | `static int findPeakBin(const float* mags, int numBins)` | Index of the maximum value; -1 for null or empty |
| `formatHzLabel` | `static juce::String formatHzLabel(float hz)` | `100` becomes `"100Hz"`, `1000` becomes `"1kHz"`, `10000` becomes `"10kHz"` |
| `freqToXStatic` | `static float freqToXStatic(float freq, float width)` | Log-scaled freq to x pixel; mirrors private `freqToX` (minFreq 20, maxFreq 20000) |
| `dbToYStatic` | `static float dbToYStatic(float db, float height)` | dB to y pixel; mirrors private `dbToY` (minDb -40, maxDb 50) |
| `plotDb` | `static float plotDb(float db)` | Caps at `maxDb`; leaves values below `minDb` unclamped so the stroke can leave the view |

All axis helpers forward to `synth::ui::FrequencyGrid`; they stay as the component's public API so
existing callers and tests are unaffected.

## EQCurveComponent

`Source/UI/ModuleViews/EQCurveComponent.h`. An interactive response curve for `ParametricEQModule`,
in the traditional DAW idiom. It is used twice over the same module — inline on the card and inside
the pop-out `EQWindow` — and both views stay in sync automatically, each one's timer picking the
other's edits up on its next tick.

On the card the curve is 150 px tall and laid out *beside* the port labels (`x` from 88 to
`width - 88`, starting at `y = 60`) rather than below them. The labels occupy only a narrow gutter
down each edge, so on a six-input module this reclaims about 125 px of otherwise dead space at the
top of the card.

**Gestures.** All four bands start disabled, so the curve starts empty with a "Double-click to add
an EQ point" hint.

| Gesture | Effect |
|---|---|
| Double-click empty space | Adds a point, enabling the slot that best fits that frequency (`findBandForNewPoint`) |
| Double-click a point | Removes it (disables that band; its settings are kept) |
| Drag a point | Sets frequency (x) and gain (y) |
| Scroll over a point | Widens or narrows it (Q), multiplicatively |
| Hover | Halos the handle and shows a freq / gain / Q readout bottom-right |

The mouse handlers are deliberately thin wrappers over public `addPointAt` / `removeBand` /
`dragBandTo` / `nudgeBandQ` / `hitTestBand`, so the interaction is unit-tested without synthesising
`juce::MouseEvent`s (`EQCurveInteraction.*`).

**Undo.** `onGestureStart` / `onGestureEnd` bracket every parameter-changing gesture;
`ModuleComponent::wireEqGestureCallbacks` binds them to `AppUndoManager::captureBeforeState` /
`pushSnapshotFromCapture`, so one drag is one undo step. They are deliberately *not* opened on
`mouseDown` — a click that never becomes a drag would otherwise push an empty undo entry.
`dragBandTo` itself does no bracketing, since it is called repeatedly during a drag. Both are wired
through a `Component::SafePointer`, so a pop-out window that outlives its card becomes a no-op
rather than a dangling call.

- **dB window**: symmetric +/-30 dB, so 0 dB sits at the exact vertical centre and boosts read as
  the mirror of cuts. Gridlines at +/-12 and +/-24; the 0 dB line is drawn brighter as the reference
  the curve is read against. `gainAtY` clamps to the parameter's +/-24 dB, since the view is
  deliberately wider than the range.
- **Curve source**: `ParametricEQModule::responseDb()` — the analytic prototypes the module's biquad
  coefficients are derived from, so the drawing and the DSP cannot drift apart, locked by
  `ParametricEQAudio.MeasuredResponseTracksTheAnalyticCurve`. Sampled at 512 log-spaced points; the
  gradient fill hangs off the 0 dB line rather than the bottom edge, so a cut fills downward and a
  boost upward.
- **Band handles**: one numbered ring per *enabled* band at (centre freq, band gain) — the parameter
  pair, so a vertical drag maps 1:1 to gain. A shelf's handle sits at its corner frequency at the
  full shelf gain, which is above the curve there, because a shelf reaches its full gain only well
  past the corner.
- **Spectrum overlay**: a 1024-point FFT of the module's `VisualBuffer`, drawn *behind* the curve
  over its own -80 to 0 dB window, smoothed with an exponential moving average. **On by default**
  here, unlike the Filter card, because the curve is meant to be read against it.
- **Repaint discipline**: a 30 Hz timer that repaints when a band setting or the output trim
  changed, when hover or selection changed, or while the spectrum has actual signal. The analyser
  gates on peak below 1e-5 and repaints one final frame on the transition to silence, so a
  default-on spectrum still settles to **zero repaints on an idle patch**. That gate is what keeps
  this compliant with [rendering](rendering.md). Never make it unconditional.

## EQWindow

`Source/UI/ModuleViews/EQWindow.h`. The pop-out editor for a Parametric EQ, opened from the card's
"Open EQ Window" button. It is a content-only `juce::Component`, the same pattern as
`SettingsWindow`; the caller wraps it in a `juce::DialogWindow` via `LaunchOptions::launchAsync()`.
It hosts a second `EQCurveComponent` over the same module at 720x420 (resizable), plus a spectrum
toggle and a gesture hint.

`ModuleComponent` holds the dialog as a `Component::SafePointer` and **deletes it in
`detachFromProcessor()`** — the window references the module, so leaving it open across a graph
rebuild would dangle. Re-clicking the button brings the existing window to front rather than opening
a second one.

## ScopeComponent

`Source/UI/ModuleViews/ScopeComponent.h`. An oscilloscope waveform display used by every module that
has a `VisualBuffer`.

It draws horizontal amplitude grid lines at +/-0.5 and +/-1.0 in `theme.colors.border` at 0.6 alpha,
a slightly brighter centre (0.0) line (`border.brighter(0.3f)`) serving as the waveform baseline,
and a centred "No Signal" empty-state text in `textDisabled` when the buffer peak is at or below
0.02. That empty state **replaces** the waveform path draw entirely — the `return` is early, so no
waveform is drawn on silence.

**Public static helpers** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `isNoSignal` | `static bool isNoSignal(float peak) noexcept` | `true` when `peak <= 0.02f` |
| `amplitudeToY` | `static float amplitudeToY(float amp, juce::Rectangle<float> bounds) noexcept` | Maps amplitude [-1,1] to a y pixel; +1 is top, -1 is bottom, 0 is centre; uses 45% height per side |

**Themed colours** resolve via `dynamic_cast<AppLookAndFeel*>`: `border` for the grid,
`textDisabled` for the no-signal label, `accent` for the waveform. When the cast fails (headless),
hardcoded fallbacks are used (`0xff2A2F38`, `0xff5C6470`, limegreen).

## ThresholdControlComponent

`Source/UI/ModuleViews/ThresholdControlComponent.h`. A live threshold readout used by Sample & Hold
(meter-only, bipolar), ADSR (slider plus unipolar meter) and Comparator (slider plus bipolar meter).
`TriggerMeterComponent` is a compatibility alias for the same type. A Decibels scale is available as
a third `ThresholdScale`; no module currently selects it.

Two presentations:

- **Meter-only**: a thin 18 px bar. The module keeps its own rotary for Threshold.
- **Slider plus meter**: a caption, a level bar, and a linear slider whose thumb is the threshold
  slice. The bar fills to the current input, so the slice can be set by eye.

The bar switches to the `gateWire` colour while the detector is armed. A marker sits at the
**effective** threshold, knob plus Threshold CV. It reads `ThresholdMeterSource`:
`getMeterLevel()`, `getEffectiveThreshold()`, `isOverThreshold()`, `getTriggerCount()`.

**Why it is a separate component, not something `ModuleComponent::paint` draws.** The card is
buffered to an image, so painting a live meter inside its `paint()` would invalidate that cached
image and re-run the module's text layout on every meter tick — exactly the repaint storm
[rendering](rendering.md) prohibits. Instead this owns a 20 Hz timer and repaints *itself* only when
a displayed value moves past a visible amount (see `needsRepaint`), leaving the parent's cached image
untouched.

**Public static helpers** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `valueToNormalized` | `static float valueToNormalized(float, ThresholdScale, float minDb)` | Maps native units to [0, 1] |
| `valueToX` | `static float valueToX(float, float x, float width, ThresholdScale, float minDb)` | Maps native units to an x offset; the bipolar default matches the original meter |
| `needsRepaint` | `static bool needsRepaint(...) noexcept` | Mirrors the `timerCallback` repaint gate |
| `getFlashFrames` | `static constexpr int getFlashFrames() noexcept` | Ticks the fired-flash stays lit |

## WavetableDisplayComponent

`Source/UI/ModuleViews/WavetableDisplayComponent.h`. The wavetable frame view on the `Wavetable`
module card. It draws the frame currently under the scan position as a solid trace, with
`kGhostFrames` (3) receding low-alpha traces sampled slightly further along the stack, so the scan
direction and the table's depth read as three-dimensional. It is captioned with the table name and
`frame/total`.

**The trace is drawn warped.** `getDisplayWaveformAt()` runs the same `readWarped()` the audio path
does. A Warp knob whose effect is invisible is a knob users do not trust.

**Repaints are gated.** The 15 Hz timer compares a change signature — quantised scan position, table
name, frame count, and `getWarpSignature()` (warp mode plus quantised amount plus interpolation
mode) — and calls `repaint()` only when it differs. This is required by [rendering](rendering.md):
the display is always visible inside a buffered module card, so an unconditional per-tick repaint
would invalidate that cache 15 times a second.

**Public static helper** (headless-testable):

| Helper | Signature | Notes |
|---|---|---|
| `quantisePosition` | `static int quantisePosition(float position, int steps = 200)` | Clamps to [0,1] and quantises the scan position into `steps` buckets — the repaint gate's change signature |

**Themed colours** resolve via `dynamic_cast<AppLookAndFeel*>`: `bg1` for the panel, `border` for
the frame and zero line, `accent` for the traces, `textMuted` for the caption, falling back to
hardcoded colours when the cast fails.

The card chrome this display sits in — the two-column jack gutter, the tabbed control body and the
chrome band beside the ports — is [module-card](module-card.md#wavetable-card).

## CurveEditorComponent

`Source/UI/ModuleViews/CurveEditor/`. A generic breakpoint curve editor: a curve drawn through
nodes, each segment carrying a bend value, with draggable nodes and bend handles. Split by concern —
`CurveModel.h/.cpp` (pure data and edits, no `juce::Component`), `CurveEditorGeometry.h/.cpp` (pure
pixel mapping, testable without a component), `CurveEditorComponent.h` plus
`CurveEditorComponent.cpp` (interaction and mouse), and `CurveEditorPaint.cpp` (paint).

It is wired into the ADSR envelope card (`ModuleComponentEnvelopeCard.cpp` — see
[modules.md](../modules.md)'s ADSR "Card UI" entry for the two-way sync, undo and playhead details)
using `CurveMode::Fixed`, the fixed origin / attack-peak / hold-end / sustain / release-end topology
this component was built for. `CurveMode::Free` (add, remove and reorder points) is the second
supported topology.

**Bend is `EnvelopeGenerator::shape`.** A segment's value at `progress` is
`start + (end - start) * shape(progress, bend)`, using `synth::EnvelopeGenerator::shape` by default;
the model accepts an alternative shape function. This is deliberate: the curve the editor draws is
exactly what the envelope's DSP plays, never a separately re-derived approximation.

Five display and interaction rules:

- **A zero-duration segment always occupies exactly `kZeroSegmentPx` (12 px) on screen.** The model
  value stays 0 — this is a display convention only. The remaining width is shared among the
  real-duration segments in proportion to `duration / visibleRange`, where
  `visibleRange = max(totalDuration * 1.1, minVisibleRange)` (or a caller-set explicit override)
  leaves headroom to the right of the last node to drag it further out. This is what keeps a
  fixed-topology envelope's hold-end node separately grabbable from its attack-peak even when hold
  is 0 ms — the two are never pixel-coincident.
- **Hit-testing picks the nearest node within `kHitRadiusPx` (10 px); on an exact pixel tie the
  EARLIER node index wins.** Nodes take priority over bend handles, checked first and
  unconditionally, so a handle is never returned while any node is in radius. A node with neither
  axis movable — a Fixed-topology origin, for example — is never hit.
- **No bend handle on a zero-duration segment, a non-bendable segment, or one whose start and end
  levels are equal.** None of the three can show a visible bend, so `bendHandlePosition` returns
  `nullopt` — not drawn, not hittable — rather than a handle that does nothing when dragged. The
  handle that IS shown sits at the point ON THE CURVE at progress 0.5, so it visibly tracks the
  curve as bend changes rather than sitting at a fixed geometric midpoint.
- **A node drag freezes the visible range for its duration**, captured at `mouseDown` on a Node hit
  and cleared at `mouseUp`. Without this, `currentGeometry()` would recompute the range off the
  model's just-edited total duration on every `mouseDrag`, rescaling the pixel-to-time mapping under
  the cursor mid-gesture — a runaway feedback loop when dragging the last node near the view's drag
  headroom. A bend-handle drag never freezes anything, since bend never moves x.
- **`setModel()` preserves a live drag, hover or selection when the new model's topology matches**
  (same node count and `CurveMode`). Only an actual topology change, or an active index now out of
  range, resets that state, pairing a still-open gesture's `onGestureEnd` right there. This is what
  lets a host push a fresh model back mid-drag — the envelope card's own parameter round trip, for
  example — without cancelling the user's gesture after its first event.

**No `juce::Timer` of its own.** It repaints only when the model, hover/selection, or playhead
actually change. `setPlayhead(std::optional<Playhead>)`, where
`Playhead { int segment; float progress; }`, is a plain setter drawing a marker dot on the curve
plus a faint vertical line; it no-ops on an unchanged value, and `nullopt` hides it. The envelope
card drives this setter from `ModuleComponent`'s own EXISTING gated 15 Hz `timerCallback` rather
than giving this component a second, independent timer — the established per-module gated-tick
pattern the `FrequencyResponseComponent` and `ScopeComponent` timers above follow, rather than a
third exception to the [time-bounded animation
rule](animation.md#the-time-bounded-animation-rule).

**Mouse handlers are thin wrappers** over public primitives (`dragNodeTo`, `dragBendBy`,
`addPointAt`, `removeNode`), the same pattern as `EQCurveComponent`: `mouseDown` hit-tests and
records the target with no gesture opened yet, since a plain click that never becomes a drag would
otherwise push an empty undo step; `mouseDrag` opens the gesture on its first call; `mouseUp` closes
it. Double-click adds (Free mode, empty space), removes (Free mode, a node), or resets a bend handle
to 0 (either mode).
