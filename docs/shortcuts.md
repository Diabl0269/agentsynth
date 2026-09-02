# Keyboard Shortcuts

Shortcuts are configurable in **Settings → Keyboard Shortcuts** (`Source/UI/ShortcutsSettingsTab.h/.cpp`).
`ShortcutManager` (`Source/ShortcutManager.h`) registers **62 actions** across four categories —
**General** (24, app-wide or routed per focused editor), **Graph** (4), **Timeline** (22) and
**Piano Roll** (12) — every one of them rebindable, including keys that used to be hardcoded:
nudge/transpose/octave, note navigation, quantise, the snap toggle, the loop keys and the six tool
digits. Click a row's binding button to rebind it (button turns orange, "Press a key…"); pressing
any key except Escape commits it, swapping with whatever action in the **same category** already
held that key. The tab groups rows into one collapsible section per category with a search box
above them (matches against both the action's description and its current binding text — "cmd"
finds every Cmd shortcut, "transpose" finds the piano-roll block) and a top strip that flips between
"COLLAPSE ALL"/"EXPAND ALL"; see [`layout_selection_canvas.md §2`](layout_selection_canvas.md#2-collapsible-library-sections) for
the shared collapsible-list pattern it mirrors. Export/import round-trip every binding as JSON;
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
| Cmd+S | Save Preset — writes a project bundle (`.agsproj`, graph + timeline) and silently resaves to the remembered bundle on every subsequent press; prompts for a location only on the first save or when no bundle is open (see [`architecture.md`](architecture.md)) |
| Cmd+Opt+S | Save Project As — always prompts for a new location |
| Cmd+Shift+E | Export Audio — opens the Export Audio dialog (bounce the arrangement or the current loop range to WAV/AIFF, see [`architecture.md`](architecture.md#bounceexport)). Greyed out while a bounce is already running |
| Cmd+O | Open Preset (file picker) |
| Cmd+Z | Undo |
| Cmd+Shift+Z | Redo |
| Cmd+M | Toggle Mod Matrix |
| Cmd+K | Toggle Minimap |
| Ctrl+A (macOS) / Cmd+Shift+A (elsewhere) | Toggle AI Panel — moved off Cmd+A so Select All could take the platform-standard chord. One of the very few per-platform defaults: on macOS Ctrl is a real separate modifier, on Windows/Linux JUCE's Cmd IS Ctrl so Ctrl+A would collide with Select All |
| Cmd+B | Toggle Module Library |
| Cmd+T | Toggle Timeline Panel (see [`timeline_panel_core.md §1`](timeline_panel_core.md)) |
| Cmd+A | Select All in Focused Editor (actionId/`AppCommands` name still `selectAllModules` — see "Surface routing" below) |
| Cmd+Shift+S | Save Selection as Snippet |
| Cmd+C | Copy (Selected Modules, or — see "Surface routing" below — the timeline's selected clips/notes) |
| Cmd+V | Paste (Modules, or the copied clips/notes) |
| Cmd+D | Duplicate (Selected Modules, or the selected clips/notes) |
| Cmd+X | Cut — Copy then delete, as ONE undo step (Selected Modules, or the timeline's selected clips/notes; see "Surface routing" below) |
| Cmd+R | Repeat — prompts for a count (1–64) via an `AlertWindow` and creates that many back-to-back copies of the selection, tiled forward one selection-span at a time, as ONE undo step. Timeline-only: inactive on the Graph surface (see below) |
| Space | Toggle Playback (play/stop the timeline transport) |
| Cmd+= | Zoom In (routed per focused surface — see [**Zoom**](#zoom) below) |
| Cmd+- | Zoom Out |
| Cmd+Shift+= | Zoom In Vertically |
| Cmd+Shift+- | Zoom Out Vertically |

Cmd+T and Space are always active — the timeline is GA (see [`timeline_panel_core.md §1`](timeline_panel_core.md)). The grid
and zoom commands below are inactive whenever the panel itself isn't open (`isTimelineVisible`),
the same as any other timeline-only command.

`Cmd+A` is the platform-standard Select All (the way Cubase and every text field read it), so it
owns the bare chord and the AI panel sits on a REAL `Ctrl+A` on macOS (Ctrl is a distinct physical
modifier there) and on `Cmd+Shift+A` everywhere else: on Windows/Linux JUCE's `commandModifier` IS
Ctrl, so a Ctrl+A default there would be the same chord as Select All and the two commands would
collide. `Cmd+Shift+S` keeps its Shift variant because `Cmd+S` (Save Preset) is bound. Like every row above, all of these are rebindable in
Settings — and note that a machine which already persisted the old bindings keeps them until
"Reset to Defaults" (bindings are stored per actionId, defaults only fill the gaps).

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

### Surface routing: who Cmd+C/V/D/X/R and Cmd+A act on

Cmd+C/V/D/X/R and Cmd+A are global commands (`MainComponent::getAllCommands`/
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
| Graph | Copies the selected modules (unchanged) | Pastes at the next cascade position | Copies the selection one step down-right, clipboard untouched | Composed from Copy + Delete (`copySelection()` then `deleteSelection()`), one undo step | **Inactive** — a spatial canvas has no time axis to tile copies along; Duplicate is the graph's equivalent gesture (see below) |
| TimelineClips | Serialises the selected clips — notes (each with its own muted flag), name, length, muted flag and every audio field (`assetRef`, gain, both fades, `sourceStartSeconds`) — into the panel's own clip clipboard, starts relative to the earliest selected clip | Re-inserts every clipboard clip onto **its original track**, re-based so the earliest clip lands at the transport's current position (snapped to the view-state's snap setting); the track fallback is **kind-aware** — a clip lands back only on a track that still plays its payload (audio → `TrackKind::Audio`, MIDI → `TrackKind::Midi`), else the doc's first track of the required kind, else the clip is skipped. Audio fields go back through `setClipAsset`/`setClipGainDb`/`setClipFades` (never a raw struct write), so a clipboard `assetRef` is re-validated exactly like a freshly-loaded file's — a clipboard is only as trustworthy as whatever filled it. One undo step for the whole paste; the pasted clips end up selected. | `TimelineDoc::duplicateClip` per selected clip, batched into one undo step; the new clips end up selected | `TimelinePanelComponent::cutSelectedClips()` — copy, then delete the selection, as ONE `recordTimelineChange` (so undo restores it in a single step) | `repeatSelectedClips(count)` — `count` back-to-back copies of the selection's own span (`max end - min start`, not each clip's own length, so a multi-clip rhythm tiles intact), the first starting one span-length after the selection's start. One undo step; every created clip ends up selected. |
| PianoRoll | Copies the selected notes (each field, `muted` included) into the roll's OWN note clipboard, offsets stored relative to the earliest selected note — the clipboard is a member of the roll, so it survives switching clips (`openClip`), and a block copied in one clip pastes into another | `pasteNotesAtPlayhead()` anchors the block at the **snapped, clip-relative playhead position** when that lands inside `[0, clip length)`, else at 0.0; `MainComponent::perform` primes the playhead from the live transport (`setPlayheadBeat(transport.getPositionSnapshot().ppq)`) immediately before pasting, so a paste with the transport stopped still lands under the position the user can see rather than wherever a stale internal beat was left. Notes at/after the clip's end are skipped, an overrunning note's length is clamped to the clip's end. One undo step; the pasted notes end up selected. | Copies the selection to immediately after its own span (same pitches), one undo step, selects the copies — does NOT touch the clipboard (duplicating isn't copying, and silently stomping a clipboard the user filled deliberately would be a surprise) | `cutSelectedNotes()` — copy then delete, one undo step (fills the clipboard first, so a cut is always paste-able) | `repeatSelectedNotes(count)` — `count` copies of the selection block, each one span further along, **clipped at the clip's end**: placement stops at the first block that would fall entirely outside the clip rather than piling every remaining copy onto the last beat. One undo step; every created note ends up selected. |

`getCommandInfo` marks Paste active only when the **surface-matching** clipboard has something in
it — the Graph clipboard, the TimelineClips clipboard and the PianoRoll's note clipboard are three
entirely separate stores, so copying modules does not make Paste live on the clip lanes or the
roll, or vice versa. Cut shares Copy's enablement predicate on every surface (a cut is a copy that
also deletes, so anything copyable is cuttable). Repeat is active whenever the acting surface has a
selection (`hasClipSelection()` / `hasNoteSelection()`) and — uniquely among these verbs — is
**always inactive on Graph**, regardless of selection.

`Cmd+A` (`selectAllModules` — the actionId and `AppCommands` name are frozen so a persisted
user binding keeps resolving, even though the verb widened) is routed by the same
`resolveEditSurface()`: it selects every clip (`TimelinePanelComponent::selectAllClips()`) on
TimelineClips, every note in the open clip (`PianoRollComponent::selectAllNotes()`) on PianoRoll,
and every module (`GraphEditor::selectAllModules()`) on Graph. Unlike the clipboard verbs it is
**always active** on every surface — it needs no pre-existing selection, and each surface's own
`selectAll*` just returns `false` harmlessly (no status-bar lie) when there is nothing to select.

### Zoom

Cmd+=/Cmd+- is the platform's own zoom accelerator (every browser, every editor); Cmd+Shift+=/-
is the vertical axis, mirroring the modifier the mouse wheel already uses (Cmd+wheel = horizontal,
Cmd+Shift+wheel = vertical — see [`timeline_panel_core.md §2`](timeline_panel_core.md)), so the keyboard and the wheel teach
the same shape. All four are `AppCommands`/`ApplicationCommandManager` commands (unlike the
Timeline/PianoRoll surface keys below) and route by the SAME `resolveEditSurface()` the clipboard
verbs use, in/out factor `1.25` / `1 / 1.25` (`MainComponent::kZoomInFactor`/`kZoomOutFactor`):

| Surface | Horizontal (Cmd+=/-) | Vertical (Cmd+Shift+=/-) |
|---|---|---|
| Graph | `GraphEditor::zoomAroundCentre` — the canvas' one zoom level | **Inactive** — the canvas zooms uniformly (one `zoomLevel`, no separate axes), so a second key that did the same thing under a different modifier would be a trap, not a feature |
| TimelineClips | `TimelinePanelComponent::zoomTimelineHorizontal` — `TimelineViewState::pixelsPerBeat`, anchored at the visible centre | `zoomTimelineVertical` — `TimelineViewState::rowHeightScale` (track row height), anchored at the visible centre |
| PianoRoll | `PianoRollComponent::zoomHorizontal` — the roll's OWN `pixelsPerBeat` (never the shared `TimelineViewState` — see [`timeline_panel_clips_automation.md §2`](timeline_panel_clips_automation.md)) | `zoomVertical` — `pixelsPerSemitone_` |

Each keypress reports its own status-bar message ("Canvas: zoom", "Timeline: zoom" / "Timeline:
track height", "Piano roll: zoom" / "Piano roll: vertical zoom") so a held key's effect is visible
even with no mouse involved.

## Graph

| Shortcut | Action |
|----------|--------|
| Cmd+L | Auto Arrange |
| Cmd+Shift+S | Save Selection as Snippet |
| Cmd+G | Group Selection into Macro |
| Cmd+Shift+G | Ungroup Macro |

Graph holds only the four verbs that mean nothing on any other surface — auto-arrange,
save-selection-as-snippet, and now grouping/ungrouping a macro (P8-12) — everything that means the
same thing everywhere (copy/paste/cut/duplicate/repeat/select-all, both zoom pairs) is General
instead, so it can route through `resolveEditSurface()`.

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
| 1 / 3 / 4 / 5 / 7 / 8 | Switch the active edit tool: 1 Select, 3 Split, 4 Glue, 5 Erase, 7 Mute, 8 Draw (Cubase's own numbering — see [`timeline_panel_core.md §7`](timeline_panel_core.md)) |
| Option+1 | Jump to Locator 1 — parks the cursor on the LEFT loop locator (`timelineJumpToLocator1`) |
| Option+2 | Jump to Locator 2 — the RIGHT loop locator (`timelineJumpToLocator2`) |

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
JUCE bubbles an unhandled key. Under the timeline panel the only things that take keyboard focus are
the clip lane area and the piano roll — **not** the ruler, the track headers or the transport bar. So
setting the locators by dragging the ruler (the obvious way to do it) left focus on the canvas and
the keystroke died in `MainComponent::keyPressed`, which only dispatches commands.
`MainComponent::keyPressed` therefore ends with a **last-chance forward** of a two-id whitelist
(`forwardsToTimelinePanel`) back into `TimelinePanelComponent::keyPressed`. Deliberately a whitelist
and not a blanket forward: forwarding everything the panel resolves would make its bare letters and
tool digits (J/L/P/F, 1/3/4/5/7/8) fire while the graph canvas has focus, which is a different
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

**Three Timeline families share the digit row**, separated only by their modifier set: **bare** =
the edit tools, **Ctrl+Shift** = set the grid, **Option** = jump to a locator. Modifier equality on
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

All twelve are surface-resolved — consulted directly by `PianoRollComponent::keyPressed()`, never
dispatched through `ApplicationCommandManager`:

| Shortcut | Action |
|----------|--------|
| ← / → | Nudge Notes Left / Right — the whole selection, by one grid division (one snap cell; a sixteenth when snap is off) |
| ↑ / ↓ | Transpose Up / Down **one visible row** — the whole selection. A semitone normally; the next **scale degree** while *Show Only Scale Notes* is on, because the row set IS the scale then. One implementation either way (`transposeSelectedNotesByRow`, stepping `visiblePitches_` through the same `rowShiftedPitch` seam a drag uses), so an arrow key can never strand a note on a hidden out-of-scale row |
| Shift+↑ / Shift+↓ | Transpose Up / Down an Octave (12 semitones) — the same octave-jump convention every DAW uses, a separate action rather than a modifier read off the plain one so it can be rebound on its own |
| Alt+← / Alt+→ | Select Previous / Next Note — navigates BETWEEN notes in the clip's canonical (start, pitch) order, collapsing a multi-selection onto the outer neighbour; scrolls an off-screen target into view. Selection-only, never a document edit. Alt+↑/↓ is reserved (unclaimed) |
| **Q** | **Quantise Selected Notes** (`pianoRollQuantise`) — one-shot: snap the selected notes' STARTS (or all notes when nothing is selected) to the chosen grid, even while snap is toggled off. **Cubase parity:** on a note editor the bare, most reachable key belongs to the verb you use constantly, not to a switch you set once a session |
| Option+Shift+Q | Quantise Note Pitches to Scale (`pianoRollQuantisePitches`) — snaps the selected notes' PITCHES (or all notes when nothing is selected) into the scale picked in Scale Assist, via `MusicalScale::snapPitch`. Falls THROUGH (returns `false`) when no scale is chosen: "No scale" has nothing to quantise into. Matched BEFORE bare Q, since it is the more specific chord |
| J | Toggle Snap — grid magnetism on/off, the **shared** `timelineSnapToggle` the timeline panel also uses (one binding, one key, whichever surface has focus; deliberately NOT duplicated into a piano-roll action, since two "Toggle Snap" rows on the same key flipping the same flag is a Settings list nobody could reason about). **Magnetism only: the chosen grid stays VISIBLE either way** |
| Ctrl+S | Toggle the Scale Assist panel (`pianoRollToggleScalePanel`) — real Control, not Cmd (Cmd+S stays the app's save); inert while a text field inside the panel has focus |
| Option+S | **Show Only Scale Notes** (`pianoRollToggleScaleFilter`) — collapses the out-of-scale rows out of the grid, and makes ↑/↓ step by scale degree (above). One modifier away from Ctrl+S on purpose: adjacent verbs on adjacent chips should rhyme, and modifier equality is exact so they cannot collide. Remembered per clip; falls through with no clip open |

Every arrow/octave/nav action returns `false` (falls through) when nothing is selected, so the key
keeps whatever meaning it has elsewhere with an empty selection — nudge/transpose EDIT the
selection, the two navigation actions only MOVE it. Order matters only where one default is a
modified form of another (Shift+Up vs Up, Alt+Left vs Left): the more specific action is matched
first, so rebinding only one half of a pair can't let the other swallow it. `juce::KeyPress`
equality is exact on modifiers, which is what keeps Left/Shift+Left/Alt+Left three separate
actions. Digit keys are deliberately absent here — tool switching belongs to the panel (see
Timeline above), so the roll and the panel can never disagree about which tool is active.

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

**Every one of these keys has a header chip twin**, and each chip does exactly one thing on a plain
click (no modifier variants anywhere in the header any more): **Snap**, **Quantise**, **Quantise
Pitches**, **Scale** and **Show Only Scale Notes**. The first three and the last carry small drawn
vector glyphs rather than letters — "Q" for a grid toggle was the same letter the timeline binds to
snap, and a second "Q" beside it for pitch-quantise told the user nothing. See
[`timeline_panel_clips_automation.md §2`](timeline_panel_clips_automation.md).

2 (Range Selection), 6 (Zoom) and 9 (Play/Scrub) are Cubase tools this app doesn't ship yet and stay
**unassigned on purpose** — `editToolForKeyChar` (`Source/UI/EditTool.h`) returns `nullopt` for
them, so those three digits are simply never consumed rather than remapping the six shipped tools
onto 1–6. Shipping one of the missing three later costs no rebind: the digit is already reserved.

## Command vs surface actions

The 62 actions split into two kinds, and telling them apart is the key to reasoning about "why
doesn't this key do anything":

- **Command-dispatched** (38 actions) — every General action, all four Graph actions, and the Timeline
  category's eight grid-set + two grid-cycle commands. `AppCommands::getCommandForAction(actionId)`
  returns a real `juce::CommandID` for these; `MainComponent` implements
  `ApplicationCommandTarget`, so they appear in the native menu bar, drive toolbar tooltip text, and
  their enabled/disabled state is whatever `getCommandInfo` reports.
- **Surface-resolved** (24 actions) — the timeline panel's own keys (`timelineSnapToggle`,
  `timelineToggleLoop`, `timelineLoopSelection`, `timelineFollowPlayheadToggle`, the six
  `timelineTool*` digits, and the two `timelineJumpToLocator*` keys) plus every piano roll action.
  `AppCommands::getCommandForAction` returns `AppCommands::kNoCommand` (`0`,
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
(`Tests/ShortcutManagerTests.cpp`), walks every id a component is known to consult and asserts it
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
| Escape | AI panel, request in flight | Cancel the in-flight AI request (same as the Cancel button — actually aborts it, see [`AI_Engine_providers_accounts.md`](AI_Engine_providers_accounts.md#10-request-cancellation)) |
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
an idle panel — `Tests/FocusArbitrationTests.cpp`'s `DeletePerSurface` pins exactly this: a
clips-focused Delete never touches the graph, a graph-focused Delete never touches the clips, and an
empty selection on either falls through. `AIChatComponent::keyPressed()`'s Escape only acts while a
request is in flight; otherwise it is passed through so it keeps whatever meaning the enclosing
window gives it.

Arrow keys and the tool digits are split across two components with opposite rules:
`PianoRollComponent::keyPressed()` consumes an arrow **only when something is selected** (an
empty-selection arrow falls through, so it keeps whatever meaning it has elsewhere) and
deliberately does **not** consume the tool digits at all — tool switching belongs to the panel, so
the roll and the panel can never disagree about which tool is active. The whole selection nudges/
transposes by ONE shared delta (never per-note), clamped so the group stays inside the clip window
(`[0, clipLength)`) or the pitch range (`[0, 127]`) as a unit — the same "clamp the group together"
rule `TimelineClipLaneArea`'s cross-track move drag uses (see [`timeline_panel_clips_automation.md §1`](timeline_panel_clips_automation.md)).

## Canvas mouse gestures

Multi-select is layered on top of the existing pan gesture rather than replacing it, so no existing
habit changes. See [`layout_selection_canvas.md §1`](layout_selection_canvas.md) for the full contract.

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
