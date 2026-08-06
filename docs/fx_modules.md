# FX Modules Reference

Technical documentation for the Agent Synth effects suite.

## Distortion Module
- **Algorithm**: Three waveshaper types selectable via the Type parameter:
  - **Soft** (type 0): Rational waveshaper `f(x) = x * (1 + k) / (1 + k * |x|)` where `k = drive - 1`. Gain-neutral at `k=0`; increasingly compressed at higher drive values. This is NOT a tanh curve.
  - **Hard** (type 1): Hard clipper — scales input by drive then clips to a threshold that tightens as drive increases.
  - **Foldback** (type 2): Folds the signal back at a fixed threshold of ±0.8 after scaling by drive.
- **Oversampling**: Configurable oversampling mode (Off, 2x, 4x) using FIR equiripple half-band filters (`filterHalfBandFIREquiripple`) to reduce aliasing. A latency-compensation delay line keeps the dry signal aligned for wet/dry mixing. Default is 2x (backward-compatible).
- **Makeup Gain**: Automatic dynamic makeup gain (computed per-block from RMS ratio of dry vs. wet, clamped 0.01–1.0 linear, 50 ms smoothing). Not a user-visible parameter.
- **Parameters**: Drive (1–20), Mix (0–1), Type (Soft/Hard/Foldback), Oversampling (Off/2x/4x).
- **CV Inputs**: Drive (ch2), Mix (ch3).

## Delay Module
- **Type**: Stereo feedback delay.
- **Technique**: Fractional delay line with linear interpolation for smooth "time" parameter changes.
- **Parameters**: Time (ms), Feedback, Mix.
- **Smoothing**: Glide-on-time prevents pitch glitches during modulation.

## Reverb Module
- **Type**: Algorithmic stereo reverb.
- **Implementation**: Uses standard Schroeder/Freeverb-inspired techniques for lush acoustic simulation.
- **Parameters**: Room Size, Damping, Width, Mix.

## Chorus Module
- **Implementation**: `juce::dsp::Chorus<float>`.
- **CV Modulation**: Rate (ch2) and Depth (ch3). CV is sampled per block (RMS-gated) and added to the smoothed parameter value.
- **Parameters**: Rate (0.1–10 Hz), Depth (0–1), Centre Delay (1–30 ms), Feedback (-1–1), Mix (0–1).

## Phaser Module
- **Implementation**: `juce::dsp::Phaser<float>`.
- **CV Modulation**: Rate (ch2) and Depth (ch3). CV is sampled per block (RMS-gated) and added to the smoothed parameter value.
- **Parameters**: Rate (0.1–20 Hz), Depth (0–1), Centre Freq (200–10 000 Hz), Feedback (-1–1), Mix (0–1).

## Compressor Module
- **Implementation**: `juce::dsp::Compressor<float>`.
- **Makeup Gain**: Manual, user-controlled. Range: -20 to +40 dB, default 0 dB. Applied per-sample with 5 ms smoothing after the compressor. There is no automatic gain compensation.
- **CV Modulation**: None — no CV input channels.
- **Parameters**: Threshold (-60–0 dB), Ratio (1–20), Attack (0.1–200 ms), Release (10–1000 ms), Makeup Gain (-20–+40 dB).

## Flanger Module
- **Implementation**: `juce::dsp::Chorus<float>` configured for flanger character by constraining the centre-delay range to 1–5 ms (versus Chorus's 1–30 ms). Default centre delay is 2 ms.
- **CV Modulation**: Rate (ch2) and Depth (ch3). CV is sampled per block (RMS-gated) and added to the smoothed parameter value.
- **Parameters**: Rate (0.05–5 Hz), Depth (0–1), Centre Delay (1–5 ms), Feedback (-1–1), Mix (0–1).

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

## Pitch Shifter Module
Two engines behind one Mode switch, because they answer the same question with opposite characters: Pitch mode multiplies frequencies (harmonic), Frequency mode adds to them (inharmonic).

- **Pitch mode**: Two-tap crossfaded delay line. The read heads sweep across one window half a window apart; the phase advances at `(1 - ratio) / windowSamples` per sample, which is the condition for the read position to advance at `ratio`. Taps are summed with an equal-power window (`sin`/`|cos|` of the phase) so each tap's wrap-around lands exactly where its own gain is zero. Reads use cubic Hermite interpolation — linear interpolation audibly dulls a transposed signal.
  - **Unity dead zone**: when `|ratio - 1| < 1e-4` (about 0.0017 semitones) the two taps would sit at fixed, different delays and comb-filter the input, so the module emits the signal untransposed instead. This makes the default (Pitch 0, Mix 1) bit-transparent.
  - **Window**: trades artifacts against latency. Short windows chop the signal at a higher rate (audible as AM sidebands at `|1 - ratio| / window` Hz); long windows smear transients. 50 ms default.
- **Frequency mode**: Single-sideband modulation. A Hilbert transform pair (two cascaded 4-section 2nd-order allpass chains, `H(z) = (a² - z⁻²)/(1 - a²z⁻²)`, using Olli Niemitalo's wideband 90° coefficients) feeds a quadrature oscillator: `out = I·cos(ωt) + Q·sin(ωt)`. Measured rejection of the unwanted sideband is ~55 dB. A negative Shift runs the oscillator backwards — no separate code path. Harmonics stop being integer multiples of the fundamental, which is what produces the "alien voice" / metallic timbre.
- **Feedback**: routes the shifted output back into the input, so each pass is shifted again — cascading octaves in Pitch mode, barber-pole / Shepard-tone illusions in Frequency mode. Soft-clipped with `tanh` so the loop stays bounded at the 0.95 maximum.
- **CV Modulation**: Pitch (ch2, ±24 semitones), Shift (ch3, ±1000 Hz), Mix (ch4, ±1), Feedback (ch5, ±0.95). CV is added per-sample to the smoothed parameter value and clamped; Window and Fine have no CV input.
- **Parameters**: Mode (Pitch/Frequency), Pitch (-24–+24 semitones), Fine (-100–+100 cents), Shift (-1000–+1000 Hz), Window (10–100 ms), Feedback (0–0.95), Mix (0–1, default 1).
