# FX Modules Reference

Technical documentation for the Agent Synth effects suite.

## Stereo I/O (Dual I/O toggle)

Every FX module (and Voice Mixer outputs) processes a fixed **raw** stereo pair on channels 0/1.
That is always stereo DSP — Dual I/O is **not** a mono/stereo switch, and turning it off does not
sum to mono or drop a channel. JUCE freezes the bus layout at construction, so the UI cannot grow
or shrink the channel count — it only changes how many jacks you see:

| Dual I/O | Visible audio jacks | Wiring |
|---|---|---|
| **Off (default)** | One `"Audio"` in and out | Dragging onto the jack fans both raw L/R legs (`polyVoiceSpan == 2`). A mono source is duplicated onto both legs. |
| **On** | Separate `"Left"` / `"Right"` | Independent cables per leg — for mid/side tricks, dual-mono patches, or feeding only one side. |

CV jacks (Drive, Rate, …) keep their **raw** channel indices so presets and the AI schema stay stable;
in single-jack mode they simply shift one slot down in the visible column.

Toggling Dual I/O **does not rewire existing cables away**. Raw ch0/ch1 stay connected; only jack
visibility changes. If only the left output was patched (common when the next node is Audio Output),
toggling Dual I/O on completes the pair (`L→L` / `R→R`) so the Right jack is not left hanging.
Splitting only one end of a cable still draws the Right jack: the far end's collapsed `"Audio"` jack
owns both raw legs, so the extra wire lands on that same jack rather than waiting for both modules
to split. The control is a header icon (`Icon::ModuleDualIO`, a Y-fork into two jacks) with an
on/off tooltip — not a labelled checkbox.

Wavetable and Sampler used to keep permanent `Audio L` / `Audio R` jacks. Since issue #219 they are
on this toggle too, along with Oscillator, Filter and VCA — see below for what "off" means on a
module whose right leg is not the contiguous ch0/ch1 pair.

### Split-block modules share the toggle, not the channel layout

Every stereo-capable module has the Dual I/O toggle — the FX, Voice Mixer's output, and (since
issue #219) Oscillator, Wavetable, Filter, VCA and Sampler. What differs is where the right leg
lives, and that changes what "off" means:

| Layout | Modules | Dual I/O off |
|---|---|---|
| **Contiguous pair** (raw ch0/ch1) | FX, Voice Mixer out, Sampler | One `"Audio"` jack **owning both raw legs** — a mono source fans onto L and R. |
| **Split block** (`kRightBase`) | Oscillator, Wavetable, Filter, VCA | One `"Audio"` jack carrying the **left leg only**; the right block is hidden and unpatchable. |

The split-block modules cannot collapse the FX way: a collapsed jack can only fan to *adjacent* raw
channels, and their right leg deliberately sits above the mod-CV inputs because ch1 is Waveform /
Position / Cutoff / gain CV. So on those modules the toggle picks between "one jack, left leg" and
"two jacks". Collapsed, their jack layout is exactly what it was before #219 — a Filter shows
`Audio`, `Cutoff`, `Resonance`, `Drive` again.

Two consequences worth knowing:

- Collapsing a split-block module **drops the cables on its right leg** (`GraphEditor::completeStereoPairConnections`), because an invisible jack cannot be unplugged. Collapsing an FX module drops nothing — its collapsed jack still owns both legs.
- Merge-mode auto-connect (and smart-connection drops onto a mono destination) wires `Audio L` only, matching what Wavetable has always done.

Anything pairing two modules' legs must ask `ModuleBase::rightAudioLegChannel()` rather than
assuming ch1 — on a voice module ch1 is CV, and wiring it as audio corrupts the patch.

**Preferences → "Split Left/Right jacks"** applies in both directions to modules already on the
canvas *and* to anything created afterwards, overriding each module's own default. Scoping it to new
modules only made it look broken — the obvious way to check a setting is to flip it and watch the
patch in front of you, which never changed.

Only a deliberate change retro-applies. `PreferencesSettingsTab::setGraphEditor` (called every time
the Settings window opens) and the startup restore in `MainComponent` push the value **without**
re-laying the patch — otherwise opening Settings would collapse jacks the user had split by hand,
and every launch would collapse the factory preset's voice modules.

