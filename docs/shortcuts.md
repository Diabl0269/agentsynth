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
| Cmd+Shift+A | Select All in Focused Editor (still `AppCommands`/actionId `selectAllModules` — see "Surface routing" below) |
| Cmd+Shift+S | Save Selection as Snippet |
| Cmd+C | Copy (Selected Modules, or — see "Surface routing" below — the timeline's selected clips/notes) |
| Cmd+V | Paste (Modules, or the copied clips/notes) |
| Cmd+D | Duplicate (Selected Modules, or the selected clips/notes) |
| Cmd+X | Cut — Copy then delete, as ONE undo step (Selected Modules, or the timeline's selected clips/notes; see "Surface routing" below) |
| Cmd+R | Repeat — prompts for a count (1–64) via an `AlertWindow` and creates that many back-to-back copies of the selection, tiled forward one selection-span at a time, as ONE undo step. Timeline-only: inactive on the Graph surface (see below) |
| Space | Toggle Playback (play/stop the timeline transport — `SYNTH_ENABLE_TIMELINE` builds only) |

Cmd+T and Space are also inactive (in addition to a `SYNTH_ENABLE_TIMELINE`-OFF build) whenever
Preferences → "Show timeline (experimental)" is turned off — the runtime kill switch described in
[`layout.md §16`](layout.md). While it's off, the timeline transport simply isn't reachable; that's
intended, not a bug.

`Cmd+Shift+A` / `Cmd+Shift+S` use the Shift variants because `Cmd+A` (Toggle AI Panel) and `Cmd+S`
(Save Preset) are already bound. Like every row above, both are rebindable in Settings.

`Cmd+C` / `Cmd+V` (and, by the same reasoning, Space) are safe to claim app-wide because JUCE's
`TextEditor` consumes them itself while it has focus — Cmd+C/V by copying/pasting text, Space by
typing a literal space character — so they only reach `MainComponent::keyPressed` (the sole
dispatch point) when no text field is being edited; the AI chat input keeps its normal copy/paste
and spacebar behaviour. `Cmd+X` reuses the same safety: `x` claims no other binding in this table
and no component's local `keyPressed` hardcodes a bare `x` either (the panel-local letters are
Q/L/P, the roll's is Q, the lane area's is P). `Cmd+R` (`r`) is likewise free on both counts. Copy,
Paste, Duplicate and Cut are marked **inactive** when there is nothing to act on (nothing selected /
an empty clipboard on the acting surface — see below), which greys the menu row and makes
`ApplicationCommandTarget::tryToInvoke` refuse the key outright; Repeat is inactive with no
selection AND always inactive on the Graph surface.

### Surface routing (TL5-10): who Cmd+C/V/D/X/R and Cmd+Shift+A act on

Cmd+C/V/D/X/R and Cmd+Shift+A are global commands (`MainComponent::getAllCommands`/
`getCommandInfo`/`perform`), but "the canvas" is not the only editable surface once the timeline
panel is open — the clip lanes and the piano roll are too. `MainComponent::resolveEditSurface()`
is the **one** focus-ownership rule that decides which surface's clipboard and selection these
verbs act on:

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

| Surface | Copy | Paste | Duplicate | Cut | Repeat |
|---|---|---|---|---|---|
| Graph | Copies the selected modules (unchanged since before TL5-10) | Pastes at the next cascade position | Copies the selection one step down-right, clipboard untouched | Composed from Copy + Delete (`copySelection()` then `deleteSelection()`), one undo step | **Inactive** — a spatial canvas has no time axis to tile copies along; Duplicate is the graph's equivalent gesture (see below) |
| TimelineClips | Serialises the selected clips — notes (each with its own muted flag), name, length, muted flag and every audio field (`assetRef`, gain, both fades, `sourceStartSeconds`) — into the panel's own clip clipboard, starts relative to the earliest selected clip | Re-inserts every clipboard clip onto **its original track**, re-based so the earliest clip lands at the transport's current position (snapped to the view-state's snap setting); the track fallback is **kind-aware** — a clip lands back only on a track that still plays its payload (audio → `TrackKind::Audio`, MIDI → `TrackKind::Midi`), else the doc's first track of the required kind, else the clip is skipped. Audio fields go back through `setClipAsset`/`setClipGainDb`/`setClipFades` (never a raw struct write), so a clipboard `assetRef` is re-validated exactly like a freshly-loaded file's — a clipboard is only as trustworthy as whatever filled it. One undo step for the whole paste; the pasted clips end up selected. | `TimelineDoc::duplicateClip` per selected clip, batched into one undo step; the new clips end up selected | `TimelinePanelComponent::cutSelectedClips()` — copy, then delete the selection, as ONE `recordTimelineChange` (so undo restores it in a single step) | `repeatSelectedClips(count)` — `count` back-to-back copies of the selection's own span (`max end - min start`, not each clip's own length, so a multi-clip rhythm tiles intact), the first starting one span-length after the selection's start. One undo step; every created clip ends up selected. |
| PianoRoll | Copies the selected notes (each field, `muted` included) into the roll's OWN note clipboard, offsets stored relative to the earliest selected note — the clipboard is a member of the roll, so it survives switching clips (`openClip`), and a block copied in one clip pastes into another | `pasteNotesAtPlayhead()` anchors the block at the **snapped, clip-relative playhead position** when that lands inside `[0, clip length)`, else at 0.0; `MainComponent::perform` primes the playhead from the live transport (`setPlayheadBeat(transport.getPositionSnapshot().ppq)`) immediately before pasting, so a paste with the transport stopped still lands under the position the user can see rather than wherever a stale internal beat was left. Notes at/after the clip's end are skipped, an overrunning note's length is clamped to the clip's end. One undo step; the pasted notes end up selected. | Copies the selection to immediately after its own span (same pitches), one undo step, selects the copies — does NOT touch the clipboard (duplicating isn't copying, and silently stomping a clipboard the user filled deliberately would be a surprise) | `cutSelectedNotes()` — copy then delete, one undo step (fills the clipboard first, so a cut is always paste-able) | `repeatSelectedNotes(count)` — `count` copies of the selection block, each one span further along, **clipped at the clip's end**: placement stops at the first block that would fall entirely outside the clip rather than piling every remaining copy onto the last beat. One undo step; every created note ends up selected. |

`getCommandInfo` marks Paste active only when the **surface-matching** clipboard has something in
it — the Graph clipboard, the TimelineClips clipboard and the PianoRoll's note clipboard are three
entirely separate stores, so copying modules does not make Paste live on the clip lanes or the
roll, or vice versa. Cut shares Copy's enablement predicate on every surface (a cut is a copy that
also deletes, so anything copyable is cuttable). Repeat is active whenever the acting surface has a
selection (`hasClipSelection()` / `hasNoteSelection()`) and — uniquely among these verbs — is
**always inactive on Graph**, regardless of selection.

`Cmd+Shift+A` (`selectAllModules` — the actionId and `AppCommands` name are frozen so a persisted
user binding keeps resolving, even though the verb widened) is routed by the same
`resolveEditSurface()`: it selects every clip (`TimelinePanelComponent::selectAllClips()`) on
TimelineClips, every note in the open clip (`PianoRollComponent::selectAllNotes()`) on PianoRoll,
and every module (`GraphEditor::selectAllModules()`) on Graph. Unlike the clipboard verbs it is
**always active** on every surface — it needs no pre-existing selection, and each surface's own
`selectAll*` just returns `false` harmlessly (no status-bar lie) when there is nothing to select.

## Context-specific

| Shortcut | Context | Action |
|----------|---------|--------|
| Escape | AI panel, request in flight | Cancel the in-flight AI request (same as the Cancel button — actually aborts it, see [`AI_Engine.md`](AI_Engine.md#request-cancellation)) |
| Escape | Canvas, modules selected | Clear the selection |
| Delete / Backspace | Canvas, modules selected | Delete every selected module (one undo step) |
| Escape | Clip lanes, clips selected | Clear the clip selection |
| Delete / Backspace | Clip lanes, clips selected | Delete every selected clip (one undo step) |
| P | Clip lanes, clips selected | Loop the selection: sets the transport loop to the selected clips' `[min start, max end]` span and switches looping on |
| P | Timeline panel (any focus target inside it) | Same loop-the-selection; with the piano roll open the "selection" is the edited clip. Whether P also ARMS looping is Settings → Preferences → "Timeline: P (loop selection) also switches looping on" (default on; off = locators only) |
| L | Timeline panel (any focus target inside it) | Toggle looping on/off, keeping the existing loop bounds (the transport bar's loop button) |
| Q | Timeline panel (any focus target inside it) | Toggle snap (grid magnetism) on/off — the chosen division survives underneath. Also a lit "Q" toggle button next to the snap selector in the transport bar |
| Shift+Q | Piano roll | One-shot quantise: snap the selected notes (or all notes when nothing is selected) to the chosen grid, even while snap is toggled off |
| Escape | Piano roll, notes selected | Clear the note selection |
| Escape | Piano roll, nothing selected | Close the roll, back to the clip lanes |
| Delete / Backspace | Piano roll, notes selected | Delete every selected note (one undo step) |
| 1 / 3 / 4 / 5 / 7 / 8 | Timeline panel (any focus target inside it) | Switch the active edit tool: 1 Select, 3 Split, 4 Glue, 5 Erase, 7 Mute, 8 Draw (Cubase's own numbering — see [`layout.md §16`](layout.md)). `TimelinePanelComponent::keyPressed()` matches these BEFORE the Q/L/P fallbacks above, and only when Cmd is not held (a Cmd-modified digit is left for a host/app menu shortcut) |
| ←/→ | Piano roll, notes selected | Nudge the whole selection by one grid division (one snap cell; a sixteenth when snap is off) |
| ↑/↓ | Piano roll, notes selected | Transpose the whole selection by one semitone |
| Shift+↑/↓ | Piano roll, notes selected | Transpose the whole selection by one octave (12 semitones) |

Arrow keys and the tool digits are deliberately split across two components with opposite rules:
`PianoRollComponent::keyPressed()` consumes an arrow **only when something is selected** (an
empty-selection arrow falls through, so it keeps whatever meaning it has elsewhere) and
deliberately does **not** consume the tool digits at all — tool switching belongs to the panel, so
the roll and the panel can never disagree about which tool is active. The whole selection nudges/
transposes by ONE shared delta (never per-note), clamped so the group stays inside the clip window
(`[0, clipLength)`) or the pitch range (`[0, 127]`) as a unit — the same "clamp the group together"
rule `TimelineClipLaneArea`'s cross-track move drag uses (see `layout.md §16`).

2 (Range Selection), 6 (Zoom) and 9 (Play/Scrub) are Cubase tools this app doesn't ship yet and are
**deliberately left unassigned** — `editToolForKeyChar` (`Source/UI/EditTool.h`) returns `nullopt`
for them, so the panel leaves those digits unconsumed rather than remapping the six shipped tools
onto 1–6. Shipping one of the missing three later costs no rebind: the digit is already reserved
for exactly that tool. None of 1/3/4/5/7/8 (nor 2/6/9) are `ShortcutManager` bindings — like
Delete/Escape/P/L/Q above, they are hardcoded in `keyPressed()` rather than the app-wide table, for
the identical reason: an app-wide binding fires from whichever panel doesn't consume the key first,
and a numeric tool switch has to be scoped to the timeline panel specifically.

Escape is handled by `AIChatComponent::keyPressed()` and only acts while a request is in flight;
otherwise it is passed through so it keeps whatever meaning the enclosing window gives it. It is not
user-rebindable — it is a panel-local binding, not part of the `ShortcutManager` table.

The canvas selection keys are handled by `GraphEditor::keyPressed()`, the clip-lane ones (including
`P`) by `TimelineClipLaneArea::keyPressed()`, the piano-roll ones by
`PianoRollComponent::keyPressed()`, and the panel-wide `Q`/`L`/`P` fallbacks by
`TimelinePanelComponent::keyPressed()` (they fire when the focused child did not consume the key —
JUCE bubbles unhandled keys up the parent chain)
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
| Double-click a connected jack | Disconnect every cable on that port (on by default; `Settings → Preferences`) |

Right-clicking empty canvas keeps the selection rather than clearing it, so the menu can still act
on what is selected. "Paste Here" drops the group at the click point and re-anchors the paste
cascade there, so a following `Cmd+V` continues from the same place.
