# LFO Module

Low-frequency oscillator / CV source (`Source/Modules/LFOModule.h`), type-name string `"LFO"`. Its
raw channel map is in [`poly-channel-layout.md`](poly-channel-layout.md#channel-table); the rest of
the module reference is in [`modules.md`](modules.md).

- **Source file**: `Source/Modules/LFOModule.h`
- **Waveforms**: 6 shapes — Sine, Triangle, Sawtooth, Square, S&H (Sample-and-Hold), Custom
  (FRO114, `LFOModule::kCustomShapeIndex == 5`).
- **Custom waveform** (FRO114): a small breakpoint list (`synth::LfoCustomWave`, `Source/Modules/
  Lfo/LfoCustomWave.h`, headless/Core) — up to 64 `(x, y, bend)` points, `x`/`y` in `[0, 1]`,
  `bend` the outgoing segment's shape amount evaluated with the same `synth::EnvelopeGenerator::
  shape` function the envelope card's bend handles use. `evaluate(phase)` is right-continuous at
  a zero-length (step) segment — the segment STARTING at the step wins, not the one ending there.
  `renderTable` bakes it into a 1024-entry lookup table (`renderTable`'s `[1024]` duplicates
  `[0]`, a wrap guard so linear interpolation across the phase = 1 seam needs no modulo);
  `processBlock`'s case 5 interpolates linearly between adjacent table entries. The message
  thread renders a fresh table on every `setCustomWave` and crosses it to the audio thread with a
  fixed-size array under a `juce::SpinLock` try-lock (`publishCustomTable`/
  `adoptPendingCustomTable`, mirroring `WavetableOscillatorModule`'s own publish/adopt — see
  `Source/Modules/CLAUDE.md`), adopted once per block before the sample loop. Phase and retrig are
  unchanged from every other shape (a shared `phase` in `[0, 1)`, reset to 0 by Retrig). The
  default (and what a freshly-appended Custom shape starts as) is the Triangle preset. Unipolar
  output equals the curve's own `y`; bipolar maps it to `-1..1` like every other shape.
- **Extra state** (FRO114): `LFOModule::getExtraState()`/`setExtraState()` carry the custom wave
  as `{"version":1,"points":[{"x":..,"y":..,"bend":..}, ...]}` — emitted only when the wave is
  non-default, and regardless of the CURRENT shape (a sculpted wave survives switching to another
  shape and back). `setExtraState` always resolves via `LfoCustomWave::fromVar`'s sanitise rules
  (a non-object/bad-version/malformed input falls back to the Triangle default) and, like every
  module's extra state, is applied on the **trusted path only** (`docs/ai/patch-safety.md`) — the
  AI may author `"shape":"Custom"` (`AIStateMapper`'s schema is generated from the live choice
  list) but a model-authored node always gets the default Triangle wave, never a caller-supplied
  points array.
- **Card UI** (FRO114, `Source/UI/Graph/ModuleComponent/ModuleComponentLfoCard.cpp`): the
  Custom-wave section (a Grid/Shapes/Tools toolbar + a `CurveEditorComponent` in
  `CurveMode::Free`) shows only while shape == Custom, driven by `parameterValueChanged("shape"/
  "bipolar")` and synced once at construction. Grid: Off/1/4/1/8 (default)/1/16/1/32, with snap-
  to-grid on for every setting but Off; holding Shift bypasses snap for that one drag/add. Shapes
  replaces the whole wave with a named preset (Triangle/Ramp Up/Ramp Down/Square/Pulse 1/4/
  Steps (4)/Soft Sine); Tools apply Invert/Reverse/Straighten/Clear/Reset to Default to it — each
  one whole undo step. Every drag, add, remove, bend-reset, preset and tool funnels through
  `AppUndoManager::recordNodeExtraStateChange` at gesture end: `before` is the module's
  `getExtraState()` captured at gesture start, `after` is read only once the card's own write has
  landed. Reverse sync (undo/redo, a preset load, automation) polls `LFOModule::
  getCustomWaveGeneration()` against the card's own last-seen value on the existing gated 15 Hz
  timer and rebuilds the graph from the module when they differ — never while a gesture the card
  itself started is still open. The playhead is `LFOModule::getPhaseForUI()` (a lock-free
  `std::atomic<float>`, written once per block, the same pattern as `ADSRModule::
  playheadProgress`) mapped through `CurveModel::playheadForX`, polled from the same 15 Hz timer
  only while the section is visible, and pixel-quantised so `CurveEditorComponent::setPlayhead`'s
  unchanged-value early return skips redundant repaints.
- **Rate modes**:
    - **Hz mode**: Free-running, 0.01–20.0 Hz (default 1.0 Hz, skewed range). Rate CV (ch0) applies here.
    - **Sync mode**: Tempo-locked to host BPM (falls back to 120 BPM if no PlayHead). Subdivisions: 1/1, 1/2, 1/4 (default), 1/8, 1/16, 1/32. The rate is the tempo division, not the `rateHz` knob, so Rate CV is deliberately ignored in this mode.
- **Parameters**: Shape (choice), Sync (bool mode toggle), Rate Hz (float), Sync Rate (choice), Bipolar (bool, default true), Retrig (bool), Level (0.0–1.0, default 1.0), Glide (0.0–1.0, S&H only).
- **Bipolar/Unipolar**: When `Bipolar` is true, output range is −1 to +1. When false (unipolar), mapped to 0 to +1.
- **S&H Glide**: On each phase wrap a new random value is drawn; `Glide > 0` ramps to it over up to 0.5 s using `juce::LinearSmoothedValue`.
- **Retrig**: A MIDI Note On resets the phase to 0.0 when `Retrig` is enabled — unaffected by the CV jacks below.
- **CV inputs** (FRO284): Rate ch0, Level ch1, Glide ch2 — all `PortRole::ModCV`, mono. Each follows the normalised-CV convention (`ModuleBase::blockCV` + `modulateNormalised`, read once per block; see [`modulation.md`](modulation.md#cv-in-normalised-units)): Rate moves `rateHz` in its own skewed range (Hz mode only, see above); Level moves the `level` knob before the 10 ms smoother; Glide moves the `glide` knob at the same S&H step-edge read the knob itself uses. ch0 doubles as the CV output on the way out (read before overwrite, the same shared-channel convention as Oscillator/Wavetable ch0 and Sample & Hold ch0); ch1/ch2 are cleared each block so the raw CV doesn't leak downstream as output. `getModulationTargets()` sets `paramId` on all three (`rateHz`/`level`/`glide`).
- **Output**: Single CV channel (ch0), the module's only visible output jack. Declares 3 raw output channels (docs/modules/poly-channel-layout.md — ch1/ch2 exist only to keep JUCE from aliasing them onto ch0's buffer) but ch1/ch2 carry no signal. Pushes to `VisualBuffer` for scope display.
- **Smoothing**: Level is smoothed over 10 ms per sample — it scales the emitted CV, so a step there steps every destination downstream. Rate is a frequency (phase-continuous) and Glide is read only at an S&H step edge, so neither is smoothed.
- **Width**: SINGLE (280 px). See [docs/layout/module-card.md](../layout/module-card.md#width-buckets).
