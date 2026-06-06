# Core Modules Reference

Detailed specifications for Gravisynth's primary synthesis modules.

## Oscillator Module
- **Waveforms**: Sine, Square, Saw, Triangle.
- **Features**: 
    - PolyBLEP anti-aliasing on Square and Saw; PolyBLAMP on Triangle.
    - Waveform crossfade (64-sample fade on waveform changes).
    - MIDI-to-Frequency tracking with unison and detune support.
    - Integrated visual buffer for real-time waveform display.
- **Poly mode**: 8 voices driven by pitch CV (Hz). See [Poly Channel Layout](#poly-channel-layout) for channel details.
- **Buffer aliasing note**: Declared with 14 output channels so JUCE's `AudioProcessorGraph` correctly copies shared mod-CV input channels (8-13) when they fan out to multiple downstream nodes. Channels 8-13 of the output are silent pass-throughs.

## Filter Module
- **Types**: 7 filter types — `LPF24`, `LPF12`, `HPF24`, `HPF12`, `BPF24`, `BPF12`, `Notch`.
- **Implementation**: `LPF24/12`, `HPF24/12`, `BPF24/12` use `juce::dsp::LadderFilter`; `Notch` uses `juce::dsp::StateVariableTPTFilter` (notch computed as input minus bandpass).
- **Parameters**: Cutoff (20–20000 Hz), Resonance (0–1), Drive (1–10), Filter Type (choice), Poly (bool).
- **CV inputs (mono mode)**: Cutoff = ch1, Resonance = ch2, Drive = ch3.
- **CV inputs (poly mode)**: Cutoff = ch8, Resonance = ch9, Drive = ch10.
- **Poly mode**: 8 per-voice audio inputs (ch0-7) + 3 shared CV inputs (ch8-10). Shared CV is computed once per block and applied to all active voices.
- **`isAutoPromotableModTarget`**: Returns `false` in poly mode (poly CV connections stay plain `DirectCV`, not auto-wrapped in attenuverters).

## ADSR (Envelope) Module
- **Stages**: Attack, Decay, Sustain, Release.
- **Mono output**: Generates a single control signal (0.0 to 1.0) on channel 0, triggered by MIDI.
- **Poly mode**: 8 gate CV inputs (ch0-7) drive 8 independent ADSR instances; outputs 8 per-voice envelopes (ch0-7).
- **Uses**: Modulation of VCA gain, Filter cutoff, or Oscillator Level.

## VCA (Amplifier) Module
- **Inputs**: 
    - Mono mode: Audio (ch0), CV (ch1).
    - Poly mode: Per-voice audio ch0-7, per-voice envelope/CV ch8-15.
- **Poly summing**: In poly mode, multiplies each voice's audio by its corresponding envelope CV, then sums all 8 voices to stereo (ch0/ch1) with `tanh` soft saturation and 1/8 normalization.
- **Features**: Parameter smoothing for click-free gain changes.

## Poly MIDI Module
- **Capacity**: 8 simultaneous voices.
- **Allocation**: Least Recently Used (LRU) algorithm.
- **Outputs**: 16 total channels — ch0-7 = per-voice pitch (Hz), ch8-15 = per-voice gate (0/1).
- **Visible ports**: 1 output jack ("Poly Out") representing the entire poly bus.

## Voice Mixer Module
- **Purpose**: Explicit 8-to-stereo voice summing with level control and soft saturation. An alternative to VCA's internal poly summing for patches that need a separate mix stage.
- **Inputs**: ch0-7 (up to 8 voice signals).
- **Outputs**: ch0 (Left), ch1 (Right) — stereo copy of the mono sum.
- **Processing**: Sums all 8 input channels, scales by Level parameter (0–1, default 0.125), then applies `std::tanh` soft saturation for gentle clip protection. Level smoothed over 10 ms to prevent clicks.
- **Parameter**: `Level` (0.0–1.0, default 0.125).

## MIDI Keyboard Module
- **Purpose**: Provides an interactive on-screen keyboard for MIDI input.
- **Features**:
    - **Octave Shift**: Shift the keyboard range by ±2 octaves.
    - **Visual Feedback**: Real-time display of pressed keys.
    - **MIDI Output**: Generates standard MIDI messages for driving oscillators or other MIDI-capable modules.

## Attenuverter Module (Hidden)
- **Purpose**: Invisible gain/polarity stage automatically inserted on every mono CV connection routed via the mod matrix.
- **Parameters**: `Amount` — ranges from -1.0 (full inversion) to +1.0 (full depth), **constructor default 0.0**.
    - `AudioEngine::addModRouting()` immediately sets Amount to 1.0 when a connection is created through the mod matrix.
    - `AudioEngine::addEmptyModRouting()` leaves Amount at 0.0 (empty routing placeholder).
- **Interaction**: Controlled via the **Smart Cable knob** on the graph or the **Mod Matrix** slider.
- **Serialization**: Saved and restored as part of every preset alongside the connection it belongs to.

---

## Poly Channel Layout

This table shows the raw `AudioProcessorGraph` channel assignments for each poly-capable module. See [docs/modulation.md](modulation.md) for how `getModulationRoutings()` and the logical-port API collapse these into visible wires.

### Rule for new poly modules

In poly mode, voices occupy channels 0-7 (audio/pitch/gate) and the shared-CV block starts at channel 8. **Declare `numOutputs >= highest CV input channel index you read**, to avoid JUCE `AudioProcessorGraph` buffer aliasing when `inputChan >= getTotalNumOutputChannels()`.

| Module | Channel | Direction | Content |
|--------|---------|-----------|---------|
| **PolyMidi** | ch0-7 | Out | Pitch CV per voice (Hz) |
| **PolyMidi** | ch8-15 | Out | Gate CV per voice (0/1) |
| **Oscillator (poly)** | ch0-7 | In | Per-voice pitch CV (Hz) |
| **Oscillator (poly)** | ch8 | In | Shared Waveform CV |
| **Oscillator (poly)** | ch9 | In | Shared Octave CV |
| **Oscillator (poly)** | ch10 | In | Shared Coarse CV |
| **Oscillator (poly)** | ch11 | In | Shared Fine CV |
| **Oscillator (poly)** | ch12 | In | Shared Level CV |
| **Oscillator (poly)** | ch0-7 | Out | Per-voice audio |
| **Oscillator (poly)** | ch8-13 | Out | Silent pass-throughs (prevent buffer aliasing) |
| **Oscillator (mono)** | ch0 | In/Out | Pitch CV in / Audio out (shared channel, CV saved before clear) |
| **Oscillator (mono)** | ch1 | In | Waveform CV |
| **Oscillator (mono)** | ch2 | In | Octave CV |
| **Oscillator (mono)** | ch3 | In | Coarse CV |
| **Oscillator (mono)** | ch4 | In | Fine CV |
| **Oscillator (mono)** | ch5 | In | Level CV |
| **Filter (poly)** | ch0-7 | In | Per-voice audio |
| **Filter (poly)** | ch8 | In | Shared Cutoff CV |
| **Filter (poly)** | ch9 | In | Shared Resonance CV |
| **Filter (poly)** | ch10 | In | Shared Drive CV |
| **Filter (poly)** | ch0-7 | Out | Filtered audio per voice |
| **Filter (mono)** | ch0 | In/Out | Audio in / filtered audio out |
| **Filter (mono)** | ch1 | In | Cutoff CV |
| **Filter (mono)** | ch2 | In | Resonance CV |
| **Filter (mono)** | ch3 | In | Drive CV |
| **VCA (poly)** | ch0-7 | In | Per-voice audio |
| **VCA (poly)** | ch8-15 | In | Per-voice envelope/CV |
| **VCA (poly)** | ch0-1 | Out | Stereo sum (L/R) |
| **ADSR (poly)** | ch0-7 | In | Per-voice gate CV |
| **ADSR (poly)** | ch0-7 | Out | Per-voice envelope (0–1) |

---

## Modulation System

Gravisynth uses a **hidden Attenuverter architecture** for modulation depth control, inspired by Serum's mod matrix. The engine derives a first-class `ModulationRouting` model from the graph at runtime. See [docs/modulation.md](modulation.md) for the full reference.

### Smart Cables
- Every mono CV cable (AttenuverterChain routing) renders a circular knob at the midpoint of the bezier curve.
- **Drag up/down** to sweep depth from -100% to +100%.
- **Double-click** to instantly delete the connection.

### Poly Bus Wires
- When N per-voice `DirectCV` connections share the same source module and destination visible jack, the GraphEditor collapses them into a single wire with an "xN" badge (e.g. "x8").
- These `PolyBus` wires have no midpoint knob because there is no attenuverter in the path.

### Mod Matrix Panel
- Sits on the right edge of the Graph Editor (toggleable).
- Lists every active CV connection as a labelled row with a bipolar slider.
- Sliders and smart cable knobs are **bidirectionally synced** in real time via a 30 Hz timer.

### Panel Toggles
- **Hide AI / Show AI** — collapses the right-side AI chat panel.
- **Hide Matrix / Show Matrix** — collapses the Mod Matrix panel.
- Both buttons live in the top application bar, and the Graph Editor canvas expands to fill reclaimed space.
