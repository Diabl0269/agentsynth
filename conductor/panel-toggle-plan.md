# Add Shortcuts for Mod Matrix and AI Panel Toggles

## Objective
Enable toggling of the Mod Matrix and AI Chat panels via keyboard shortcuts (Cmd+M/Ctrl+M for Mod Matrix, Cmd+A/Ctrl+A for AI Panel) and set both panels to be closed by default on application launch.

## Key Files & Context
- `Source/ShortcutManager.h`: Update to include new command actions and default bindings.
- `Source/MainComponent.h`: Change initial visibility state (`isAiPanelVisible` and default mod matrix state).
- `Source/MainComponent.cpp`: Implement command handling for the new toggles, update the shortcut registration logic.

## Implementation Steps
1. **ShortcutManager Update**:
   - Add `toggleModMatrix` and `toggleAiPanel` to `GravisynthCommands::CommandIDs`.
   - Update `getCommandForAction` to return these new IDs.
   - Update `resetToDefaults` to include default key mappings.
   - Add new actions to the `actionIds` array.
   - Add `getActionDescription` mappings for the new actions.

2. **MainComponent Initial State**:
   - Change `isAiPanelVisible` in `Source/MainComponent.h` to `false`.
   - Modify the default visibility of the Mod Matrix in `Source/UI/GraphEditor` or `Source/MainComponent` to be `false` on startup.

3. **MainComponent Command Handling**:
   - Add the new commands to the array in `MainComponent` constructor (or `getCommandInfo`/`perform`).
   - Implement the toggle logic inside `MainComponent::perform` (or wherever `GravisynthCommands` are processed).

4. **Refactor/Cleanup**:
   - Ensure the UI update logic (`resized()`) handles the "closed by default" state correctly on initial launch.

## Verification & Testing
- **Manual Verification**:
  - Launch app: Verify both panels are hidden by default.
  - Test Shortcuts: Cmd+M toggles Mod Matrix, Cmd+A toggles AI Panel.
  - Verify shortcut conflicts are handled by `ShortcutManager`.
- **Tests**:
  - Update `Tests/MainComponentTests.cpp` to verify initial state is "closed".
  - Add unit tests for the new shortcut actions in `Tests/ShortcutManagerTests.cpp` (if exists).

## Docs Updates
- Update `CLAUDE.md` to document new keyboard shortcuts.
