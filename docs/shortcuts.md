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
| Cmd+K | Toggle Minimap |
| Cmd+A | Toggle AI Panel |
| Cmd+L | Auto Arrange |
| Cmd+B | Toggle Module Library Sidebar |
| Cmd+Shift+A | Select All Modules |
| Cmd+Shift+S | Save Selection as Snippet |

`Cmd+Shift+A` / `Cmd+Shift+S` use the Shift variants because `Cmd+A` (Toggle AI Panel) and `Cmd+S`
(Save Preset) are already bound. Like every row above, both are rebindable in Settings.

## Context-specific

| Shortcut | Context | Action |
|----------|---------|--------|
| Escape | AI panel, request in flight | Cancel the in-flight AI request (same as the Cancel button — actually aborts it, see [`AI_Engine.md`](AI_Engine.md#request-cancellation)) |
| Escape | Canvas, modules selected | Clear the selection |
| Delete / Backspace | Canvas, modules selected | Delete every selected module (one undo step) |

Escape is handled by `AIChatComponent::keyPressed()` and only acts while a request is in flight;
otherwise it is passed through so it keeps whatever meaning the enclosing window gives it. It is not
user-rebindable — it is a panel-local binding, not part of the `ShortcutManager` table.

The canvas selection keys are handled by `GraphEditor::keyPressed()` and are likewise **not**
rebindable. This is deliberate: an unmodified `Delete` registered in the app-wide `ShortcutManager`
table would fire from any panel that does not consume the key first. Both are canvas-scoped —
`GraphEditor` takes keyboard focus on mouse-down — and both return `false` when nothing is selected
so the key keeps its normal meaning elsewhere.

## Canvas mouse gestures

Multi-select is layered on top of the existing pan gesture rather than replacing it, so no existing
habit changes. See [`layout.md §12`](layout.md) for the full contract.

| Gesture | Action |
|---------|--------|
| Drag on empty canvas | Pan (unchanged) |
| **Shift** + drag on empty canvas | Marquee-select, replacing the selection |
| **Cmd/Ctrl + Shift** + drag | Marquee-select, adding to the selection |
| Click a module | Select just that module |
| **Shift**/**Cmd** + click a module | Toggle that module in the selection |
| Drag any selected module | Move the whole selection together |
| Click empty canvas | Clear the selection |

Escape is handled by `AIChatComponent::keyPressed()` and only acts while a request is in flight;
otherwise it is passed through so it keeps whatever meaning the enclosing window gives it. It is not
user-rebindable — it is a panel-local binding, not part of the `ShortcutManager` table.
