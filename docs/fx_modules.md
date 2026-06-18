# FX Modules Reference

Technical documentation for the Gravisynth effects suite.

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
