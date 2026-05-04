# Architecture Overview

AgentSynth is built on a modular graph architecture using JUCE's `AudioProcessorGraph` (implicitly managed by `AudioEngine`).

## Core Components

### 1. AudioEngine
The central manager for the audio device and the processor graph. It handles:
- Audio callback via `audioDeviceIOCallback`.
- Dynamic addition/removal of modules.
- Loading and saving graph states.

### 2. ModuleBase
Every audio processing unit inherits from `ModuleBase`.
- Extends `juce::AudioProcessor`.
- Provides a standard interface for parameter management (`addParameter`).
- Supports a high-performance visual buffer for scope visualization.

### 3. GraphEditor
The visual patching interface.
- Represented as a node graph.
- Handles mouse interactions for "dragging" cables between inputs and outputs.
- Maps UI connections to internal `AudioProcessorGraph` connections.

## Signal Flow
Modules communicate via two main signal types:
- **Audio Channels**: Stereo (usually) audio buffers containing PCM data.
- **CV (Control Voltage)**: Handled as control signals within the audio buffer (e.g., ADSR output feeding into VCA input 1).

## Quality Standards
All modules follow specific DSP requirements:
- **Smoothing**: All gain/cutoff parameters use linear smoothing to avoid clicks.
- **Antialiasing**: Oscillators use PolyBLEP for sharp waveforms.
- **Oversampling**: Nonlinear effects support configurable oversampling (e.g., Distortion offers Off/2x/4x modes).
