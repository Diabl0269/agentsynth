# Implementation Plan: Mute Option for Modules

## Objective
Implement a "Mute" option for audio modules to allow complete silencing of their output, distinct from the existing "Bypass" functionality.

## Scope & Impact
- Add a new `mutedParam` (`juce::AudioParameterBool`) to `ModuleBase`.
- Update all modules inheriting from `ModuleBase` to check `isMuted()` in their `processBlock` and silence output if true.
- Update `AudioEngine` to expose mute state in `getModulationDisplayInfo` (if applicable) or similar display info.
- Update `ModuleComponent` UI to include a "Mute" toggle button.

## Implementation Steps

1. **`Source/Modules/ModuleBase.h`**:
    - Add `juce::AudioParameterBool* mutedParam`.
    - Initialize it in the constructor.
    - Add `bool isMuted() const` and `void setMuted(bool)`.

2. **Module `processBlock` Updates**:
    - Update each module (Oscillator, Filter, VCA, etc.) to check `if (isMuted()) { buffer.clear(); return; }` at the start of `processBlock`.

3. **`Source/UI/ModuleComponent.cpp`**:
    - Add a Mute button to the UI.
    - Link the button to `ModuleBase::setMuted()`.

4. **Testing & Verification**:
    - Create new tests in `Tests/ModuleMuteTests.cpp` to verify that muting a module indeed produces silence.
    - Verify that Mute and Bypass can coexist (Mute silences output, Bypass ignores processing).
    - Verify undo/redo functionality for Mute.

## Tests
- `TestModuleMute`: Verify that turning on mute on a module (e.g., Oscillator) results in silent buffer output.
- `TestMuteAndBypass`: Verify that toggling both states simultaneously works as expected.
- `TestMuteUndoRedo`: Ensure Mute state is correctly captured by `GravisynthUndoManager`.

## Docs Updates
- Update `docs/modules.md` to describe the new Mute feature.
- Update `CLAUDE.md` to note the new parameter.
