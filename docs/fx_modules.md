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

## Parametric EQ Module
- **Bands**: Four, in a fixed console-strip layout — `0` Low Shelf, `1` Peak (bell), `2` Peak (bell), `3` High Shelf. Both shelves are hard-wired to Q = 1/√2 (the RBJ "S = 1" case, maximally flat, no shelf overshoot); only the bells expose Q.
- **Implementation**: RBJ cookbook biquads via `juce::dsp::IIR::Filter<float>`, one filter per channel per band (2 × 4). Coefficients are computed in-house by `ParametricEQModule::writeBiquad()` rather than `IIR::Coefficients::makePeakFilter()` et al., because the module also needs the matching *analog prototype* magnitude for the visualiser — see below.
- **No audio-thread allocation**: the four `Coefficients` objects are allocated once in `prepareToPlay` and their raw values rewritten in place each block. The two channels share each band's `Coefficients` object; `juce::dsp::IIR` keeps filter state in the `Filter`, not the `Coefficients`, so sharing is safe (this is what `ProcessorDuplicator` does internally).
- **Coefficient update rate**: once per block, from 20 ms-smoothed parameter/CV values. The per-block step is small enough that swapping coefficients wholesale is inaudible while keeping the inner loop a plain biquad. Output gain is applied per sample so its smoothing is genuinely continuous.
- **Analytic response**: `bandMagnitudeDb()` / `responseDb()` are pure static functions evaluating the analog prototypes the digital coefficients are derived from (`H(s)` forms for peak / low shelf / high shelf). `EQCurveComponent` draws the curve with them, so the display and the DSP agree — `ParametricEQAudio.MeasuredResponseTracksTheAnalyticCurve` asserts they stay within 1 dB of each other at real audio frequencies.
- **Channels**: 6 in / 2 out — `0-1` stereo audio, `2` B1 Freq CV, `3` B1 Gain CV, `4` B2 Freq CV, `5` B2 Gain CV. Only the two bells take CV, following the rest of the FX suite (CV on the parameters worth modulating, not one jack per knob).
- **CV mapping**: Freq CV is exponential over the band's own range — `+1` sweeps to the top of the range, `-1` to the bottom, matching FilterModule's cutoff-CV feel. Gain CV maps linearly onto the full ±24 dB range and adds to the knob value. Both are sampled once per block and RMS-gated, so an unpatched jack reads as exactly 0.
- **Parameters**: Low Freq (20–1000 Hz, default 120), Low Gain (±24 dB), B1 Freq (40–8000 Hz, default 500), B1 Gain (±24 dB), B1 Q (0.1–10, default 0.707), B2 Freq (200–16000 Hz, default 3000), B2 Gain (±24 dB), B2 Q (0.1–10, default 0.707), High Freq (1000–20000 Hz, default 8000), High Gain (±24 dB), Output (±24 dB). Frequency and Q ranges are skewed (`setSkewForCentre`) so the knobs feel logarithmic.
- **UI**: `Source/UI/EQCurveComponent.h` — log-frequency response curve over a symmetric ±30 dB window (0 dB dead centre), with a per-band handle dot at (centre freq, gain), hollow when that band is flat, plus an optional FFT spectrum underlay behind a "Show Spectrum" toggle. Axis maths is shared with the Filter module's `FrequencyResponseComponent` via `Source/UI/FrequencyGrid.h`.
