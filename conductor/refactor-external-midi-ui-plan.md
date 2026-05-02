# Plan: Refactor External MIDI Module UI

## Objective
Replace current integer-based parameter knobs for MIDI device and channel selection in `ExternalMidiModule` with proper dropdown (ComboBox) selectors in the UI.

## Key Files & Context
- `Source/Modules/ExternalMidiModule.h`: Update module parameters/logic.
- `Source/UI/ModuleComponent.cpp`: Implement custom UI rendering for `ExternalMidiModule`.

## Implementation Steps
1. **Module Parameters**:
   - Remove integer `AudioParameterInt` parameters if not needed, or use them only for state storage.
   - The UI will need to interface directly with `juce::MidiInput::getAvailableDevices()`.
2. **UI Component**:
   - In `ModuleComponent.cpp`, add a special case for `ModuleType::ExternalMidi`.
   - Implement `juce::ComboBox` for device selection and MIDI channel selection (1-16 + 'All').
3. **Synchronization**:
   - Ensure the selected device and channel are stored/restored correctly with the preset system.

## Verification & Testing
- **Verification**:
  - Open the graph, add an External MIDI module.
  - Verify that the UI displays a ComboBox for device selection and another for channel selection.
  - Verify that changing these updates the active routing in `AudioEngine`.
- **Tests**:
  - `Tests/ExternalMidiModuleTests.cpp`: Add tests for parameter state persistence.
