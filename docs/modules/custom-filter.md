# Filter: Custom Type

The Filter module's eighth type (`filterType` = `Custom`, `FilterModule::kCustomTypeIndex == 7`,
appended so saved indices 0-6 are unchanged). Instead of the ladder/notch, the signal goes through one
FIR kernel designed from user-drawn magnitude shapes. The rest of the Filter is in
[`modules.md`](modules.md#filter-module); its raw channels are in
[`poly-channel-layout.md`](poly-channel-layout.md#channel-table).

Code: `Source/Modules/Filter/` — `CustomFilterShapes` (data model), `CustomFilterKernelDesigner`
(minimum-phase design), `PartitionedConvolver` (convolution), `CustomFilterEngine` (handoff, blend,
redesign policy), `CustomFilterBlend` (the blend as numbers). `FilterModule::processCustomMode` wires it in. Shapes
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

## Parameters

| ID | Range | Default | Effect |
|---|---|---|---|
| `morphX`, `morphY` | 0–1 | 0.5 | The morph cursor on the shape map |
| `shift` | −48..+48 st | 0 | Moves the whole response along frequency: bin f reads the curve at f / 2^(shift/12) |
| `depth` | 0–2 | 1 | Multiplies every dB value; 0 is flat (0 dB), 2 doubles every boost and cut |

All four are added after Key Track (Level and Mute stay last) and each has a CV jack. On the card they
are a "Custom" section shown only while Custom is the type (`DefaultCardLayoutsFilterDynamics.cpp`,
revision 2); a full shape editor is not built yet. Cutoff, Resonance and Drive are ignored in
Custom. **Key Track** adds `keyTrack × 12 × log2(pitch / 261.63 Hz)` semitones of shift, **mono only**:
every poly voice shares one kernel, so there is no per-voice pitch to follow.

## CV jacks

Morph X CV = ch28 (`kMorphXCVChannel`), Morph Y CV = ch29, Shift CV = ch30, Depth CV = ch31, each the
**same channel in both voice modes**, after Key Track in the jack order. Shift and Depth have CV too
because every continuous knob on a card must accept a modulation cable. All four are **knob-only**
targets (`ModulationTarget::knobOnly`): they never draw a gutter jack, in any filter type, so a cable
reaches them only by being dropped on the Custom knob and lands on its ring. A Filter of any other type
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
- **One kernel, many channels**: both legs × up to 8 voices (16 channels) share the kernel; each
  channel keeps its own history and frequency-domain delay line.
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

## Saved state

`FilterModule::getExtraState()` returns nothing while the shapes are the defaults, so projects that never
use Custom save exactly as before. Otherwise:

```json
{"version": 1, "shapes": [{"name": "ah", "position": [0.2, 0.25],
  "curve": {"version": 1, "points": [{"x": 0, "y": 0.3, "bend": 0}, {"x": 1, "y": 0.13, "bend": 0}]}}]}
```

`CustomFilterShapes::fromVar` never fails: a missing/newer version or a non-array `shapes` gives the
defaults, an empty list gives the defaults, a shape with an unreadable curve keeps its slot with a flat
0 dB curve, positions are padded to two coordinates and clamped to 0..1, names are capped at 64
characters, and at most 64 shapes are read. Model-authored patches reach the narrower
`CustomFilterShapes::fromUntrustedVar` instead (via `setUntrustedExtraState`): curve points, at most 3 map
values per shape, a name cut to 24 characters of `[A-Za-z0-9 -]`, at most 16 shapes, every other key
ignored, and unusable input leaves the current shapes alone (see `docs/ai/patch-safety.md`).

## Shape editor window

The big editor for the drawn shapes, `Source/UI/ModuleViews/ShapeEditor/`. It is a window titled "Filter shapes:
<module name>" that grows out of the card's centre and shrinks back into it
([animation](../layout/animation.md#popup-windows)); Esc or Done closes it. Left a large
[morph map](#morph-map); right a row of shape chips (colour dot and name; pressing one selects the shape), the curve
editor (`CurveEditorComponent`, Free mode) for the selected shape and a row of buttons: **Add shape** (a copy of the
blend at the cursor, in the free spot furthest from the other shapes), **Duplicate**, **Delete** (one shape always
stays), **Rename**, **Start from** and **Draw**. The bottom bar keeps an empty slot for per-note settings.

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
  the Start from entries and the draw range), not the Filter: `FilterShapesAdapter` is the Filter's. The LFO's custom
  wave can plug in with its own adapter later.

## Morph map

One dot per shape, coloured by shape index, and a dark cursor at (Morph X, Morph Y). Drag a dot to move the shape,
drag the cursor to set Morph X and Y (one host gesture and one undo step per drag, so automation records it), click
empty space to add a shape there that copies the current blend (the blended dB curve at the cursor sampled back into 16
points), double-click a dot (or press Return on it) to open the editor on it, right-click a dot for Edit, Duplicate,
Rename and Delete. Delete on a focused dot removes it; the last shape never goes. Tooltips: "<name> - drag to move,
double-click to edit", "Morph cursor - drag to blend shapes". Keys and focus order are in
[visualizers.md](../layout/visualizers.md#morphmap).
