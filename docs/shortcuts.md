# Keyboard Shortcuts

Shortcuts are configurable in **Settings → Keyboard Shortcuts** tab (renamed from "General" in Phase 5; extracted into `ShortcutsSettingsTab.h/.cpp`). Click any binding to rebind it; conflict detection swaps the displaced binding automatically. Export/import shortcuts as JSON or reset to defaults. A native macOS menu bar (File + Edit) provides Undo/Redo via `ApplicationCommandManager`, while `keyPressed()` handles all shortcuts with case-insensitive key matching.

| Shortcut | Action |
|----------|--------|
| Cmd+, | Open Settings |
| Cmd+S | Save Preset |
| Cmd+O | Open Preset (file picker) |
| Cmd+Z | Undo |
| Cmd+Shift+Z | Redo |
| Cmd+M | Toggle Mod Matrix |
| Cmd+A | Toggle AI Panel |
| Cmd+L | Auto Arrange |
| Cmd+B | Toggle Module Library Sidebar |
