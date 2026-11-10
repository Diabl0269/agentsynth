# Filter: Custom Type

The Filter module's eighth type (`filterType` = `Custom`, `FilterModule::kCustomTypeIndex == 7`,
appended so saved indices 0-6 are unchanged). Instead of the ladder/notch, the signal goes through one
FIR kernel designed from user-drawn magnitude shapes. The rest of the Filter is in
[`modules.md`](modules.md#filter-module); its raw channels are in
[`poly-channel-layout.md`](poly-channel-layout.md#channel-table).

Code: `Source/Modules/Filter/` — `CustomFilterShapes` (data model), `CustomFilterKernelDesigner`
(minimum-phase design), `PartitionedConvolver` (convolution), `CustomFilterEngine` (handoff, blend,
redesign policy; its per-voice path in `CustomFilterEngineVoices.cpp`), `CustomFilterBlend` (the blend as
numbers), `CustomFilterCutoff` (what Cutoff and Resonance mean, as pure functions), `CustomFilterPerNote` (per-note settings) and `CustomFilterVoiceCursors` (per-voice note spotting and
glide). `FilterModule::processCustomMode` wires it in. Shapes
reach the module through its extra state (`setCustomShapes` / `setExtraState`) and are edited in the
[shape editor window](#shape-editor-window).

## Shapes

- A shape is a name, a curve and a map position. The curve reuses the LFO's point model
  (`synth::LfoCustomWave`: up to 64 `(x, y, bend)` points, same sanitising): `x` is log frequency,
  20 Hz at 0 to 20 kHz at 1; `y` maps linearly to −48..+12 dB, so `y = 0.8` is 0 dB. Outside
  20 Hz–20 kHz the curve holds its end values.
- The position is a list of coordinates in 0..1 (two read today; more are kept and clamped, for a
  later 3D map).
- No fixed shape count; at least one is always kept. A new Filter starts with three vowel shapes,
  `ah` (0.2, 0.25), `ee` (0.8, 0.25) and `oo` (0.5, 0.8), whose peaks stay under the gain audit's
  +6 dB ceiling (the gain audit sweeps `filterType`), so switching to Custom sounds at once at a level close to the
  other types.
- **Vowel shapes**: each vowel is a few formant peaks over deep valleys. The peak frequencies and relative levels come
  from the Csound manual's tenor formant table (a, e, i, u); `ih` is not in that table and uses Peterson and Barney's
  male F1-F3 (390, 1990, 2550 Hz). The bandwidths are filter-style, wider than the table's 40-130 Hz: those are for
  a synthesiser that imposes formants whatever the pitch, but a filter's peak narrower than the harmonic spacing
  falls between a low note's harmonics and the vowel vanishes. Here F1 is 110-160 Hz wide (about a third of an octave),
  F2 100-190 Hz and F3/F4 about 250 Hz, all well under half an octave. Every formant is its -3 dB points and its peak,
  with bent skirts into a deep valley (at least 20 dB below F1 between F1 and F2, at least 10 dB between F2 and F3
  for ee and ih); ah and oo draw F3 and F4 as one shoulder. Each curve is 16-19 points. The five curves are
  loudness-matched: each is lowered until a band-limited saw at 110 Hz and at 220 Hz comes out within about 2.5 dB of
  the others (and of the old ah), so swapping or morphing vowels does not jump in level; the loudest peak is at most
  +2.3 dB, under the +6 dB ceiling of the gain audit. Projects that stored shapes keep the curves they saved; only a
  Filter still on the defaults gets the current ones.

## Shared with the LFO

The LFO's Custom wave is the same idea over phase ([`lfo.md`](lfo.md)), so the pieces that do not depend on what a
curve means live in `Source/Modules/Shapes/` and both modules use them:

- `morph::Shape` (name, `LfoCustomWave` curve, map position) is `CustomFilterShape`; `morph::blendWeights` is the
  inverse-distance blend below (`CustomFilterEngine::blendWeights` calls it); `morph::shapeToVar`,
  `shapesFromVar` (trusted and untrusted caps) and `sanitiseShapes` are the JSON and sanitising rules of both lists.
  The caps (`kMaxShapes` 64, 16 untrusted, 24-character names, 3 map values) are `CustomFilterShapes::k...` aliases.
- `LockFreeHandoff<T>` is the pending slot and retire FIFO that `CustomFilterEngine` uses to give the audio thread a
  new table without allocating or freeing there; `LfoShapeBlender` uses the same one.
- The shape list editor window and morph map take any `ShapeListAdapter`: `FilterShapesAdapter` and `LfoShapesAdapter`.

What stays per module: the Filter's dB/Hz curve meaning, presets, defaults and format version
(`CustomFilterShapes`), the LFO's phase curve and plain-wave save format (`LfoCustomShapes`).

## Parameters

| ID | Range | Default | Effect |
|---|---|---|---|
| `morphX`, `morphY` | 0–1 | 0.5 | The morph cursor on the shape map |
| `shift` | −48..+48 st | 0 | Moves the whole response along frequency: bin f reads the curve at f / 2^(shift/12) |
| `depth` | 0–2 | 1 | Multiplies every dB value; 0 is flat (0 dB), 2 doubles every boost and cut |
| `cutoff` | 20–20000 Hz | 440 | Moves the whole drawn response along frequency, on top of Shift: a further `12 × log2(cutoff / 440)` semitones, so 440 Hz (the default) moves nothing and 1760 Hz moves it up two octaves |
| `resonance` | 0–1 | 0.1 | Adds a resonant bell at the cutoff frequency, after Depth: see [Cutoff and Resonance](#cutoff-and-resonance) |

The four Custom parameters are added after Key Track (Level and Mute stay last) and each has a CV jack; Cutoff and
Resonance keep their old jacks. Drive is ignored in Custom. A patch saved with Custom and a Cutoff other than 440 Hz
(or a Resonance above 0.1) sounds different from before Cutoff and Resonance were used: the response is moved and gets
its bell. **Key Track** adds `keyTrack × 12 × log2(pitch / 261.63 Hz)` semitones of shift, **mono only**:
in poly the voices share one kernel unless a [per-note mode](#per-note-cursors) is on, and Key Track does not
follow a voice's pitch there either. In mono with a per-note mode on, the shift applies to the note's own kernel too.

## Cutoff and Resonance

Both are read once per block like Shift and Depth (the knob plus its CV: Cutoff's CV sweeps exponentially as in the
other types, +1 reaching 20 kHz and -1 20 Hz; Resonance's CV adds to the knob, held to 0..1) and are shared by every poly
voice, per-note mode included. They are not Key Track targets.

- **Cutoff** adds `12 × log2(cutoff / 440)` semitones to Shift (and Key Track). The designer applies it, so
  `CustomFilterKernelDesigner::design(grid, shift, depth, resonance, cutoffHz, out)` is the whole recipe.
- **Resonance** adds, after Depth, a resonant peak at the cutoff frequency itself (the bell is not moved by the
  shift Cutoff causes). Its height is `customfilter::resonancePeakDb`: about 20 dB per unit of Resonance up to 0.5,
  then steeper (1.7 dB at 0.1, 4.4 at 0.25, 10 at 0.5, 17.8 at 0.75, 24.9 at 0.9), held to 32 dB at 1. Its shape is
  the gain a resonant two-pole low-pass of that Q has over one of Q 1, so it is symmetric in octaves and its -3 dB
  width is about 1.44 / Q octaves (1.56 octaves at 0.25, 0.51 at 0.5, 0.19 at 0.75, 0.083 at 0.9). Both were fitted
  to an offline measurement of a commercial analogue-style low-pass (12 and 24 dB per octave behave the same) at a
  1 kHz cutoff; `CustomFilterResonanceLawTests` holds the measured table and checks the law against it (peak within
  1 dB, width within 12 % from 0.5 up; below that the measured width is set by the low-pass's own skirt). That filter
  self-oscillates at Resonance 1, and a 2048-tap kernel cannot hold a ring that narrow, so ours stops at 32 dB (the
  same synth's morphing 12 dB type reaches 33.6 dB there). Even so, at 1 the heard peak is about 1.5 dB under the
  drawn one at a 1 kHz cutoff, and more at lower cutoffs, because the ring outlasts the kernel; up to 0.9 the two
  agree within 1 dB. The default 0.1 adds about 1.7 dB. Depth 0 with resonance up still gives a peak on a flat
  response, like a resonant filter, and a dip drawn at the cutoff lowers the peak by the same dB.
- **One definition.** `customfilter::cutoffShiftSemitones`, `cutoffX` and `resonanceBellDb`
  (`CustomFilterCutoff.h`) are the only definitions; the designer and `customfilter::responseGridDb(shapes, x, y, shift,
  depth, cutoffHz, resonance)` (the card's response view) both call them, so the curve drawn is the curve designed. At
  cutoff 440 Hz and resonance 0 the grid equals the older `responseGridDb(shapes, x, y, shift, depth)` exactly.
- **Redesign.** The engine redesigns when the cutoff's shift moves by more than 0.001 semitone or the resonance by more
  than 0.0001, like Shift and Depth, in the shared and the per-voice path alike; the new kernel crossfades in over the
  block, so a fast cutoff LFO makes no click. `design()` allocates nothing, as before.

## CV jacks

Morph X CV = ch28 (`kMorphXCVChannel`), Morph Y CV = ch29, Shift CV = ch30, Depth CV = ch31, each the
**same channel in both voice modes**, after Key Track in the jack order. Shift and Depth have CV too
because every continuous knob on a card must accept a modulation cable. All four are **knob-only**
targets (`ModulationTarget::knobOnly`): they never draw a gutter jack, in any filter type, so a cable
reaches Shift and Depth only by being dropped on their knob (it lands on the ring), and Morph X and Y on the
[morph map's slider](#card) while the map shows, else on their knob. A Filter of any other type
keeps exactly its old jack column and card height. They cannot follow
Cutoff's mono/poly pair: the mono CV block (ch1-3) has the poly voice fan directly above it and the poly block
(ch8-10) has Audio R, so neither can grow without renumbering saved channels; Key Track set the
precedent of one appended shared channel. CV moves the knob in its normalised range
(`ModuleBase::modulateNormalised`). The Filter now declares 32 channels in and out.

## Blend and kernel design

Once per block, morph, shift, depth, cutoff and resonance (knob plus CV, read at the block's first sample) are compared
with the values the live kernel was designed for; a redesign happens only when one moved by more than a
small epsilon or the shapes changed.

1. **Weights**: inverse distance, `w_i = 1 / max(d_i, 0.02)^2.5`, normalised (`d_i` = 2D distance from
   the cursor to shape i).
2. **Blend in dB, peaks moved together** (`CustomFilterPeakMorph`): each shape's dB curve is pre-rendered on a
   1024-point log-frequency grid, then summed with those weights after its peaks are moved, so a morph from ah to oo
   glides each formant from one frequency to the other (a "yah"/"wah") instead of fading one vowel's formants out
   while the other's fade in. Kernels are never blended: a sum of minimum-phase kernels is neither minimum phase nor
   the blended magnitude. [Peak morph](#peak-morph) has the rules.
3. **Shift, cutoff, depth and resonance**: each FFT bin reads the blended curve at its shifted log-frequency position
   (Shift plus the semitones Cutoff adds; linear interpolation), the dB is multiplied by depth, and then the
   [resonant bell](#cutoff-and-resonance) is added.
4. **Minimum phase by the real cepstrum**: FFT size N = 4L = 8192, kernel length L = 2048. ln|H| →
   inverse FFT (real cepstrum) → fold onto positive quefrencies → FFT → complex exp → inverse FFT;
   the first L samples, with a half-Hann fade over the last 256, are the kernel. The magnitude matches
   the curve within 1 dB from 100 Hz to 10 kHz for a smooth shape
   (`CustomFilterEngine.ImpulseResponseFollowsTheDrawnShapeWithin1dB`).

`CustomFilterKernelDesigner` is its own class so the design can move to a worker thread later. All
its buffers and its `juce::dsp::FFT` are allocated in `prepare()`; `design()` does no allocation, locks
or system calls (on macOS JUCE's FFT is vDSP; elsewhere its fallback FFT takes an uncontended
per-instance `SpinLock` and uses `alloca` scratch at these sizes).

### Peak morph

- **Peaks, found once per shape list** (`renderTable`, message thread): local maxima of a shape's grid away from its
  two ends whose topographic prominence (height above the higher of the two lowest points met walking out to higher
  ground) is at least `kMinPeakProminenceDb`; a ripple on a skirt is not a peak. At most 6 per shape, kept in
  ascending frequency in the table. ah and oo have three, ee, eh and ih four, the animal calls fewer.
- **Matching by order**: every shape's first peak goes with every other shape's first peak, and so on. Shape s's
  k-th peak is moved to `p_s + sum(w_s' * (p_s' - p_s))` over the shapes s' that have a k-th peak, in log frequency.
  When every shape has that peak, that is the weighted mean, the same for all, so the peaks meet.
- **Fallback**: a peak only some shapes have is pulled only by them; the rest of the weight leaves it where it is,
  so it fades in and out as the plain cross-fade did.
- **Warp**: each shape's curve is stretched piecewise-linearly in log frequency (grid ends fixed, its peaks on their
  targets) and read by linear interpolation, then the dB values are weighted and summed. Only frequencies move, so
  the blend is never louder than the loudest shape (the gain rule).
- **Continuity**: the targets have no participation threshold and no division by a weight sum, so they move
  continuously with the cursor everywhere, including where a shape with fewer peaks joins. When order matching
  folds (a heavy shape's only peak sits above another shape's second peak), the targets are pushed apart by a
  running max and min, also continuous, that keeps every stretch of a shape at least `kMinStretch` (25%) of its own
  width: squeezed into a single bin, a valley would sweep its whole depth past each grid point as the cursor moved.
  One shape at full weight is copied exactly.
- **One blend, three callers**: the shared kernel, every per-voice kernel (`designKernel`) and `blendedGridDb` (the
  response view, "add a shape here") call `customfilter::blendShapes`, so what is drawn is what is heard.
- The LFO's Custom wave (`LfoShapeBlender`) still cross-fades its shapes.
- Tests: `CustomFilterPeakMorphTests.cpp` (detection, one F1 at the log-mean halfway, a monotone glide, continuity by
  step halving, exact copy at full weight, no added gain, a saw swept ah to oo with no click, UI blend equal to the
  engine's).

## Convolution

`juce::dsp::Convolution` was checked and not used: `loadImpulseResponse` moves an `AudioBuffer` into a
`std::function` (allocation on the calling thread), the new engine is built on its
`ConvolutionMessageQueue`'s own background thread (not something a block can pump), and its swap is a
fixed ~50 ms smoothed crossfade — so a kernel cannot become live within one block without audio-thread
allocation.

`PartitionedConvolver` instead:

- **Zero latency**: the first P = 64 taps run as a direct-form FIR per sample; the remaining taps are
  split into 64-tap partitions, uniform-partitioned overlap-save with 128-point FFTs. Partition m
  starts at tap 64 + 64m, so its share of an output sub-block depends only on input sub-blocks that are
  already complete.
- **Kernel groups**: both legs × up to 8 voices (16 channels), each with its own history and frequency-domain
  delay line, read one of nine kernels: group 0 is the shared kernel every channel starts on, groups 1-8 belong
  to the voices in a [per-note mode](#per-note-cursors) (both legs of voice v read group v + 1; mono is voice 0,
  so its two legs read group 1). Each group swaps and crossfades on its own. With every channel on group 0
  (per-note Off, or before the first note) the arithmetic is
  the old single-kernel path plus one index lookup per channel (`CustomFilterEngine.DISABLED_CpuCost` measures
  the same cost as before). A channel can move to another group between any two samples, because the delay line
  holds input spectra only; the group it moves onto starts as a copy of the shared kernel, so the move is silent.
- **Synchronous, block-aligned swap**: a new kernel goes into the second kernel slot and the output
  crossfades linearly from old to new across the block it arrives in (at least 64 samples; a redesign
  waits while a fade is still running). The delay line holds input spectra only, so the new kernel is
  exact from its first sample and the output equals the new kernel's from the next block on
  (`PartitionedConvolver.SwapCrossfadesThenMatchesNewKernelExactly`, `FilterCustom.MorphXParameterStepShowsUpWithinTwoBlocks`).
- **Idle channels are free**: a channel whose input has been silent for longer than the kernel plus two
  sub-blocks is skipped (its output is zero for any kernel).
- History is cleared on entering Custom or changing the voice count, so a tail from an earlier stint
  never plays.

## Shape handoff

Shape edits happen on the message thread (`FilterModule::setCustomShapes` / `setExtraState`). The engine
renders the list into an immutable table (dB grid + positions) and exchanges it into an atomic pending
slot; a table the audio thread never took is freed right there. At the top of a block the audio thread
exchanges the slot with null, adopts the new table and pushes the old one into a fixed-size retire FIFO,
which the message thread drains (frees) on its next publish and in the destructor. Nothing is allocated
or freed on the audio thread (`CustomFilterEngine.ShapeEditsReachTheAudioThreadWithoutAllocatingThere`,
`CustomFilterEngine.MovingCursorNeverAllocatesOnTheAudioThread`).

## Per-note cursors

Each note can sit at its own place on the morph map instead of sharing the Morph X/Y cursor, in mono and in poly
alike.
The setting is **Notes move the cursor** in the [shape editor window](#shape-editor-window)'s bottom bar
(`CustomFilterPerNote`, saved in the extra state):

- **Off** (default): one shared cursor, exactly the single-kernel path above.
- **By hand**: a key map gives each MIDI note a shape (or none). A note with no shape follows the shared cursor.
- **By pitch**: notes from C2 (36) to C7 (96) are split into as many equal bands as there are shapes, lowest band
  on the leftmost shape (shapes ordered by map x); notes outside the range clamp to the ends.
- **Random**: each new note draws a shape, never the one the voice had (when there is another), from a
  fixed-seed generator, so two renders of the same notes match.

**Smoothness** (Hard to Smooth, 0..1) is the glide time: a new note moves the voice's cursor from where it was to
its shape in `smoothness × 500 ms`, linearly; 0 lands in the note's first block. The target is read every block,
so moving a shape (or Morph X/Y, for a note with no shape) moves held notes with it. A voice that has had no note
yet stays on the shared kernel, so turning a mode on changes nothing until notes arrive; switching back to Off puts
every voice on the shared kernel at once.

**Mono is voice 0.** A mono Filter runs exactly poly voice 0's path: the same note spotting on its one Pitch input
(ch19) and its audio, the same shape choice, glide and design budget, and its two legs move together onto kernel
group 1 at the first note (a silent move: the group starts as a copy of the shared kernel; the note's own kernel then
crossfades in over its block). So mono and poly voice 0 give the same output, sample for sample, for the same notes
(`FilterPerNote.EveryModeInMonoMatchesPolyVoiceZero`, `FilterPerNoteMonoRender.*`: per band the magnitude response
matches poly within 0.01 dB, and a hard shape change under a held tone adds no step or spike). A mono Filter saved
with a mode other than Off by an older build, where the mode did nothing, now plays per note; Off is unchanged.

**The Pitch cable is made for you.** Picking **By hand** or **By pitch** in the window, with the Filter's Pitch input
uncabled, cables that input from the Pitch source of the voice chain feeding the Filter (the Poly MIDI
Pitch that drives the Oscillator upstream, found on a Poly Oscillator's Pitch input or on whatever input a Pitch
output feeds, which is how a mono Oscillator's pitch CV is found; `upstreamPitchSource`, `Source/Mixer/ChannelFlows/UpstreamPitchSource.cpp`),
by the same path as a hand-drawn cable (mono reads only voice 0's ch19), in the same undo step as the mode change, so one undo takes
both back. An input that is already cabled
gets no second cable; **Random** needs none. A line under the mode switch says why a mode cannot work yet: "Needs a
Pitch cable: wire a Pitch source (like Poly MIDI) to the Filter's Pitch input. Audio alone carries none." (By hand or
By pitch, Pitch uncabled, and no source to take it from, or the cable was removed later). A mono Oscillator with no
Pitch cable is such a case: its pitch jack is an input, it sends no pitch on, and its audio holds none. The line fades in and out and follows the cabling at 10 Hz.

**How a note is spotted.** The Filter has no gate input and takes no MIDI; it sees each voice's Pitch (ch19-26; ch19 alone in mono)
and audio. A voice starts a new note when its pitch settles (moves less than 0.1 semitone across a block) more
than half a semitone away from its current note's pitch, or when its audio returns after a silent block (below
about -80 dBFS on both legs). An audio return with a pitch still moving waits for the pitch to settle, so a
Poly MIDI note, whose pitch steps while its sound starts (from 0 on a voice's first note), counts once
and with its final pitch. With no
Pitch cable, only the audio return counts (By hand and By pitch then have no note and use the shared cursor). A
repeat of the same note on the same voice, into a Filter whose input never falls silent (an oscillator straight
into the Filter, the VCA after it), is not seen as a new note.

**Kernels on the audio thread, budgeted.** Every voice's cursor moves every block (cheap); kernels follow within
a budget of about one design per 128 samples, never fewer than two per block nor more than eight
(`CustomFilterEngine::voiceDesignBudget`): a note that just started first (so Smoothness 0 is heard in the note's
first block; a pitch that steps on a silent voice does not count), then voices whose shapes, Shift or Depth
changed, then the voice whose cursor is furthest from its kernel's. A voice silent long enough to be idle waits; a skipped voice keeps gliding and takes a coarser step when its turn comes; a
chord of more notes than the budget settles over a few blocks. Each design crossfades in over its block like any
kernel change. A worker thread was not used: a note's kernel must be live in the block that spotted it, and an
offline render must not depend on thread timing. Measured (Release, Apple Silicon, 44.1 kHz, 512-sample blocks,
`DISABLED_CpuCost`): one design is about 65 µs; 8 voices × 2 legs with a still shared cursor cost 2.7% of one
core, with a moving shared cursor 5.9%, and with all 8 voices on their own moving cursors (a new note per voice
every 20 blocks, Smoothness 1, the budget used every block) 6.3%. Nothing is allocated on the audio thread
(`FilterPerNote.EightMovingVoicesNeverAllocateOnTheAudioThread`).

**Handoff.** Mode and smoothness cross to the audio thread as relaxed atomics; the 128-entry key map crosses the
way the LFO's custom table does, a pending copy under a `SpinLock` that the audio thread only try-locks.

## Saved state

`FilterModule::getExtraState()` returns nothing while the shapes and the per-note settings are both the
defaults, so projects that never use Custom save exactly as before. Otherwise:

```json
{"version": 1, "shapes": [{"name": "ah", "position": [0.2, 0.25],
  "curve": {"version": 1, "points": [{"x": 0, "y": 0.3, "bend": 0}, {"x": 1, "y": 0.13, "bend": 0}]}}]}
```

Each shape also stores `"colourIndex"`, its palette slot: a shape keeps its slot when others are added, inserted, duplicated or deleted, and a new shape takes the lowest free one, so no dot changes colour because another one came or went. A list saved without slots wears the slots of its order (0..n-1) until its first add or delete writes them; a repeated or out-of-range slot is replaced by the lowest free one on load, and model-authored lists never carry one. A shape the user recoloured also has `"colour"`, an ARGB integer (absent: the palette colour by its slot; the LFO's list stores it the same way, and an unnamed centre shape with a colour saves as a list, not the bare curve). Model-authored lists never carry one.

Per-note settings other than the defaults add a `"perNote"` object beside `"shapes"` (the outer `version` stays 1,
so an older build still reads the shapes and ignores it):

```json
{"version": 1, "shapes": [...], "perNote": {"version": 1, "mode": "byHand", "smoothness": 0.25,
  "keys": [[60, 1], [62, 0]]}}
```

`mode` is `off`, `byHand`, `byPitch` or `random`; `keys` lists `[note, shape index]` for assigned notes only. A
project without `"perNote"` loads as Off; a newer `version`, an unknown mode or a malformed object gives the
defaults (smoothness clamped to 0..1, key entries outside 0..127 or with a shape outside 0..63 dropped), and the
shapes beside it still load. Keys store shape indices, so after a shape is deleted each key reads whichever shape now
has its index, and a key whose index no longer exists follows the shared cursor.

`CustomFilterShapes::fromVar` never fails: a missing/newer version or a non-array `shapes` gives the
defaults, an empty list gives the defaults, a shape with an unreadable curve keeps its slot with a flat
0 dB curve, positions are padded to two coordinates and clamped to 0..1, names are capped at 64
characters, and at most 64 shapes are read. Model-authored patches reach the narrower
`CustomFilterShapes::fromUntrustedVar` instead (via `setUntrustedExtraState`): curve points, at most 3 map
values per shape, a name cut to 24 characters of `[A-Za-z0-9 -]`, at most 16 shapes, every other key
ignored, and unusable input leaves the current shapes alone (see `docs/ai/patch-safety.md`). Of `"perNote"` the
untrusted path reads only `mode` and `smoothness`, never the key map ([patch format](../ai/patch-format.md)).

## Shape library

Named curves outside any one Filter: the user's saved shapes and a factory set, listed in the Sounds view's
[Shapes place](../layout/sound-browser.md#shapes) and in each shape window's Start from menu. `synth::shapes::ShapeLibrary`
(`Source/Modules/Shapes/`, Core) owns it; the Filter and the LFO share it and tell their shapes apart by kind.

- **Storage**: one JSON file per saved shape, `<settings folder>/Shapes/Filter/<name>.json` or `.../LFO/<name>.json`:
  `{"version":1,"kind":"filter","name":..,"curve":{LfoCustomWave::toVar}}`. Names are made safe as file names and cut to
  24 characters; "already saved" is decided on the file, which the disk compares ignoring case.
- **Untrusted files**: a file over 64 KB, not JSON, newer than version 1, of the other kind, or without two finite
  curve points is skipped; the curve goes through `LfoCustomWave::sanitise` (clamped, finite, at most 64 points); at
  most 500 files a kind are read.
- **Factory set** (compiled in, read-only, `FactoryShapes.cpp`): Filter `ah`, `ee`, `oo`, `eh`, `ih` (the preset curves
  above), `telephone band` (a band-pass, -3 dB at 300 Hz and 3.4 kHz, below -30 dB at 100 Hz and 8 kHz), `comb` (a
  24 dB notch every kHz from 1 to 19 kHz) and `tilt` (0 dB at 20 Hz sloping to -12 dB at 20 kHz), then the starter set:
  simple `low-pass ramp`, `band`, `notch`, `two formants` and experimental `wide comb`, `stairs`, `double peak`,
  `seesaw` (sixteen Filter and sixteen LFO shapes in all). Every Filter shape
  peaks at or below 0 dB (the vowels at most +2.3 dB), under the gain audit's +6 dB ceiling.
- **Stamping**: a library shape replaces one shape's curve (the name and map position stay) as ONE undo step, from Start
  from, by [dragging](../layout/sound-browser.md#dragging-a-shape) it onto a shape chip in the window, or onto a Filter
  card on its Custom type (the shape nearest the cursor), or with Return on a shape row in the Sounds view while that
  card is the selected one. Each stamp adds the shape to Used recently. `rename` and `remove` change a saved shape's file
  (`remove` is what the Sounds view's Delete calls after asking); `noteUsed` tells listeners a shape was stamped.

### Shape picker

One picker for every place a module holds shapes (`ShapePicker.h`, `makeShapePickerMenu`): a menu whose rows are a
52 x 20 thumbnail of the curve (`paintShapeThumbnail`, the same drawing as the Sounds view's Shapes rows), the name and
a Badge, under **Your shapes** then **Factory shapes**. It opens from the **Pick shape** button beside Edit shapes in the
Custom section of the Filter and LFO cards (`MorphShapesCard`; the selected dot, else the shape nearest the morph cursor, takes the pick as ONE
undo step and the pick joins Used recently) and from the shape window's **Start from**. Enter or Space on the button
opens it, arrows move, Enter picks, Escape closes; each row is named "<name>, <badge>" and the button has a title, a
tooltip and the shared focus ring.

## Shape editor window

The big editor for the drawn shapes, `Source/UI/ModuleViews/ShapeEditor/`. It is a window titled "Filter shapes:
<module name>" that grows out of the card's centre and shrinks back into it
([animation](../layout/animation.md#popup-windows)). It is not modal: the main window keeps working beside it, and
it stays above the main window (a child window on macOS). Esc, Cmd+W, Done or its close button closes it, and it is
deleted a turn later (also when its card goes away, and on quit). A shape dragged from the Sounds view's Shapes onto
its map stamps the dot under the pointer (curve only: the name and place stay) or, over empty space, adds a new shape
there named after it, selected; each is one undo step, and a full list says "No room for another shape"
([sound browser](../layout/sound-browser.md#dragging-a-shape)). The keyboard focus starts on the map (Tab works at once; asking for the window again
brings it back with the focus there). It is its own top-level window, so a key it does not use (Space, Cmd+Z, Cmd+Shift+Z, the
transport and panel shortcuts) is handed to the main window, which performs it a turn later
([shortcuts](../control/shortcuts.md#open-context-menu)); Delete, Backspace, the arrows, Tab and the canvas-selection
commands (copy, paste, select all, ...) are never handed over. Left a large
[morph map](#morph-map); right a row of shape chips (colour dot and name; pressing one selects the shape), the curve
editor (`CurveEditorComponent`, Free mode) for the selected shape and a row of buttons: **Add shape** (a copy of the
blend at the cursor, in the free spot furthest from the other shapes), **Duplicate**, **Rename**, **Start from** and **Draw**,
and, alone at the right end of the row and in the theme's error red, **Delete** (one shape always stays). The bottom bar holds the [per-note settings](#per-note-cursors)
beside **Done** (`PerNoteBar`, shown when the adapter has them): **Notes move the cursor** (a segmented switch: Off,
By hand, By pitch, Random), **Smoothness** (a slider from Hard to Smooth; its value reads "Hard" or "125 ms
glide") and, under them, for By hand only, the key strip (`NoteKeyStrip`): an octave and a half of keys (C to F)
with **<** / **>** buttons for the octave. Each key wears the colour dot of its shape; clicking a key cycles it
through no shape, then each shape in turn. The key row keeps its slot and fades in and out
([FadeVisibility](../layout/animation.md#fading-things-in-and-out)), so nothing moves. Every click, key step and
slider drag is one undo step (the settings are part of the module's extra state).

- **Keys and names.** Tab order after Draw: Notes move the cursor (Left/Right/Home/End pick a mode), Smoothness
  (Left/Right step 5 ms of glide), Octave down, the key strip, Octave up, Done. The key strip is one Tab stop:
  Left/Right move the focused key (and shift the octave past either end), Space or Return cycles it, Delete or
  Backspace clears it, Page Up/Down shift the octave; the focused key draws the focus ring. To a screen reader it
  is a group named "Key map" holding one button per key, titled "C4: ah" or "C4: no shape"; each control has a
  tooltip naming what it does.

- **Curve axes.** Across: 20 Hz to 20 kHz on a log axis, with 100, 1k and 10k labelled. Up: dB, with the 0 dB line
  (curve y = 0.8) drawn bright and +6, -12, -24 and -36 dB marked; the fill runs to the 0 dB line. A curve holds at most
  64 points.
- **Grid.** Two rows above the curve (`PitchGridRows`, the lines from `PitchGrid.h`). The first row: **Grid**, a
  segmented choice of Off (the default), Octaves, Notes or Harmonics, with its **Snap** pill; then **Level**, Off, 12,
  6, 3 or 1.5 dB, with its own **Snap** pill. The second row shows what the kind needs (it fades between them):
  - **Octaves**: Steps of 1 oct, 1/2, 1/3, 1/4, 1/6 or 1/12, counted from 1 kHz (1 oct has lines at 250, 500, 1k, 2k
    and 4k Hz).
  - **Notes**: a line on every equal-tempered note (A4 = 440 Hz), each C a landmark labelled "C4". **Key** is the
    shared Key field (`FollowKeyField`): it follows the song key by default or takes its own key. Notes outside the key
    are drawn dotted and are never snapped to.
  - **Harmonics**: lines on the multiples of a **Root**: a picked note (C1 to B6, A2 by default), the **Played note**
    (the pitch last heard on the Filter's Pitch input, `FilterModule::getLastPlayedPitchHz`, a relaxed atomic written
    on the audio thread; A2 until a note plays; the tooltip says when the Pitch input has no cable) or the **Song key
    root** (in octave 2). The powers of two are landmarks; x1 to x16 are labelled.
  A Snap pill off draws the lines but leaves that axis free; Level Off draws no level lines and leaves levels free.
  Points snap when added, dragged or moved with Alt+arrows (Alt+arrow steps to the next line that snaps, so in a key it
  skips the dotted notes); Shift while dragging bypasses it. The old one-row choice ("1/3 oct") is migrated to Octaves
  at that step with 6 dB levels.
  Line importance reads by brightness and width first, never hue alone: the **landmark** lines (C, the powers of two,
  the labelled guides 100, 1k, 10k and 0 dB) are brighter and wider, the **snap** lines 1 px in the faint grid colour,
  the **out-of-key** notes dotted. Kind hue is the second cue: note lines lean toward the accent, harmonic lines toward
  the warning colour. Where lines fall closer than 5 px the fainter ones are not drawn (they still snap), and labels
  that would touch are skipped. The line nearest the pointer (within 6 px) lights up in full accent with its name
  ("A4 · 440 Hz", "x3 · 330 Hz", "-6 dB"), and while a point is dragged the line it snapped to lights up the same way.
  A **Guides** toggle (on by default, remembered) at the right of the second row hides the guide lines and their
  labels only. Every choice is remembered for Filter shapes across windows and launches (user settings, not the
  patch); the LFO's window keeps its note-length grid. The curve editor's `CurveGrid` takes an explicit `xLines` list
  (each line with a tier and a kind) for lines that are not even cells.
- **Ring helpers.** A button after Draw opens a menu whose items rewrite the selected shape's curve, each as one undo
  step: one click places dots at the harmonic ratios of a note, so a resonant filter rings at those pitches.
  **Tune selected peak to a note** moves the selected point (or the highest one) to the nearest note, of the key when
  there is one; **Stack harmonics** opens a small popover (Root note, Harmonics 2 to 16, Each quieter by 0 to 6 dB,
  Width Narrow, Medium or Wide; Return stacks, Esc cancels) and puts that many peaks on the Root's harmonics over a
  -12 dB floor (the defaults: eight, 2 dB quieter each, Medium); **Chord peaks from the key** puts peaks on the key's root, third and fifth in octaves 3
  and 4; **Octave bells** puts thin peaks on the Root and its octaves up to 10 kHz. The Root and key are the grid
  rows' (`ShapeRingHelpers.h`, `ShapeStackPanel.h`). With Root set to Playing note, the Harmonics lines glide to each
  new note over 160 ms (they jump under Reduce Motion).
- **Cutoff Snap to notes.** On the Custom type, a right-click on the card's Cutoff knob offers **Snap to notes**: Free
  (the default), Notes (every semitone) or By key (the song key's notes). Snapped, the knob drags to notes, Up/Down
  step one note, and it reads "A3 220" (the note and the whole hertz, no unit, to fit the knob's value box). The choice is the card's view state (`"cardView"`), saved with the project
  (`CutoffNoteSnap.h`).
- **Show spectrum.** A toggle at the right end of the same row (off by default, remembered) draws the filter's live
  spectrum faintly behind the grid and the curve, on the curve's own log-frequency axis (level -80 to 0 dB over the
  full height). It is the reading the card's response view shows ([the card](#card)): the module's 4096-sample
  spectrum buffer, transformed by `FilterSpectrumAnalyser`, one class shared by both views. Each point takes the
  loudest bin under it (bass points between bins interpolate by power), so a sine shows its true level. The trace
  moves like a meter, timed in seconds rather than frames: it rises in about 10 ms and falls about 30 dB in the half
  second after a note stops (`kAttackSeconds`, `kReleaseSeconds`, both in the analyser). A 30 Hz tick runs only while
  the toggle is on and the window is visible and repaints only the curve; with the toggle off there is no tick and the
  curve paints exactly as before (the LFO's window has no such toggle). Tab order: the map, the chips, Grid, its Snap, Level,
  its Snap, the second row (Steps, Key or Root), Guides, Show spectrum, the curve, the buttons (Ring helpers after Draw).
- **The filter's response over the editor.** While the morph cursor (the dark main dot) is the selection in the map
  (pressed, dragged or focused, until a shape dot is chosen), the window draws the filter's live frequency response
  as an accent line over the curve: `customfilter::responseGridDb` with the morph cursor, Shift, Depth, Cutoff and
  Resonance, the same curve as the card's Show response, on the curve's own log-frequency axis and dB range. It
  follows Cutoff, the cursor and their modulation live (the editor's tick re-reads it and repaints only on a change);
  the map's cursor follows Morph X/Y modulation the same way, on the card and in the window, while a drag or an arrow
  key still sets the knobs. Choosing a shape dot
  hides it. It sits in the same backdrop painter as the spectrum, so both can show at once. A screen reader hears
  "Showing the filter's current response" when it appears. The map keeps this selection (`MorphMap::isCursorSelected`,
  `onCursorSelected`); the LFO's window has no response (`ShapeListAdapter::hasResponse`, true for the Filter only).
- **Start from** stamps the vowel (ah, ee, oo, eh, ih) or animal (wolf, crow, frog) curve onto the selected shape. Its
  name and map position stay. The curves are `CustomFilterShapes::presetCurve`; a new Filter starts with the three
  vowels, which are the same curves. After the built-ins the menu lists **Your shapes** (the Filter shapes saved with
  Save shape) and **Factory shapes** (the [factory set](#shape-library) that is not already a built-in's curve:
  telephone band, comb, tilt). Each row is a shared [shape picker](#shape-picker) row (a small picture of the curve, the name and a Yours or
  Factory badge, spoken "name, Yours"). Each row carries its curve, so a library that changes while the menu is open
  cannot change what a row stamps.
- **Save shape** (right of Draw) saves the selected shape's curve to the [shape library](#shape-library) under a name
  typed in the usual alert prompt (it starts from the shape's name, is cut to 24 characters and refuses an empty
  name by asking again). A name already saved asks "Replace it?" first. A Tab stop named "Save shape" with a tooltip.
- **Draw** works like the timeline's Draw tool: pick a pen, then drag on the curve. The button opens the automation lane's own
  shape flyout (`DrawShapeFlyout`); a pick turns draw mode on and the button shows that pen's icon (`DrawShapeIcons`) and
  lights; pressing it again leaves draw mode. While a pen is on, a press on the curve starts a stroke instead of moving a
  point: **Sine, Triangle, Saw and Square** grow a dashed preview and a chip ("2 cycles") with the drag and fill only the
  dragged stretch with whole cycles (one per grid cell while a Grid is chosen, else three across the full width; fewer
  when more would not fit the shape's 64 points) between the default band (-18 and +6 dB for the Filter); **Line**
  ramps from the press to the release; **Free** follows the mouse and is thinned on release with the Pencil's rule
  (`AutomationRecorder::thinPoints`). Only the points inside the stretch are replaced. The stretch's ends snap to the
  Grid unless Shift is held. Escape during a drag cancels it. One stroke is one undo step. The pen is remembered per kind
  of list in the window settings (`shapeWindowPen_<kind>`). Code: `CurveDrawGesture` (in the curve editor) hands its
  stroke to `drawOnCurve` (`ShapeDrawing.cpp`), the one function both the preview and the commit call, which builds the
  points with `LaneShapeGenerator` so a sine here is a lane's sine. Keyboard route: with a pen on and the curve focused,
  **Enter** applies the pen to the stretch between the first and last selected points (the whole curve when fewer than
  two are selected) as one undo step; Free has no keyboard route and says so. The LFO's window works the same way.
- **Live and undoable.** Every curve change goes to the module at once (the engine renders a new table; the audio
  thread picks it up at the next block), and each drag, button or menu pick is one undo step recorded as the module's
  extra state. The window follows changes made elsewhere (undo, the card's map, gliding the curve's points when an undo or redo moved them) by polling
  `FilterModule::getCustomShapesGeneration`, which every replacement of the list bumps.
- **Reuse.** The window edits a `ShapeListAdapter` (read and write the list, bracket an undo step, describe the axes,
  the Start from entries, the draw range, the grid it offers and whether it has a spectrum), not the Filter: `FilterShapesAdapter` is the Filter's and
  `LfoShapesAdapter` the LFO's. Both share `ModuleShapesAdapter` (the undo bracket and the morph cursor's parameters). The window and the card's map edit the adapter through `ShapeMapController`, so every edit is one undo step.

## Morph map

One dot per shape, coloured by its own palette slot (kept for the shape when others come and go, see [Saved state](#saved-state)) unless you gave it a colour (**Colour...** in its menu, or Alt+C on a focused dot: the picker previews live, one undo step; the chip and the per-note key dots wear it too), and a dark cursor at (Morph X, Morph Y). Drag a dot to move the shape,
drag the cursor to set Morph X and Y (one host gesture and one undo step per drag, so automation records it), double-click
empty space to add a shape there that copies the current blend (the blended dB curve at the cursor sampled back into at most 16
points placed on its shape: both ends, every prominent peak with its dip and flanks, then a point wherever the straight segments still miss the blend by more than half a dB, so narrow formants survive and a flat or straight blend is two points; a single click adds nothing, and the new dot takes keyboard focus so Delete or Backspace removes it at once),
double-click a dot to delete it, press Return on a dot (or choose Edit in its right-click menu, which also has Duplicate,
Rename and Delete) to open the editor on it. The menu opens beside the dot; in the shape editor window it has no Edit
(a press already selected the dot). Right-click a Morph X or Y slider, on the card or in the window, for the usual knob menu of
that parameter (Automate, MIDI Learn). Delete on a focused dot removes it and focuses the next dot; the last shape
never goes, by any of these routes. A dot that is added (or brought back by an undo) grows in with the 8% bounce over
200 ms and a removed one shrinks out over 180 ms (`MorphMap.cpp`, dots keyed by name, one `AnimationDriver`); Reduce
Motion makes both an 80 ms fade and Animations: Off lands at once. Cmd+Z and Cmd+Shift+Z of a dot or cursor move glide it back and forth (160 ms `easeOutCubic`, `MorphMap::setDots`/`setCursor` with the restore flag `ShapeMapController` passes; a plain Reduce Motion or Animations: Off lands at once; [undo-glide.md](../layout/undo-glide.md)). Pressing a dot or the cursor off its centre keeps the
offset between pointer and centre, so it never jumps under the pointer. On a
focused dot, the cursor or a slider an arrow moves it one step (Shift: a bigger one) and, held, glides it on:
slowly at first (about 0.15 map units a second), faster the longer it is held (about 1.2 a second after two seconds),
Shift faster still; two arrows held move it diagonally, a slider moves only along its own axis, and the whole hold is
one gesture and one undo step. The map is a 2D map with no left-to-right order, so it is the one point editor that
keeps its own keys (arrows move the focused dot, Tab steps to the next) instead of the shared map in
[`control/point-editing.md`](../control/point-editing.md). The curve is the timeline cursor's (`synth::CursorGlide`); the map's own model is
`MorphMapGlide`, driven by a frame pump that runs only while an arrow is down. A refresh of the map (every key
step causes one) keeps the dot stops it has, so the focused dot stays focused. Tooltips: "<name> - drag to move,
double-click to delete, Return to edit", "Morph cursor - drag to blend shapes". Two sliders hugging the map set Morph X and Y alone. Keys and focus order are in
[visualizers.md](../layout/visualizers.md#morphmap).

## Card

With `filterType` Custom the Filter card (`DefaultCardLayoutsFilterDynamics.cpp`, revision 5; the logic is
`MorphShapesCard`, `Source/UI/Graph/ModuleComponent/MorphShapesCard/`, which the LFO card reuses for its Custom shape). It is a collaborator, not `ModuleComponent` members: it owns the morph map, the Edit shapes button and the Show Morph Map pill, gets the card's hooks through its `Host`, and `ModuleComponent` holds one pointer to it (`getMorphCard()`):

- **Tone row and Shift/Depth.** The tone row (Cutoff, Resonance, Drive) is the same in every type. A section of its
  own, Shift and Depth, shows only in Custom (a `visibleWhen` section, so the card grows and gives the height back);
  a cable dropped on a knob lands on its ring. Every other type has exactly its old card.
- **Response view** ("Show Response", closed until opened): draws the blended curve at the cursor with Shift, Cutoff,
  Depth and Resonance applied (`customfilter::responseGridDb`, the numbers the kernel is designed from), follows the
  cursor, the shapes, the knobs and their modulation (each control's CV as the last audio block applied it), and draws the cutoff marker (a thin vertical line) like the other types. Double-clicking it opens the shape editor on the shape nearest the cursor.
- **Edit shapes** button (a row under the response view) opens the [shape editor window](#shape-editor-window) on the
  selected dot, else the shape nearest the cursor (`MorphShapesCard::targetShapeIndex`; a drop on the card still replaces the one nearest the cursor).
- **Curve view** (252 x 110, under the button, above the map; `MorphShapesCardCurve.cpp`): the shape under the morph
  cursor on log-frequency and dB axes (`FilterShapesAdapter::getAxisGuides`). With the cursor on a dot (within 0.06
  map units, or the only shape) it shows and edits that shape in place: a drag goes through the adapter's
  `beginEdit` / `setShapes` / `endEdit`, so it is one undo step, the sound and the Edit shapes window follow, and
  double-clicking empty space opens the window. **Between dots it shows the blend the cursor hears and is read-only**
  (no pointer or keyboard editing; its screen-reader name says so). **Show Curve** is a footer pill like Show Morph
  Map, remembered as `CardViewState::showCurveView` (`"cardView"."curveView"`); with no choice it shows from two shapes
  up. The LFO card keeps its own in-card editor (`ModuleComponentLfoCard.cpp`).
- **Morph map** (252 x 159, under the button) with thin **Morph X** (bottom edge) and **Morph Y** (left edge) sliders
  hugging it; the right edge is left free. The slider thumbs sit at the cursor; a drag, a click or the arrow keys set
  that one parameter (one host gesture and one undo step). **Show Morph Map** is a footer pill, shown only in Custom;
  the choice is remembered on the module (`CardViewState::showMorphMap`, saved as `"cardView"."morphMap"`); with no
  choice the map shows from two shapes up and is hidden with one.
- **Morph X / Y knobs and the map.** The two knobs are the Custom section; the card hides that section while the map
  shows (`CardBody::setSectionHidden`) and shows it again when the map is hidden, so the module's CV targets always have
  a landing widget. A cable dropped on a slider drives that parameter and lands on the slider; with the map hidden it
  lands on the knob's ring as before (`ModuleComponent::getModTargetPortForPoint` / `getModTargetKnobAnchor` ask
  `MorphShapesCard` first).
- **A slider's modulation is a fader's.** Beside each slider runs a 3 px modulation bar and, past it, the cable's landing
  dot (`MorphMap::stripAnchor`: the dot's radius and a 2 px gap beyond the bar, so it clears both; the map's insets leave
  room for it). While a cable lands there the bar shows the reachable range faintly (`modDepthBandRange`) and the live
  value-to-value-plus-CV bar over it, painted with the fader's `paintCardFaderModBar`; `MorphShapesCard::poll` reads
  them on the card's gated tick. The dot gets the same `ModDotButton` a knob's has (named "Morph X modulation, 1
  source", tooltip, Up/Down steps the amount, Return opens the menu), on the slider's Tab stop. A press on the bar or
  the dot is offered to `MorphMap::pressGesture` first, which runs the fader's `CardControlGestures`: a drag on the bar
  changes the amount of the chosen source, a click or drag on the dot opens or drags it, Cmd-press picks the cable up,
  Alt-press on the slider drags the amount; each is one undo step and never moves the cursor. A plain press on the
  slider itself still sets the cursor.
- **Motion.** The map and the Edit shapes row fade and take their height with the shared `CardBlockFade`; the Morph
  knobs fade through the card body's conditional-section fade and the Shift / Depth section through the same fade;
  the card's height eases and the neighbours make room once per direction. Reduce Motion is the short plain fade and
  Animations: Off lands at once ([animation](../layout/animation.md#fading-things-in-and-out)).
- **Keys.** Tab visits the map, each dot, the cursor, then the X and Y sliders; keys are in
  [visualizers.md](../layout/visualizers.md#morphmap).
