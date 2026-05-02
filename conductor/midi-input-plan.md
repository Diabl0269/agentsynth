# Implementation Plan: Support External MIDI Input Devices

## Objective
Enable hardware MIDI controller support by implementing MIDI input device selection and routing in the audio engine.

## Key Files & Context
- `Source/AudioEngine.h/cpp`: Needs to handle MIDI input device registration and message routing.
- `Source/UI/SettingsWindow.h/cpp`: Needs a new section for selecting MIDI input devices.
- `juce::MidiInput`, `juce::AudioProcessorPlayer`, `juce::MidiMessageCollector`

## Implementation Steps
1. **Audio Engine Update**:
   - Add `juce::MidiInput` support in `AudioEngine`.
   - Implement `juce::MidiInputCallback` to forward messages to the audio processing graph.
2. **Settings UI**:
   - Add a MIDI Input Device dropdown in the Settings window (accessible via `SettingsWindow`).
   - Allow users to select one or multiple MIDI input devices.
3. **MIDI Message Routing**:
   - Ensure MIDI messages from external devices are injected into the polyphonic MIDI processing chain.
4. **Cleanup**:
   - Proper management of MIDI device connections (connect/disconnect).

## Verification & Testing
- **Verification**:
  - Open the application.
  - Connect a hardware MIDI keyboard.
  - Verify that the device appears in the Settings MIDI input dropdown.
  - Select the device and verify that playing notes triggers sound.
- **Tests**:
  - `Tests/AudioEngineTests.cpp`: Add unit tests for MIDI message injection.
  - `Tests/SettingsWindowTests.cpp`: Verify MIDI device selection persistence.

## Docs Updates
- `docs/modules.md`: Update MIDI input section.
- `CLAUDE.md`: Update architecture summary to reflect MIDI support.
