# Implementation Plan: Add shortcuts and default visibility

## Tasks
- [ ] Task: ShortcutManager Updates
    - [ ] Update `GravisynthCommands` with `toggleModMatrix` and `toggleAiPanel`.
    - [ ] Update `ShortcutManager::resetToDefaults` with new bindings.
- [ ] Task: UI Visibility Updates
    - [ ] Change `isAiPanelVisible` to `false` in `MainComponent`.
    - [ ] Modify `ModuleComponent`/`ScopeComponent` to default visibility to `false`.
    - [ ] Task: Conductor - User Manual Verification 'UI Visibility Updates' (Protocol in workflow.md)
- [ ] Task: Command Implementation
    - [ ] Update `MainComponent` to handle toggle commands.
    - [ ] Ensure persistence via `appProperties`.
    - [ ] Task: Conductor - User Manual Verification 'Command Implementation' (Protocol in workflow.md)
