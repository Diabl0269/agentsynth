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
| Cmd+T | Toggle Timeline Panel (`SYNTH_ENABLE_TIMELINE` builds only — see [`layout.md §16`](layout.md)) |
| Cmd+Shift+A | Select All Modules |
| Cmd+Shift+S | Save Selection as Snippet |
| Cmd+C | Copy (Selected Modules, or — see "Surface routing" below — the timeline's selected clips) |
| Cmd+V | Paste (Modules, or the copied clips) |
| Cmd+D | Duplicate (Selected Modules, or the selected clips) |
| Space | Toggle Playback (play/stop the timeline transport — `SYNTH_ENABLE_TIMELINE` builds only) |

`Cmd+Shift+A` / `Cmd+Shift+S` use the Shift variants because `Cmd+A` (Toggle AI Panel) and `Cmd+S`
(Save Preset) are already bound. Like every row above, both are rebindable in Settings.

`Cmd+C` / `Cmd+V` (and, by the same reasoning, Space) are safe to claim app-wide because JUCE's
`TextEditor` consumes them itself while it has focus — Cmd+C/V by copying/pasting text, Space by
typing a literal space character — so they only reach `MainComponent::keyPressed` (the sole
dispatch point) when no text field is being edited; the AI chat input keeps its normal copy/paste
and spacebar behaviour. Copy, Paste and Duplicate are marked **inactive** when there is nothing to
act on (nothing selected / an empty clipboard on the acting surface — see below), which greys the
menu row and makes `ApplicationCommandTarget::tryToInvoke` refuse the key outright.

### Surface routing (TL5-10): who Cmd+C/V/D act on

Cmd+C/V/D are global commands (`MainComponent::getAllCommands`/`getCommandInfo`/`perform`), but
"the canvas" is not the only editable surface once the timeline panel is open — the clip lanes and
the piano roll are too. `MainComponent::resolveEditSurface()` is the **one** focus-ownership rule
that decides which surface's clipboard and selection C/V/D act on:

```cpp
enum class EditSurface { Graph, TimelineClips, PianoRoll };
```

- **TimelineClips** — the timeline panel is visible AND real keyboard focus
  (`juce::Component::getCurrentlyFocusedComponent()`) sits inside the clip-lane area.
- **PianoRoll** — same, but focus sits inside the piano roll.
- **Graph** — every other case (including the timeline panel being hidden entirely, regardless of
  what a stale focus pointer might point at).

Every one of those surfaces already grabs keyboard focus on `mouseDown` (`GraphEditor::mouseDown`
is the original of this idiom; `TimelineClipLaneArea`, `PianoRollComponent` and
`AutomationLaneEditor` all copy it), so "the surface you last clicked owns the verbs" falls out of
ordinary JUCE focus tracking — `resolveEditSurface()` adds no bookkeeping of its own beyond reading
`getCurrentlyFocusedComponent()`. Headless tests can't always create a real focus grab (it needs a
native peer), so `MainComponent::setEditSurfaceOverrideForTest()` short-circuits the resolver for
`Tests/FocusArbitrationTests.cpp`.

What each surface does:

| Surface | Copy | Paste | Duplicate |
|---|---|---|---|
| Graph | Copies the selected modules (unchanged since before TL5-10) | Pastes at the next cascade position | Copies the selection one step down-right, clipboard untouched |
| TimelineClips | Serialises the selected clips — notes, lengths, and starts relative to the earliest selected clip — into the panel's own clip clipboard | Re-inserts every clipboard clip onto **its original track**, re-based so the earliest clip lands at the transport's current position (snapped to the view-state's snap setting); a clip whose original track is gone lands on the doc's first `Midi`-kind track, or is skipped if there is none. One undo step for the whole paste; the pasted clips end up selected. | `TimelineDoc::duplicateClip` per selected clip, batched into one undo step; the new clips end up selected |
| PianoRoll | *(inactive — v1 deliberate gap; the roll's own note-editing gestures, e.g. double-click-to-create and double-click-to-delete, cover copy/duplicate/undo already)* | *(inactive)* | *(inactive)* |

`getCommandInfo` marks Paste active only when the **surface-matching** clipboard has something in
it — the Graph clipboard and the TimelineClips clipboard are entirely separate, so copying modules
does not make Paste live on the clip lanes or vice versa.

## Context-specific

| Shortcut | Context | Action |
|----------|---------|--------|
| Escape | AI panel, request in flight | Cancel the in-flight AI request (same as the Cancel button — actually aborts it, see [`AI_Engine.md`](AI_Engine.md#request-cancellation)) |
| Escape | Canvas, modules selected | Clear the selection |
| Delete / Backspace | Canvas, modules selected | Delete every selected module (one undo step) |
| Escape | Clip lanes, clips selected | Clear the clip selection |
| Delete / Backspace | Clip lanes, clips selected | Delete every selected clip (one undo step) |
| P | Clip lanes, clips selected | Loop the selection: sets the transport loop to the selected clips' `[min start, max end]` span and switches looping on |
| Escape | Piano roll, notes selected | Clear the note selection |
| Escape | Piano roll, nothing selected | Close the roll, back to the clip lanes |
| Delete / Backspace | Piano roll, notes selected | Delete every selected note (one undo step) |

Escape is handled by `AIChatComponent::keyPressed()` and only acts while a request is in flight;
otherwise it is passed through so it keeps whatever meaning the enclosing window gives it. It is not
user-rebindable — it is a panel-local binding, not part of the `ShortcutManager` table.

The canvas selection keys are handled by `GraphEditor::keyPressed()`, the clip-lane ones (including
`P`) by `TimelineClipLaneArea::keyPressed()`, and the piano-roll ones by
`PianoRollComponent::keyPressed()`
— all three are likewise **not** rebindable. This is deliberate: an unmodified `Delete` registered
in the app-wide `ShortcutManager` table would fire from any panel that does not consume the key
first — see the "surface routing" section above, which is exactly the same problem Cmd+C/V/D had
to solve for a table-driven binding. Every one of these three components takes keyboard focus on
mouse-down (the same idiom `resolveEditSurface()` relies on), and every one returns `false` when
its own selection is empty, so an unmodified Delete/Escape keeps its normal meaning elsewhere
instead of being silently swallowed by an idle panel — `Tests/FocusArbitrationTests.cpp`'s
`DeletePerSurface` pins exactly this: a clips-focused Delete never touches the graph, a
graph-focused Delete never touches the clips, and an empty selection on either falls through.

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
| Right-click a module | Copy / Duplicate / Paste / Save as Snippet / Delete for the whole selection |
| Right-click empty canvas | Paste Here (at the click point) / Select All Modules |

Right-clicking empty canvas keeps the selection rather than clearing it, so the menu can still act
on what is selected. "Paste Here" drops the group at the click point and re-anchors the paste
cascade there, so a following `Cmd+V` continues from the same place.
