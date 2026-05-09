# Implementation Plan: Add shortcuts and default visibility

## Tasks
- [x] Task: ShortcutManager Updates
    - [x] Update `GravisynthCommands` with `toggleModMatrix` and `toggleAiPanel`.
    - [x] Update `ShortcutManager::resetToDefaults` with new bindings.
- [x] Task: UI Visibility Updates
    - [x] Change `isAiPanelVisible` to `false` in `MainComponent`.
    - [x] Modify `ModuleComponent`/`ScopeComponent` to default visibility to `false`.
    - [x] Task: Conductor - User Manual Verification 'UI Visibility Updates' (Protocol in workflow.md)
- [x] Task: Command Implementation
    - [x] Update `MainComponent` to handle toggle commands.
    - [x] Ensure persistence via `appProperties`.
    - [x] Task: Conductor - User Manual Verification 'Command Implementation' (Protocol in workflow.md)
