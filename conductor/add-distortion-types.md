# Plan: Add Distortion Types

## Objective
Implement additional distortion algorithms (e.g., Hard Clip, Foldback) in `DistortionModule` and allow the user to select them via a new UI parameter.

## Implementation Steps
1.  **Modify `DistortionModule`**:
    *   Add a `typeParam` (`juce::AudioParameterChoice`) with options: "Soft", "Hard", "Foldback".
    *   Update `applyWaveshaper` to accept the selected type and implement the new algorithms.
    *   Update parameter list registration.
2.  **UI Update**:
    *   The `ModuleComponent` should auto-detect the new `ChoiceParameter` and update the UI (Agent Synth's auto-UI should handle this automatically based on existing parameter handling).
3.  **Testing**:
    *   Update `Tests/DistortionSweepTests.cpp` to verify all distortion types.
    *   Add a test case to ensure modulation still works correctly with new types.

## Verification
*   Build and run `Agent SynthTests`.
*   Verify UI displays the new "Type" parameter.
*   Verify distortion sounds different for different types (via `DistortionSweepTests`).

## Docs Updates
*   Update `docs/fx_modules.md` to reflect the new distortion types.
