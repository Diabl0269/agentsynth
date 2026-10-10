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
    - **Sync mode**: Tempo-locked to host BPM (falls back to 120 BPM if no PlayHead). The LFO has its own rate list (`synth::lfoRateDivisions()`, `Source/Modules/LfoRateDivisions.h`), shortest first: 1/128, 1/64, 1/32, 1/16, 1/8, 1/4 (default), 1/2, 1/1, then 2/1, 4/1, 8/1 (cycles of 2, 4 and 8 bars of 4/4). ADSR and Delay keep the shorter list ending at 1/1. **Transport-locked phase**: with Sync on, Retrig off and the transport playing, the phase at the start of each block is the song position inside one cycle (`frac(ppq / cycleBeats)`), so a 1-bar LFO starts its cycle on every bar line and a 4/1 LFO on every fourth; the Phase offset still adds on top. Stopped transport, Free (Hz) mode or Retrig on keep the free-running phase. The rate is the tempo division, not the `rateHz` knob, so Rate CV is deliberately ignored in this mode.
- **Custom-wave grid**: the editor's grid combo offers Off, 1/4, 1/8 (default), 1/16, 1/32, 1/64 and 1/128.
- **Saved state**: projects store `rateSync` by choice name, so the longer rates cannot change what an old project means. Plugin-host state (`ModuleBase::getStateInformation`) now also stores each choice parameter's name (`<id>__name`) and prefers it on load; a legacy blob without it is decoded with `ModuleBase::legacyChoiceCount(paramId)`, which LFO overrides to 8 for `rateSync`.
- **Parameters**: Shape (choice), Sync (bool mode toggle), Rate Hz (float), Sync Rate (choice), Bipolar (bool, default true), Retrig (bool), Level (0.0–1.0, default 1.0), Glide (0.0–1.0, S&H only), Phase (0–360 degrees, default 0), Fade In (0–10000 ms, default 0).
- **Bipolar/Unipolar**: When `Bipolar` is true, output range is −1 to +1. When false (unipolar), mapped to 0 to +1.
- **S&H Glide**: On each phase wrap a new random value is drawn; `Glide > 0` ramps to it over up to 0.5 s using `juce::LinearSmoothedValue`.
- **Retrig**: A MIDI Note On resets the phase to 0.0 when `Retrig` is enabled — unaffected by the CV jacks below. A restart is the LFO's only notion of a note: it is also what starts Fade In.
- **Phase** (`phase`, 0-360 degrees, default 0): the wave is read at the free-running phase plus the offset, so one path covers both modes — with Retrig on a note-on restarts the cycle AT the offset (90 degrees starts a sine at its peak, 180 degrees starts a square inverted), with Retrig off it shifts the free-running wave by the same amount. An offset of 0 reads the bare phase, bit for bit as before. S&H does not read the wave and ignores it. The offset is smoothed over 10 ms while the knob moves but lands exactly on the value at a restart and on the first block after prepare; the card's playhead (`getPhaseForUI`) reports the offset phase.
- **Fade In** (`fadeIn`, 0-10000 ms, default 0 = off): after a restart the output (after Level, and after the unipolar conversion, so a unipolar wave fades up from 0) ramps linearly from 0 to full over the time. Each new restart starts the ramp again. With Retrig off the LFO is not note-aware, so Fade In never runs; there is no ramp at start-up or on a fresh patch either.
- **CV inputs** (FRO284): Rate ch0, Level ch1, Glide ch2, Phase ch3, Fade In ch4 — all `PortRole::ModCV`, mono. Phase and Fade In (appended after Glide, so saved routings keep their channels) follow the same convention: Phase CV moves the offset over its 0-360 range, Fade In CV sets the ramp time at the restart; the module declares 5 inputs and 5 outputs so ch1-4 never alias ch0. Each follows the normalised-CV convention (`ModuleBase::blockCV` + `modulateNormalised`, read once per block; see [`modulation.md`](modulation.md#cv-in-normalised-units)): Rate moves `rateHz` in its own skewed range (Hz mode only, see above); Level moves the `level` knob before the 10 ms smoother; Glide moves the `glide` knob at the same S&H step-edge read the knob itself uses. ch0 doubles as the CV output on the way out (read before overwrite, the same shared-channel convention as Oscillator/Wavetable ch0 and Sample & Hold ch0); ch1/ch2 are cleared each block so the raw CV doesn't leak downstream as output. `getModulationTargets()` sets `paramId` on all three (`rateHz`/`level`/`glide`).
- **Output**: Single CV channel (ch0), the module's only visible output jack. Declares 5 raw output channels (docs/modules/poly-channel-layout.md — ch1-ch4 exist only to keep JUCE from aliasing them onto ch0's buffer) but ch1-ch4 carry no signal. Pushes to `VisualBuffer` for scope display.
- **Smoothing**: Level is smoothed over 10 ms per sample — it scales the emitted CV, so a step there steps every destination downstream. Rate is a frequency (phase-continuous) and Glide is read only at an S&H step edge, so neither is smoothed.
- **Width**: SINGLE (280 px). See [docs/layout/module-card.md](../layout/module-card.md#width-buckets).