Known rough edge: collapsing frees a jack row, so a card gets ~20px shorter (and taller again when
split). Making the toggle height-neutral means reserving the dual-layout gutter in both states,
which would add a row of blank gutter to all twelve FX — they default to collapsed — and force
another rebake of the factory preset rows. `ModuleBase::getReservedInputPortCount` /
`getReservedOutputPortCount` compute the reserved counts if a follow-up wants to make that trade.

See [`modules.md § Oscillator`](modules.md#oscillator-module) and
[`§ Filter`](modules.md#filter-module) for the channel maps.

## Output Level (shared stage)

Modules whose output is **audio** carry a `Level` parameter (`outputLevel`, linear 0.0–1.0, default 1.0) so their output can be scaled without patching a VCA into the chain. Provided by `ModuleBase` as three opt-in pieces:

| Piece | Where to call it |
|---|---|
| `addOutputLevelParameter()` | in the ctor, **after** all of the module's own `addParameter()` calls |
| `prepareOutputLevel(sampleRate)` | in `prepareToPlay` — sets up the 10 ms anti-click ramp |
| `applyOutputLevel(buffer, numAudioChannels)` | at the **end of the normal `processBlock` path** |

Adopting modules: **Distortion, Delay, Reverb, Chorus, Phaser, Flanger, Filter, Bitcrusher, Pitch Shifter, Ring Modulator**.

**This is a standing rule, not a one-off.** Every *new* module whose output carries audio must have a level control — the shared stage here, or its own `level`/`gain` parameter. `Tests/ModuleAdoptionTests.cpp` enforces it: it classifies every module the factory can build into one of three buckets (shared stage / own parameter / no level by design, with a rationale), and a new module that nobody classified fails `EveryFactoryModuleIsClassified`. A renamed or deleted module fails `ClassificationTableHasNoStaleEntries`. So the decision cannot be skipped — only made explicitly.

Rules that make this safe:

- **Opt-in, never in the `ModuleBase` ctor.** "Gain" is wrong on pitch/gate CV outputs — scaling a V/oct pitch CV transposes it, and scaling a gate drops it under the `> 0.5f` trigger threshold that ADSR uses. Sequencer, ADSR, LFO, Poly MIDI, MIDI Keyboard and External MIDI therefore do **not** adopt it. Attenuverter does not either — it already *is* a gain/polarity stage.
- **Added last in the parameter list among value params.** Parameter position is load-bearing for positional `getParameters()[n]` call sites; appending keeps existing indices pointing at the same parameter. `addDualIOParameter()` (layout toggle) is added *before* Level so Level stays the last continuous control. Pinned by `OutputLevelTests.LevelParameterIsAddedLast` and `…AttenuverterKeepsAmountAtParameterIndexOne`.
- **Modules that already have a level/gain parameter do not get a second one** — Oscillator, LFO, Noise and Voice Mixer have `level`; VCA has `gain`; Limiter has `inputGain`; Compressor has `makeupGain`.
- **`numAudioChannels` excludes CV.** Only the leading audio channels are scaled; CV input channels are left for the module's own clearing logic. Filter passes `8` in poly mode and `1` in mono.
- **Bypass/mute contract is unchanged.** `applyOutputLevel` is never reached on the bypass branch (dry pass-through stays untouched, so Level cannot silence a bypassed module) nor on mute (already cleared). See [`architecture.md`](architecture.md).
- **It is an output stage, not an insert.** Delay applies it after the feedback write, so lowering Level does not starve the repeats; Reverb applies it after the wet/dry mix, so Wet/Dry still sets balance and Level sets absolute loudness.

CV control of Level is deliberately **not** implemented — it would need a new input channel on every module, and CV channel indices are positional and hard-coded across `getModulationTargets`, `mapInputChannel`, the AI patch schema and the tests. Mono CV connections already route through an Attenuverter, which provides per-connection gain.

## Distortion Module
- **Algorithm**: Three waveshaper types selectable via the Type parameter:
  - **Soft** (type 0): Rational waveshaper `f(x) = x * (1 + k) / (1 + k * |x|)` where `k = drive - 1`. Gain-neutral at `k=0`; increasingly compressed at higher drive values. This is NOT a tanh curve.
  - **Hard** (type 1): Hard clipper — scales input by drive then clips to a threshold that tightens as drive increases.
  - **Foldback** (type 2): Folds the signal back at a fixed threshold of ±0.8 after scaling by drive.
- **Oversampling**: Configurable oversampling mode (Off, 2x, 4x) using FIR equiripple half-band filters (`filterHalfBandFIREquiripple`) to reduce aliasing. A latency-compensation delay line keeps the dry signal aligned for wet/dry mixing. Default is 2x (backward-compatible).
- **Makeup Gain**: Automatic dynamic makeup gain (computed per-block from RMS ratio of dry vs. wet, clamped 0.01–1.0 linear, 50 ms smoothing). Not a user-visible parameter.
- **Parameters**: Drive (1–20), Mix (0–1), Type (Soft/Hard/Foldback), Oversampling (Off/2x/4x), Level (0–1). Level is applied after the wet/dry mix and before the scope push, so the visualiser shows the real output.
- **CV Inputs**: Drive (ch2), Mix (ch3).

## Ring Modulator Module
- **Algorithm**: Parker diode-ring (DAFx-11). Four parallel piecewise-quadratic diode approximations
  `out = d(m + c/2) + d(-m + c/2) - d(m - c/2) - d(-m - c/2)`. This is **not** a clean multiply — Math's `Mult` output already covers that. The diode dead-zone is what gives the metallic, gated, bell-like character.
- **Oversampling**: Same real-time-safe scheme as Distortion. `Off` / `2x` / `4x` (default `2x`); both oversamplers are pre-allocated in `prepareToPlay` and swapped via an `AudioProcessorParameter::Listener`. A latency-compensation delay line keeps the dry carrier aligned for wet/dry mixing. Oversampling is excluded from `getModulationTargets()`.
- **I/O**: Carrier (ch0), Modulator (ch1), Mix CV (ch2), Drive CV (ch3), Character CV (ch4). Stereo out is the mono ring-mod result duplicated to Left/Right. Dry is the unprocessed carrier. No internal carrier oscillator — patch an Oscillator into Carrier.
- **Parameters**: Drive (0.5–8, default 1), Mix (0–1, default 1), Character (0–1, default 0.5), Oversampling (Off/2x/4x), Level (0–1). Character maps the diode forward-bias / linear-region breakpoints (`vb` / `vl`) from near-clean multiply (`vb≈0.02`, `vl≈0.05`) to hard gated (`vb≈0.5`, `vl≈1.0`). Parker's typical `vb≈0.2` / `vl≈0.4` sits at the default.
- **CV Inputs**: Mix (ch2), Drive (ch3), Character (ch4). Every continuous parameter has a CV jack; Oversampling does not.

## Delay Module
- **Type**: Stereo feedback delay.
- **Technique**: Fractional delay line with linear interpolation for smooth "time" parameter changes.
- **Parameters**: Time (ms), Feedback, Mix, Level (0–1). Level sits outside the feedback path — the delay line stores the pre-level signal.
- **Smoothing**: Glide-on-time prevents pitch glitches during modulation.

## Reverb Module
- **Type**: Algorithmic stereo reverb.
- **Implementation**: Uses standard Schroeder/Freeverb-inspired techniques for lush acoustic simulation.
- **Parameters**: Room Size, Damping, Wet, Dry, Width, Level (0–1). Wet/Dry set the balance; Level scales the summed result.

## Chorus Module
- **Implementation**: `juce::dsp::Chorus<float>`.
- **CV Modulation**: Rate (ch2) and Depth (ch3). CV is sampled per block (RMS-gated) and added to the smoothed parameter value.
- **Parameters**: Rate (0.1–10 Hz), Depth (0–1), Centre Delay (1–30 ms), Feedback (-1–1), Mix (0–1), Level (0–1).

## Phaser Module
- **Implementation**: `juce::dsp::Phaser<float>`.
- **CV Modulation**: Rate (ch2) and Depth (ch3). CV is sampled per block (RMS-gated) and added to the smoothed parameter value.
- **Parameters**: Rate (0.1–20 Hz), Depth (0–1), Centre Freq (200–10 000 Hz), Feedback (-1–1), Mix (0–1), Level (0–1).

## Compressor Module
- **Implementation**: `juce::dsp::Compressor<float>`.
- **Makeup Gain**: Manual, user-controlled. Range: -20 to +40 dB, default 0 dB. Applied per-sample with 5 ms smoothing after the compressor. There is no automatic gain compensation.
- **CV Modulation**: None — no CV input channels.
- **Parameters**: Threshold (-60–0 dB), Ratio (1–20), Attack (0.1–200 ms), Release (10–1000 ms), Makeup Gain (-20–+40 dB).

## Flanger Module
- **Implementation**: `juce::dsp::Chorus<float>` configured for flanger character by constraining the centre-delay range to 1–5 ms (versus Chorus's 1–30 ms). Default centre delay is 2 ms.
- **CV Modulation**: Rate (ch2) and Depth (ch3). CV is sampled per block (RMS-gated) and added to the smoothed parameter value.
- **Parameters**: Rate (0.05–5 Hz), Depth (0–1), Centre Delay (1–5 ms), Feedback (-1–1), Mix (0–1), Level (0–1).

## Limiter Module
- **Implementation**: Brickwall `juce::dsp::Limiter<float>`.
- **Input Gain**: Pre-limiter drive parameter. Range: -20 to +20 dB, default 0 dB. Applied per-sample with 5 ms smoothing before the limiter stage.
- **CV Modulation**: None — no CV input channels.
- **Parameters**: Threshold (-20–0 dB, default -1 dB), Release (1–500 ms), Input Gain (-20–+20 dB).

## Bitcrusher Module
- **Implementation**: Downsampling and bit-depth quantization effect with dither.
- **Quantization**: Rounds input signal to `2^depth` discrete levels with `std::round` (clamped to ±1.0) to eliminate DC quantization bias.
- **Sample Rate Reduction**: Holds sample values for `rate` sample clocks (scaled relative to 44.1 kHz for device independence).
- **CV Modulation**: Rate (ch2), Depth (ch3), Mix (ch4). CV presence is detected via non-zero sample checking.
- **Parameters**: Rate (1–50), Depth (1–24 bits), Mix (0–1), Dither (0–1).

## Parametric EQ Module

Modelled on a traditional DAW channel EQ (Cubase's, specifically): four fixed band slots, **all disabled by default**, with points added and shaped directly on the response curve.

- **Bands**: Four slots with fixed types — `1` Low Shelf, `2` Peak (bell), `3` Peak (bell), `4` High Shelf. Every slot exposes Freq / Gain / Q uniformly, so the same gestures work on all of them; only the shape differs. Q defaults to 1/√2 (the RBJ "S = 1" case — maximally flat, no shelf overshoot) but is user-adjustable on shelves too.
- **Disabled by default**: a freshly dropped EQ is a straight wire and the curve starts empty. `processBlock` skips a disabled band's filter loop entirely, and its coefficients are written as a literal unity biquad, so "off" really is bypassed rather than "gain happens to be 0".
- **Implementation**: RBJ cookbook biquads via `juce::dsp::IIR::Filter<float>`, one filter per channel per band (2 × 4). Coefficients are computed in-house by `ParametricEQModule::writeBiquad()` rather than `IIR::Coefficients::makePeakFilter()` et al., because the module also needs the matching *analog prototype* magnitude for the visualiser — see below.
- **No audio-thread allocation**: the four `Coefficients` objects are allocated once in `prepareToPlay` and their raw values rewritten in place each block. The two channels share each band's `Coefficients` object; `juce::dsp::IIR` keeps filter state in the `Filter`, not the `Coefficients`, so sharing is safe (this is what `ProcessorDuplicator` does internally).
- **Coefficient update rate**: once per block, from 20 ms-smoothed CV values. The per-block step is small enough that swapping coefficients wholesale is inaudible while keeping the inner loop a plain biquad. Output gain is applied per sample so its smoothing is genuinely continuous.
- **Analytic response**: `bandMagnitudeDb()` / `responseDb()` are pure static functions evaluating the analog prototypes the digital coefficients are derived from (`H(s)` forms for peak / low shelf / high shelf); `responseDb` sums only the *enabled* bands. `EQCurveComponent` draws the curve with them, so the display and the DSP agree — `ParametricEQAudio.MeasuredResponseTracksTheAnalyticCurve` asserts they stay within 1 dB of each other at real audio frequencies.
- **Snapshot semantics**: `getBandSnapshots()` reads the *parameters*, not values cached during `processBlock`, so the curve tracks a dragged point even with no audio flowing. The two bell bands have their live CV-modulated frequency/gain overlaid, but only while CV is actually driving them.
- **Channels**: 6 in / 2 out — `0-1` stereo audio, `2` B2 Freq CV, `3` B2 Gain CV, `4` B3 Freq CV, `5` B3 Gain CV. Only the two bells take CV, following the rest of the FX suite (CV on the parameters worth modulating, not one jack per knob); the shelves are parameter-only.
- **CV mapping**: Freq CV is exponential over the full 20 Hz – 20 kHz range — `+1` sweeps to 20 kHz, `-1` to 20 Hz, matching FilterModule's cutoff-CV feel. Gain CV maps linearly onto the full ±24 dB range and adds to the knob value. Both are sampled once per block and RMS-gated, so an unpatched jack reads as exactly 0.
- **Parameters** (per band N = 1–4): `bandNOn` (bool, default off), `bandNFreq` (20–20 000 Hz; slot defaults 100 / 500 / 3000 / 8000), `bandNGain` (±24 dB, default 0), `bandNQ` (0.1–10, default 0.707). Plus `outputGain` (±24 dB). Frequency and Q ranges are skewed (`setSkewForCentre`) so the knobs feel logarithmic, and all three carry `stringFromValue` formatters because the raw skewed values read as `2999.9` / `0.7071` — see the UI note below for the exact strings.
- **Slot selection**: `findBandForNewPoint(freqHz)` returns the disabled slot whose home frequency is nearest on a log axis, so a click down low lands on the low shelf and one up top on the high shelf; `-1` once all four are in use.
- **UI**: double-width card (560 × 592) — response curve set between the port-label gutters, then a row of [Show Spectrum] [Open EQ Window] with the Output trim at its right, then one column per band (type-labelled on/off checkbox above Freq / Gain / Q). Knob geometry is deliberately identical to the generic auto-UI (70 × 60 slider, 20 px label, 50 px text box) so EQ knobs are the same size as every other module's; the columns are wider than a knob, so each is centred in its column. Value formatters are correspondingly compact ("3.2k", "-9.0", "0.71") to fit that shared 50 px box. See [`docs/layout.md` §9](layout.md) for `EQCurveComponent` (interactive curve) and `EQWindow` (pop-out editor).
## Pitch Shifter Module
Two engines behind one Mode switch, because they answer the same question with opposite characters: Pitch mode multiplies frequencies (harmonic), Frequency mode adds to them (inharmonic).

- **Pitch mode**: Two-tap crossfaded delay line. The read heads sweep across one window half a window apart; the phase advances at `(1 - ratio) / windowSamples` per sample, which is the condition for the read position to advance at `ratio`. Taps are summed with an equal-power window (`sin`/`|cos|` of the phase) so each tap's wrap-around lands exactly where its own gain is zero. Reads use cubic Hermite interpolation — linear interpolation audibly dulls a transposed signal.
  - **Unity dead zone**: when `|ratio - 1| < 1e-4` (about 0.0017 semitones) the two taps would sit at fixed, different delays and comb-filter the input, so the module emits the signal untransposed instead. This makes the default (Pitch 0, Mix 1) bit-transparent.
  - **Window**: trades artifacts against latency. Short windows chop the signal at a higher rate (audible as AM sidebands at `|1 - ratio| / window` Hz); long windows smear transients. 50 ms default.
- **Frequency mode**: Single-sideband modulation. A Hilbert transform pair (two cascaded 4-section 2nd-order allpass chains, `H(z) = (a² - z⁻²)/(1 - a²z⁻²)`, using Olli Niemitalo's wideband 90° coefficients) feeds a quadrature oscillator: `out = I·cos(ωt) + Q·sin(ωt)`. Measured rejection of the unwanted sideband is ~55 dB. A negative Shift runs the oscillator backwards — no separate code path. Harmonics stop being integer multiples of the fundamental, which is what produces the "alien voice" / metallic timbre.
- **Feedback**: routes the shifted output back into the input, so each pass is shifted again — cascading octaves in Pitch mode, barber-pole / Shepard-tone illusions in Frequency mode. Soft-clipped with `tanh` so the loop stays bounded at the 0.95 maximum.
- **CV Modulation**: Pitch (ch2, ±24 semitones), Shift (ch3, ±1000 Hz), Mix (ch4, ±1), Feedback (ch5, ±0.95). CV is added per-sample to the smoothed parameter value and clamped; Window and Fine have no CV input.
- **Parameters**: Mode (Pitch/Frequency), Pitch (-24–+24 semitones), Fine (-100–+100 cents), Shift (-1000–+1000 Hz), Window (10–100 ms), Feedback (0–0.95), Mix (0–1, default 1).
