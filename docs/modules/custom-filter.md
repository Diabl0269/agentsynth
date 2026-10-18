# Filter: Custom Type

The Filter module's eighth type (`filterType` = `Custom`, `FilterModule::kCustomTypeIndex == 7`,
appended so saved indices 0-6 are unchanged). Instead of the ladder/notch, the signal goes through one
FIR kernel designed from user-drawn magnitude shapes. The rest of the Filter is in
[`modules.md`](modules.md#filter-module); its raw channels are in
[`poly-channel-layout.md`](poly-channel-layout.md#channel-table).

Code: `Source/Modules/Filter/` — `CustomFilterShapes` (data model), `CustomFilterKernelDesigner`
(minimum-phase design), `PartitionedConvolver` (convolution), `CustomFilterEngine` (handoff, blend,
redesign policy; its per-voice path in `CustomFilterEngineVoices.cpp`), `CustomFilterBlend` (the blend as
numbers), `CustomFilterPerNote` (per-note settings) and `CustomFilterVoiceCursors` (per-voice note spotting and
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
- No fixed shape count; at least one is always kept. A new Filter starts with three vowel-like shapes,
  `ah` (0.2, 0.25), `ee` (0.8, 0.25) and `oo` (0.5, 0.8), whose peaks stay at or below +2 dB, so
  switching to Custom sounds at once and adds no gain (the gain audit sweeps `filterType`).

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

All four are added after Key Track (Level and Mute stay last) and each has a CV jack. Cutoff and Resonance are
ignored in Custom; the [card](#card) swaps them in place for Shift and Depth. **Key Track** adds `keyTrack × 12 × log2(pitch / 261.63 Hz)` semitones of shift, **mono only**:
in poly the voices share one kernel unless a [per-note mode](#per-note-cursors) is on, and Key Track does not
follow a voice's pitch there either.

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

Once per block, morph, shift and depth (knob plus CV, read at the block's first sample) are compared
with the values the live kernel was designed for; a redesign happens only when one moved by more than a
small epsilon or the shapes changed.

1. **Weights**: inverse distance, `w_i = 1 / max(d_i, 0.02)^2.5`, normalised (`d_i` = 2D distance from
   the cursor to shape i).
2. **Blend in dB**: each shape's dB curve, pre-rendered on a 1024-point log-frequency grid, summed with
   those weights. Kernels are never blended: a sum of minimum-phase kernels is neither minimum phase nor
   the blended magnitude.
3. **Shift and depth**: each FFT bin reads the blended curve at its shifted log-frequency position
   (linear interpolation), and the dB is multiplied by depth.
4. **Minimum phase by the real cepstrum**: FFT size N = 4L = 8192, kernel length L = 2048. ln|H| →
   inverse FFT (real cepstrum) → fold onto positive quefrencies → FFT → complex exp → inverse FFT;
   the first L samples, with a half-Hann fade over the last 256, are the kernel. The magnitude matches
   the curve within 1 dB from 100 Hz to 10 kHz for a smooth shape
   (`CustomFilterEngine.ImpulseResponseFollowsTheDrawnShapeWithin1dB`).

`CustomFilterKernelDesigner` is its own class so the design can move to a worker thread later. All
its buffers and its `juce::dsp::FFT` are allocated in `prepare()`; `design()` does no allocation, locks
or system calls (on macOS JUCE's FFT is vDSP; elsewhere its fallback FFT takes an uncontended
per-instance `SpinLock` and uses `alloca` scratch at these sizes).

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
  to the poly voices in a [per-note mode](#per-note-cursors) (both legs of voice v read group v + 1). Each group
  swaps and crossfades on its own. With every channel on group 0 (mono, poly with per-note Off) the arithmetic is
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

In poly, each note can sit at its own place on the morph map instead of every voice sharing the Morph X/Y cursor.
The setting is **Notes move the cursor** in the [shape editor window](#shape-editor-window)'s bottom bar
(`CustomFilterPerNote`, saved in the extra state):

- **Off** (default): one shared cursor, exactly the single-kernel path above. Mono always works this way.
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

**The Pitch cable is made for you.** Picking **By hand** or **By pitch** in the window, with the Filter in Poly and
its Pitch input uncabled, cables that input from the Pitch source of the voice chain feeding the Filter (the Poly MIDI
Pitch that drives the Oscillator upstream; `upstreamPitchSource`, `Source/Mixer/ChannelFlows/UpstreamPitchSource.cpp`),
all 8 voices, in the same undo step as the mode change, so one undo takes both back. An input that is already cabled
gets no second cable; **Random** needs none. A line under the mode switch says why a mode cannot work yet: "Needs a
Pitch cable: connect your Poly MIDI's Pitch to this Filter's Pitch input." (By hand or By pitch, Pitch uncabled, and
no source to take it from, or the cable was removed later) and "Per-note works in Poly: turn on the Filter's Poly."
(any mode but Off on a mono Filter). The line fades in and out and follows the cabling at 10 Hz.

**How a note is spotted.** The Filter has no gate input and takes no MIDI; it sees each voice's Pitch (ch19-26)
and audio. A voice starts a new note when its pitch settles (moves less than 0.1 semitone across a block) more
than half a semitone away from its current note's pitch, or when its audio returns after a silent block (below
about -80 dBFS on both legs). An audio return with a pitch still moving waits for the pitch to settle, so a
Poly MIDI note, whose pitch glides for 5 ms while its sound starts (from 0 on a voice's first note), counts once
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

## Shape editor window

The big editor for the drawn shapes, `Source/UI/ModuleViews/ShapeEditor/`. It is a window titled "Filter shapes:
<module name>" that grows out of the card's centre and shrinks back into it
([animation](../layout/animation.md#popup-windows)); Esc or Done closes it, and the keyboard focus starts on the map (Tab works at once; asking for the window again
brings it back with the focus there). It is its own top-level window, so a key it does not use (Space, Cmd+Z, Cmd+Shift+Z, the
transport and panel shortcuts) is handed to the main window, which performs it a turn later
([shortcuts](../control/shortcuts.md#open-context-menu)); Delete, Backspace, the arrows, Tab and the canvas-selection
commands (copy, paste, select all, ...) are never handed over. Left a large
[morph map](#morph-map); right a row of shape chips (colour dot and name; pressing one selects the shape), the curve
editor (`CurveEditorComponent`, Free mode) for the selected shape and a row of buttons: **Add shape** (a copy of the
blend at the cursor, in the free spot furthest from the other shapes), **Duplicate**, **Delete** (one shape always
stays), **Rename**, **Start from** and **Draw**. The bottom bar holds the [per-note settings](#per-note-cursors)
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
- **Start from** stamps the vowel (ah, ee, oo) or animal (wolf, crow, frog) curve onto the selected shape. Its
  name and map position stay. The curves are `CustomFilterShapes::presetCurve`; a new Filter starts with the three
  vowels, which are the same curves.
- **Draw** opens the automation lane's own shape flyout (`DrawShapeFlyout`) and applies the pick to the curve through
  `LaneShapeGenerator`'s points, so a sine here is a lane's sine: Free changes nothing, Line joins the first and last
  levels, Sine, Triangle, Saw and Square repeat three times between -18 and +6 dB (a hold becomes a vertical step).
- **Live and undoable.** Every curve change goes to the module at once (the engine renders a new table; the audio
  thread picks it up at the next block), and each drag, button or menu pick is one undo step recorded as the module's
  extra state. The window follows changes made elsewhere (undo, the card's map) by polling
  `FilterModule::getCustomShapesGeneration`, which every replacement of the list bumps.
- **Reuse.** The window edits a `ShapeListAdapter` (read and write the list, bracket an undo step, describe the axes,
  the Start from entries and the draw range), not the Filter: `FilterShapesAdapter` is the Filter's and
  `LfoShapesAdapter` the LFO's. Both share `ModuleShapesAdapter` (the undo bracket and the morph cursor's parameters). The window and the card's map edit the adapter through `ShapeMapController`, so every edit is one undo step.

## Morph map

One dot per shape, coloured by shape index, and a dark cursor at (Morph X, Morph Y). Drag a dot to move the shape,
drag the cursor to set Morph X and Y (one host gesture and one undo step per drag, so automation records it), click
empty space to add a shape there that copies the current blend (the blended dB curve at the cursor sampled back into 16
points), double-click a dot (or press Return on it) to open the editor on it, right-click a dot for Edit, Duplicate,
Rename and Delete. Delete on a focused dot removes it and focuses the next dot; the last shape never goes. On a
focused dot, the cursor or a slider an arrow moves it one step (Shift: a bigger one) and, held, glides it on:
slowly at first (about 0.15 map units a second), faster the longer it is held (about 1.2 a second after two seconds),
Shift faster still; two arrows held move it diagonally, a slider moves only along its own axis, and the whole hold is
one gesture and one undo step. The curve is the timeline cursor's (`synth::CursorGlide`); the map's own model is
`MorphMapGlide`, driven by a frame pump that runs only while an arrow is down. A refresh of the map (every key
step causes one) keeps the dot stops it has, so the focused dot stays focused. Tooltips: "<name> - drag to move,
double-click to edit", "Morph cursor - drag to blend shapes". Two sliders hugging the map set Morph X and Y alone. Keys and focus order are in
[visualizers.md](../layout/visualizers.md#morphmap).

## Card

With `filterType` Custom the Filter card (`DefaultCardLayoutsFilterDynamics.cpp`, revision 3; the logic is
`MorphShapesCard`, `Source/UI/Graph/ModuleComponent/MorphShapesCard/`, which the LFO card reuses for its Custom shape). It is a collaborator, not `ModuleComponent` members: it owns the morph map, the Edit shapes button and the Show Morph Map pill, gets the card's hooks through its `Host`, and `ModuleComponent` holds one pointer to it (`getMorphCard()`):

- **Tone row.** Cutoff and Resonance (ignored here) swap in place for Shift and Depth, the same cells, with the
  swap-in-place motion of any condition; Drive stays. Every other type has exactly its old card.
- **Response view** ("Show Response", closed until opened): draws the blended curve at the cursor with Shift and Depth
  applied (`customfilter::responseGridDb`, the numbers the kernel is designed from) and follows the cursor, the shapes
  and the knobs; no cutoff marker. Double-clicking it opens the shape editor on the shape nearest the cursor.
- **Edit shapes** button (a row under the response view) opens the [shape editor window](#shape-editor-window) on the
  shape nearest the cursor.
- **Morph map** (252 x 150, under the button) with thin **Morph X** (bottom edge) and **Morph Y** (left edge) sliders
  hugging it; the right edge is left free. The slider thumbs sit at the cursor; a drag, a click or the arrow keys set
  that one parameter (one host gesture and one undo step). **Show Morph Map** is a footer pill, shown only in Custom;
  the choice is remembered on the module (`CardViewState::showMorphMap`, saved as `"cardView"."morphMap"`); with no
  choice the map shows from two shapes up and is hidden with one.
- **Morph X / Y knobs and the map.** The two knobs are the Custom section; the card hides that section while the map
  shows (`CardBody::setSectionHidden`) and shows it again when the map is hidden, so the module's CV targets always have
  a landing widget. A cable dropped on a slider drives that parameter and lands on the slider; with the map hidden it
  lands on the knob's ring as before (`ModuleComponent::getModTargetPortForPoint` / `getModTargetKnobAnchor` ask
  `MorphShapesCard` first).
- **Motion.** The map and the Edit shapes row fade and take their height with the shared `CardBlockFade`; the Morph
  knobs fade through the card body's conditional-section fade and the Cutoff / Resonance swap through its swap motion;
  the card's height eases and the neighbours make room once per direction. Reduce Motion is the short plain fade and
  Animations: Off lands at once ([animation](../layout/animation.md#fading-things-in-and-out)).
- **Keys.** Tab visits the map, each dot, the cursor, then the X and Y sliders; keys are in
  [visualizers.md](../layout/visualizers.md#morphmap).
