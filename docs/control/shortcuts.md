# Keyboard Shortcuts

Shortcuts are configurable in **Settings → Keyboard Shortcuts** (`Source/UI/Settings/ShortcutsSettingsTab.h/.cpp`).
`ShortcutManager` (`Source/ShortcutManager/ShortcutManager.h`) registers **127 actions** across six categories —
**General** (55, app-wide or routed per focused editor), **Graph** (15), **Timeline** (34),
**Piano Roll** (14), **Mixer** (5) and **Layout Editor** (4) — every one of them rebindable, including keys that used to be hardcoded:
nudge/transpose/octave, note navigation, quantise, the snap toggle, the loop keys and the seven tool
digits. Click a row's binding button to rebind it (button turns orange, "Press a key…"); pressing
any key except Escape commits it, swapping with whatever action in the **same category** already
held that key. The tab groups rows into one collapsible section per category with a search box
above them (matches against both the action's description and its current binding text — "cmd"
finds every Cmd shortcut, "transpose" finds the piano-roll block) and a top strip that flips between
"COLLAPSE ALL"/"EXPAND ALL"; see [`layout/module-library.md`](../layout/module-library.md#collapsible-sections) for
the shared collapsible-list pattern it mirrors. The whole tab works from the keyboard: Tab visits the search box, the strip, each
section header and each row's binding button (named "<Action> shortcut"), and while a row is listening
that button keeps focus and takes every key, Space and Return included. Export/import round-trip every binding as JSON;
Reset restores the defaults below. A native macOS menu bar (File + Edit) provides Undo/Redo via
`ApplicationCommandManager`.

Not every action reaches the settings tab's rows the same way it reaches a keypress — see
[**Command vs surface actions**](#command-vs-surface-actions) below for the split that matters most
when reasoning about a key that "does nothing."

## General

| Shortcut | Action |
|----------|--------|
| Cmd+, | Open Settings |
| Cmd+N | New Patch (clear canvas) |
| Cmd+S | Save Project — writes a project bundle (`.agsproj`, graph + timeline) and silently resaves to the remembered bundle on every subsequent press; prompts for a location only on the first save or when no bundle is open (see [`architecture/project-bundle.md`](../architecture/project-bundle.md#opening-and-saving-one-from-the-app)) |
| Cmd+Shift+S | Save Project As — always prompts for a new `.agsproj` location |
| Cmd+Shift+E | Export Audio — opens the Export Audio dialog (bounce the arrangement or the current loop range to WAV/AIFF/FLAC/MP3, see [`architecture/audio-engine.md`](../architecture/audio-engine.md#bounceexport)). Greyed out while a bounce is already running |
| (menu only) | Export Stems — opens the same dialog in its stems mode, rendering each mixer channel to its own file in a folder (see [`architecture/audio-engine.md`](../architecture/audio-engine.md#stem-export)). A menu-only `AppCommands::exportStems` (File menu, immediately after Export Audio), with no default shortcut, like `openPreset`. Also greyed out while a render is already running |
| (menu only) | Collect & Archive... — copies every sample and wavetable the project uses from elsewhere on disk into the project folder and saves, optionally zipping the project for sending (see [`architecture/app-wiring.md`](../architecture/app-wiring.md#collect--archive)). A menu-only `AppCommands::collectAndArchive` (File menu, after Export MIDI), with no default shortcut. Greyed out while a collect, zip or render is running |
| Cmd+Shift+P | Export Patch Only — saves just the patch (a legacy `.json` via `GraphEditor::savePreset`) without the timeline or bundle, never touching the window title. Rebindable |
| Cmd+O | Open Project - a `.agsproj` bundle (patch + timeline). This was split from the former combined "Load from file..." chooser; it took Cmd+O from the old combined open, which is now the menu-only "Open Patch" |
| (menu only) | Open Patch - a plain `.json` preset (graph only). A menu-only `AppCommands::openPreset` (the Load icon's **Patches** submenu and the top-bar **File** menu), with no default shortcut, like `checkForUpdates` |
| (menu only) | Contribute to Agent Synth... — **Help** menu (macOS and Windows; the app has no menu on the plugin path). `AppCommands::contribute` opens `branding::kContributeUrl` (agentsynth.app/contribute) in the default browser: no dialog, no startup prompt, no analytics event, no shortcut. Always enabled; a test replaces the browser launch via `MainComponent::setUrlOpenerForTest`. |
| Cmd+Z | Undo — the project history, or the controller edit history while the Controllers panel holds focus (see [`midi-remote.md`](midi-remote.md#undo)) |
| Cmd+Shift+Z | Redo — routed the same way as Undo |
| Cmd+M (macOS) / Ctrl+Alt+M (Windows, Linux) | Toggle Mod Matrix — off the Mac Cmd is Ctrl, and Ctrl+M belongs to Toggle Metronome |
| Cmd+K | Toggle Minimap |
| Ctrl+A (macOS) / Cmd+Shift+A (elsewhere) | Toggle AI Panel — moved off Cmd+A so Select All could take the platform-standard chord. One of the very few per-platform defaults: on macOS Ctrl is a real separate modifier, on Windows/Linux JUCE's Cmd IS Ctrl so Ctrl+A would collide with Select All |
| Cmd+B | Toggle Module Library |
| Cmd+T | Toggle Bottom Panel (`toggleBottomPanel`) — the ONE show/hide toggle for the whole bottom-docked panel; reopens on whichever tab was last active. A press that opens the pane leaves keyboard focus on its tab strip; one that closes it while focus was inside moves focus to the canvas. See [`timeline/timeline.md`](../timeline/timeline.md#docking-toggle-and-the-bottom-dock) |
| Cmd+1 / Cmd+2 / Cmd+3 | Show Timeline / Mixer / Controllers Tab (`toggleTimelinePanel`/`toggleMixerPanel`/`toggleMidiRemotePanel`) — opens the dock if it's hidden and switches to that tab; a second press is a no-op (only Cmd+T closes the dock), and for a docked tab keyboard focus lands on the tab strip (a detached tab raises its window instead). The three numbers follow the dock's own tab order, which the tab strip's drag-to-reorder changes — see [`docs/mixer/panel.md#placement-and-detachable-windows`](../mixer/panel.md#placement-and-detachable-windows) |
| Cmd+Shift+B | Show/Hide Side Pane (`toggleSidePane`) — toggles the ACTIVE bottom-panel tab's left side pane (the Mixer and the Timeline have one). With the bottom panel hidden it opens the panel and makes sure the pane is open; a no-op when the active tab has no pane. View menu item "Show/Hide Side Pane"; each pane's toggle button shows the key in its Cmd-hold hint and tooltip. See [`layout/side-pane.md`](../layout/side-pane.md) |
| Cmd+A | Select All in Focused Editor (actionId/`AppCommands` name still `selectAllModules` — see "Surface routing" below) |
| Cmd+Opt+S | Save Selection as Snippet |
| Cmd+C | Copy (Selected Modules, or — see "Surface routing" below — the timeline's selected clips/notes) |
| Cmd+V | Paste (Modules, or the copied clips/notes) |
| Cmd+D | Duplicate (Selected Modules, the selected clips/notes, or the focused automation lane: [picks a parameter for the copy](../timeline/automation.md#change-parameter-and-duplicate)) |
| Cmd+X | Cut — Copy then delete, as ONE undo step (Selected Modules, or the timeline's selected clips/notes; see "Surface routing" below) |
| Cmd+R (macOS) / Ctrl+Shift+R (Windows, Linux) | Repeat — off the Mac Cmd is Ctrl, and Ctrl+R belongs to Record. Prompts for a count (1–64) via an `AlertWindow` and creates that many back-to-back copies of the selection, tiled forward one selection-span at a time, as ONE undo step. Timeline-only: inactive on the Graph surface (see below) |
| Space | Play / Stop (toggle the timeline transport) |
| Ctrl+M | Toggle Metronome (`transportToggleMetronome`) on every platform. On macOS a REAL Control, so Cmd+M stays Toggle Mod Matrix; on Windows/Linux Mod Matrix moved to Ctrl+Alt+M — see [**Transport family**](#transport-family) |
| Ctrl+R | Record (`transportRecord`) on every platform. On macOS a REAL Control, so Cmd+R stays Repeat; on Windows/Linux Repeat moved to Ctrl+Shift+R — see [**Transport family**](#transport-family) |
| *(unbound)* | Play / Stop / Toggle Looping / Return to Start / Move Cursor Back or Forward (Beat, Bar) / Jump to Loop Start or End / Jump to Next or Previous Marker — see [**Transport family**](#transport-family) below |
| *(unbound)* | Select Next / Previous Module, Select Next / Previous Track — see [**Selection stepping**](#selection-stepping) below |
| Cmd+= | Zoom In (routed per focused surface — see [**Zoom**](#zoom) below) |
| Cmd+- | Zoom Out |
| Cmd+Shift+= | Zoom In Vertically |
| Cmd+Shift+- | Zoom Out Vertically |
| Tab / Shift+Tab | Focus Next / Previous Region — cycles keyboard focus between whichever of the app's focus regions are currently OPEN (Toolbar, Library, Canvas, Dock tabs, Timeline, Timeline routing pane, piano-roll scale pane, Mixer, Controllers, AI Panel, Mod Matrix); wraps at both ends. See [**Focus regions**](#focus-regions) below |
| Cmd+Shift+T | Focus Timeline — opens the Timeline panel first if it's closed, then focuses it |
| Cmd+Shift+L | Focus Library — opens the Module Library sidebar first if it's closed, then focuses it (lands on the sidebar container, not the search field — see Cmd+F below) |
| Shift+F10 | Open Context Menu (`openContextMenu`) — opens the right-click menu of whatever holds keyboard focus, anchored at that item. See [**Open Context Menu**](#open-context-menu) below. Rebindable |
| Cmd+F | Focus Library Search — opens the Module Library first if it's closed, then focuses its search field specifically. See [**Library keyboard navigation**](#library-keyboard-navigation) below |

Cmd+T (now `toggleBottomPanel`) and Space are always active (see
[`timeline/timeline.md`](../timeline/timeline.md#always-compiled-never-gated)). The grid
and zoom commands below are inactive whenever the panel itself isn't open (`isBottomDockVisible`),
the same as any other timeline-only command.

`Cmd+A` is the platform-standard Select All (the way Cubase and every text field read it), so it
owns the bare chord and the AI panel sits on a REAL `Ctrl+A` on macOS (Ctrl is a distinct physical
modifier there) and on `Cmd+Shift+A` everywhere else: on Windows/Linux JUCE's `commandModifier` IS
Ctrl, so a Ctrl+A default there would be the same chord as Select All and the two commands would
collide. Like every row above, all of these are rebindable in
Settings — and note that a machine which already persisted the old bindings keeps them until
"Reset to Defaults" (bindings are stored per actionId, defaults only fill the gaps).

FRO333: Cmd+1/2/3 replaced the Timeline tab's old bare `Cmd+T` and the Mixer tab's old
`Cmd+Alt+M` (a saved install migrates the old Cmd+T binding onto the new toggleBottomPanel action,
one-shot, the first time it loads its settings — `ShortcutManager::migrateBottomPanelToggleKeys`;
an old `Cmd+Alt+M` for Mixer just carries forward unchanged, since it never collided with anything).
Dragging a tab in the strip to reorder it re-keys Cmd+1/2/3 to match the new order
(`BottomDockComponent::permuteShortcutKeysForNewOrder`) — but only while all three still hold a bare
Cmd+digit as a set; rebinding one of the three away from that convention (in Settings) opts it out of
future re-keying, same guard shape as the Save-As/Save-Snippet chord-swap migration below.

`Cmd+C` / `Cmd+V` (and, by the same reasoning, Space) are safe to claim app-wide because JUCE's
`TextEditor` consumes them itself while it has focus — Cmd+C/V by copying/pasting text, Space by
typing a literal space character — so they only reach `MainComponent::keyPressed` (the sole
dispatch point) when no text field is being edited; the AI chat input keeps its normal copy/paste
and spacebar behaviour. `Cmd+X` reuses the same safety: `x` claims no other binding in this table
and no component's local `keyPressed` hardcodes a bare `x` either (the panel-local letters are
J/L/P, the roll's is Q, the lane area's is P). `Cmd+R` (`r`) is likewise free on both counts. Copy,
Paste, Duplicate and Cut are marked **inactive** when there is nothing to act on (nothing selected /
an empty clipboard on the acting surface — see below), which greys the menu row and makes
`ApplicationCommandTarget::tryToInvoke` refuse the key outright; Repeat is inactive with no
selection AND always inactive on the Graph surface.

### Focus regions

Tab and Shift+Tab move between the open focus regions (Toolbar, Library, Canvas, Dock tabs, Timeline,
routing pane, scale pane, Mixer, Controllers, AI Panel, Mod Matrix). How the regions are registered, nested and outlined, and
what the keys do inside each one, is in [`focus-regions.md`](focus-regions.md).

### Open Context Menu

`openContextMenu` (General, **Shift+F10** by default, rebindable) opens the same menu a right-click on the
keyboard-focused item opens, placed at that item instead of at the mouse. It is a command like the
focus-cycle keys: `MainComponent` performs it, inactive while the launch overlay is up front, and a
detached panel window resolves the same bound key itself (`isOpenContextMenuKeyPress`), because
`MainComponent::keyPressed` never sees keys typed in another top-level window.

On a track row, an automation lane header and a modulator row, a bare Return opens that same menu too (those rows
have no "..." button), and their tooltips name Right-click, Shift+F10 and Return. The Cmd-hold hint bubbles only
label static buttons, so these rows carry the key in their tooltips instead.

The action starts from the focused component and walks up its parents to the first one that implements
`KeyboardContextMenuProvider` (`Source/UI/Layout/KeyboardContextMenu.h`). That provider's answer is
final: a provider with nothing to open a menu for returns false, the key stays unhandled, and no outer
provider is tried. Each provider calls its surface's existing right-click menu builder (the mouse path
and the key share one function), never a second copy of the menu:

| Focus is in | Menu opened |
|-------------|-------------|
| Timeline track header row (or its name label / chip) | The track's menu (Make Channel, Save Track as Preset, Delete Track), at the row |
| Timeline clip lane | The selected clip's menu (the first one when several are selected), at the clip; "Split at pointer" acts at the clip's middle |
| Mixer panel | The focused column's header menu (Pin left / Pin right / Unpin), at the column. Nothing with no column focused |
| Graph canvas | The selected card's menu when exactly one module is selected, at the card; otherwise the canvas menu at the middle of the view. Nothing while focus is in the Mod Matrix |
| Module library | The keyboard-focused row's menu, at the row. Only snippet rows have one (Delete Snippet) |
| MIDI Remote panel | The selected controller's menu (Rename, Export, Delete, feedback output), at its row in the Controllers list |

A new surface opts in by implementing the provider on the component that is, or contains, the focused
one; no per-surface key handling is needed.

### Library keyboard navigation

Arrow-key navigation WITHIN the Library region builds on top of the focus-region framework, plus a
new direct-focus shortcut for the search field specifically (`Cmd+F`, distinct from `Cmd+Shift+L`'s
region-root destination). `ModuleLibraryComponent` had zero keyboard handling before this — it is a
hand-rolled, not-a-`Viewport` component (drag-and-drop constraint, see
[`layout/module-library.md`](../layout/module-library.md)), so this is new keyboard subsystem
work, not a rewire of something that already listened for keys.

- **Two different focus destinations feed the same navigation** — `Cmd+Shift+L` / Tab-cycling land
  real keyboard focus on `ModuleLibraryComponent` itself; `Cmd+F` lands it on the search field (a
  child `juce::TextEditor`). Both reach the same Up/Down/Left/Right/Enter handling: the component's
  own `keyPressed()` override for the first case, a `juce::KeyListener` registered on the search
  field for the second — required because a single-line `TextEditor` unconditionally consumes
  Up/Down/Return itself (`moveCaretUp`/`moveCaretDown` collapse to
  `moveCaretToStartOfLine`/`EndOfLine` for a single-line editor, and JUCE's own
  `moveCaretWithTransaction` always returns `true`), so they never bubble out on their own.
  `ComponentPeer::handleKeyPress` runs a component's key LISTENERS before its own `keyPressed`,
  which is what makes interception possible ahead of the editor.
- **Up/Down walk every visible navigable row** — draggable rows (Module/Snippet/Plugin), the Action
  row ("Scan for plugins..."), AND Header/SubHeader rows (so Left/Right below has something to
  fold), skipping only the non-interactive `EmptyHint` placeholder. Clamped at both ends, no
  wraparound. Starting with nothing focused, Down lands on the first row and Up on the last — typing
  a query in the search field and pressing Down immediately starts browsing the results.
- **Left/Right fold/expand a focused (Sub)Header** via the exact same `setSectionCollapsed()` a
  mouse click on its chevron already calls. LOCKED decision: a no-op on any other focused row kind
  ("a focused child row"), not a bubble — and, from the search field specifically, Left/Right are
  never intercepted at all (they keep moving the text caret through the typed query; folding a
  section as a side effect of editing text would be a surprising behaviour).
- **Enter-to-insert is genuinely new behaviour**, not a rewire — Module and Snippet rows had NO
  click-to-add path before this (`mouseDown` starts a drag immediately for them), so Enter on a
  keyboard-focused row is the first way to add one without dragging. Fires
  `onModuleActivated`/`onSnippetActivated` (new callbacks paralleling `onPluginActivated`); the
  Action and Plugin rows keep firing their existing callbacks via the same `activateRow()` the mouse
  path already used. A disabled (already-in-patch singleton) Module row inserts nothing.
- **Tab is deliberately left alone everywhere** — `TextEditor` already never consumes it
  (`tabKeyUsed` defaults `false`, no key-function table entry claims it), so it keeps bubbling to
  `MainComponent`'s `focusNextRegion` cycle with no special-case code required; the risk this task's
  own notes flagged ("Tab needs the same explicit allowance [as Escape/Return]") turned out to be
  the INVERSE — the new Up/Down/Enter interception is what needed care not to also catch Tab.
- **Scrolling is manual, unlike the track-header/mixer-column focus work** — the sidebar is hand-scrolled (`scrollOffset` + a
  `juce::ScrollBar`), not a real `juce::Viewport`, so arrow navigation calls
  `scrollKeyboardFocusIntoView()` itself rather than getting auto-scroll for free.
- **Keyboard focus is clamped like hover, plus one more check** — `keyboardFocusedIndex` is
  re-validated at every site that already clamps `hoveredIndex` (a snippet save, a plugin scan, a
  collapse animation finishing, a search-query change), AND checked against the row's *kind* at that
  index, not just its visibility — a shrinking entry list (deleting a snippet) can leave a different,
  non-navigable row (the "No snippets yet" `EmptyHint`) occupying the same numeric index.
- **Visual** — a focused row gets a solid 1px accent-coloured outline, distinct from the existing
  translucent hover fill, so a mouse hover and a keyboard focus on different rows never read as the
  same state.

### Surface routing: who Cmd+C/V/D/X/R and Cmd+A act on

Cmd+C/V/D/X/R and Cmd+A are global commands (`MainComponent::getAllCommands`/
`getCommandInfo`/`perform`), but "the canvas" is not the only editable surface once the timeline
panel or the mixer is open — the clip lanes, the piano roll and the mixer are too.
`MainComponent::resolveEditSurface()` is the **one** focus-ownership rule that decides which
surface's clipboard and selection these verbs act on:

```cpp
enum class EditSurface { Graph, TimelineClips, PianoRoll, Mixer, AutomationLane };
```

- **TimelineClips** — the timeline panel is visible AND real keyboard focus
  (`juce::Component::getCurrentlyFocusedComponent()`) sits inside the clip-lane area.
- **PianoRoll** — same, but focus sits inside the piano roll.
- **AutomationLane** — same, but focus sits on an automation lane's editor or header: Select All takes every point of the
  lane, Copy/Cut/Paste move points ([timeline/automation.md](../timeline/automation.md#selecting-points)),
  Duplicate copies the lane below itself once a parameter is picked
  ([timeline/automation.md](../timeline/automation.md#change-parameter-and-duplicate)), Repeat is inactive, and zoom is
  the timeline's.
- **Mixer** — the mixer panel is actually showing (docked-and-active on the tab strip, an "Own
  panel" strip, or detached into its own window — `BottomDockComponent::isMixerShowing()` /
  `MixerPlacementController::isOwnPanelShowing()`) AND real keyboard focus sits inside
  `MixerPanelComponent` (FRO18: the mixer's single focusable leaf — every column control is
  `setWantsKeyboardFocus(false)`, so a column's own controls resolve here too).
- **Graph** — every other case (including every one of the panels above being hidden entirely,
  regardless of what a stale focus pointer might point at).

Every one of those surfaces already grabs keyboard focus on `mouseDown` (`GraphEditor::mouseDown`
is the original of this idiom; `TimelineClipLaneArea`, `PianoRollComponent` and
`AutomationLaneEditor` all copy it) or on construction (`MixerPanelComponent` wants keyboard focus
outright), so "the surface you last clicked/focused owns the verbs" falls out of ordinary JUCE
focus tracking — `resolveEditSurface()` adds no bookkeeping of its own beyond reading
`getCurrentlyFocusedComponent()`. Headless tests can't always create a real focus grab (it needs a
native peer), so `MainComponent::setEditSurfaceOverrideForTest()` short-circuits the resolver for
`Tests/App/FocusArbitration/FocusArbitrationTests.cpp`.

What each surface does:

| Surface | Copy | Paste | Duplicate | Cut | Repeat |
|---|---|---|---|---|---|
| Graph | Copies the selected modules (unchanged) | Pastes at the next cascade position | Copies the selection one step down-right, clipboard untouched | Composed from Copy + Delete (`copySelection()` then `deleteSelection()`), one undo step | **Inactive** — a spatial canvas has no time axis to tile copies along; Duplicate is the graph's equivalent gesture (see below) |
| TimelineClips | Serialises the selected clips — notes (each with its own muted flag), name, length, muted flag and every audio field (`assetRef`, gain, both fades, `sourceStartSeconds`) — into the panel's own clip clipboard, starts relative to the earliest selected clip. With a live **Range** tool range it copies the range instead: each clip's part inside it (`TimelineDoc::clipToRange`), starts relative to the range start — see [clips](../timeline/clips.md#range-tool) | Re-inserts every clipboard clip onto **its original track**, re-based so the earliest clip lands at the transport's current position (snapped to the view-state's snap setting); the track fallback is **kind-aware** — a clip lands back only on a track that still plays its payload (audio → `TrackKind::Audio`, MIDI → `TrackKind::Midi`), else the doc's first track of the required kind, else the clip is skipped. Audio fields go back through `setClipAsset`/`setClipGainDb`/`setClipFades` (never a raw struct write), so a clipboard `assetRef` is re-validated exactly like a freshly-loaded file's — a clipboard is only as trustworthy as whatever filled it. One undo step for the whole paste; the pasted clips end up selected. | `repeatSelectedClips(1)`: one copy of the whole selection, starting where the selection ends (so adjacent clips never overlap their copies), one undo step; the new clips end up selected | `TimelinePanelComponent::cutSelectedClips()` — copy, then delete the selection, as ONE `recordTimelineChange` (so undo restores it in a single step); with a live range, `cutRange()` — the range copy, then the lanes' own range Delete, also one step | `repeatSelectedClips(count)` — `count` back-to-back copies of the selection's own span (`max end - min start`, not each clip's own length, so a multi-clip rhythm tiles intact), the first starting one span-length after the selection's start. One undo step; every created clip ends up selected. |
| PianoRoll | Copies the selected notes (each field, `muted` included) into the roll's OWN note clipboard, offsets stored relative to the earliest selected note — the clipboard is a member of the roll, so it survives switching clips (`openClip`), and a block copied in one clip pastes into another | `pasteNotesAtPlayhead()` anchors the block at the **snapped, clip-relative playhead position** when that lands inside `[0, clip length)`, else at 0.0; `MainComponent::perform` primes the playhead from the live transport (`setPlayheadBeat(transport.getPositionSnapshot().ppq)`) immediately before pasting, so a paste with the transport stopped still lands under the position the user can see rather than wherever a stale internal beat was left. Notes at/after the clip's end are skipped, an overrunning note's length is clamped to the clip's end. One undo step; the pasted notes end up selected. | Copies the selection to immediately after its own span (same pitches), one undo step, selects the copies — does NOT touch the clipboard (duplicating isn't copying, and silently stomping a clipboard the user filled deliberately would be a surprise) | `cutSelectedNotes()` — copy then delete, one undo step (fills the clipboard first, so a cut is always paste-able) | `repeatSelectedNotes(count)` — `count` copies of the selection block, each one span further along, **clipped at the clip's end**: placement stops at the first block that would fall entirely outside the clip rather than piling every remaining copy onto the last beat. One undo step; every created note ends up selected. |
| Mixer | **Inactive** — the mixer has no clipboard model of its own; its own keyboard verbs (Left/Right column walk, Up/Down fader nudge, Enter select-on-canvas, M/S/R) are resolved directly by `MixerPanelComponent::keyPressed`, not routed through `resolveEditSurface()` | **Inactive** | **Inactive** | **Inactive** | **Inactive** — same "no time axis to tile along" reasoning as Graph, with no equivalent gesture at all |

`getCommandInfo` marks Paste active only when the **surface-matching** clipboard has something in
it — the Graph clipboard, the TimelineClips clipboard and the PianoRoll's note clipboard are three
entirely separate stores, so copying modules does not make Paste live on the clip lanes or the
roll, or vice versa. Cut shares Copy's enablement predicate on every surface (a cut is a copy that
also deletes, so anything copyable is cuttable). Repeat is active whenever the acting surface has a
selection (`hasClipSelection()` / `hasNoteSelection()`) and — uniquely among these verbs — is
**always inactive on Graph**, regardless of selection (Mixer is unconditionally inactive here too,
same as every other clipboard verb).

`Cmd+A` (`selectAllModules` — the actionId and `AppCommands` name are frozen so a persisted
user binding keeps resolving, even though the verb widened) is routed by the same
`resolveEditSurface()`: it selects every clip (`TimelinePanelComponent::selectAllClips()`) on
TimelineClips, every note in the open clip (`PianoRollComponent::selectAllNotes()`) on PianoRoll,
and every module (`GraphEditor::selectAllModules()`) on Graph. Unlike the clipboard verbs it is
**always active** on every surface — it needs no pre-existing selection, and each surface's own
`selectAll*` just returns `false` harmlessly (no status-bar lie) when there is nothing to select.
On Mixer it stays active (there is no per-surface enablement gap to fill) but is a deliberate no-op
— there is no multi-column selection model to select all of, so it reports "nothing to select"
rather than falling through to the graph's own Select All.

### Zoom

Cmd+=/Cmd+- is the platform's own zoom accelerator (every browser, every editor); Cmd+Shift+=/-
is the vertical axis, mirroring the modifier the mouse wheel already uses (Cmd+wheel = horizontal,
Cmd+Shift+wheel = vertical — see [`timeline/view.md`](../timeline/view.md#wheel-and-trackpad-bindings)), so the keyboard and the wheel teach
the same shape. All four are `AppCommands`/`ApplicationCommandManager` commands (unlike the
Timeline/PianoRoll surface keys below) and route by the SAME `resolveEditSurface()` the clipboard
verbs use, in/out factor `1.25` / `1 / 1.25` (`MainComponent::kZoomInFactor`/`kZoomOutFactor`):

| Surface | Horizontal (Cmd+=/-) | Vertical (Cmd+Shift+=/-) |
|---|---|---|
| Graph | `GraphEditor::zoomAroundCentre` — the canvas' one zoom level | **Inactive** — the canvas zooms uniformly (one `zoomLevel`, no separate axes), so a second key that did the same thing under a different modifier would be a trap, not a feature |
| TimelineClips | `TimelinePanelComponent::zoomTimelineHorizontal` — `TimelineViewState::pixelsPerBeat`, anchored at the visible centre | `zoomTimelineVertical` — `TimelineViewState::rowHeightScale` (track row height), anchored at the visible centre |
| PianoRoll | `PianoRollComponent::zoomHorizontal` — the roll's OWN `pixelsPerBeat` (never the shared `TimelineViewState` — see [`timeline/piano-roll.md`](../timeline/piano-roll.md#horizontal-mapping)) | `zoomVertical` — `pixelsPerSemitone_` |
| Mixer | **Inactive** — the column strip has a fixed layout, no zoom concept at all | **Inactive** |

Each keypress reports its own status-bar message ("Canvas: zoom", "Timeline: zoom" / "Timeline:
track height", "Piano roll: zoom" / "Piano roll: vertical zoom") so a held key's effect is visible
even with no mouse involved.

### Transport family

Every transport verb is promoted to a command-dispatched `AppCommands` action — the
[`midi-remote.md`](midi-remote.md#action-targets) prerequisite for a controller hardware button to trigger
one via `ApplicationCommandManager::invokeDirectly`. All of them are **General**, and ship **unbound**
by default (no default keypress) — they exist first as command/MIDI-Remote targets, and a user may
still bind one from Settings like any other action. The two exceptions are **Record** and **Toggle
Metronome**: they default to a literal **Ctrl+R** and **Ctrl+M** on every platform (Option+letter types
characters in text fields on the Mac, and Alt+letter is menu-mnemonic territory on Windows). On macOS Control is
a distinct physical key, so Cmd+R Repeat and Cmd+M Toggle Mod Matrix are untouched; on Windows/Linux, where Cmd
is Ctrl, those two moved to Ctrl+Shift+R and Ctrl+Alt+M to free the chords. Holding Ctrl (Cmd on Windows/Linux)
labels both transport buttons with their keys. An install that saved its settings before the chords existed holds
both as unbound (and, off the Mac, holds Ctrl+R / Ctrl+M on Repeat and the matrix), so a one-shot migration
(`ShortcutManager::migrateTransportCtrlChords`) moves Repeat / the matrix off the old chords when they still hold
them, gives Record / Metronome the new chords unless another action already uses one, and leaves any key the
user chose alone. `ShortcutManager::setDefaultsPlatform` builds the other platform's table, which is how the
Windows/Linux defaults are tested from a Mac host:

| Action id | Display name | Behaviour |
|---|---|---|
| `transportPlay` | Play | `TransportService::play()` if not already playing (a direction, not a toggle) |
| `transportStop` | Stop | `TransportService::stop()` if currently playing |
| `transportTogglePlayStop` | Play / Stop | **Alias, not a new action** — `AppCommands::getCommandForAction` resolves it straight to the existing `togglePlayback` command id. It is not a registered/rebindable id of its own and has no row in Settings; `togglePlayback` keeps its own Space binding untouched (see "Why the locator jumps are on plain Option+digit" above for why a default never migrates a persisted key, and why this alias is a lookup rather than a rename) |
| `transportToggleLoop` | Toggle Looping | Triggers the transport bar's own loop button — the exact `setLoop(start, end, !looping)` verb the surface-resolved `timelineToggleLoop` key already performs, which keeps working unchanged |
| `transportRecord` | Record | Triggers the transport bar's own Record button, so it reaches `MainComponent::handleRecordToggle`'s armed-track gate exactly as a mouse click would — never bypassed |
| `transportToggleMetronome` | Toggle Metronome | Triggers the transport bar's own metronome button, so its persisted `timelineMetronomeEnabled` state stays authoritative |
| `transportReturnToStart` | Return to Start | `TransportService::locateBeat(0)` — relocates only, does not stop. The transport bar's Return to Start button runs this same command ([`transport.md`](../timeline/transport.md#return-to-start-button)) |
| `transportNudgeBackBeat` / `transportNudgeForwardBeat` | Move Cursor Back (Beat) / Move Cursor Forward (Beat) | Relocates one beat (a quarter note) back or forward from the current position, clamped at beat 0. Works playing or stopped; never starts or stops the transport |
| `transportNudgeBackBar` / `transportNudgeForwardBar` | Move Cursor Back (Bar) / Move Cursor Forward (Bar) | As above, by one bar of the current time signature (`numerator × 4 / denominator` beats — 4 in 4/4, 3 in 3/4, 3 in 6/8) |
| `transportJumpToLoopStart` / `transportJumpToLoopEnd` | Jump to Loop Start / Jump to Loop End | Relocates to the left / right loop locator. A no-op when the locators span no range (end at or before start) |
| `transportJumpToNextMarker` / `transportJumpToPreviousMarker` | Jump to Next Marker / Jump to Previous Marker | Relocates to the nearest `TimelineDoc` marker strictly ahead of / behind the current position. A no-op past the last marker (next) or before the first (previous) — it never wraps |

The label "Play / Stop" is shared by `togglePlayback` and its alias so the toggle sits next to
"Play" and "Stop" in the Controllers action picker. Cursor moves posted faster than the audio thread
applies them (a jog wheel, key repeat) accumulate — each builds on the previous request rather than
on the once-per-block position snapshot, so no step is lost (`Source/Transport/TransportNudge.h`).
The marker jumps share that same accumulate state (`Source/Transport/MarkerJump.h`), so pressing
next/previous twice in quick succession steps two markers rather than losing the first press.

See [`timeline/transport.md`](../timeline/transport.md) for the transport bar itself.

### Selection stepping

Four more command-dispatched General actions, unbound by default, so a controller pad or a key can
walk the selection instead of the mouse:

| Action id | Display name | Behaviour |
|---|---|---|
| `selectNextModule` / `selectPreviousModule` | Select Next Module / Select Previous Module | `GraphEditor::selectAdjacentModule`: selects one module, stepping left to right across the canvas (centre x, then y; `Source/UI/Graph/ModuleStepOrder.h`). Nothing selected starts at the first (next) or last (previous) module; a multi-selection steps from its last (next) or first (previous) member. Clamps at the ends, never wraps. Pans the canvas only when the new module is not already fully on screen. Cards hidden inside a collapsed macro are skipped |
| `selectNextTrack` / `selectPreviousTrack` | Select Next Track / Select Previous Track | `TimelinePanelComponent::selectAdjacentTrack`: moves track-header focus one row, the same step the header's own Up/Down keys take, so the bare `M`/`S`/`R` keys then act on that track. Nothing focused starts at the first row; clamps at the ends. Inactive while the timeline panel is closed |

These are two explicit pairs rather than one pair routed by `resolveEditSurface()` like Select All
and Zoom. A controller has no notion of "where the last click was", and moving track focus itself
changes which surface `resolveEditSurface()` reports (a track header is neither the clip lane nor
the piano roll), so a routed pair would flip from tracks to modules between two presses of the same
pad.

### Switching tabs

Every tab strip is one Tab stop; with it focused, plain Left / Right step to the previous / next tab (stopping at
the ends, no wrap) and Home / End jump to the first / last, switching at once. Return (and Space, in Settings) or Tab
moves into the open tab and Shift+Tab comes back to the strip ([rule](../development/accessibility.md#tab-strips)). These
keys belong to the focused strip and are not actions. The former Cmd+Opt+Left / Right `tabPrevious` / `tabNext`
actions are gone; a saved shortcut or MIDI mapping that still names them is ignored on load.

- **Bottom dock**: Cmd+1/2/3 show a tab and Cmd+T opens the pane, and both leave focus on the strip
  ([details](../layout/chrome.md#tab-strip-keyboard-and-screen-reader-access)).
- **Settings window**: Cmd+1..9 opens the Nth tab, a fixed key, not an action (a text field passes it up). Holding
  Cmd over the window shows each tab's `Cmd+N` as a hint badge ([hints](#shortcut-hints)).

### Editing keys in text fields

In every text field, Cmd+Backspace deletes everything left of the caret back to the start of the line (just the selection when
one is highlighted; on macOS only), and Option+Backspace (Ctrl+Backspace on Windows and Linux) deletes the word before the caret,
which `juce::TextEditor` already does. Read-only fields ignore both. `synth::ui::TextFieldKeys` (`Source/UI/Layout/`) provides the
first: `MainComponent` starts one app-wide instance that listens on any `juce::TextEditor` as it takes focus, so no field needs a
subclass and a field added later is covered without a call. The listener runs before the app's shortcut map, so a shortcut on
Cmd+Backspace never fires while typing.

## Graph

| Shortcut | Action |
|----------|--------|
| Cmd+L | Auto Arrange |
| Cmd+Opt+S | Save Selection as Snippet |
| Cmd+G | Group / Toggle Macro |
| Cmd+Shift+G | Ungroup Macro |
| Cmd+Alt+G | Collapse / Expand Macro (toggle) |
| Cmd+Shift+M | Go to Output (was "Locate Master") — selects Master (falling back to Audio Output when there is no Master yet) and centres the view on the whole output dock (Master, Rec Tap, Audio Output); a graceful no-op with neither. Also on the canvas's right-click menu. See [**Locate Master**](#locate-master) below |

| ← / → / ↑ / ↓ | Select the nearest card in that direction (`canvasSelectCardLeft` / `Right` / `Up` / `Down`). See [**Canvas card keys**](#canvas-card-keys) |
| Alt+← / → / ↑ / ↓ | Move the selected cards one grid step (`canvasMoveCardLeft` / `Right` / `Up` / `Down`), one undo step |
| Return | Enter the selected card: focus goes to its first control (`canvasEnterCard`) |

Besides the card keys, Graph holds only the six command verbs that mean nothing on any other surface — auto-arrange,
save-selection-as-snippet, grouping/ungrouping/collapsing a macro, and locating Master —
everything that means the same thing everywhere (copy/paste/cut/duplicate/repeat/select-all, both
zoom pairs) is General instead, so it can route through `resolveEditSurface()`.

**Cmd+G is smart (`MacroGroupController::groupOrToggleSelectionMacros`)**: a selection of only whole
macros toggles them collapsed/expanded; two or more modules or whole macros at the same level (top
level, or directly inside one open macro) are grouped into a new macro there, nesting included; any
other selection that touches a macro toggles the touched macros and leaves modules outside a macro
alone, reporting that in the status bar. The matrix is in
[menu and membership](../macros/menu-and-membership.md#cmdg). Because Cmd+G now covers two verbs depending on selection state, its label/description
is the single static string "Group / Toggle Macro", same reasoning as Cmd+Alt+G below.

Cmd+Alt+G stays a separate, **always-toggle** binding
(`GraphEditor::toggleSelectionMacrosCollapsed`) even though Cmd+G now does the same thing for a
selection that already touches a macro: it is unambiguous when the user wants to be certain they're
toggling and not grouping, and removing a binding would break anyone's saved keybindings. It
collapses every macro touched by the selection that is currently expanded, or expands them if none
are (a mixed selection spanning a collapsed and an expanded macro collapses both — collapsing
always wins). One command works because the menu/Settings-list label is a single static string,
"Collapse / Expand Macro", that reads right regardless of which way the toggle is about to go —
Ungroup above stays its own command because dissolving a macro is a different precondition and a
genuinely different verb from either grouping or toggling.

### Canvas card keys

With the canvas focused (Tab to the Canvas region, or click empty canvas), the keyboard drives the same
selection the mouse does; the selected card's accent border is its focus ring. All nine are Graph-category
surface actions resolved by `CanvasCardKeyboard::keyPressed` (`Source/UI/Graph/CanvasCardKeyboard/`), not
commands, so they appear in Settings > Keyboard Shortcuts and a rebind takes effect at once.

| Default | Action id | Behaviour |
|---|---|---|
| ← → ↑ ↓ | `canvasSelectCard{Left,Right,Up,Down}` | Selects the nearest visible card whose centre lies on that side, centre to centre, scoring distance along the arrow plus twice the distance across it (`Source/UI/Graph/CardNavigation.h`). Nothing selected picks the first card in [selection-stepping](#selection-stepping) order. No card that way: consumed, nothing changes. Pans the view only when the card is off-screen. A multi-selection moves from its last-added card. Falls through on an empty canvas |
| Alt+← → ↑ ↓ | `canvasMoveCard{Left,Right,Up,Down}` | Moves the selected cards one 8 px grid step through the same commit a mouse drag makes (`finalizeModuleDrag`, or the group's rigid-body `finalizeSelectionDrag`), one undo step. A spot taken by another card resolves to the nearest free slot, as a drop there would. Falls through with nothing selected, and does nothing for a selected collapsed macro (the arrows only ever land on module cards) |
| Return | `canvasEnterCard` | Moves keyboard focus to the selected card's first control. Falls through with nothing selected |

Inside a card: Tab and Shift+Tab walk its controls in reading order (body first, then the header buttons);
Escape, Tab on the last control and Shift+Tab on the first all return focus to the canvas with the card still
selected, so focus never gets stuck in a card (the arrows then move between cards, the next Tab moves to the
next region). No control inside a card swallows Escape or a Cmd chord such as Cmd+Shift+T. A focused knob turns with Up/Right and Down/Left (one
percent of its travel; Shift a tenth of that), Page Up/Down (ten percent) and Home/End (minimum/maximum). Those
keys are the knob's own (`CardKnobSlider::keyPressed`), not actions, and each press is one change gesture, so
one undo step and one automation touch. Shift+F10 still opens the selected card's menu. A key that bubbles up
from a control inside a card or from the Mod Matrix never moves the canvas selection.

### Locate Master

*Now labelled **Go to Output** everywhere (menu row, command, Preferences); the command id and this anchor
are unchanged. It centres on the whole output dock, not the one card
([`docs/layout/layout.md#output-dock`](../layout/layout.md#output-dock)). The rest of this section describes
the original behaviour.*

Founder feedback on a live check: once Master and Audio Output exist (an Audio Output is seeded
on New Patch, [`docs/mixer/mixer.md`](../mixer/mixer.md)), auto-arrange or an ordinary drag can leave either node
anywhere on the canvas, and there was no way to find it short of scrolling around. Cmd+Shift+M
(and the canvas right-click menu's "Locate Master" row) selects Master, falling back to Audio
Output when the patch has no Master yet, and pans the view so it sits centred on screen — a
graceful no-op, with the menu row disabled, when the patch has neither node
(`GraphEditor::locateMasterOrOutput`). The minimap highlight comes for free: it already derives a
node's highlighted state from the current selection, so selecting Master by this route highlights
it there too, with no separate flash/pulse mechanism.

This is the lightweight, canvas-only stopgap the founder asked for — the durable answer is the
future mixer panel ([`docs/mixer/mixer.md`](../mixer/mixer.md)), which does not exist yet. Cmd+Shift+M was reserved for a
future "Mixer-focus" shortcut before this (see the Focus regions section above); locating Master is
that same "find the mix bus" need in its interim, pre-panel form, so it claims the chord now rather
than leaving it idle. Reuses the same select-by-NodeID path `MainComponent::selectNodeInGraph`
already uses for the timeline binding chip (`GraphEditor::selectModule`) plus the same pan
primitive the minimap's own click-to-navigate uses (`GraphEditor::centreViewOn`) — no new
selection or pan mechanism was added.

## Timeline

Two different kinds of binding share this category — see
[**Command vs surface actions**](#command-vs-surface-actions) for why that split exists and what it
means for rebinding.

**Panel keys** (surface-resolved — consulted directly by `TimelinePanelComponent::keyPressed()`,
and, for the loop-selection key, `TimelineClipLaneArea::keyPressed()` too):

| Shortcut | Action |
|----------|--------|
| J | Toggle Snap (grid magnetism) — the chosen division survives underneath, and so do the grid LINES (see below); shared with the piano roll (one binding, `timelineSnapToggle`, whichever surface has focus). Cubase's snap key; **Q** is Cubase's *quantise*, which is what the roll uses it for |
| L | Toggle Looping, keeping the existing bounds — the transport bar's loop button |
| F | Toggle Follow Playhead (`timelineFollowPlayheadToggle`) — mirrors the transport strip's follow button; panel-scoped like J/L/P, so it works whichever timeline surface (lanes or roll) has focus |
| P | Loop the Selection — sets the transport loop to the selected clips' (or, with the roll open, the edited clip's) span. Whether it also arms looping is `Settings → Preferences → "Timeline: P (loop selection) also switches looping on"` (default on; off = locators only) |
| 1 / 3 / 4 / 5 / 7 / 8 | Switch the active edit tool: 1 Select, 3 Split, 4 Glue, 5 Erase, 7 Mute, 8 Draw (Cubase's own numbering — see [`timeline/edit-tools.md`](../timeline/edit-tools.md#numbering)) |
| Shift+1 .. Shift+6 | Pick the Draw tool and a shape: 1 Free, 2 Line, 3 Sine, 4 Triangle, 5 Saw, 6 Square (`timelineShapeFree` .. `timelineShapeSquare`, titled "Free Draw Shape", "Line Shape", ...). With a lane range selected the shape is also stamped over it. Pressing the Draw key again while Draw is the tool steps to the next shape, wrapping ([automation](../timeline/automation.md#draw-shapes-and-the-lane-range)). Stored as Shift plus the digit; `keyPressMatches` maps the `!`..`^` macOS delivers back to the digit. Shift+digit was never bound before, so no saved setting shadows these |
| Delete / Backspace, Esc (with a lane range) | Remove the points inside the automation lane range (one undo step); Esc clears the lane range |
| Cmd+Left / Cmd+Right (hold) | Glide the cursor back / forward, accelerating while held, settling to the grid on release when snap is on (`timelineGlideBack` / `timelineGlideForward`) — [`timeline/transport.md`](../timeline/transport.md#gliding-the-cursor). A tap moves one grid step (one beat with snap off). Neither chord was bound before: the other Timeline arrows are bare or Alt, the grid cycle is Ctrl+Shift |
| Option+1 | Jump to Locator 1 — parks the cursor on the LEFT loop locator (`timelineJumpToLocator1`) |
| Option+2 | Jump to Locator 2 — the RIGHT loop locator (`timelineJumpToLocator2`) |

**Track header focus** — `TimelineTrackHeaderComponent` is now itself a real focusable leaf
(`setWantsKeyboardFocus(true)`, matching the clip lane area/piano roll's own pattern), scoped to TRACK
HEADERS ONLY per the locked decision — the clip/automation lanes are untouched. A click on a row
(anywhere that isn't the name label or a control — see below) or a bare **Down** on the Timeline
region root focuses it; Up/Down then walk sibling rows, clamped at the ends (never wrapping, the same
rule `cycleSnapValue` uses for the grid). The focused row is `TimelinePanelComponent::
focusedTrackIndex_` — ephemeral UI state, deliberately **not** on `TimelineDoc` (it never touches
undo/reconcile/persistence) — and auto-scrolls into view through the SAME `trackScrollY`/`scrollTrackRows`
plumbing the mouse wheel and vertical zoom already use, via `ensureTrackVisible()`.

| Shortcut | Action |
|----------|--------|
| ↑ / ↓ | Move focus to the previous/next track header row; ↓ on the Timeline region root lands on the **+ Track** button first and ↓ again on the first row; ↑ off the first row comes back to **+ Track**, and ↑ there returns to the root. ↓ on the last row also moves on to **+ Track**, and ↑ from it entered that way returns to the last row (not rebindable — arrow-key row navigation isn't a `ShortcutManager` action anywhere else in this app either, see the Library navigation's `ModuleLibraryComponent` precedent) |
| M | Mute Focused Track (`timelineMuteFocusedTrack`) — flips `Track::muted` on whichever row holds focus, through the exact same `performTrackEdit` one-undo-step path the M **button** already used |
| S | Solo Focused Track (`timelineSoloFocusedTrack`) — `Track::soloed`, same path |
| R | Arm Focused Track (`timelineArmFocusedTrack`) — `Track::armed`, same path |
| Return / Space on **+ Track** | Opens the add-track menu, exactly like a click. ↓ on the Timeline region root lands on the button first, with or without tracks. Space is claimed by the panel so it does not toggle playback while the button has focus. Closing a menu opened this way (a pick or Esc) puts focus back on **+ Track** |
| Option+= / Option+- / Option+0 | Increase / Decrease / Reset Track Height (`timelineIncreaseTrackHeight`, `timelineDecreaseTrackHeight`, `timelineResetTrackHeight`) — sizes the focused row only, ×1.25 a step, one undo step each ([tracks](../timeline/tracks.md#one-tracks-height)); no Option+=/-/0 is bound anywhere else |
| A | Show/Hide Track Automation (`timelineToggleTrackAutomation`) — folds the focused row's automation lanes open or closed, like its fold arrow ([automation](../timeline/automation.md#lane-rows)); bare A is free in every category (every other `a` binding carries a modifier) |

#### Settings and dialog arrow keys

The Settings tabs and the Export Audio, Sign in and Configure I/O dialogs also use hard-coded arrow keys,
not `ShortcutManager` actions (same precedent as the Library and track-header rows above): **Up / Down**
move focus to the previous / next control (clamped at the ends), **Right / Left** tick / untick a focused
check box or unfold / fold a focused section header. A combo box, slider or text field keeps its own
arrows, and a Keyboard Shortcuts row that is listening for a new key captures them as the binding. →
[`accessibility.md`](../development/accessibility.md#arrow-keys-in-lists-of-controls)

M/S/R are rebindable, Timeline category, bare-letter defaults matching the J/L/P/F convention — free
on all three (no other binding in this table is a BARE, unmodified m/s/r; every existing use of those
letters carries a modifier). **Naming, deliberately not "Mute"/"Solo"/"Arm":** `timelineToolMute`
(bare **7**) already reads "Mute Tool" in the Settings list — a different key (a digit) so there is no
BINDING collision, but the Settings search matches description text too, so these three are "Mute/
Solo/Arm **Focused Track**" to keep the two rows from reading as the same feature when a search
narrows to "mute". A row's own keyboard-focus outline reuses `paintFocusRegionOutline` verbatim (see
the Focus regions section above) — same colour/alpha/thickness as a whole region root's, just painted
around one row via the row's own `paintOverChildren`.

**Clip keyboard mode** — from a focused track header, **Right** moves keyboard focus into that track's
clips, and the arrow keys then walk them without the mouse. The "keyboard clip" is the single
selected clip (stepping replaces the selection, so every selection-based verb — Delete, Cmd+C, the
Split/Mute/Loop-the-selection keys — applies to it), drawn with an accent ring. All seven keys are
rebindable Timeline-category actions, resolved by `TimelineClipLaneArea::keyPressed` (and, for the
header's Right, `TimelineTrackHeaderComponent::keyPressed`). See
[`timeline/focus.md`](../timeline/focus.md#clip-keyboard-mode).

| Shortcut | Action |
|----------|--------|
| → (on a track header or the Timeline region root) | Next Clip (`timelineClipNext`) — enters that track's clips: the first clip starting at or after the playhead, else the track's first. Does nothing on a track with no clips |
| ← / → (in clip mode) | Previous Clip / Next Clip (`timelineClipPrevious` / `timelineClipNext`) — the neighbour on the same track; the first/last clip stays put |
| ↑ / ↓ | Clip on Track Above / Below (`timelineClipAbove` / `timelineClipBelow`) — the clip nearest in start time on the closest track above/below that has clips (empty tracks are skipped; an exact tie goes to the earlier clip) |
| Return | Open Clip in Editor (`timelineClipOpen`) — the same hook a double-click on the clip fires (the piano roll for a MIDI clip) |
| Alt+← / Alt+→ | Move Clip Earlier / Later by One Grid Step (`timelineClipMoveEarlier` / `timelineClipMoveLater`) — one grid division (the chosen Snap division even with the snap switch off; one beat with Snap Off), one undo step, clamped at beat 0 |
| Cmd+Alt+↑ / Cmd+Alt+↓ | Move Automation Lane Up / Down (`timelineMoveLaneUp` / `timelineMoveLaneDown`) — the lane whose header controls or curve editor hold focus swaps with its neighbour within its track, one undo step ([automation](../timeline/automation.md#reordering-lanes)) |
| Esc | Back to the clip's track header. Fixed, like Delete (not in the action table); the clip stays selected |

Left/Right share `timelineClipNext`/`timelineClipPrevious` between the header and the lane because two
Timeline actions on one default key would read as a binding conflict in Settings. No existing action
covered any of these verbs: `pianoRollNudge*` moves notes inside the roll and `transportNudge*` moves
the playhead, so clip movement is new.

### Mixer column navigation

**Mixer column navigation** — parallel to Track header focus above, but the region ROOT is the focusable leaf
here, not a per-column child: `MixerPanelComponent` is the Mixer region's own root (see **Focus
regions** above), `setWantsKeyboardFocus(true)`, and every child control inside a column (the
fader/pan sliders, the M/S buttons, Direct's "Make channel" button) gives up keyboard focus
(`setWantsKeyboardFocus(false)`), except the small icon buttons that take no key beyond Return and Space (the colour dot,
the sources badge and the per-row bypass buttons, besides the toolbar), so they can never intercept these keys — the same trap
`TimelineTrackHeaderComponent` sidesteps by being the focusable leaf itself, just one level higher
here because a column hosts several controls, not one. The focused column is
`MixerPanelComponent::focusedColumnIndex_` — ephemeral UI state, an index into the same
left-to-right order `rebuild()` lays columns out in (strips in track order, then Direct if visible,
then Master if visible) — and survives a `rebuild()` of the same strip by re-resolving through the
strip's own uuid (Direct/Master match by kind alone), never a raw index; the focused strip/Direct/
Master column also paints its own outline, reusing `paintFocusRegionOutline`'s colour/alpha/
thickness the same way a track-header row does.

| Shortcut | Action |
|----------|--------|
| Left / Right | Move focus to the previous/next column (strips, then Direct, then Master), clamped at either end — never wraps. From nothing focused, either direction seeds column 0. Auto-scrolls the focused column into view |
| Up / Down | Nudge the focused fader by 1.0 dB (undo: one step, the same `parameterGestureChanged` bracket a mouse drag uses). No-op on Direct (no fader) or with nothing focused |
| Shift+Up / Shift+Down | Nudge by 0.1 dB — the gain parameter's own declared interval |
| Enter | Select the focused column's macro (or bare node, if unboxed) on the canvas — mirrors clicking the column |
| Tab | Move into the focused column's send and insert rows (`mixerEnterRows`) — see [Mixer row navigation](#mixer-row-navigation). With no column focused, or nothing to enter, the key is left unhandled and the app-wide region cycle gets it |
| E | Open the focused strip's EQ window (`mixerOpenEq`) — only while the EQ section is shown and the strip has a Parametric EQ insert |
| M | Mute Focused Track (`timelineMuteFocusedTrack`) — same action id and `performTrackEdit`-equivalent undo bracket the Timeline row's M key uses; Master has its own mute, Direct has none |
| S | Solo Focused Track (`timelineSoloFocusedTrack`) — strips only, always through `AudioEngine::setChannelStripSoloed`, never a direct `setSoloed()` (root `CLAUDE.md`'s invariant); no-op on Direct/Master |
| R | Arm Focused Track (`timelineArmFocusedTrack`) — only when the focused strip is linked to exactly one track (`MixerColumn.linkedToTrack`); routes through `MainComponent::performTrackEdit`, never a direct `TimelineDoc` write |

M/S/R resolve the exact same rebindable Timeline-category action ids the track-header row above
already binds — deliberately, so a user's rebind applies to whichever of the two surfaces has
focus, and a new id would not have inherited an existing rebind. Falls back to hardcoded bare
letters with no `ShortcutManager` installed, the same "no manager installed" contract every other
surface action in this app follows.

Accessibility (JUCE `AccessibilityHandler`, this ticket's other half): the fader and pan sliders
report their value as spoken text (`"-3.0 dB"`, `"50% left"`) via `textFromValueFunction`; the
meter is a read-only `staticText` value reporting its displayed level as a percentage; a column's
own handler is a `group` role titled with the channel name; the M/S buttons carry an explicit
on/off state in their title (`"Lead 1 mute, on"`) since they are built with
`setClickingTogglesState(false)`, which would otherwise report a plain button to a screen reader
rather than a toggle.

### Mixer row navigation

With the Inserts or Sends section shown, **Tab** (`mixerEnterRows`, Mixer category) moves keyboard focus from the
focused column into its rows. Row focus is state of the panel (`MixerPanelComponent::rowFocus_`, a kind and an
index, like `focusedColumnIndex_`), never real focus on a child, so the panel stays the single focusable leaf.
Rows walk in layout order: the insert rows, then the send rows. The focused row is drawn with
`synth::ui::paintFocusRing` and scrolled into its section's frame, and the panel's accessibility description
announces it ("Send to Reverb Bus, -6.0 dB", "Insert 2, Compressor").

| Shortcut | Action in row mode |
|----------|--------------------|
| Up / Down | Previous / next row, clamped at either end |
| Left / Right | Lower / raise the focused send's level by 1.0 dB (Shift: 0.1 dB); one undo step per press, through the parameter's change gesture. No-op on an insert row (the arrows never walk columns while rows hold the keys) |
| Return | On an insert, select its channel on the canvas, exactly what the EQ thumbnail's click does |
| Delete / Backspace | Remove the focused send or insert through the same path its menu uses; one undo step. A branching insert chain is read-only and keeps its rows. Focus moves to a surviving row, or back to the column when none is left |
| B | Bypass the focused row (`mixerToggleRowBypass`): an insert's module, or the send's own bypass. One undo step; B again toggles it back. Claimed only in row mode |
| Esc | Back to column mode |

Tab never traps: it is claimed only when a column is focused and one of its shown sections has rows, and it falls
through to the app-wide region cycle otherwise. Row mode also ends when the panel loses real focus, and is kept
valid when a rebuild or a section toggle removes the row it names.

**Why E and not Return opens the EQ.** Return already selects the focused column on the canvas, so the EQ action
defaults to a bare E (Ctrl+E, the section toggle, is unaffected). It does what the card's "Open EQ Window" button
does: it opens the pop-out Parametric EQ editor of the strip's first EQ insert.

**Snap toggles MAGNETISM, not the grid.** Turning snap off stops edits being pulled onto the
division; it does **not** change which grid lines are drawn. Paint sites read
`TimelineViewState::divisionBeatsRaw()` (the chosen division, whatever the switch says) and only
magnetism reads `divisionBeats()` (which collapses to `0` when the switch is off). Getting this
backwards made the lanes' subdivision lines vanish the moment a user turned magnetism off, leaving
them eyeballing positions against nothing. Same split in the piano roll — see its own note.

**Why the locator jumps are on plain Option+digit** — and the bug that put them there. They shipped
briefly on `Ctrl+Shift+1/2`, the chord the grid-set family owns, and were **dead in the app**:

- `ShortcutManager::saveToProperties` writes **every** action's binding, so one rebind of anything
  freezes the whole table on disk. Moving a DEFAULT therefore does not move a user's PERSISTED key.
- So on any install whose settings had ever been saved, `snapSetWhole`/`snapSetHalf` were still
  sitting on `Ctrl+Shift+1/2`, and `getActionsForKeyPress` returned **both** ids for the chord.
- `MainComponent::keyPressed` takes "the first action bound to this key that HAS a command" — so the
  stale grid command won and the locator jump never ran.

`Option+digit` was never bound to anything in any shipped version, so no persisted value can shadow
it. Option is also the one modifier family immune to the macOS shifted-character problem below
(`charactersIgnoringModifiers` *does* ignore Option), so these two need no rescue at all.
Pinned by `ShortcutManagerTest.APersistedCommandBindingShadowsASurfaceActionOnTheSameChord`.

**Reachability: a surface action needs focus inside its own panel — except these two.** A surface
action only runs if the focused component is inside the owning panel's subtree, because that is how
JUCE bubbles an unhandled key. Under the timeline panel, the things that take keyboard focus are the
clip lane area, the piano roll, and each track header row; the ruler and the transport
bar still do not. So setting the locators by dragging the ruler (the obvious way to do it) left focus on the canvas and
the keystroke died in `MainComponent::keyPressed`, which only dispatches commands.
`MainComponent::keyPressed` therefore ends with a **last-chance forward** of a two-id whitelist
(`forwardsToTimelinePanel`) back into `TimelinePanelComponent::keyPressed`. Deliberately a whitelist
and not a blanket forward: forwarding everything the panel resolves would make its bare letters and
tool digits (J/L/P/F, 1/2/3/4/5/7/8) fire while the graph canvas has focus, which is a different
feature with its own design question.

Both jumps are **surface**-resolved rather than commands: a locator jump acts on the timeline's own
transport and means nothing on any other surface. A **degenerate or unset** span
(`loopEnd <= loopStart`) is a no-op that reports the key unhandled rather than swallowing it, and
looping being switched OFF does not matter — the locators are a *range*, and disarming them only
stops playback wrapping (the same rule `TimelineRulerComponent::braceStateFor` follows).

**Grid commands** (`AppCommands`/`ApplicationCommandManager` — dispatched through
`MainComponent::perform`, active exactly while the timeline panel is on screen):

| Shortcut | Action |
|----------|--------|
| Ctrl+Shift+1 | Set Grid to 1 (whole bar) |
| Ctrl+Shift+2 | Set Grid to 1/2 |
| Ctrl+Shift+3 | Set Grid to 1/4 |
| Ctrl+Shift+4 | Set Grid to 1/8 |
| Ctrl+Shift+5 | Set Grid to 1/16 |
| Ctrl+Shift+6 | Set Grid to 1/32 |
| Ctrl+Shift+7 | Set Grid to 1/64 |
| Ctrl+Shift+8 | Set Grid to 1/128 |
| Ctrl+Shift+Left | Grid Coarser (step toward Bar) |
| Ctrl+Shift+Right | Grid Finer (step toward 1/128) |

**Four Timeline families share the digit row**, separated only by their modifier set: **bare** =
the edit tools, **Shift** = the Draw shapes, **Ctrl+Shift** = set the grid, **Option** = jump to a locator. Modifier equality on
the binding side is exact (`keyPressMatches` normalizes only the key CODE), so none of the three can
reach another; `ShortcutManagerTest.BareToolDigitsDoNotCollideWithTheGridOrLocatorCommands` is the
tripwire. The grid-set family is on its ORIGINAL `Ctrl+Shift+digit` home, which is also what every
existing install has persisted — see the locator note above for why moving it was the wrong half of
that problem to solve.

**Shift-chorded symbol keys and the macOS peer.** Binding lookups go through
`ShortcutManager::keyPressMatches`, not exact `KeyPress` equality: JUCE's macOS peer builds a key
code from `charactersIgnoringModifiers`, which — per its own source comment — does *not* ignore
Shift, so Ctrl+Shift+1 arrives as `!` and Cmd+Shift+`=` arrives as `+`. The matcher folds both sides
through a US-layout unshift map when both carry Shift (other layouts degrade to exact match).
Conflict detection deliberately stays exact so two different stored chords never merge. Headless
tests construct `KeyPress('1', mods)` directly and would never catch this class of bug —
`FocusArbitrationTest.ShiftedSymbolKeyCodesFromTheRealKeyboardReachTheGridCommands` pins the
real-event form instead, on the grid family's own `!`/`^`/`&`/`*`.

Note that `charactersIgnoringModifiers` DOES ignore **Option**, which is half of why the locator
jumps sit on plain `Option+digit`: a real Option+2 arrives carrying `2` and matches by key code
directly, so that pair is outside this bug's reach entirely. The rescue matters for the eight
Ctrl+Shift grid commands and the Cmd+Shift zoom pair.

**Ctrl, not Cmd — deliberately, including on macOS.** `ShortcutManager::resetToDefaults` binds
these with `juce::ModifierKeys::ctrlModifier`, a REAL Ctrl rather than `commandModifier`. On macOS
the Ctrl+digit space is genuinely free (Cmd+digit is reserved by hosts and by the native menu bar);
on Windows/Linux `commandModifier` IS `ctrlModifier`, so these read as Ctrl+Shift+digit on every
platform with no per-platform branch needed. Because the tool-switching digits above are BARE (no
modifier) and modifier equality in `ShortcutManager::bindingMatches` is exact, Ctrl+Shift+1 can
never be mistaken for a bare `1` — category scoping is what makes the two safe to coexist in the
same section at all.

**The eight set-commands and the two cycle-commands are eight+two separate commands, not one
parameterised command** — `juce::ApplicationCommandManager` has no notion of an argument, so a menu
row and a key binding are per-command; "set the grid to 1/8" has to BE a command to be rebindable or
show up in a menu at all.

**Cycle rules** (`TimelinePanelComponent::cycleSnapValue`): the nine musical divisions (`Bar`
through `HundredTwentyEighth`) are declared coarsest→finest, so a cycle step is a **clamped** ±1 on
that ordering — never wrapped. Holding the key parks on `Bar` or `1/128` rather than silently wrapping back
around, which would be far more surprising under a held key. From `Snap::Off` there is no position
for "one step finer/coarser" to be relative to, so **both directions re-enter at the last musical
division the user actually chose** (`Bar` if there wasn't one yet) — picking a direction to "end" at
would be an arbitrary choice the from-Off case doesn't need to make. Every set/cycle call goes
through `setSnapValue`, which always re-arms `snapEnabled` — picking (or stepping to) a division is
an explicit "snap to THIS," so `Snap::Off`'s own separate meaning ("no grid") only applies until the
next explicit choice. The status bar reports where the grid ENDED UP after a cycle, not which way it
moved — a held key parked at a clamp says so instead of implying another step happened.

## Piano Roll

All fourteen are surface-resolved — consulted directly by `PianoRollComponent::keyPressed()`, never
dispatched through `ApplicationCommandManager`:

| Shortcut | Action |
|----------|--------|
| ← / → | Nudge Notes Left / Right — the whole selection, by one grid division (one snap cell; a sixteenth when snap is off) |
| ↑ / ↓ | Transpose Up / Down **one visible row** — the whole selection. A semitone normally; the next **scale degree** while *Show Only Scale Notes* is on, because the row set IS the scale then. One implementation either way (`transposeSelectedNotesByRow`, stepping `visiblePitches_` through the same `rowShiftedPitch` seam a drag uses), so an arrow key can never strand a note on a hidden out-of-scale row |
| Shift+↑ / Shift+↓ | Transpose Up / Down an Octave (12 semitones) — the same octave-jump convention every DAW uses, a separate action rather than a modifier read off the plain one so it can be rebound on its own |
| Alt+← / Alt+→ | Select Previous / Next Note — navigates BETWEEN notes in the clip's canonical (start, pitch) order, collapsing a multi-selection onto the outer neighbour; scrolls an off-screen target into view. Selection-only, never a document edit. Alt+↑/↓ is reserved (unclaimed) |
| **Q** | **Quantise Selected Notes** (`pianoRollQuantise`) — one-shot: snap the selected notes' STARTS (or all notes when nothing is selected) to the chosen grid, even while snap is toggled off. **Cubase parity:** on a note editor the bare, most reachable key belongs to the verb you use constantly, not to a switch you set once a session |
| Alt+Q | **Quantise Selected Note Lengths** (`pianoRollQuantiseLength`) — the length twin of bare Q: snaps the selected notes' LENGTHS (or all notes when nothing is selected) to the nearest positive multiple of the chosen grid, floored at one grid unit so a note can never become zero-length. Exact-modifier matching keeps this clear of both bare Q and Option+Shift+Q, so no ordering is needed among the three. Has its own header chip (**Quantise Length**, between Quantise and Quantise Pitches) |
| Option+Shift+Q | Quantise Note Pitches to Scale (`pianoRollQuantisePitches`) — snaps the selected notes' PITCHES (or all notes when nothing is selected) into the scale picked in Scale Assist, via `MusicalScale::snapPitch`. Falls THROUGH (returns `false`) when no scale is chosen: "No scale" has nothing to quantise into. Matched BEFORE bare Q, since it is the more specific chord |
| J | Toggle Snap — grid magnetism on/off, the **shared** `timelineSnapToggle` the timeline panel also uses (one binding, one key, whichever surface has focus; deliberately NOT duplicated into a piano-roll action, since two "Toggle Snap" rows on the same key flipping the same flag is a Settings list nobody could reason about). **Magnetism only: the chosen grid stays VISIBLE either way**. The roll's own Snap header chip was removed as redundant with the timeline toolbar's own Snap button — both read/write this same shared flag by reference, and J remains the roll's own control for it |
| Ctrl+S | Toggle the Scale Assist panel (`pianoRollToggleScalePanel`) — real Control, not Cmd (Cmd+S stays the app's save); inert while a text field inside the panel has focus |
| Ctrl+V (macOS) / Cmd+Shift+V (Windows, Linux) | **Show or Hide Velocity Strip** (`pianoRollToggleVelocityLane`) — the Velocity header chip's twin. A real Control on macOS, where Cmd+V is Paste; on Windows/Linux JUCE's Cmd *is* Ctrl, so Ctrl+V would be Paste and those platforms take Cmd+Shift+V instead — the same per-platform split as the AI panel's Ctrl+A / Cmd+Shift+A. Inert while a text field (the header's velocity box) has focus. See [`timeline/piano-roll.md`](../timeline/piano-roll.md#velocity-strip) |
| Option+S | **Show Only Scale Notes** (`pianoRollToggleScaleFilter`) — collapses the out-of-scale rows out of the grid, and makes ↑/↓ step by scale degree (above). One modifier away from Ctrl+S on purpose: adjacent verbs on adjacent chips should rhyme, and modifier equality is exact so they cannot collide. Remembered per clip; falls through with no clip open |

Every arrow/octave action returns `false` (falls through) when nothing is selected, so the key
keeps whatever meaning it has elsewhere with an empty selection. The two navigation actions are
the exception: with nothing selected, Next Note selects the clip's first note and Previous Note its
last, so a keyboard-only user can reach a note without the pointer. Nudge/transpose EDIT the
selection, while the navigation actions only MOVE it. Order matters only where one default is a
modified form of another (Shift+Up vs Up, Alt+Left vs Left): the more specific action is matched
first, so rebinding only one half of a pair can't let the other swallow it. `juce::KeyPress`
equality is exact on modifiers, which is what keeps Left/Shift+Left/Alt+Left three separate
actions. Digit keys are deliberately absent here — tool switching belongs to the panel (see
Timeline above), so the roll and the panel can never disagree about which tool is active.

**The focused note.** The note Alt+Left/Right land on is the roll's *focused note*: the one note the
selection holds (a multi-selection, or none, has no focused note). While the grid holds keyboard focus
it is outlined with the accent focus ring, and a screen reader hears it as the grid's value, e.g.
"C4, bar 2 beat 1, length 1/8, velocity 100" (see [`timeline/focus.md`](../timeline/focus.md#piano-roll-focus)).
Alt+Left/Right still fall through when nothing is selected, so a first note is picked with the pointer.
Each header chip has a keyboard button over it: Tab reaches it, **Return** runs its action (Space stays
the global play/stop) and its tooltip names its shortcut.

**One letter, one verb, on both surfaces.** Snap moved off Q to J for the *timeline* too, so bare Q
is now unambiguously "quantise" (the roll's `pianoRollQuantise`) and bare J is unambiguously "snap"
(the shared `timelineSnapToggle`) — the roll resolves the latter directly rather than owning a
duplicate of it. Pitch-quantize is matched **before** bare Q, since `Option+Shift+Q` is the more
specific chord.

**Option+letter is stored as a key CODE, never as a character.** macOS delivers Option+Q to the app
as the Unicode glyph `œ`, not as `'q'` plus an Alt flag, so `pianoRollQuantisePitches` and
`pianoRollToggleScaleFilter` are `juce::KeyPress('<letter>', <modifiers>, 0)` and are matched through
`ShortcutManager::keyPressMatches` (key code + exact modifier set) — the same shape the arrow-key
bindings already use, and the reason they survive the platform's own key translation.

**Most of these keys have a header chip twin**, and each chip does exactly one thing on a plain
click (no modifier variants anywhere in the header any more): **Quantise**, **Quantise Length**,
**Quantise Pitches**, **Scale**, **Show Only Scale Notes** and **Velocity**. One key is deliberately keyboard-only
with no chip: `J` (Toggle Snap — its chip was removed as redundant with the timeline
toolbar's own Snap button, which shares the same underlying flag). Four of the remaining chips carry
small drawn vector glyphs rather than letters — a second "Q" beside the Quantise chip for
length-quantise or pitch-quantise would have told the user nothing about which was which. See
[`timeline/piano-roll.md`](../timeline/piano-roll.md#header-chips).

6 (Zoom) and 9 (Play/Scrub) are Cubase tools this app doesn't ship yet and stay **unassigned on
purpose** — `editToolForKeyChar` (`Source/UI/Timeline/EditTool.h`) returns `nullopt` for them, so
those two digits are simply never consumed rather than remapping the shipped tools onto 1–7.
Shipping one later costs no rebind: the digit is already reserved — which is exactly how the Range
tool (`timelineToolRange`, bare **2**) arrived without moving any other key.

## Mixer

The five Mixer actions are surface-resolved like the piano roll's: `MixerPanelComponent::keyPressed()`
reads them only while the mixer (the panel, a column control, or its side pane's list) has focus. The first
three are the keyboard twins of the toolbar's Inserts / Sends / EQ toggles and call the same
`MixerSectionLayout::toggleHidden`, so a key and a click can never disagree; the toggles' tooltips
name the current binding ("Hide Sends  (Ctrl+S)") and follow a rebind. The other two are
[row navigation and the EQ key](#mixer-row-navigation).

| Shortcut (macOS) | Windows / Linux | Action |
|------------------|-----------------|--------|
| Ctrl+I | Ctrl+Alt+I | Show or Hide Mixer Inserts (`mixerToggleInserts`) |
| Ctrl+S | Ctrl+Alt+S | Show or Hide Mixer Sends (`mixerToggleSends`) |
| Ctrl+E | Ctrl+Alt+E | Show or Hide Mixer EQ (`mixerToggleEq`) |
| Tab | Tab | Enter Mixer Send and Insert Rows (`mixerEnterRows`) |
| E | E | Open Mixer EQ (`mixerOpenEq`) |
| B | B | Bypass Mixer Send or Insert (`mixerToggleRowBypass`) |

**Real Control on macOS, Ctrl+Alt elsewhere.** Cmd+S is Save and Cmd+I / Cmd+E are taken, so macOS uses
the physical Control key, as `pianoRollToggleScalePanel` does. On Windows and Linux JUCE's Cmd *is* Ctrl,
so a bare Ctrl+S would shadow Save whenever the mixer has focus; those platforms add Alt instead.

The side pane's own list keys (Up/Down, Space, Alt+Up/Down, Esc) are fixed, like the Library list's
arrows: see [`mixer/panel.md`](../mixer/panel.md#side-pane-zones-and-visibility).

## Layout Editor

The card layout editor's list ([module-card-layout.md](../layout/module-card-layout.md#editing-a-layout))
resolves these itself (`CardLayoutEditorComponent`'s row keys) while one of its rows has focus; the panel is
the only thing focused while it is open, so they share keys freely with every other category. Up/Down
between rows are fixed list keys, and Escape is left to the panel's CallOutBox, which closes it.

| Shortcut (macOS) | Windows / Linux | Action |
|------------------|-----------------|--------|
| Space | Space | Show or Hide the Control on the Card (`layoutEditorToggleShown`) |
| Cmd+Up | Ctrl+Up | Move the Control Up (`layoutEditorMoveUp`), into the group above at a group's top |
| Cmd+Down | Ctrl+Down | Move the Control Down (`layoutEditorMoveDown`), into the group below at a group's end |
| Return | Return | Rename the Control (`layoutEditorRename`), or the focused group's title |

## Command vs surface actions

The 127 actions split into two kinds, and telling them apart is the key to reasoning about "why
doesn't this key do anything":

- **Command-dispatched** (71 actions) — every General action (including the transport family
  above), the six Graph command actions, and the Timeline category's eight grid-set + two grid-cycle
  commands. `AppCommands::getCommandForAction(actionId)`
  returns a real `juce::CommandID` for these; `MainComponent` implements
  `ApplicationCommandTarget`, so they appear in the native menu bar, drive toolbar tooltip text, and
  their enabled/disabled state is whatever `getCommandInfo` reports.
- **Surface-resolved** (56 actions) — the canvas's nine card keys (`canvasSelectCard*`, `canvasMoveCard*`,
  `canvasEnterCard`, resolved by `CanvasCardKeyboard::keyPressed`), the timeline panel's own keys (`timelineSnapToggle`,
  `timelineToggleLoop`, `timelineLoopSelection`, `timelineFollowPlayheadToggle`, the six
  `timelineTool*` digits, and the two `timelineJumpToLocator*` keys), the three track-header
  keys (`timelineMuteFocusedTrack`/`timelineSoloFocusedTrack`/`timelineArmFocusedTrack`), the seven
  clip-keyboard keys (`timelineClip*`), every piano roll action, the five `mixer*` actions and the four `layoutEditor*` keys. `AppCommands::getCommandForAction` returns `AppCommands::kNoCommand` (`0`,
  `juce::ApplicationCommandManager`'s own "not a command" value) for every one of these — they are
  never dispatched through the command manager at all. Instead, the owning component's own
  `keyPressed()` calls a small `matchesAction(key, actionId, fallback)` helper that reads
  `ShortcutManager::getBinding(actionId)` directly.

**Strict resolution.** With a `ShortcutManager` installed, `matchesAction` requires
`binding.isValid() && key == binding` — there is no fallback to the hardcoded default once a
manager exists. Clearing a surface action's binding in Settings therefore genuinely unbinds it: the
key does nothing on that surface, rather than quietly resurrecting the old default. The fallback
key is used ONLY when `shortcuts_ == nullptr` (headless tests, or an embedding built with no
settings store), so old callers that never wired a manager keep working unchanged. Missing a
surface-resolved id from `ShortcutManager`'s defaults table would make that key **silently inert**
the moment a manager IS installed — the ordering tripwire test,
`ShortcutManagerTest.EverySurfaceResolvedIdExistsInTheDefaultsTable`
(`Tests/App/ShortcutManager/ShortcutManagerTests.cpp`), walks every id a component is known to consult and asserts it
exists in the table with a valid default binding, precisely to catch that failure mode before it
ships. Two complementary tests pin the rest of the split: `SurfaceActionsMapToNoCommand` (a surface
id must never also claim a command id, or `MainComponent::keyPressed` would try to dispatch one
and bypass the component's own handling) and `EveryNonSurfaceActionHasACommand` (the converse — a
rebindable key with no command behind it would fire and do nothing).

**Category-scoped conflicts.** `ShortcutManager::getConflictingAction(actionId, key)` only reports a
collision within the SAME category (`ShortcutCategory`: General/Graph/Timeline/PianoRoll). The
timeline and the piano roll can never hold keyboard focus at the same time, so a bare key repeating
across categories is legal and must not be reported as a conflict — that's what lets Q mean
"toggle snap" identically on both surfaces, and lets the timeline's bare-key DAW conventions (J/L/P,
the tool digits) and the roll's bare arrow keys coexist with General's Cmd-modified table without
forcing any of them into a modifier combination nobody uses. WITHIN a category the check is as
strict as ever — a second General Cmd+X still reports the first one, which is what the Settings
tab's rebind-swap acts on. An action id this build has never heard of is treated as General, the
widest scope, so an unknown id can never quietly duplicate a real app-wide binding.

**Escape and Delete/Backspace stay fixed — never in the `ShortcutManager` table at all, regardless
of whether a manager is installed.** "Cancel" and "delete the current selection" are platform
conventions every surface in the app answers identically — not app shortcuts a user would expect to
find in a rebinding list. Each surface's own `keyPressed()` hardcodes them directly:

| Shortcut | Context | Action |
|----------|---------|--------|
| Escape | AI panel, request in flight | Cancel the in-flight AI request (same as the Cancel button — actually aborts it, see [`ai/ollama-provider.md`](../ai/ollama-provider.md#request-cancellation)) |
| Escape | Canvas, modules selected | Clear the selection |
| Delete / Backspace | Canvas, modules selected | Delete every selected module (one undo step) |
| Escape | Clip lanes, clips selected | Clear the clip selection |
| Delete / Backspace | Clip lanes, clips selected | Delete every selected clip (one undo step) |
| Escape | Piano roll, notes selected | Clear the note selection |
| Escape | Piano roll, nothing selected | Close the roll, back to the clip lanes |
| Delete / Backspace | Piano roll, notes selected | Delete every selected note (one undo step) |

The canvas selection keys are handled by `GraphEditor::keyPressed()`, the clip-lane ones by
`TimelineClipLaneArea::keyPressed()`, the piano-roll ones by `PianoRollComponent::keyPressed()`.
Every one of these components takes keyboard focus on mouse-down (the same idiom
`resolveEditSurface()` relies on) and returns `false` when its own selection is empty, so an
unmodified Delete/Escape keeps its normal meaning elsewhere instead of being silently swallowed by
an idle panel — `Tests/App/FocusArbitration/FocusArbitrationTests.cpp`'s `DeletePerSurface` pins exactly this: a
clips-focused Delete never touches the graph, a graph-focused Delete never touches the clips, and an
empty selection on either falls through. `AIChatComponent::keyPressed()`'s Escape only acts while a
request is in flight; otherwise it is passed through so it keeps whatever meaning the enclosing
window gives it.

A **hosted plugin editor window** (a separate top-level window, not a panel inside the main one)
has its own fixed convention, same "never in the `ShortcutManager` table" shape as the table above:
Esc closes it only if the plugin's own editor didn't handle it first — a native NSView editor gets
first crack at it too, via the AppKit responder chain, and only an unhandled Esc reaches
`HostedPluginEditorWindow::keyPressed()` from there (verified against Apple's own AUDelay) — while
Cmd+W always closes it, via a mac-only `NSEvent` monitor scoped to the key window. See
[`architecture/plugin-layer.md#editor-windows`](../architecture/plugin-layer.md#editor-windows) (FRO337) for the full split.

Arrow keys and the tool digits are split across two components with opposite rules:
`PianoRollComponent::keyPressed()` consumes an arrow **only when something is selected** (an
empty-selection arrow falls through, so it keeps whatever meaning it has elsewhere) and
deliberately does **not** consume the tool digits at all — tool switching belongs to the panel, so
the roll and the panel can never disagree about which tool is active. The whole selection nudges/
transposes by ONE shared delta (never per-note), clamped so the group stays inside the clip window
(`[0, clipLength)`) or the pitch range (`[0, 127]`) as a unit — the same "clamp the group together"
rule `TimelineClipLaneArea`'s cross-track move drag uses (see [`timeline/clips.md`](../timeline/clips.md#cross-track-drag)).

## Shortcut hints

Hold **Cmd** (Ctrl off the Mac) on its own for about half a second and a small key-cap bubble
appears on each visible button that has a shortcut: `⌘T` on the Show/Hide Panel toolbar button,
`⌘1`–`⌘3` inside the bottom-panel tabs, Space on the play buttons. Each bubble grows out of the
button it labels; release and they shrink back and fade out. The
overlay is `ShortcutHintOverlay` (`Source/UI/Chrome/ShortcutHint/`), a full-window child of
`MainComponent` that paints only while the hints are up.

- **Four hold keys.** **Cmd** alone and **Ctrl** alone (macOS; off the Mac Ctrl is Cmd) both show
  every target that has any binding, bare keys included (the timeline's edit tools, Follow playhead,
  Space on Play), so whichever modifier you reach for reveals everything. **Option/Alt** alone shows
  only the targets whose *current* binding uses Option -- the Mac Option+letter shortcuts (`⌥S` on the
  piano roll's Scale filter chip). **Shift** alone does the same for Shift: it shows the targets whose current
  binding uses Shift, such as the timeline's Draw shape buttons (Shift+1..6) while their strip is out. Shift is
  ignored while a text field has focus (that is typing capitals), and a mouse press cancels it like any other
  hold, so Shift+drag and Shift+click work as before. The filter reads the binding fresh each time, so a rebind moves a target in or out
  of the set and its bubble follows the new key. Switching from one hold key to another, or adding a
  second modifier, cancels like any chord; the latch, fade-out resume and delay all work per key. A
  different key pressed while the hints are fading out starts over from the 500 ms delay.
- **Settings tabs.** The Settings window carries its own overlay: holding Cmd shows `⌘1`, `⌘2`, ... on its tab
  buttons. The keys are positional and fixed (`addFixedKeyTarget` takes the key as given instead of an action
  id, so a rebind never changes them), shown in the platform's key text like every other badge, and the window feeds
  the overlay from its `modifierKeysChanged`.
- **Area targets.** The piano roll's header chips are painted rectangles, not child components, so
  they register with `addAreaTarget(owner, areaInOwner, actionId)`: the same visibility check runs on
  the area's centre through the owner's parents (a closed roll gives no bubble) and the bubble
  anchors on the chip (`PianoRollComponent::getHeaderChipBounds`).
- **The key is never hard-coded.** A button is registered with the shortcut *action* it triggers
  (`MainComponentShortcutHints.cpp` names them); the text is read from `ShortcutManager` each time
  the hints appear and re-read on a rebind while they are up. An action with no key gets no bubble.
- **Timing.** They appear 500 ms after Cmd goes down alone and animate in over 160 ms
  (`easeOutCubic`); on release the same motion runs back over 110 ms (`easeInCubic`). Pressing Cmd
  again while they are fading out resumes from where they are, with no second delay. Any other key (including a Cmd chord such as Cmd+S), a mouse click, another
  modifier held with Cmd, or the window losing focus cancels at once with no fade, and the hold stays
  spent until Cmd is released. A key that a focused text field consumes never reaches the cancel hook,
  so the hints can still appear there. Nothing runs at rest: a one-shot 500 ms timer is armed only
  while Cmd is down alone, and the one `AnimationDriver` exists only while the tween is in flight. Cmd is noticed
  through the global mouse listener (JUCE sends a fake mouse move on every modifier change) and
  `MainComponent`'s existing 10 Hz poll as a backstop.
- **Only showing components get one.** Hidden, collapsed, scrolled-away or covered buttons (the
  centre of the button must hit-test to the button itself) are skipped, and nothing appears while a
  modal is up.
- **Placement** (`ShortcutHintLayout.h`, pure geometry): centred under the button, overlapping its
  bottom edge by 4 px; flipped above if that leaves the window (or the bottom panel, for buttons inside
  it); an overlapping later bubble slides sideways by the overlap, up to half its width, else it is
  staggered up to two rows further from its button, sliding again in each row (2 px gap; a row of narrow icon buttons with wide "Shift+2"
  key text off the Mac, like the Draw shapes), and left out only when no row fits. A dock tab carries its bubble inside the tab, 6 px after the name.
- **Motion.** One tween value `t` (0 hidden, 1 settled) drives every bubble's scale, position and
  opacity: `hint::animatedBubbleBounds(target, origin, t)` is `target` at t = 1 and, at t = 0, 0.6 of
  its size centred on `origin`. The origin is the labelled button's centre for a bubble below or
  above it (so it slides down or up out of the button), the tab's centre for an in-tab cap (slides
  right), and the pill's own centre 12 px lower for the hidden-panel row (rises). Placement and
  collision use the settled rectangles only.
- **Bottom panel hidden.** The tabs are not on screen, so the panel toggle's hint and each strip
  tab's line up centred along the window bottom, 8 px above the status bar, in tab order, each as a
  pill (24 px tall, label one point above the `label` size) holding the key cap and the name. A tab detached to its own window is left out
  (`BottomDockComponent::getStripTabs()`).
- **The look** is one shared function, `AppLookAndFeel::drawShortcutKeyCap`
  (`AppLookAndFeelShortcutHints.cpp`): a `cornerRadiusSmall` cap 20 px tall (at least 20 wide, 6 px
  side padding), `surfaceHi` fill, a `textDisabled` outline with a 2 px bottom edge (`border` is
  nearly the fill on dark themes, so the cap vanished), a soft shadow (0.5 alpha, 2 px down, blur
  radius 4), the mono face at the `value` size plus 2 px in `textPrimary`. The medium weight comes from
  requesting `Font::bold` on the mono family: the embedded mono faces load their Medium cut for it.
  The in-tab cap is 15 px tall with the `value` size plus 1 px (the tab is 17 px tall, so the cap keeps
  a pixel above and below; the strip does not grow). Text comes from `hint::formatKeyCapText`: Mac glyphs on macOS,
  `Ctrl+Shift+Z` style elsewhere.
- **Never in the way.** The overlay takes no clicks, is hidden from the accessibility tree, never
  takes keyboard focus and never moves or resizes anything.

Covered today: the toolbar's Library, New, Save, Load, Settings, Undo, Redo, Auto-arrange, Minimap,
Mod Matrix, AI Panel and Show/Hide Panel buttons; the dock tabs; the timeline transport bar's
play/stop, record, loop and metronome buttons (loop shows the bare L of `timelineToggleLoop`, or the
`transportToggleLoop` binding while L is unbound); the timeline's seven edit-tool buttons, Snap and Follow
playhead (`TimelinePanelComponent::getShortcutHintTargets()` is the one list, so a new shortcut button there is
labelled by adding it to it; the per-row M/S/R buttons act on the focused row and are not labelled, and neither are the mixer's per-row bypass buttons, whose tooltips name B instead); the status bar's play/stop; the mixer toolbar's
Inserts, Sends and EQ toggles; and, while a clip is open, the piano roll's Quantise, Quantise length,
Quantise pitches, Scale, Scale filter and Velocity chips.

## Canvas mouse gestures

Pan, marquee, click and drag on the canvas are mouse gestures, not rebindable shortcuts; the table
is in [`layout/selection.md`](../layout/selection.md#canvas-mouse-gestures).
