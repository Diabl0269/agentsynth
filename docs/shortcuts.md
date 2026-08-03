# Keyboard Shortcuts

Shortcuts are configurable in **Settings → Keyboard Shortcuts** tab (renamed from "General" in Phase 5; extracted into `ShortcutsSettingsTab.h/.cpp`). Click any binding to rebind it; conflict detection swaps the displaced binding automatically. Export/import shortcuts as JSON or reset to defaults. A native macOS menu bar (File + Edit) provides Undo/Redo via `ApplicationCommandManager`, while `keyPressed()` handles all shortcuts with case-insensitive key matching.

| Shortcut | Action |
|----------|--------|
| Cmd+, | Open Settings |
| Cmd+N | New Patch (clear canvas) |
| Cmd+S | Save Preset |
| Cmd+O | Open Preset (file picker) |
| Cmd+Z | Undo |
| Cmd+Shift+Z | Redo |
| Cmd+M | Toggle Mod Matrix |
| Cmd+A | Toggle AI Panel |
| Cmd+L | Auto Arrange |
| Cmd+B | Toggle Module Library Sidebar |

## Context-specific

| Shortcut | Context | Action |
|----------|---------|--------|
| Escape | AI panel, request in flight | Cancel the in-flight AI request (same as the Cancel button — actually aborts it, see [`AI_Engine.md`](AI_Engine.md#request-cancellation)) |

Escape is handled by `AIChatComponent::keyPressed()` and only acts while a request is in flight;
otherwise it is passed through so it keeps whatever meaning the enclosing window gives it. It is not
user-rebindable — it is a panel-local binding, not part of the `ShortcutManager` table.
