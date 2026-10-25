# Sequencer and Poly Sequencer: pattern, playback and tools (design)

How the two melodic step sequencers (`SequencerModule`, `PolySequencerModule`) work after round 2. **Status:
the engine and the card are built** (pattern as module state, 1-128 steps, Rate, lanes, scales, legacy adoption, chance and
conditions, sample-accurate clock). **Next, not built:** the pattern tools and the chord picker; those
sections say so. The drum step grid is a separate module with its own design.

Behaviour summary: [modules.md, Sequencer](modules.md#sequencer-module) and
[Poly Sequencer](modules.md#poly-sequencer-module). Card layout model:
[module-card-layout.md](../layout/module-card-layout.md). Motion rules:
[animation.md](../layout/animation.md#motion-rules). Wire format of the pattern:
[patch-format.md](../ai/patch-format.md#per-node-state).

---

## The pattern is module state

Round 1 had 24 per-step parameters per module and a fixed 8 steps. Round 2 holds the steps as **pattern data**
(`Source/Modules/Sequencer/SequencerPattern.h`) in the module's extra state, next to a handful of whole-pattern
parameters. 128 steps with ten settings each would have been over a thousand parameters (host automation lists,
catalog size, one undo step per knob); as state it is one value that the card edits and one undo step per gesture.

```
SequencerPattern { std::array<SequencerStep, 128> steps; uint32 seed; uint16 customMask; String customName; }
SequencerStep    { pitch 0-127, on, gate 0.05-1, velocity 1-127, chance 0-100, repeat 1-8, slide, nudge -0.5..0.5,
                   condition hit/of (1-8), filter 0-1, chord }
```

- **Default**: today's patterns (mono pitches F3 F4 Gb3 Db4 F3 A3 Gb3 C4, poly roots C3 E3 G3 C4 C3 G3 E3 C4, gate
  0.5, filter 0.5); steps 9-128 repeat steps 1-8, so raising `steps` first repeats the old pattern.
- **JSON** (`toVar` / `fromVar`): `{"version": 1, "seed": n, "customScale": {...}, "steps": [...]}`; a step writes only
  the fields that differ from a plain step, trailing steps equal to the factory step are dropped, gates and nudges keep
  three decimals. `fromVar` is the untrusted door too: it clamps every value, ignores unknown keys and wrong types,
  reads at most 128 steps and keeps the factory pattern for anything missing.
- **Seed**: random when the module is created and saved with the pattern; it feeds the chance rolls and the
  Random chord, so two renders of one project are identical.
- **Ownership and handoff**: the message thread owns the pattern (`getPattern()`, `setPattern()`); `setPattern` publishes a
  copy through `synth::LockFreeHandoff<SequencerPattern>` and bumps `patternGeneration()` (the card polls it). The audio
  thread adopts at the top of a block and never locks or allocates.
- **Edits are state**: an undo restores the pattern through `setExtraState` (re-applied only when it changed).

### Parameters

| ID | Type, range | Default | Notes |
| :-- | :-- | :-- | :-- |
| `run`, `bpm`, `syncToTransport` | as before | off, 120, off | |
| `steps` | int 1-128 | 8 | Steps that play before the pattern wraps |
| `rate` | choice, the shared note-length list | `1/4` | Step length; `1/4` with 8 steps is round 1 |
| `transpose` | int -24..24 | 0 | Semitones, before the scale snap |
| `gate`, `velocity`, `chance` | float 0-2, 0-2, 0-1 | 1 | Multiply each step's own value |
| `scaleRoot` | choice C..B ("Key") | C | |
| `scale` | `No scale`, the built-in presets, `Custom` | `No scale` | `Custom` uses the pattern's `customMask` |
| `followSongKey` | bool | on | Plays in the song key; off = `scaleRoot` and `scale` above ([Key](#key)) |

### Key

Built. A sequencer follows the song key ([song-key.md](../timeline/song-key.md#following-the-song-key)) or plays its
own key. `followSongKey` on (every new card): the steps snap to the song key, and No key plays every note. Off: they
snap to `scaleRoot` and `scale`, whatever the song key is. Changing the song key never rewrites a written pitch; it
only changes what plays.

- **Audio thread.** `renderPattern` resolves the scale every block before `SequencerScaleSnap::update`, whatever Sync
  says: a follower reads the song key from `TransportService::getSongKeyPacked()` (a lock-free mirror written by
  `setSongKey`) through its playhead and feeds it in as the Custom index with the key's mask. A playhead that is not
  our `TransportService` (a foreign host, a bare test) has no song key and plays every note.
- **Message thread.** `SequencerModuleBase::resolvedScale(songKey)` is the same rule for the card; `getOwnKey()` is
  the own key (nullopt for No scale); `ownKeyValuesFor(key)` maps a picked key to Key, Scale and, for a mask that is
  no preset (a user scale), Custom plus `customMask`. A user scale's name is not stored, so it reloads as "D Custom".
- **Old projects.** `applyStoredParamDefaults`: a stored node without `followSongKey` follows only if its `scale` is
  No scale, so a saved scale loads as its own key and nothing an old song played changes.

### Legacy adoption

The round 1 ids (`Pitch N`, `Gate N`, `F.Env N`; `Step N Root`, `Step N Chord`, `Gate N` for N = 1-8) are gone as
parameters. `ModuleBase::adoptLegacyParams` (called at the end of `applyParamsToProcessor`, trusted and untrusted)
reads them into the pattern: `Pitch` 0 turns the step off, a chord is read by name or index, every value is clamped.
`isLegacyParamId` keeps `validatePatch` from rejecting them. State is applied after params on every load path, so an
old save loads its eight steps, a new save (state, no old keys) is untouched, an AI patch from a backend that still
lists the old ids keeps working, and a step edited after an adoption is saved as state. The presets that wrote
`Pitch N` use the `state` form.

---

## Playback

Code: `SequencerClock` (position), `SequencerPlayer` (events), `SequencerScaleSnap` and `SequencerChords`.

- **Position in step units.** One step is `rate` beats. Synced: `ppq / rateBeats` for the block, split in two at a
  transport loop wrap. Free-running: advances by `numSamples / samplesPerStep` per block from 0 at the first running
  block (a restart goes back to 0).
- **Sample accurate.** Global step *k* (>= 0) plays pattern step `k % steps` at unit `k + nudge`, at its exact sample
  in both modes. Each repeat hit *j* of *n* is at `k + nudge + j/n`. The hits of a block are sorted by time before
  they are played, so a nudge can never put a note-off before its note-on. A hit is never fired twice; the memory of
  the last hit is dropped at a locate, a loop wrap, a clock source change and a stop.
- **Per hit:** skip if the step is off, if its condition fails (`(k / steps) % of != hit - 1`), or if the chance roll
  fails (`hash(seed, k) % 100 < chance * chanceAmount`; the roll is per step, so all repeats agree). Then CC 74
  (`round(filter * 127)`, Sequencer only), then the note-on(s) at `round(velocity * velocityAmount)` clamped 1-127. Pitch
  is `snap(clamp(pitch + transpose, 0, 127))` through the scale table.
- **Note lengths.** A hit lasts `gate * gateAmount / repeat` of a step (at least one sample). Note-offs wait in a
  fixed list of 256 held notes and cross blocks; a full list ends the oldest note now. A new hit first ends every note
  still held. Stop, bypass and a stopped transport end every held note once at sample 0.
- **Slide** (Sequencer only): the slid note is not ended by its gate; the next played note-on goes first and the
  note-off follows at the same sample (the same pitch just keeps sounding). It is held at most half a step past its
  own slot, so a slide into a rest does not stick. With repeats only the last hit slides.
- **Chords** (Poly): legacy and fixed kinds stack their intervals on the snapped root, unsnapped; `Scale *` kinds use
  `chordInScale` at the snapped root (No scale falls back to Major at the root); Random is the root plus one note up to
  11 above and one up to 11 below, from `hash(seed, k)`. Duplicate and out-of-range pitches are dropped.
- **Audio thread.** No locks, no allocation, no logging: the snap table is rebuilt only when key, scale or custom
  mask changed; hits and held notes live in fixed arrays (512 hits per block, 256 held notes).
- **Audition (preview) path** (`SequencerPreview`, shared by both modules). The message thread calls
  `previewNotes(notes, count, velocity)` (at most 8 notes) or `stopPreview()`; the request goes into a fixed slot guarded
  by a sequence counter (odd while being written, every field atomic). The audio thread reads it at the end of
  `processModuleBlock`, after the pattern rendered: a reader that sees the counter change keeps the old state and tries
  again next block, so nothing waits. A new request first ends the sounding preview (note-offs at sample 0), then
  plays the new notes (note-ons at sample 0) on the pattern's channel, and ends them 300 ms later, counted in samples. It
  works with Run on or off and while a transport-synced sequencer is stopped. A preview note-off is skipped for any pitch
  the pattern is holding at that moment (`SequencerPlayer::isHolding`), so the pattern's own note-off ends it and nothing
  hangs; if the pattern ends such a note first, the preview note ends with it. Bypass drops the request and ends what is
  sounding (the Sequencer has no mute parameter; a module that gains one is silenced the same way). The slot is unrelated to
  the pattern handoff, so a preview never waits on a pattern change.
- **Round 2 changes to the old sound.** The old free-running clock tested step boundaries once per block, so notes
  slid by up to a block and the first note-on was one sample late; the new clock puts them on the exact sample. CC 74
  is `round(0.5 * 127) = 64` for the default filter amount (was 63). The Poly Sequencer no longer forces a root below
  24 to C3 and sends no CC.
- `currentActiveStep` is the step of the last first hit, whether or not it played.

## Shared scale engine

Built (FRO1001). `Source/Music/MusicalScale.h` is header-only, `namespace synth`, and needs only
`juce_core`; the piano roll already uses it, the sequencers will.

```cpp
struct MusicalScale {                                 // root pitch class + 12-bit root-relative mask
    int rootPitchClass; std::uint16_t mask; juce::String name;
    bool isChromatic() const noexcept;
    bool contains(int midiPitch) const noexcept;
    int snapPitch(int midiPitch) const noexcept;      // nearest, ties to the lower
    int notesPerOctave() const noexcept;              // popcount(mask & 0xFFF)
    int stepBy(int midiPitch, int scaleSteps) const noexcept;  // snap, then move N scale notes; clamped 0..127
    int degreeOf(int midiPitch) const noexcept;       // 0-based degree of the snapped pitch; -1 if mask == 0
    std::array<std::int8_t, 128> snapTable() const noexcept;   // audio-thread lookup; build once per change
};

const std::vector<ScalePreset>& builtInScalePresets();
MusicalScale makeScale(int rootPitchClass, int presetIndex);
MusicalScale makeScaleFromMask(int rootPitchClass, std::uint16_t mask, juce::String name = {});
std::vector<UserScale> parseUserScales(const juce::String&);
juce::String serializeUserScales(const std::vector<UserScale>&);
std::vector<int> pickPitches(int count, int minPitch, int maxPitch, const MusicalScale*, juce::Random&);

enum class ScaleChordKind { Triad, Seventh, Sus2, Sus4, Add9, Sixth };
struct ScaleChord { int rootPitch; std::array<int, 6> notes; int count; };
ScaleChord chordInScale(const MusicalScale&, int rootPitch, ScaleChordKind) noexcept;
ScaleChord chordFromIntervals(int rootPitch, const std::vector<int>& semitones);   // outside the key
juce::String chordName(const ScaleChord&);                       // "C#", "Dm7", "Bm7b5"
juce::String romanNumeral(const MusicalScale&, const ScaleChord&);  // "ii", "vii°", "V7", "bVII"
```

- `chordInScale` snaps the root, then stacks scale notes at degree offsets (Triad 0,2,4; Seventh
  0,2,4,6; Sus2 0,1,4; Sus4 0,3,4; Add9 0,2,4,`notesPerOctave()+1`; Sixth 0,2,4,5). Notes above 127
  are dropped. Chromatic or empty scales fall back to Major intervals from the root.
- `romanNumeral` uses I..VII for 7-note scales when the chord root is in the scale; otherwise the
  chromatic distance from the scale root with Major names and a b/# prefix.
- `generateRandomNotes` lives in `Source/Timeline/ScaleNoteGenerator.h` as a thin wrapper over
  `pickPitches`, so `Source/Music/` never includes Timeline. Tests are in `Tests/Music/`.

---

## Pattern tools

Built. Pure functions in `Source/Modules/Sequencer/SequencerPatternOps.{h,cpp}` (namespace `synth::sequencer_ops`) take a
`SequencerPattern` and the number of active steps and return a new pattern; only steps `[0, steps)` change.

| Tool | Function | Effect |
| :-- | :-- | :-- |
| Randomize | `randomized(p, n, scale, RandomizeSettings, rng)` | `pickPitches` within the range; unless Keep my rhythm, each step plays with probability `density` (at least one always does); Keep my rhythm leaves `on` and gate alone |
| Shift left / right | `shiftLeft`, `shiftRight` | Rotate every field within the active steps, wrapping round |
| Reverse | `reversed` | Reverse every field within the active steps |
| Double | `doubled`, `canDouble` | Copy steps 1..N into N+1..2N; the card then sets `steps` to 2N; off when 2N > 128 (the card also disables it past 64 steps) |
| Up / Down | `movedByScaleStep` | `stepBy(+/-1)` on every pitch (one semitone with No scale, clamped to 0..127) |
| Fit to scale | `fitToScale`, `fitToScaleWouldChange` | `snapPitch` on every pitch; off with No scale and when nothing would change |

**The row** (`StepGridTools`, `Source/UI/ModuleViews/StepGrid/`) is its own 28 px row at the top of the step grid, above the
page strip, with eight 24 px chips right-aligned: Randomize, shift left, shift right, Reverse, Double, up, down, Fit to
scale. Every button is a Tab stop with a focus ring; its tooltip is its screen-reader name ("Randomize notes in the scale",
"Shift pattern one step left", "Reverse pattern", "Copy the steps into a pattern twice as long", "Move every note up one
scale note", "Rewrite the notes into the scale", and so on).

**One click is one undo step that glides.** `StepGridComponent::applyTool` computes the new pattern, writes it with
`setPattern`, records the module's extra state before and after (`StepGridHooks::recordEdit`) and starts the bar glide
itself, because the grid's own edits do not glide. The pitch tools work on the pitch a step *plays* (written plus
Transpose), so a transposed pattern moves and fits as it sounds. **Double** also sets the Steps parameter: it records
through `StepGridHooks::recordEditWithParameter` (`AppUndoManager::recordNodeExtraStateAndParameterChange`), which puts the
pattern change and the parameter change in one transaction, so a single undo gives both back.

**Randomize** opens a 250 px popover (`StepGridRandomizePanel`, a `juce::CallOutBox` with `CalloutReveal`) with the note range
picker ([note-range-picker.md](../layout/note-range-picker.md)), a "Steps that play" slider (75 percent by default), the
"Keep my rhythm" pill (on by default) and Roll. Roll applies `randomized` once and each Roll is one undo step; the popover
stays open between rolls. Esc closes it. The range, density and rhythm choice live on the grid for the session and are not
saved; the range starts at C2 to C4 (the grid's note numbering), widened to cover the pattern's notes.

---

## Poly Sequencer chords

Built. A step stores a chord *type* (`SequencerChord`); its notes are built on the step's played pitch.

- **The label** over each bar is the chord's name (`chordName`) with its roman numeral (`romanNumeral`) smaller and muted when
  a scale is set and both fit the column, e.g. "Cmaj7" with "Imaj7"; a single note stays the note name. When a chord changes
  (a pick, undo, redo) the old and new label cross-fade in 120 ms (80 under Reduce Motion). The step's screen-reader text
  gains ", chord <name>".
- **The picker** (`StepGridChordPicker`, 250 px, `juce::CallOutBox` with `CalloutReveal`) opens from a click on the label
  (tooltip "Choose a chord for this step") or Enter on a focused step in the Pitch lane. Header "Chords that fit C major on C"
  ("Chords on C" with No scale). Rows: Triad, Seventh, Sus (sus2, sus4), Add (add9, sixth), then "Outside the key": Borrowed
  (the fixed qualities whose notes differ from every in-key chip) and More (Unison, 5ths, Octs, Random, so older chords stay
  reachable). The current chord is accent-highlighted. Up/Down move between rows, Left/Right between chips, Enter picks, Esc
  closes. A pick sets `step.chord` as one undo step; chord ids are unchanged, so old projects load identically.
- **Hover audition.** Resting the pointer on a chip (or moving the keyboard onto one) for 60 ms plays that chord through the
  module's output, as the step would play it: `sequencerChordNotes` at the step's played root, velocity 0.8. Moving across
  chips plays only the one the pointer settles on; leaving the chips, picking and closing end the sound. The chip light-up is
  unchanged. The picker reports the settled chip (`onAudition`); `StepGridComponent::auditionChord` turns it into
  `previewNotes` / `stopPreview` on the module. The audio side is the audition path under [Playback](#playback).
- The contents and names are pure (`StepGridChords.{h,cpp}`) and unit-tested.

---

## Card

Built. Both sequencers are ordinary layout cards: no bespoke branch is left in `ModuleComponent`.

- **Layout.** The type's code default (`DefaultCardLayoutsSequencers.cpp`) is four `row` sections (Play: Run and Sync
  as pills and BPM as a fader that swaps out while Sync is on; Time: Rate and Steps; Notes: Key; Pattern:
  Transpose, Gate, Velocity, Chance) and then the `stepGrid` view. Consecutive `row` sections sit side by side and wrap
  only when the 560 px card is too narrow ([module-card-layout.md](../layout/module-card-layout.md#rendering)).
  Steps is a stepper that can also be typed: double-click the value, or press a digit key with a button focused.
- **Key** (`Source/UI/ModuleViews/SequencerKey/SequencerKeyField`, on the shared `Layout/FollowKeyField`): the Key
  cell (any layout that names `scaleRoot`, a saved one too) holds a field that reads "Song key · D Dorian" in the
  muted colour while following and "A Phrygian · own" otherwise. It opens the key picker with a Follow song key row on
  top. A pick writes Key, Scale, `followSongKey` (and `customMask`) as one undo step; Follow song key turns following
  on and keeps the own key for later. `scale` and `followSongKey` get no widget of their own. The card's 15 Hz tick
  reads the song key from the app's transport (never the node's playhead, which a card built on load or with no audio
  device does not have yet) and hands it to the field and the grid. When the followed key changes the field glows
  once (300 ms; none under reduced motion). The field is bound to no single parameter, so it has no MIDI Learn.
- **Randomize range** starts two octaves up from the resolved key's root in octave 2 (C2 to C4 with no key, D2 to
  D4 in D; the grid's numbering), widened to take in the notes the pattern plays. A key change moves it until the
  range is touched.
- **The grid** (`Source/UI/ModuleViews/StepGrid/`): the pattern tools row ([Pattern tools](#pattern-tools)), a page strip, 16 columns of the viewed page, the Pitch lane as a
  mini piano roll, a row of step pads, the lane chips ([lane-chips.md](../layout/lane-chips.md)) and the other lanes
  stacked under them. Steps past `steps` are dimmed, never removed.
  - **Pitch** shows each step as a bar at the pitch it plays (scale rows shaded, the key's root tinted, the note name or
    chord label over the bar); when the scale moved the note, the written note is a dashed outline. A drag lands only on
    scale notes (`MusicalScale::snapPitch`; chromatic with No scale); Shift moves by octaves; the written pitch is the
    played pitch minus Transpose. The rows are frozen while a drag is in progress.
  - **Pads** turn a step on or off; a stroke across them paints one state. **Gate, Velocity, Chance, Repeat, Nudge,
    Filter** are bar lanes: drag across them to draw, interpolating between the columns the pointer skipped.
    **Slide** (mono only) toggles the link between a step and the next. **Condition** opens a menu of `1/1` ... `8/8`.
  - Lanes shown default to Pitch and Velocity; the choice is saved with the node
    (`CardViewState::stepGridLanes`). Filter stays on the Poly Sequencer although it sends no CC 74 and so has no
    audible effect there.
  - **Pages.** The strip shows one bar per 16 steps once Steps passes 16 (its row is always reserved, so the card never
    changes height when Steps crosses 16), the page holding the playhead pulses, and the Follow pill (off by default)
    turns the viewed page with the playhead.
- **Undo.** A gesture (a pad stroke, a bar drag, a pitch drag, a menu pick, one key press) is one undo step: the
  module's extra state is snapshotted when the gesture starts and recorded once at its end
  (`AppUndoManager::recordNodeExtraStateChange`). A change from outside (undo, a preset, the AI) is not an edit; the
  bars glide to it.
- **Motion** (all time-bounded, none free-running): lanes fold 160 ms (the card's height follows every frame, the
  neighbours make room once per direction), the columns slide 160 ms between pages, bars glide 160 ms with a 4 ms
  per-column stagger, dimming 120 ms, the page pulse 220 ms. Reduce Motion shortens them to 80 ms without the stagger;
  Animations Off lands at once.
- **Accessibility.** The grid is one Tab stop, read as "Step 5 of 16, F2, on, chance 100 percent". Keys: Left/Right
  pick the step (the page follows), Up/Down change the focused lane's value (Pitch: one scale note, Shift: one octave;
  Slide: toggles; Condition: next or previous condition), Space turns the step on or off (Slide lane: the link), Enter
  opens the Condition menu or acts as Space, Page Up/Down move to the lane above or below, Home/End the first and last
  step. The page strip and the chip row are Tab stops of their own.
- **Size.** 560 wide; the card is 482 tall at the default lanes (Pitch and Velocity; the grid is 256 px, 28 of them the tools row), 758 with every lane (730 on the Poly Sequencer, which has no Slide lane); a 17+ step pattern adds nothing because the page strip's row is always reserved. The Row sections take two lines at 560 px: Play and Time on the first, Notes and Pattern on the second.

## Chord picker

Built: see [Poly Sequencer chords](#poly-sequencer-chords). A step picks from 25 chord types; the picker offers the ones in
the key first (Triad, Seventh, Sus, Add), then Borrowed (the fixed qualities) and More.

---

## AI

The pattern is the node's `state`, accepted on the untrusted path and clamped ([patch-format.md](../ai/patch-format.md#per-node-state));
`params` carries `steps`, `rate`, `scale` and the rest. The module catalog lost 48 (Sequencer) and 24 (Poly) per-step
entries and gained eleven whole-pattern ones; the system prompt names the `state` shape in one bullet. The hosted
backend's catalog and its patch envelope (which does not list `state` on nodes) are separate work.

---

## Risks

- **Scale snapping is invisible in the stored data**; the card must always show what plays.
- **Slide** depends on the receiving instrument's legato; on one without glide it only removes the gap between notes.
- **A model that sends `state` replaces the whole pattern**; the current patch it sees includes the sequencer's
  `state`, so a change should send the full steps it wants to keep.
- **The patch envelope** (generated from the backend's schema) lists no `state` on a node, so a constrained local
  decoder cannot emit one until the backend adds it; `params` (`steps`, `rate`, `scale`...) and the old step keys work
  meanwhile.
- **Chance and the seed**: copying a module keeps its seed, so two copies roll alike until the copy is re-seeded.
