# Sequencer and Poly Sequencer: scales, step count and pattern tools (design)

Engineers' design for the next round of the two melodic step sequencers. **Status: designed, not
built.** Nothing here describes current behaviour unless it says "today". Choices still open are
marked **pending founder pick**, each with the recommended default the build assumes. The drum
step grid is a separate module with its own design; this doc covers only `SequencerModule` and
`PolySequencerModule`.

Current behaviour: [modules.md, Sequencer](modules.md#sequencer-module) and
[Poly Sequencer](modules.md#poly-sequencer-module). Card layout model:
[module-card-layout.md](../layout/module-card-layout.md). Motion rules:
[animation.md](../layout/animation.md#motion-rules).

---

## Today

- `SequencerModule`: 8 fixed steps, one step per beat, per step `Gate N`, `Pitch N` (0..127, 0 = rest),
  `F.Env N` (sent as CC 74); fixed velocity 100. `PolySequencerModule`: 8 steps of `Step N Root` +
  `Step N Chord` (Unison, Major, Minor, Maj7, Min7, 5ths, Octs, Random) + `Gate N`. Both have `run`,
  `bpm` and `syncToTransport` (last). Synced, the step index is `beat % 8`.
- Neither knows about scales. `Source/Timeline/MusicalScale.h` (header-only: root + 12-bit mask,
  `snapPitch`, built-in presets, user-scale JSON, `generateRandomNotes`) serves only the piano roll's
  `ScaleAssistPanel`. It includes `TimelineDoc.h` only for `MidiNote`.
- Both cards are bespoke (`ModuleComponentPaint.cpp`, `layoutSequencerStepColumn`): three rows of 8
  knobs, fixed 560x406, excluded from data-driven layouts (`cardBodyLayoutIsDataDriven`).

---

## Decisions

1. **Old projects sound identical.** Every new parameter is appended after `syncToTransport`, and every
   default reproduces today's schedule byte for byte (Steps 8, Rate 1/4, Scale "No scale", velocity
   100, chance 100, repeat 1, slide off, step on). `Pitch N` = 0 keeps meaning rest.
2. **One shared scale engine** in a new `Source/Music/` folder (still in the `Core` target), free of
   Timeline types. The piano roll and both sequencers use it; the built-in list and the user's saved
   scales are the same everywhere.
3. **Scale applies while playing** *(pending founder pick, recommended)*: stored pitches are never
   rewritten; the audio thread snaps each step's pitch (mono) or root (poly) through the scale. The
   card's drag and keyboard move by scale note. Alternatives: rewrite stored notes on scale change;
   store scale degrees (breaks the plain-note model and what the AI writes).
4. **Step count 1..16** *(pending founder pick, recommended)*, an int parameter. Steps 9..16 get their
   own parameters. 32 can be added later by appending steps 17..32 (append-safe). Alternatives: fixed
   8/16/32; 1..32 with two pages.
5. **Rate** (step length) is added as a choice so 16 steps can be one bar: `1/32, 1/16, 1/8, 1/4,
   1/2, 1/1`, default `1/4` (today's one-step-per-beat). Triplets can be appended later.
6. **Per-step lanes, first round** *(pending founder pick, recommended)*: Velocity, Chance, Repeat
   (ratchet 1..4), Slide (mono only), Step on. Later: timing nudge, "every Nth loop" conditions, swing.
7. **Poly chords from the scale** *(pending founder pick, recommended)*: three chord types appended to
   the Chord list (`Triad in scale`, `Seventh in scale`, `Sus4 in scale`), built by stacking scale
   notes from the (snapped) root. Fixed types keep their meaning. With No scale the in-scale types
   fall back to Major triad, Maj7 and sus4 intervals. The card shows the chord's numeral in the key.
8. **Card: lane tabs over one step grid** *(pending founder pick, recommended)*; the control row is
   layout data, the grid is a view (see [Card](#card)).
9. **Pattern tools on the card** *(pending founder pick, recommended)*: Randomize (with a small panel),
   Shift left/right, Reverse, Double, Up/Down one scale note. Each is one undo step.
10. **Each sequencer has its own key** *(pending founder pick, recommended)*. A song-wide key is a
    later feature that can sit on top of the same parameters.

---

## Parameters

Append order matters: it is the positional order every existing `getParameters()[n]` lookup and the
param-ID golden rely on. Choices are saved by name, so appending choice values is safe for projects;
a plugin host's normalised value shifts, which `ModuleBase::legacyChoiceCount()` already handles for
the Chord list.

### Sequencer (after `syncToTransport`)

| # | ID | Type, range | Default | Notes |
| :-- | :-- | :-- | :-- | :-- |
| 1 | `Steps` | int 1..16 | 8 | Steps that play before wrapping |
| 2 | `Rate` | choice `1/32 1/16 1/8 1/4 1/2 1/1` | `1/4` | Step length in note values |
| 3 | `Scale Root` | choice `C`..`B` | `C` | |
| 4 | `Scale` | choice `No scale`, the built-in presets in their current order, `Custom` | `No scale` | `No scale` = chromatic, identity snap |
| 5 | `Custom Scale` | int 0..4095 | 4095 | 12-bit root-relative mask, read only when Scale is `Custom` |
| 6 | `Gate 9`..`Gate 16` | float 0.1..1 | 0.5 | |
| 7 | `Pitch 9`..`Pitch 16` | int 0..127 | copy of steps 1..8 | So Steps 16 first plays the old pattern twice |
| 8 | `F.Env 9`..`F.Env 16` | float 0..1 | 0.5 | |
| 9 | `Velocity 1`..`Velocity 16` | int 1..127 | 100 | |
| 10 | `Chance 1`..`Chance 16` | int 0..100 (%) | 100 | |
| 11 | `Repeat 1`..`Repeat 16` | int 1..4 | 1 | Even hits inside the step, each `gate / n` long |
| 12 | `Slide 1`..`Slide 16` | bool | off | Next note-on is sent before this note-off (legato) |
| 13 | `Step On 1`..`Step On 16` | bool | on | Off mutes the step without losing its note |

### Poly Sequencer (after `syncToTransport`)

Same rows 1..5, then `Gate 9..16`, `Step 9..16 Root` (defaults copy steps 1..8), `Step 9..16 Chord`
(default Unison), `Step N Velocity`, `Step N Chance`, `Step N Repeat`, `Step N On` for N = 1..16. No
Slide. Chord list appended with `Triad in scale`, `Seventh in scale`, `Sus4 in scale`.

About 135 parameters on the Sequencer and 125 on the Poly Sequencer.

---

## Playback

- **Step clock.** Free-running: samples per step = `60 / bpm * sampleRate * rateBeats`; advance
  `currentStep = (currentStep + 1) % steps`. Synced: step boundary every `rateBeats` of ppq, index
  `floor(ppq / rateBeats) % steps`. With Steps 8 and Rate 1/4 both reduce to today's code paths;
  keep the legacy-schedule golden tests green unchanged.
- **Per step, at its boundary:** skip if `Step On` is off or pitch is 0; roll `Chance` (skip on miss);
  snap the pitch or root through the scale's 128-entry table; send CC 74 then note-on at `Velocity`;
  schedule `Repeat` hits at `stepLen * k / n`; Slide delays the note-off past the next note-on.
- **Audio-thread rules.** No allocation: the snap table is rebuilt only when Scale, Scale Root or
  Custom Scale change (compare on block start); chords are built into a fixed `std::array<int, 6>`.
  Chance uses a module-owned `juce::Random`, reseeded on `prepareToPlay` and on transport locate so
  an offline render of the same project is repeatable.
- `currentActiveStep` keeps feeding the card's playhead.

---

## Shared scale engine

`Source/Music/MusicalScale.h` (header-only, `namespace synth`, needs only `juce_core`):

```cpp
struct MusicalScale {
    int rootPitchClass = 0;
    std::uint16_t mask = 0xFFF;
    juce::String name;

    bool isChromatic() const noexcept;
    bool contains(int midiPitch) const noexcept;
    int snapPitch(int midiPitch) const noexcept;          // unchanged: nearest, ties to the lower
    int notesPerOctave() const noexcept;                  // popcount(mask)
    int stepBy(int midiPitch, int scaleSteps) const noexcept;  // move N scale notes up/down, clamped 0..127
    int degreeOf(int midiPitch) const noexcept;           // 0-based degree of the snapped pitch
    // Stacks `size` notes, every `stride` scale notes, from the snapped root. Returns the count written.
    int chordAt(int rootPitch, int size, int stride, std::array<int, 6>& out) const noexcept;
    std::array<std::int8_t, 128> snapTable() const noexcept;  // audio-thread lookup
};

const std::vector<ScalePreset>& builtInScalePresets();   // unchanged list and order
MusicalScale makeScale(int rootPitchClass, int presetIndex);
MusicalScale makeScaleFromMask(int rootPitchClass, std::uint16_t mask, juce::String name = {});
juce::String romanNumeral(const MusicalScale&, int rootPitch, int chordSize);  // "iv", "VI", "ii°"

// User scales: unchanged parse/serialize, same settings key the piano roll uses today.
std::vector<UserScale> parseUserScales(const juce::String&);
juce::String serializeUserScales(const std::vector<UserScale>&);

// Shared random picker; the piano roll and the sequencer's Randomize both call it.
std::vector<int> pickPitches(int count, int minPitch, int maxPitch, const MusicalScale*, juce::Random&);
```

`generateRandomNotes` moves to `Source/Timeline/ScaleNoteGenerator.h` as a thin wrapper that calls
`pickPitches` and builds `MidiNote`s, so `Source/Music/` never includes Timeline. The five current
includers switch to the new paths (no forwarding header). `MusicalScaleTests` moves to
`Tests/Music/` and gains tests for `stepBy`, `chordAt`, `romanNumeral` and `snapTable`.

---

## Pattern tools

Message-thread functions in a pure unit (`Source/Modules/Sequencer/SequencerPatternOps.h`) that take a
plain `SequencerPattern` value (the per-step arrays plus Steps) and return a new one; the card writes
the result back as parameter changes inside one undo transaction.

| Tool | Effect |
| :-- | :-- |
| Randomize | `pickPitches` within Lowest..Highest; "Steps that play" sets `Step On` by density; "Keep my rhythm" leaves `Step On` and Gate alone |
| Shift left / right | Rotate every lane by one within the active Steps |
| Reverse | Reverse every lane within the active Steps |
| Double | Copy steps 1..N into N+1..2N and set Steps to 2N; disabled when 2N > 16 |
| Up / Down | `stepBy(+/-1)` on every pitch or root (chromatic when No scale) |

The Randomize panel's range and density are card view state (not saved), like the piano roll's.

---

## Card

- The control row (Run, BPM, Rate, Steps as a `stepper`, Scale Root and Scale as choices, Sync as the
  footer pill) becomes the type's code-default `CardLayout`, so it gets hide/reorder like other cards.
  The Scale menu is a custom popup: No scale, built-ins, a "Your scales" group from the shared user
  scales, and "Edit custom scales..."; picking a user scale sets `Scale = Custom` and `Custom Scale`.
- A new view, `stepGrid`, in the view registry draws the tool row, the lane tab strip (a
  `CardSegmentedSwitch`), the grid and the step pads. It **owns** the per-step parameters: they are
  added to the plan's "edited elsewhere" rule when the view is present, so 100+ parameters never land
  in the More row.
- Lanes: Pitch (a mini roll with scale rows shaded and slide links between steps), Gate, Velocity,
  Chance, Repeat, Filter; Poly: Chord, Gate, Velocity, Chance, Repeat. Drag across bars draws in one
  stroke; Pitch drag moves by scale note.
- Size stays 560 wide and no taller than today's 406; unused steps beyond Steps are dimmed, never
  removed, so the card never resizes while a value changes.
- Removes the Sequencer branches from `ModuleComponentPaint.cpp`/`ModuleComponentLayout.cpp` and the
  two types from the `cardBodyLayoutIsDataDriven` exclusion.
- Accessibility: the grid is one Tab stop (Left/Right step, Up/Down value, Shift = octave on Pitch,
  Space = Step On), read as "Step 5 of 16, F2, on, chance 100 percent"; the lane strip is a radio
  group; every tool button has a tooltip and the same screen-reader name. Motion: bars glide 160 ms
  (`easeOutCubic`) on lane switch and tool actions; dimming 120 ms; popups grow from their button.
- The design system gains the step grid, lane strip and tool row.

---

## AI patch format

No contract change. A patch's `params` is an open record (`additionalProperties: true` in the
envelope), choices are written and read by name, and `validatePatch` checks keys against the live
module's parameter IDs, so the new IDs are valid the moment they exist. Updates needed: the param-ID
golden and the two card-body goldens. The module catalog and schema text grow by roughly 100 entries
per sequencer; measure prompt size with the AI harness, and if it matters, compact repeated per-step
entries in the catalog text (a formatting change, not a patch-format change).

---

## Risks

- **Parameter count** (~130 per module): long host automation lists, more state per save, and tool
  actions that change dozens of parameters must group into one undo step, not one per parameter.
- **Chance and offline renders**: without the reseed rule above, two bounces differ.
- **Slide** depends on the receiving instrument's legato/glide; on one without glide it only removes
  the gap between notes.
- **Scale snapping is invisible in the stored data**; the card must always show what plays (dashed
  outline for the written note when it differs).
- **Choice appends** shift host-normalised values; covered by `legacyChoiceCount()`, which needs an
  entry for the Chord list.
- **Card rewrite** touches a bespoke card with golden fixtures; keep the old component IDs for the
  parameters that keep widgets.

---

## Build tasks

1. Move the scale engine to `Source/Music/`, add `stepBy`, `chordAt`, `romanNumeral`, `snapTable`,
   `pickPitches`; piano roll unchanged.
2. Both sequencers: Steps, Rate, Scale Root, Scale, Custom Scale and steps 9..16; play-time snapping;
   legacy schedule goldens unchanged.
3. Per-step Velocity, Chance, Repeat, Step On (both), Slide (Sequencer), with seeded Chance.
4. Poly Sequencer in-scale chord types and numerals.
5. New card: code-default control row, `stepGrid` view with lanes, keyboard, screen reader, motion.
6. Pattern tools and the Randomize panel, one undo step each.
7. Shared scale menu with user scales in card and piano roll; update `modules.md`, the AI goldens and
   the design system.
