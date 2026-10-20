# Sequencer and Poly Sequencer: pattern, playback and tools (design)

How the two melodic step sequencers (`SequencerModule`, `PolySequencerModule`) work after round 2. **Status:
the engine is built** (pattern as module state, 1-128 steps, Rate, lanes, scales, legacy adoption, chance and
conditions, sample-accurate clock). **Next, not built:** the new card, the pattern tools and the chord picker; those
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

A "follows the song key" switch is planned as a new bool appended after these (never inserted).

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

## Pattern tools (next)

Not built. Message-thread functions in a pure unit (`Source/Modules/Sequencer/SequencerPatternOps.h`) that take a
`SequencerPattern` and return a new one; the card writes it back with `setPattern` inside one undo transaction.

| Tool | Effect |
| :-- | :-- |
| Randomize | `pickPitches` within Lowest..Highest; "Steps that play" sets `on` by density; "Keep my rhythm" leaves `on` and gate alone |
| Shift left / right | Rotate every field within the active `steps` |
| Reverse | Reverse every field within the active `steps` |
| Double | Copy steps 1..N into N+1..2N and set `steps` to 2N; disabled when 2N > 128 |
| Up / Down | `stepBy(+/-1)` on every pitch (chromatic when No scale) |

The Randomize panel's range and density are card view state (not saved), like the piano roll's.
`sequencerPlayedPitch` already gives the card the pitch a step really plays, to draw a dashed outline when it differs
from the written one.

---

## Card (next)

Not built; the old bespoke card still draws eight columns from parameters that no longer exist, so it shows nothing
until the new card lands.

- The control row (Run, BPM, Rate, Steps as a `stepper`, Key and Scale, Transpose, Gate, Velocity, Chance, Sync as
  the footer pill) becomes the type's code-default `CardLayout`. The Scale menu is a custom popup: No scale, the
  built-ins, a "Your scales" group from the shared user scales and "Edit custom scales..."; picking a user scale sets
  `scale = Custom` and the pattern's custom mask and name.
- A `stepGrid` view draws the tool row, the lane tab strip, the grid and the step pads and edits the pattern.
- Lanes: Pitch (a mini roll with scale rows shaded and slide links), Gate, Velocity, Chance, Repeat, Nudge,
  Condition, Filter; Poly: Chord, Gate, Velocity, Chance, Repeat, Nudge, Condition. Drag across bars draws in one stroke.
- Size stays 560 wide and no taller than 406; steps beyond `steps` are dimmed, never removed.
- Accessibility: the grid is one Tab stop (Left/Right step, Up/Down value, Shift = octave on Pitch, Space = step on),
  read as "Step 5 of 16, F2, on, chance 100 percent"; the lane strip is a radio group; every tool has a tooltip and a
  screen-reader name. Motion: bars glide 160 ms (`easeOutCubic`) on lane switch, dimming 120 ms.
- Removes the Sequencer branches from `ModuleComponentPaint.cpp` / `ModuleComponentLayout.cpp` and the two types from
  the `cardBodyLayoutIsDataDriven` exclusion; the design system gains the step grid, lane strip and tool row.

## Chord picker (next)

Not built. A Poly Sequencer step picks from 25 chords: the card groups them as Basic (the old eight), In key (the six
`Scale *` kinds, labelled with their roman numeral in the key) and Borrowed (the eleven fixed qualities).

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
