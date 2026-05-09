# Specification: Add shortcuts and default visibility settings

## Objective
Implement keyboard shortcuts for toggling Mod Matrix and AI panels, and change the default visibility of module scope components to hidden.

## Requirements
1. Shortcut Cmd+M (Ctrl+M) toggles Mod Matrix.
2. Shortcut Cmd+A (Ctrl+A) toggles AI Chat panel.
3. Module scope components (e.g., in `ScopeComponent`) should be initialized to hidden by default.
4. Shortcuts should be registered via `ShortcutManager`.
5. Visibility state updates should be persisted across application launches.
