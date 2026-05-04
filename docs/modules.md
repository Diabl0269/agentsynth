# Core Modules Reference

Detailed specifications for AgentSynth's primary synthesis modules.

## Oscillator Module
- **Waveforms**: Sine, Square, Saw, Triangle.
- **Features**: 
    - PolyBLEP anti-aliasing on Square and Saw.
    - MIDI-to-Frequency tracking.
    - Integrated visual buffer for real-time waveform display.

## Filter Module
- **Type**: Resonant Low-Pass.
- **Parameters**: Cutoff (Freq), Resonance (Q).
- **Quality**: Zero-delay feedback (ZDF) style filtering for analog-like response.

## ADSR (Envelope) Module
- **Stages**: Attack, Decay, Sustain, Release.
- **Output**: Generates a mono control signal (0.0 to 1.0).
- **Uses**: Modulation of VCA gain or Filter cutoff.

## VCA (Amplifier) Module
- **Inputs**: 
    - Input 0: Audio.
    - Input 1: CV (Modulation).
- **Features**: Parameter smoothing for "click-free" gain changes.

## Poly MIDI Module
- **Capacity**: 8 simultaneous voices.
- **Allocation**: Least Recently Used (LRU) algorithm.
- **Outputs**: Provides 8 separate Pitch and Gate channels for polyphonic routing.

## MIDI Keyboard Module
- **Purpose**: Provides an interactive on-screen keyboard for MIDI input.
- **Features**:
    - **Octave Shift**: Shift the keyboard range by ±2 octaves.
    - **Visual Feedback**: Real-time display of pressed keys.
    - **MIDI Output**: Generates standard MIDI messages for driving oscillators or other MIDI-capable modules.

## Attenuverter Module (Hidden)
- **Purpose**: Invisible gain/polarity stage automatically inserted on every CV connection.
- **Parameters**: `Amount` — ranges from -1.0 (full inversion) to +1.0 (full depth), default 1.0.
- **Interaction**: Controlled via the **Smart Cable knob** on the graph or the **Mod Matrix** slider.
- **Serialization**: Saved and restored as part of every preset alongside the connection it belongs to.

---

## Modulation System

AgentSynth uses a **hidden Attenuverter architecture** for modulation depth control, inspired by Serum's mod matrix.

### Smart Cables
- Every CV cable renders a circular knob at the midpoint of the bezier curve.
- **Drag up/down** to sweep depth from -100% to +100%.
- **Double-click** to instantly delete the connection.

### Mod Matrix Panel
- Sits on the right edge of the Graph Editor (toggleable).
- Lists every active CV connection as a labelled row with a bipolar slider.
- Sliders and smart cable knobs are **bidirectionally synced** in real time via a 30 Hz timer.

### Panel Toggles
- **Hide AI / Show AI** — collapses the right-side AI chat panel.
- **Hide Matrix / Show Matrix** — collapses the Mod Matrix panel.
- Both buttons live in the top application bar, and the Graph Editor canvas expands to fill reclaimed space.
