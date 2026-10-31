# Wavetable Oscillator

A wavetable oscillator (`Source/Modules/WavetableOscillatorModule/`), type-name string
`"Wavetable"`. Its raw channel map is in
[`poly-channel-layout.md`](poly-channel-layout.md#channel-table); the cross-module rules it shares
with every other module are in [`modules.md`](modules.md).

## Source layout

The module outgrew one file and is split by concern:

- `WavetableOscillatorModule.h`/`.cpp` — the module class: parameters, channel map, mode processing.
- `WavetableOscillatorModuleRender.cpp` — the per-voice render loop and one sample of a stack.
- `WavetableOscillatorModuleNotes.cpp` — MIDI note-ons, Gate edges and the restart hand-over.
- `WavetableOscillatorModuleVoicing.cpp` — the per-block unison ratios and gain mix.
- `WavetableTableBuilder.h`/`.cpp` — table geometry (mip constants, `Wavetable`) and `TableBuilder`,
  the FFT-based mip-pyramid synthesiser (see [Table synthesis](#table-synthesis)), in a `wavetable`
  namespace. `WavetableOscillatorModule` aliases these names back onto itself
  (`using Wavetable = wavetable::Wavetable;` and so on), so `WavetableOscillatorModule::Wavetable`,
  `::TablePtr`, `::TableBuilder` and the mip-geometry functions all still resolve.

## Tables, position and parameters

- **Tables**: six built-ins — `Basic Shapes` (sine → triangle → saw → square), `Harmonic Sweep`,
  `Pulse` (duty-cycle morph), `Formant`, `Bell`, `Digital` — plus `Loaded File` for a table read from
  disk. Each built-in has 32 frames.
- **Position (the "3D" scan)**: `Position` (0–1) scans continuously through the frame stack. Reads
  are **bilinear** — linear within the frame (phase) and linear between the two adjacent frames — so
  morphing is click-free. `Interp` switches the within-frame read to 4-point Catmull-Rom (see
  [Interpolation quality](#interpolation-quality)).
- **Parameters**: Table (choice), Position (0–1), Octave (−4…+4), Coarse (−12…+12), Fine (±100
  cents), Level (0–1), Poly (bool), Unison (1–8), Detune (0–100 cents), Warp (choice), Warp Amt
  (0–1), Phase (0–360°), Rand Phase (0–1), Spread (0–1), Width (0–1), Blend (0–1), Stack (choice),
  Sub (0–1), Sub Oct (−1/−2), Sub Wave (Sine/Square), Pan (±1), Sync In (choice), Import (choice),
  Interp (choice).
- **CV inputs (mono mode)**: ch0 = Pitch (inert — see below), then ch1…ch15 = Position, Octave,
  Coarse, Fine, Level, Warp, Phase, Rand, Detune, Spread, Width, Blend, Sub, Pan, Sync, and ch16 =
  Gate, shown as **Restart** (see [Phase control](#phase-control)). **17 visible jacks.**
- **Outputs**: two audio jacks, `Audio L` and `Audio R`. See [Stereo output](#stereo-output).
- **Poly mode**: 8 voices driven by pitch CV in Hz on ch0-7, shared mod CV from ch8, and a per-voice
  Gate fan on ch23-30 (`kGateBase`).
- **Octave / Coarse / Fine apply in poly mode too** — they transpose the incoming pitch CV. (This
  differs from `OscillatorModule`, where the tuning parameters only affect the MIDI fallback voice.)
- **Mono pitch CV is inert**, exactly as in `OscillatorModule`: mono jack 0 shares raw channel 0 with
  `Audio L`, so pitch comes from MIDI. The jack is kept so the mono and poly jack layouts match.

> **Channel indices are append-only.** `Position`/`Octave`/`Coarse`/`Fine`/`Level` keep the raw
> channel numbers they have always had (mono ch1-5, poly ch8-12), because saved patches route by raw
> index — inserting a new jack among them would silently repoint every existing modulation. New jacks
> go on the end. `WavetableOscillatorModuleTest.LegacyModCVChannelsKeepTheirIndices` pins this.

## Warp

`Warp` reshapes the table read after mip selection; `Warp Amt` (with CV on the Warp jack) sets the
depth. `Off` is index 0, so a preset saved before warps existed loads unwarped.

| Mode | What it does | Anti-aliasing strategy |
|---|---|---|
| `Off` | identity | — |
| `Sync` | slave phase `frac(p·k)`, `k` up to 8, reset when the master wraps | conservative mip + 4× oversampling |
| `Bend +` / `Bend −` | quadratic phase bend, slope bounded to `1±amount` | conservative mip |
| `PWM` | `w(p) − w(p+d)` — the wave minus a phase-shifted copy | **none needed** (see below) |
| `Asym` | two-segment phase map with a moving breakpoint | conservative mip |
| `Flip` | wavefolder — drive, then reflect past ±1 | 4× oversampling |
| `Mirror` | phase mirrored about the half cycle | conservative mip |
| `Quantize` | amplitude staircase | 4× oversampling |
| `Remap` | phase staircase | 4× oversampling |
| `Formant` | sped-up read windowed by a raised cosine over the master cycle | conservative mip |

Two defences keep the anti-aliasing guarantee intact, and **both** are needed:

1. **Conservative mip selection.** `warpRateFactor(mode, amount)` reports the steepest phase-map slope
   a mode reaches — the factor by which it can outrun a plain 1× read. `selectMip()` is fed
   `dt × factor`, so the stored frame is already band-limited for the fastest read the warp will
   perform.
2. **Oversampling** (the `warpNeedsOversampling` flag). Modes whose output has a *step* or an *amplitude nonlinearity* generate harmonics
   no amount of input band-limiting can prevent. Those render at 4× and come back through a 33-tap
   Blackman-windowed sinc decimator (~−74 dB stopband). The whole voice — every unison sub-oscillator
   plus the sub — is summed at the oversampled rate and decimated once per stereo leg, which is valid
   because decimation is linear and costs two filters per voice instead of two per sub-oscillator.

When oversampling is active the mip is chosen against the **oversampled** Nyquist
(`warpRate / kOversample`): those extra harmonics are representable while rendering at 4×, and the
decimator removes them on the way down. Band-limiting to the base Nyquist there instead would
collapse a hard-synced table to a sine before the warp ever saw it.

`PWM` is the interesting exception: subtracting two reads of the same band-limited table cannot
introduce harmonics the table did not already have, so it is alias-free by construction and needs
neither defence.

**`clampWarpAmount()`** covers what mips cannot. Mip selection band-limits the *harmonics* of a read,
but `Sync` and `Formant` also multiply the read's own **fundamental** — at 8×, a 4 kHz note reads at
33 kHz. The clamp backs the amount off at extreme pitches so the sped-up read stays under the **base**
Nyquist. Deliberately not the oversampled one: the oversampling headroom exists for the harmonics a
discontinuity throws off and gets filtered away on the descent, so spending it here would let `Sync`
push the slave past 22 kHz where the decimator removes it and the knob just fades to silence. At
musical pitches the clamp never binds — 8× `Sync` only starts costing amount above ~1.8 kHz.

Every mode is swept at full warp on MIDI 108 by
`WavetableWarpAliasTest.EveryWarpModeStaysCleanBelowTheFundamental` (parameterised over all 11) and
held to bounded output by `WavetableWarpBoundsTest`. **A warp mode that aliases is a regression, not a
feature** — a new mode needs one of the two defences (a `warpRateFactor` entry or the `warpNeedsOversampling` flag) **and** an entry in both suites; they iterate `Warp::Count`, so an unhandled mode fails the test run instead of shipping.

## Phase control

Every sub-oscillator restarts on note-on: a MIDI note-on in mono (and on poly voice 0), a rising edge on the
**Restart** input (the Gate jack, label and hover text as on the Oscillator) in either mode, and, in poly, a
voice going from silent to sounding. Each lands on **its own sample**: `scanMidi` records the note-on's offset
and `renderVoice` restarts the phases when it reaches it. Poly MIDI holds a voice's pitch after release, so in
poly only the gate shows a later note on a voice that already played; the Poly switch and new poly instrument
tracks wire Poly MIDI's Gate fan (ch8-15) to it. The edge detector (`GateEdges`, a Schmitt trigger at 0.1, edges
at least 16 samples apart, up to 256 per block) is shared with the Oscillator
([oscillator-voice.md](oscillator-voice.md#phase-and-rand)). The poly Gate fan (ch23-30) aliases the
Audio R block, so it is read before anything is written; `kRightBase` stays 23 and the 31 outputs
cover every input. An unpatched gate changes nothing.

**Hand-over.** A restart while the previous note still sounds would jump the waveform, so the stack as it stood
(phases, sub phase and decimator history, `StackState`) keeps running as a "ghost" and fades out over 64 samples
along the raised cosine in `Source/Modules/RestartFade.h`, while the restarted stack comes in; the sample of the
restart itself is the old sound and the new one is at its exact phase 64 samples later. A restart inside a
hand-over turns what it interrupts into one more ghost with the share it holds (`RestartGhosts.h`, up to five),
so even an audio-rate gate cannot step the output. A voice that goes from silent to sounding has nothing to fade
from and starts at once. Guarded by `SourceClickGuard` (the Wavetable is measured like the Oscillator) and
`WavetableRestartFade`. The restart point:

- **Phase** (0–360°) — where in the cycle the wave restarts.
- **Rand Phase** (0–1, default 1 on a new module; a stored module with no key keeps 0) — per-note jitter added on top.
- **Spread** (0–1) — walks the unison voices' start phases around the cycle.

At Rand Phase 0 with no `Spread`, a unison stack and a poly chord attack perfectly phase-correlated
and comb-filter themselves. All three are sampled **at the note-on instant** — they shape the attack,
not the sustain — so their CV is read at the block's first sample rather than per-sample.

**Oscillator sync.** An oscillator cabled into Restart syncs the same way as on the Oscillator
([oscillator-voice.md](oscillator-voice.md#oscillator-sync-an-oscillator-into-restart)): after three restarts in a row
under 25 ms apart (`synth::SyncRestartRun`), `restartVoice` reuses the note's per-voice random draws (`restartDraws`;
a new draw per cycle is noise at any Rand Phase above 0), places the phases and the hand-over at the gate's real
crossing (`GateEdges::firedLead`, applied by `advanceSyncLead`) and hands over in `kSyncFadeSamples` (8) instead of 64.
Measured with a Fixed-rate Oscillator master, Sine/Square/Saw/Triangle at 100, 440, 2000 and 301 Hz, Unison 1 and 5, Rand
Phase 0, 0.5 and 1, mono and poly: before, 0 to -4 dB off the master's harmonics at Rand Phase above 0 and as bad as -23
dB at 0; after, -34 dB or better (to -86 dB). A mono oscillator's audio on a poly Restart fans to all eight voices
(`resolvePolyLink`, role Gate, as for the Oscillator). Level at Rand Phase 0 is checked against what the table's saw can
reach: it starts at zero, so a master above twice the note only lets it climb part of the way. Tests:
`WavetableOscillatorSync.RestartFromARealOscillator`,
`GraphEditorTest.ResolvePolyLinkFansMonoAudioOntoAPolyWavetableRestart`.

## Unison, stacking and the sub

- **Unison** (1–8) with **Detune** (0–100 cents) spreads voices symmetrically about the root.
- **Stack** applies an interval on top of detune: `Detune` (none), `Octave`, `Power Chord`, `12th`,
  `Major`, `Minor`. Voice 0 always stays at the root pitch. The stack is constant power: it is divided by the
  root of the summed mean leg power of the voices, so its loudness stays that of one voice (the sub is added
  after this).
- **Detune** (default 35 cents) is a skewed knob (centre at 25 cents) that stores cents. A new Wavetable starts at Detune 35 cents, Blend 75 %, Width 100 %, Rand Phase 100 % like the Oscillator, so raising Unison is heard at once; a stored (trusted) module with none of those keys keeps Detune 0, Blend 100 %, Width 0, Rand Phase 0 (`applyStoredParamDefaults`); a model-authored node takes the current defaults. The stacking, Blend gain (`unisonBlendGain`), pan law (`unisonPanGains`) and restart-phase helpers live in `Source/Modules/UnisonVoicing.h`, shared with the Oscillator.
- **Blend** (0–1, default 0.75) fades the voices away from the centre (the middle voice, or the middle two for an
  even count, stay at full level; in a Stack mode it is voice 0, the root pitch) and runs to outer voices louder than the
  centre at 100 % (curve and Width pan in [oscillator-voice.md](oscillator-voice.md#blend-width-and-detune)). Every voice
  gain moves no faster than 1.0 per 10 ms (`UnisonMix.h`, ramped inside the block): Blend, Width and the voice
  count glide, a voice that leaves keeps rendering at the outermost detune and stack interval that stays while it
  fades out, and the divisor follows, so changing Unison in a held note does not step.
- **Sub** (0–1) adds a sub-oscillator one or two octaves down (`Sub Oct`), as a sine or square
  (`Sub Wave`). It is **read out of the built-in `Basic Shapes` table** rather than generated
  naively, so it inherits the same mip anti-aliasing as everything else — a naive square would alias
  at exactly the pitches the pyramid exists to protect.

## Stereo output

The module has two output jacks. `Width` (default 1; a single voice stays centred) pans the unison voices across the stereo field; `Pan` places
the whole voice.

- **Mono**: `Audio L` on ch0, `Audio R` on `kRightBase` (ch23).
- **Poly**: `Audio L` on ch0-7, `Audio R` on ch23-30. Both blocks are poly-bus heads spanning 8
  voices, so a stereo unison stack reaches the Voice Mixer as two cables rather than sixteen.

`Audio R` sits on a **dedicated block above every input channel**, not on ch1 — ch1 is `Position` CV,
and putting the right leg there would have made the module's flagship parameter unroutable.

The pan law is **balance, not equal-power**: centre leaves both legs at unity and panning only
attenuates the leg you move away from. An equal-power centre sits at 1/√2, which would have quietened
every existing mono patch by 3 dB the moment the module grew a second jack. At `Width` 0 / `Pan` 0 (or at any Width with Unison 1),
`Audio L` carries bit-for-bit what a mono build carried (`DefaultsKeepAudioLIdenticalToAudioR`).

## Sync input

The `Sync` jack takes an audio-rate signal from another oscillator. `Sync In` selects what it does:

- `Hard Sync` — resets every sub-oscillator on the master's rising zero crossing. Crossings are
  computed **once per block** into a shared array, because each voice renders in its own pass and a
  running per-sample state would be consumed by voice 0 and wrong for voice 1. At an exact integer
  frequency ratio the reset is a no-op — the slave already completes a whole number of cycles per
  master period.
- `Ring Mod` — multiplies the finished voice by the input.
- `AM` — multiplies by `0.5 + 0.5·input`.

## Anti-aliasing: the mip pyramid

Every frame is stored as an **11-level mip pyramid** instead of being filtered at render time.

- Mip `m` is band-limited to `mipHarmonicLimit(m)` harmonics: 1023, 511, 255, 127, 63, 31, 16, 8, 4,
  2, 1.
- Mip `m` is stored at `mipLength(m)` samples — `max(64, 2048 >> m)` — so the shortest mips stay long
  enough for linear interpolation to be well conditioned.
- `selectMip(dt)` picks the finest mip whose highest harmonic still clears Nyquist for that note. The
  mip is chosen from the **highest-frequency end of the block's frequency ramp**, so a rising glide
  cannot alias mid-block.
- Storage is ~17 KB per frame. Built-in tables (~557 KB each) are built once per process and **shared
  by every module instance**; only file-loaded tables are per-instance.
- Verified by `WavetableOscillatorModuleTest.HighNotesDoNotAlias` (square wave at MIDI 108 — asserts
  the band below the fundamental stays >26 dB down) and
  `WavetableMipGeometry.LimitsDecreaseMonotonicallyToTheFundamental`.

## Table synthesis

Tables are synthesised from harmonic spectra via inverse FFT (`TableBuilder`), one FFT per distinct
mip length. The builder **self-calibrates** its inverse-transform gain at construction (a unit
fundamental must come out as a unit-amplitude sine), so table amplitudes do not depend on the platform
FFT engine's normalisation convention, and every mip of a frame shares one consistent gain. Each
finished table is peak-normalised to 1.0 across mip 0, preserving relative frame levels.

## Interpolation quality

`Interp` chooses how a stored frame is read at a fractional phase:

- `Linear` — two taps.
- `Hermite` — 4-point Catmull-Rom. The coarse mips are only 64–256 samples long, where linear
  interpolation between stored points visibly droops the top harmonics; the cubic fit follows the
  curve instead of chording it, for three extra taps per read.

Frame-to-frame (scan) interpolation stays linear in both modes.

## Loading a wavetable file

- `loadWavetableFile(const juce::File&)` — **message thread only**. Accepts anything
  `juce::AudioFormatManager::registerBasicFormats()` can read (WAV, AIFF, FLAC, Ogg); stereo files
  are summed to mono.
- **Import modes** (`Import` parameter) decide how the file is cut:

  | Mode | Behaviour |
  |---|---|
  | `Auto` | whole 2048-sample blocks when the file is long enough, else one resampled cycle |
  | `256` / `512` / `1024` / `2048` | whole blocks of that size |
  | `Single Cycle` | the entire file resampled to one cycle |
  | `Pitch Detect` | normalised autocorrelation finds the period, then blocks of that period |
  | `Spectral` | like `Auto`, but every frame is resynthesised zero-phase |

- A power-of-two frame **at or below** `kFrameSize` is analysed at its own size rather than resampled
  to 2048 first, so a 256-sample table imports with none of the HF droop linear upsampling would add.
  Anything else (an odd pitch-detected period, a whole-file single cycle, a frame longer than
  `kFrameSize`) is resampled into `kFrameSize` first. **That upper bound is load-bearing** — the
  analysis buffer is exactly `kFrameSize` long, so copying a longer frame into it verbatim overflows
  the heap.
- `detectPeriod()` replaces its best lag **only on a clearly better score** (margin `1e-3`). A
  periodic signal correlates just as well at 2× and 3× its period, so without the margin
  floating-point noise decides which multiple wins and a 512-sample sine imports as 1536-sample
  frames.
- `Spectral` collapses each frame's spectrum onto sine phase, keeping magnitudes. Scanning then
  cross-fades magnitudes instead of beating phase-incoherent frames against each other — which is what
  makes a table sampled from unrelated cycles morph smoothly instead of cancelling at the midpoint.
- At most `kMaxFrames` (64) frames are kept, chosen **evenly spaced** across the file so a 256-frame
  table still spans its whole morph range.
- DC (bin 0) is discarded during analysis, so a loaded table cannot introduce a DC offset.
- The call does **not** touch any parameter. The UI's "Load Wavetable..." button selects the
  `Loaded File` choice itself after a successful load.

## Wavetable folder browser

Rather than reopening a file chooser per table, point the module at a directory once and step through
it:

- `setWavetableFolder(dir)` scans for readable audio files (non-recursive), sorts them by name and
  parks the cursor on the currently loaded file if it lives there. **Scanning loads nothing on its
  own.**
- `nextWavetable()` / `previousWavetable()` / `stepWavetable(delta)` walk the list, wrapping at both
  ends and **skipping entries that fail to load**, so one unreadable file cannot wedge the browser.
- `selectWavetableAt(index)` jumps directly.
- The folder path persists two ways: per-module through `getExtraState()`/`getStateInformation` (so a
  preset reopens pointed at the right place), and app-wide through `ApplicationProperties` —
  `GraphEditor` holds the last-used folder so a newly dropped Wavetable card seeds its browser from
  it, and `MainComponent` owns the settings round trip via `onWavetableFolderChanged`. This is the
  same split the cable-colour config uses: `GraphEditor` stays settings-free.
- Cards also accept **drag-and-drop** of an audio file, which goes through the same import path as the
  Load button.
- A failure returns `false` and leaves the current table untouched; the `Loaded File` choice falls
  back to `Basic Shapes` when nothing is loaded, so a broken preset never goes silent.
- **State**: the source path is published through `getExtraState()`/`setExtraState()` as a
  `wavetableFile` property. This is the mechanism presets and undo/redo actually use —
  `AIStateMapper::graphToJSON` persists parameters plus `getExtraState()` and never calls
  `getStateInformation`, so a path stored only in the binary `ModuleState` blob would be silently
  dropped on every preset load. `setExtraState` is reached **only on the trusted path**: untrusted
  model-authored JSON must never name a file for the app to open (the same guard as the Sampler).
- `getStateInformation`/`setStateInformation` also carry the path, for the plain
  `juce::AudioProcessor` contract; `setStateInformation` reloads the file first so the restored
  `table` choice stays authoritative, and silently skips a file that no longer exists.

## Thread-safe table handoff

Built-in tables are immutable and shared, so they need no synchronisation. A file-loaded table is
built on the message thread and handed over through a **pending / retired slot pair** guarded by a
`juce::SpinLock`:

- `publishLoadedTable()` (message thread) reclaims whatever the audio thread retired, then stores the
  new table in `pendingTable`. The lock is held for three pointer moves; the actual frees happen
  after it is released.
- `adoptPendingTable()` (audio thread, once per block) try-locks; on success it moves `pendingTable`
  into `audioLoadedTable` and the displaced table into `retiredTable`. **Pointer moves only — the
  audio thread never allocates and never frees a table.** A failed try-lock keeps the current table
  and retries next block.
- Because every publish reclaims one retired slot before filling the pending slot, the retired slot is
  always empty when the audio thread needs it.
