# Scale Assist

The piano roll's scale tooling: the docked panel, the scale engine behind it, the pitch-row
collapse it drives, and random note generation. The roll itself is [piano-roll](piano-roll.md).

## The panel

`Source/UI/PianoRoll/ScaleAssistPanel.h` (`synth::ui::ScaleAssistPanel`) is a deliberately dumb,
`kScalePanelWidth` = 170 px wide sibling docked left of the keys column, shown by the roll's header
"Scale" chip and hidden by default.

**It holds no reference to the roll, the doc, or any undo manager.** Every user action travels OUT
through a `std::function` callback (`onScaleChanged` for an own key, `onFollowSongKey`,
`onPitchVisibilityChanged`, `onGenerate`) and every piece of state the roll needs to push back IN —
switching clips, restoring a remembered scale — travels IN through `setSelection()`. That is the
same "panel owns presentation, owner owns the doc" split `PianoRollComponent` itself follows for
`TimelineViewState`.

Contents, top to bottom:

- a **Key** field (`KeyField`) that reads **Song key · D Dorian** in the muted text colour while the clip follows
  the song key, or the clip's own key ("A Phrygian") otherwise. It opens the shared key picker
  ([song-key](song-key.md#the-key-picker)) with a **Follow song key** row on top and no "No key" button: a clip's own
  key is always a key, and a clip with no key is one following a song that has none. The user's saved scales are in
  its **Your scales** group;
- a **Custom scales...** button that shows or hides the 12-toggle pitch-class plus name plus Save editor; saving makes
  the new scale the clip's own key at the current root;
- a **"Show only scale notes"** toggle — the same flag the roll's funnel chip drives, see **Show
  Only Scale Notes has one writer**;
- a **Note range** picker (`Source/UI/Layout/NoteRangePicker/`, [note-range-picker.md](../layout/note-range-picker.md): a four-octave keyboard strip, drag an edge to add or remove notes, drag the middle to move the range; it follows the panel's scale, and its root: a change of root moves both ends by the nearest step,
`synth::rangeFollowingRoot`, so C2 to C5, the default, reads D2 to D5 in D) plus a **Generate** button with an **"Add to existing"** toggle
  under it. The toggle sits under the button rather than beside it because at `kScalePanelWidth` a
  button-plus-checkbox row truncates the label the button's meaning depends on.

**Panel visibility persistence.** The panel's own open/closed state persists under the boolean key
`"pianoRollScalePanelVisible"` (default `false`) — distinct from the user-scales key below, and
owned by the roll rather than `ScaleAssistPanel` itself, since visibility is a roll-chrome decision
rather than scale data.

## Scrolling the sidebar

The panel is as tall as the roll, but a short roll can't show every control — the custom-scale
editor in particular grows the control stack taller than a cramped roll. The controls are
therefore NOT children of the panel directly: they live inside a `juce::Viewport`
(`scrollViewport_`, sized to `getLocalBounds()`) that views a single content component
(`scaleContent_`). `resized()` sizes `scaleContent_` to the panel width by
`contentNaturalHeight()` — the sum of every row's height plus inset, mirroring the row sizes
`layoutContentInto()` lays out with (a test pins the two together) — and then `layoutContentInto()` positions
the controls within that natural height.

Because the natural height is constant while the panel height varies, the vertical scrollbar shows
**only when the content is taller than the panel**: a roll tall enough to show everything offers no
scrollbar at all (the common case), a cramped roll shows one and the user scrolls within the
sidebar. The scrollbar is the viewport's own; it auto-occupies the 6px right inset rather than
narrowing the controls. No horizontal bar is shown — the width is fixed and never overflows.

The custom-scale editor's show/hide (`showCustomEditor`, fired by selecting the "Edit custom
scales..." row) toggles `customEditorVisible_` and calls `resized()`, so hiding it reclaims the
height it used and re-decides whether the panel still overflows — no stale scrollbar. The editor's
twelve keys, name field and Save button live in one `customBlock_` that fades in and out
(`FadeVisibility`, [animation](../layout/animation.md#fading-things-in-and-out)); the block is as tall as the fade
has got (`customBlockHeight()`, mirrored by `contentNaturalHeight()`), lays its rows out at full size from its top
and clips them, so the controls under it slide with the fade instead of jumping or overlapping. The panel's
background and right-edge hairline stay painted on the panel itself, behind the (transparent)
viewport, so nothing visually changes when scrolling is not needed.

## The scale engine

`Source/Music/MusicalScale.h` is deliberately header-only and free of any UI, Timeline or module
dependency (only `juce_core`); the piano roll and both sequencers share it. Only
`generateRandomNotes` needs `MidiNote`, so it lives in `Source/Timeline/ScaleNoteGenerator.h` as a
thin wrapper over the shared `pickPitches`.

`synth::MusicalScale` is a root pitch class (0=C…11=B) plus a 12-bit, root-RELATIVE interval mask —
bit *i* set means "root + i semitones, mod 12, is in the scale", so the same mask value describes a
scale's shape at any root. `contains(midiPitch)` and `snapPitch(midiPitch)` are the two queries
every caller needs; `snapPitch` walks outward from the pitch by increasing semitone distance and,
on an exact tie between an equidistant in-scale pitch above and below, resolves to the **lower**
one.

**A degenerate mask of 0 ("nothing is in scale") is handled explicitly everywhere** rather than
left to loop or crash: `contains` returns `false` for every pitch and `snapPitch` passes its input
straight through.

`builtInScalePresets()` is the fixed, indexed preset list above. User scales (`synth::UserScale {
name, mask }`) round-trip through `parseUserScales` / `serializeUserScales` — tolerant JSON: a
malformed entry is skipped individually, and a whole-string parse failure yields an empty list —
under the single properties key `"pianoRollUserScales"`, read and written by
`ScaleAssistPanel::setPropertiesFile()`, the same one-key idiom `NoteColour.h`'s own persistence
follows ([`layout/colour-overrides.md`](../layout/colour-overrides.md#note-colours)).

`setPropertiesFile(nullptr)` is a legal, permanent state: Save still works for the session, it just
never reaches disk — the same null-degrades-gracefully contract every other timeline
sub-component's setter follows.

Beyond what the piano roll uses, the same header carries the helpers the sequencers need:
`notesPerOctave`, `stepBy` (move N scale notes), `degreeOf`, `snapTable` (a 128-entry audio-thread
lookup), `makeScaleFromMask`, `pickPitches`, and in-scale chords (`chordInScale`,
`chordFromIntervals`, `chordName`, `romanNumeral`). See
[sequencer-design](../modules/sequencer-design.md#shared-scale-engine).

Tests: `Tests/Music/MusicalScaleTests.cpp` and `Tests/Music/MusicalScaleChordTests.cpp`.

## Following the song key or its own

A clip with no scale of its own follows the song key ([song-key](song-key.md)); new MIDI clips and clips in older
projects with no scale follow, and a clip that already had a scale keeps it as its own key. `synth::resolveClipKey`
(`Source/Timeline/ClipKey.h`) answers "which key does this clip use" for every caller: the roll's
`activeScaleForOpenClip()` is that resolved key, and highlighting, snapping new notes, Generate, quantise and the range
picker all read it. Writes never do: picking a key, the filter toggle and undo write `Clip::scale`, the own key, so
toggling "Show only scale notes" on a follower leaves it following. Choosing **Follow song key** clears the own key.

The song key reaches the roll through the timeline's 10 Hz transport poll (`PianoRollComponent::refreshSongKey()`): a
following clip whose shown key differs from the song key is re-shown, so its highlighting moves when the song key
changes. Nothing plays differently and no written note moves; only new notes snap.

**Snapping new notes.** A note drawn or double-clicked into a clip with a key lands on the nearest note of that key
(ties go down, `MusicalScale::snapPitch`). Holding Cmd places it where the pointer is. Moves, pastes, Generate's own
picks and the notes already in the clip never snap.

## The scale is saved on the clip

A clip owns its scale: `Clip::scale` (a root note plus the scale's notes and name; empty means it follows the song
key) and `Clip::scaleVisible` (the "Show only scale notes" flag). They are saved in the
project, so a clip opens on the scale it was left on after a restart. In the project file they are
two optional clip keys, `"scale": {"root", "mask", "name"}` and `"scaleVisible": true`, left out when
unset (the file version stays 1). A missing or unreadable scale loads as following the song key and never stops the
project loading; a root outside C to B is pulled into range and an empty mask means no scale. A scale
belongs to one clip: duplicating, splitting and copy/paste carry it, linked clips do not share it.

The piano roll keeps no copy. Every change goes through `PianoRollComponent::commitClipScale()`, which
calls `TimelineDoc::setClipScale()` as one undo step. It has to be an undo step: timeline undo restores
the whole document, so a scale change made outside the undo stack would be silently reverted by undoing an
earlier note edit. The same step marks the project changed, so autosave and the unsaved-changes prompt
see it. `openClip()` calls `restoreScaleForOpenClip()`, which shows the clip's saved scale in the panel
and the roll's own scale context, rebuilding `visiblePitches_` against it; `refreshFromDoc()` does the
same after an undo, but only when the panel disagrees with the clip (re-showing it also closes the
custom-scale editor).

## Pitch-visibility row collapse

`visiblePitches_` — the sorted, ascending list of every pitch that currently gets a drawn row — is
normally all 128 pitches. With "Show only scale notes" on and a real scale selected, it collapses
to exactly the scale's in-scale pitches **plus every pitch any note in the open clip already
uses**.

**That is the "notes stay visible" rule**: turning the toggle on must never hide a note that is
already there, only change which *additional*, currently-empty rows are offered for new ones.

Every row-index-based operation — vertical wheel-scroll, vertical zoom, the Up/Down and
Shift+Up/Down transpose-by-row deltas used while dragging a note — walks `visiblePitches_` by INDEX
rather than raw semitone arithmetic, for exactly this reason: with rows collapsed, a one-row move
can be a multi-semitone jump, and indexing by semitone would silently walk through hidden rows
instead of the next drawn one. `firstVisiblePitch_` is re-clamped to the nearest member of
`visiblePitches_` immediately on any rebuild, so it is always a real, currently-drawn row.

## Show Only Scale Notes has one writer

The roll's header chip, `Option+S` and the panel's checkbox are three views of one flag, and all
three route through `PianoRollComponent::toggleScaleFilter()`. It writes the open clip's
scale through `commitClipScale()`, pushes the scale context, and reflects the new value back into the panel via
`setSelection()`, which fires no callback by contract — it is a reflection, not an edit — so it
cannot loop back in.

The flag is saved per clip alongside the scale, and is deliberately NOT gated on a scale being
chosen: arming it first and picking the scale second is a real order of operations.
`isRowFilterActive()` (the flag AND a real scale) is the separate question anything behavioural
asks.

## Stepping by scale degree

↑/↓ step by ROW, which makes them scale-aware for free.
`PianoRollComponent::transposeSelectedNotesByRow(±1)` walks `visiblePitches_` and resolves each
note through the same `rowShiftedPitch` seam a Move drag's vertical half uses, with ONE shared row
delta clamped so the group stays in range — never per-note clamping, which would reshape a chord.

With the filter ON the row set IS the scale, so a step is the next **scale degree**: C→D is two
semitones, E→F is one, and an arrow key can no longer strand a note on a hidden out-of-scale row.
With the filter OFF `visiblePitches_` is all 128 and a row step *is* a semitone step, so chromatic
behaviour is preserved **by construction** rather than by a parallel code path that could drift.

The octave actions stay on `transposeSelectedNotes(±12)`: an octave is twelve semitones by
definition, not twelve degrees.

## Quantise pitches to the scale

Reached from the roll's **Quantise Pitches** chip or `Option+Shift+Q`, and from nowhere else — one
door, so nothing can disagree about its own enabled state.

Both entry points go through `quantisePitchesToActiveScale()`, which resolves the clip's scale via
`activeScaleForOpenClip()` and defers to `quantisePitchesToScale(scale)`. That snaps every note's
pitch via `MusicalScale::snapPitch` and writes back only the notes that actually moved
(`TimelineDoc::moveNote`, one `recordTimelineChange`) — a mutation that changes nothing pushes no
undo step, so an already-quantised clip costs zero history entries.

The selection is deliberately left untouched: this only ever moves pitches, never adds, removes or
reselects notes.

## Random generation

**Generate** (`onGenerate(minPitch, maxPitch, addToExisting)`) calls `generateRandomNotesIntoClip`,
which reads the RAW snap division regardless of the snap on/off toggle
(`TimelineViewState::divisionBeatsRaw` — the same "clean up notes drawn free-hand" reasoning
`isQuantiseEnabled` documents), falling back to a sixteenth (0.25 beats) when that division is Off
or 0, since generation always needs a concrete grid step to place notes on.

`synth::generateRandomNotes` (`Timeline/ScaleNoteGenerator.h`) then places one note per grid step from beat 0
until the step's start would fall at or past the clip's length, picking a pitch uniformly among the
in-scale pitches inside `[minPitch, maxPitch]` — every pitch in range when the scale is null or
chromatic. If no candidate pitch exists in that range at all, it returns an empty note list rather
than falling back to an out-of-range or out-of-scale pitch. The RNG is caller-owned and
default-seeded here: the panel's Generate button always wants a fresh draw, never a reproducible
one.

An **"Add to existing"** `juce::ToggleButton` under the Generate button picks between two modes. It
is session-only and starts **OFF**, so an embedding that never touches it sees the plain replace
behaviour. Its state travels OUT with the callback **by value** rather than being read back off the
panel, since the owner's handler mutates the doc and must not depend on the panel still being in
the same state — or alive — afterwards.

Either mode is ONE mutation and ONE undo step, and only the notes actually ADDED become the
selection (the diff, not the whole clip), which is what makes "generate again" reviewable:

- **OFF — replace** (the default): the single mutation clears every existing note in the clip and
  adds the fresh batch, so undo restores the old contents in one step rather than unwinding a
  clear-then-paste.
- **ON — add**: nothing is cleared, and a generated note that exactly duplicates an existing
  **(pitch, startBeat)** is SKIPPED. That is precisely the key a re-run collides on — generation
  walks the same grid steps every time, so without the check a second Generate would stack unison
  notes at the same offsets, invisible in the roll (one rect drawn over another) and unclickable
  apart. A note at the same start but a DIFFERENT pitch is a chord and is kept. A run in which
  everything was a duplicate adds nothing, selects nothing and pushes no undo step.

Tests: `Tests/UI/PianoRoll/PianoRollScaleAssistTests.cpp`.
