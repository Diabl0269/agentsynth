# Plan: Introduce External MIDI Module

## Objective
Introduce an `ExternalMidiModule` that acts as a bridge for external hardware MIDI devices to the modular graph, allowing per-device selection and routing.

## Key Files & Context
- `Source/Modules/ExternalMidiModule.h`: Create this new module.
- `Source/Modules/ModuleBase.h`: Register new `ModuleType`.
- `Source/AudioEngine.cpp`: Update to route MIDI from specific devices to the module instances.

## Implementation Steps
1. **Module Creation**:
   - Create `ExternalMidiModule` inheriting from `ModuleBase`.
   - Add parameters for: `Device` (dropdown), `Channel` (1-16 or 'All').
   - Add `juce::MidiMessageCollector` internally (or share).
2. **Graph Integration**:
   - Update `GraphEditor` to allow adding `ExternalMidiModule` via the library.
   - Modify `AudioEngine` to route messages received in `handleIncomingMidiMessage` to the corresponding `ExternalMidiModule` instance.
3. **UI Component**:
   - Create `ExternalMidiComponent` to provide the device/channel selector.

## Verification & Testing
- **Verification**:
  - Add an `ExternalMidiModule` to the graph.
  - Select a MIDI controller and channel.
  - Connect the module's MIDI output to a synth voice (e.g., Oscillator).
  - Verify that hardware MIDI triggers the synth.
- **Tests**:
  - `Tests/ExternalMidiModuleTests.cpp`: Verify MIDI message filtering by channel and device association.

## Docs Updates
- `docs/modules.md`: Add External MIDI Module section.
