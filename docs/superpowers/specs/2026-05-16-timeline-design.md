# Timeline Feature Specification

## Overview
Implement a hybrid timeline orchestration system in Gravisynth. This system consists of:
1. **Global Playback HUD:** A top-level UI component managing global transport state (Play, Pause, Stop, Loop, Locators, Tempo).
2. **Timeline Module:** A new module type that exposes MIDI output ports and contains an internal timeline/sequencer engine.

## Core Components

### 1. Global Playback HUD
- **Transport State Manager:** A singleton-like class or a component in `AudioEngine` that stores play/pause state, BPM, current playhead position (in samples/beats), and loop markers.
- **UI:** A horizontal control bar at the top of `MainComponent` providing play/pause/stop buttons, tempo input, and loop toggle.

### 2. Timeline Module (`TimelineModule`)
- **Engine:** Inherits `ModuleBase`. Maintains an internal list of MIDI patterns (clips).
- **MIDI Output:** Exposes a MIDI output port to the `GraphEditor` so it can be patched to any synth module.
- **Sync:** Listens to the `AudioEngine`'s global transport state for play/pause/sync.
- **Automation:** (Future-proofing) Data model includes support for parameter automation, though initial focus is MIDI note sequencing.

## Data Flow
- `MainComponent` (Transport HUD) -> `AudioEngine` (Sync State) -> `TimelineModule` (Processes MIDI patterns based on synced playhead).

## Data Structure
- `TimelineModule` stores `TimelinePattern` objects containing:
  - Start/End time.
  - MIDI notes or CC automation data.
  - Target MIDI channel.

## Testing Strategy
- **Unit Tests:** `TimelineModuleTests` (verify MIDI generation at specific timeline positions).
- **Integration:** `TransportSyncTests` (verify play/pause state propogation from `AudioEngine` to modules).
- **E2E:** `E2EWorkflowTests` (verify adding a `TimelineModule` to the graph, patching it to an `OscillatorModule`, and playback).

## Docs Updates
- Update `docs/modules.md` (add Timeline Module details).
- Update `docs/architecture.md` (add Transport Sync HUD architecture).
