# FX Modules Reference

Technical documentation for the Agent Synth effects suite.

## Output Level (shared stage)

Modules whose output is **audio** carry a `Level` parameter (`outputLevel`, linear 0.0–1.0, default 1.0) so their output can be scaled without patching a VCA into the chain. Provided by `ModuleBase` as three opt-in pieces:

| Piece | Where to call it |
|---|---|
| `addOutputLevelParameter()` | in the ctor, **after** all of the module's own `addParameter()` calls |
| `prepareOutputLevel(sampleRate)` | in `prepareToPlay` — sets up the 10 ms anti-click ramp |
| `applyOutputLevel(buffer, numAudioChannels)` | at the **end of the normal `processBlock` path** |

Adopting modules: **Distortion, Delay, Reverb, Chorus, Phaser, Flanger, Filter**.

Rules that make this safe:

- **Opt-in, never in the `ModuleBase` ctor.** "Gain" is wrong on pitch/gate CV outputs — scaling a V/oct pitch CV transposes it, and scaling a gate drops it under the `> 0.5f` trigger threshold that ADSR uses. Sequencer, ADSR, LFO, Poly MIDI, MIDI Keyboard and External MIDI therefore do **not** adopt it. Attenuverter does not either — it already *is* a gain/polarity stage.
- **Added last in the parameter list.** Parameter position is load-bearing for positional `getParameters()[n]` call sites; appending keeps existing indices pointing at the same parameter. Pinned by `OutputLevelTests.LevelParameterIsAddedLast` and `…AttenuverterKeepsAmountAtParameterIndexOne`.
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

