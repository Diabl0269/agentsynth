# Core Modules Reference

Detailed specifications for Agent Synth's primary synthesis modules.

> **Dual I/O is INHERITED, not registered.** A module does not opt into the stereo-jack toggle;
> `ModuleBase`'s constructor decides from its channel shape and adds the `dualIO` parameter itself.
> The rule (`ModuleBase::hasStereoOutputPairShape`) is **≥ 2 inputs and exactly 2 outputs** — audio
> on raw ch0/ch1 with any further inputs being CV, i.e. the FX shape. A module of that shape also
> inherits the collapsing output jack (`ModuleBase::hasCollapsibleOutputPair` drives the default
> `getOutputPortLabel` / `getVisibleOutputPortCount` / `mapOutputChannel`), so a new stereo FX needs
> **no Dual I/O code at all** — pinned by
> `StereoDeclaration.ANewModuleWithTheStereoShapeInheritsTheWholeToggle`. The *input* side is
> deliberately not inferred: whether ch0/ch1 are an input pair is not knowable from the shape (Voice
> Mixer's ch0-7 are voice inputs; the Ring Modulator's ch0/ch1 are Carrier and Modulator), so an
> input pair stays an explicit `mapStereoPairInput` declaration.
>
> Two exceptions, both stated in the constructor call and both swept by
> `StereoDeclaration.EveryFactoryModuleFollowsTheShapeRuleOrADocumentedException`:
>
> | `ModuleBase::StereoAudio` | Modules | Why |
> |---|---|---|
> | `Auto` (default) | every FX, Voice Mixer, Ring Modulator | shape says stereo; toggle ships **collapsed** |
> | `Declared` | Oscillator, Wavetable, Filter, VCA, Sampler | a second leg the shape cannot see (own `kRightBase` block, or a ch0/ch1 pair alongside more outputs); ships **split** |
> | `None` | Comparator, Rec Tap | shape matches by accident. Comparator's ch0/ch1 are Signal + Threshold CV in and Gate + inverted Gate out — no audio output at all. Rec Tap is a hidden recording tap whose two channels are the take's capture pair, wired by the record flow and never patched. |
>
> This replaced a per-module `addDualIOParameter()` call, which is a thing you can forget: the Ring
> Modulator shipped a stereo output pair without one for a whole release — no header toggle, no
> Preferences row, no global "Split Left/Right jacks" coverage, and nothing red in the build. It now
> gets all of it by inheritance, and `StereoDeclaration.RingModulatorGetsTheTogglePurelyByInheritance`
> is what keeps that true.
>
> `AIStateMapper::dualIOCapableModuleTypes()` remains the single authoritative *list* — probed from
> the module factory (one throwaway instance per type, asked `hasDualIOParameter()`), cached, and read
> by both consumers of the question: the Preferences per-module defaults popup
> (`PreferencesSettingsTab::getDualIOModuleTypes()`) and the default a newly created module gets
> (`GraphEditor::applyDefaultDualIOForNewModule`). Never re-list those types by hand.
> The toggle's semantics (what "off" means per layout, what happens to existing cables in each
> direction) live in [`fx-modules.md § Stereo I/O`](fx-modules.md#stereo-io-dual-io-toggle).
>
> One consequence worth knowing: `dualIO` is now parameter **index 1** on every module that has it
> (straight after `bypassed`), where it used to sit late in each module's own list. Saved patches are
> unaffected — every parameter is keyed by `paramID` in both `ModuleBase::getStateInformation` and
> `AIStateMapper`, and the id and per-module default are unchanged
> (`StereoDeclaration.APatchSavedBeforeTheMoveLoadsWithTheSameLayout` loads a pre-change patch and
> checks the layout). But it is a live demonstration of why
> [the guide](development-guide.md) says to look parameters up by ID: a `getParameters()[n]`
> site that shifted does not fail loudly, it silently resolves to the wrong parameter.

> **Factory presets author BOTH legs.** A preset is applied through `AIStateMapper` with no knowledge
> of preferences, so every module comes up on its **constructor default** — and the split-block voice
> modules default to SPLIT. A preset whose cable list only wires ch0 therefore loads as a patch with
> every `Audio R` jack bare, which is what "all the connections are one sided" meant. So each preset's
> `connections` carry the right leg explicitly (`Osc ch14 → Filter ch11 → VCA ch16 →` the FX pair or
> `Audio Output` R), and a right leg is never faked with a channel that carries nothing — Poly Pad
> wired the VCA's ch1, which is its gain-CV *input* and a silent pass-through as an output, so one
> side received actual silence. Rationale and traps: `Source/PresetManager.cpp`; enforced for every
> preset by `EveryFactoryPreset/PresetStereoTest.RendersBothChannels`.

> **Level parameters.** There is no universal per-module gain. Modules that output audio expose a level control: Oscillator, LFO, Noise and Voice Mixer have their own `level`; VCA has `gain`; Filter and the FX modules use the shared opt-in output-level stage documented in [`fx-modules.md § Output Level`](fx-modules.md#output-level-shared-stage). Modules that output **pitch/gate CV or MIDI** (Sequencer, Poly Sequencer, ADSR, Poly MIDI, MIDI Keyboard, External MIDI) deliberately have none — scaling a V/oct pitch CV transposes it, and scaling a gate drops it below the `> 0.5f` trigger threshold. Attenuverter has none because it already is a gain stage.

## Oscillator Module
- **Waveforms**: Sine, Square, Saw, Triangle.
- **Features**: 
    - PolyBLEP anti-aliasing on Square and Saw; PolyBLAMP on Triangle.
    - Waveform crossfade (64-sample fade on waveform changes).
    - MIDI-to-Frequency tracking with unison and detune support.
    - Integrated visual buffer for real-time waveform display.
- **Poly mode**: 8 voices driven by pitch CV (Hz). See [Poly Channel Layout](#poly-channel-layout) for channel details.
- **Smoothing**: Level is smoothed over 10 ms, materialised into a per-block ramp before the voice loop so poly mode does not advance the smoother once per voice. Fine reaches the output through the existing 5 ms frequency smoother; Detune is a frequency ratio (phase-continuous) and is deliberately not smoothed.
- **Stereo output (issue #219)**: two output jacks, `Audio L` and `Audio R`, plus a `Pan` parameter (−1…+1, default 0) and a `Pan` CV jack. `Audio R` lives on a dedicated block at `kRightBase` (ch14, fanning to ch14-21 in poly), **not** on ch1 — ch1 is the Waveform CV input. Poly gives each voice its own L/R pair, so a panned chord reaches the Voice Mixer as a stereo image rather than eight mono voices.
- **Pan is a balance law, not equal-power** (`ModuleBase::panGains`): centre leaves both legs at unity and panning attenuates only the leg you move away from. At the default Pan of 0, `Audio L` carries bit-for-bit what it carried while the module was mono and `Audio R` is an identical copy — an equal-power centre would have quietened every existing patch by 3 dB. Pinned by `StereoPanLaw.CentreLeavesBothLegsAtUnity` and `OscillatorStereo.CentrePanDoesNotAttenuateAudioL`.
- **Stereo and Poly are independent axes** — there is deliberately no three-way `mono / stereo / poly` switch. Poly is voice count, stereo is spatial placement, and you want both at once (a chord spread across the panorama).
- **Poly `processBlock` CV-save order**: In poly mode, both the per-voice pitch CVs (ch0-7) and the shared mod CVs (ch8-13) are copied into pre-allocated `std::array` caches (`pitchCVCache`, `waveformCVCache`, `octaveCVCache`, `coarseCVCache`, `fineCVCache`, `levelCVCache`, `panCVCache`) **before** the output buffer is cleared. This is necessary because ch0-7 carry both output audio (written after the clear) and input pitch CV, so they must be read first.
- **Buffer aliasing note**: Declared with **22** output channels (14 before #219) so JUCE's `AudioProcessorGraph` correctly copies shared mod-CV input channels (8-13) when they fan out to multiple downstream nodes. What matters is that the output count stays **above every CV input channel index** below `kRightBase` — the Audio R block raising it from 14 to 22 preserves that. Channels 8-13 of the output remain silent pass-throughs.
- **The poly CV clear is bounded at `kRightBase`.** It used to run to the end of the buffer; with Audio R sitting above the CV block, an unbounded clear would erase the right leg.
- **Unison and Detune CV (FRO314)**: declared inputs grew 14 -> 16 to add a CV jack for each — they had knobs but no way to modulate them, which is why a dropped cable used to be refused. Both are global (not per-voice) params, so they use a fixed pair of raw channels, `kUnisonCVChannel`/`kDetuneCVChannel` (ch14/15), the SAME in mono and poly — unlike the Waveform/Octave/.../Pan jacks, there is no separate poly variant to place. `kRightBase` stays a **literal 14**, not derived from the input count, so growing the inputs never moves Audio R's raw channel and no existing patch routed from it breaks. ch14/15 alias the Audio R output block exactly like ch0 already aliases Pitch CV in / `Audio L` out — both are read (a single first-sample `blockCV`-style capture; Unison/Detune are already read once per block, unsmoothed, same as the parameter itself) before this module writes anything to those channels. Unison CV moves the knob via `ModuleBase::modulateNormalised` (rounded to the nearest integer voice count, clamped 1-8); Detune CV moves it the same way as a plain float.

## Wavetable Module
Serum / Vital-style wavetable oscillator (`Source/Modules/WavetableOscillatorModule/`). Type-name string: `"Wavetable"`.

**Source layout** (split by concern since the module outgrew one file):
- `WavetableOscillatorModule.h`/`.cpp` — the module class: parameters, channel map, render path.
- `WavetableTableBuilder.h`/`.cpp` — table geometry (mip constants, `Wavetable`) and `TableBuilder`,
  the FFT-based mip-pyramid synthesiser (see [Table synthesis](#table-synthesis) below), in a
  `wavetable` namespace. `WavetableOscillatorModule` aliases these names back onto itself
  (`using Wavetable = wavetable::Wavetable;` etc.), so `WavetableOscillatorModule::Wavetable`,
  `::TablePtr`, `::TableBuilder` and the mip-geometry functions still resolve exactly as before.

- **Tables**: six built-ins — `Basic Shapes` (sine → triangle → saw → square), `Harmonic Sweep`, `Pulse` (duty-cycle morph), `Formant`, `Bell`, `Digital` — plus `Loaded File` for a table read from disk. Each built-in has 32 frames.
- **Position (the "3D" scan)**: `Position` (0–1) scans continuously through the frame stack. Reads are **bilinear** — linear within the frame (phase) and linear between the two adjacent frames — so morphing is click-free. `Interp` switches the within-frame read to 4-point Catmull-Rom (see [Interpolation quality](#interpolation-quality)).
- **Parameters**: Table (choice), Position (0–1), Octave (−4…+4), Coarse (−12…+12), Fine (±100 cents), Level (0–1), Poly (bool), Unison (1–8), Detune (0–100 cents), Warp (choice), Warp Amt (0–1), Phase (0–360°), Rand Phase (0–1), Spread (0–1), Width (0–1), Blend (0–1), Stack (choice), Sub (0–1), Sub Oct (−1/−2), Sub Wave (Sine/Square), Pan (±1), Sync In (choice), Import (choice), Interp (choice).
- **CV inputs (mono mode)**: ch0 = Pitch (inert — see below), then ch1…ch15 = Position, Octave, Coarse, Fine, Level, Warp, Phase, Rand, Detune, Spread, Width, Blend, Sub, Pan, Sync. **16 visible jacks.**
- **Outputs**: two audio jacks — `Audio L` and `Audio R`. See [Stereo output](#stereo-output).
- **Poly mode**: 8 voices driven by pitch CV in Hz on ch0-7, shared mod CV from ch8. See [Poly Channel Layout](#poly-channel-layout).
- **Octave / Coarse / Fine apply in poly mode too** — they transpose the incoming pitch CV. (This differs from `OscillatorModule`, where the tuning parameters only affect the MIDI fallback voice.)
- **Mono pitch CV is inert**, exactly as in `OscillatorModule`: mono jack 0 shares raw channel 0 with `Audio L`, so pitch comes from MIDI. The jack is kept so the mono and poly jack layouts match.

> **Channel indices are append-only.** `Position`/`Octave`/`Coarse`/`Fine`/`Level` keep the raw channel numbers they had before issue #180 (mono ch1-5, poly ch8-12) because saved patches route by raw index — inserting a new jack among them would silently repoint every existing modulation. New jacks go on the end. `WavetableOscillatorModuleTest.LegacyModCVChannelsKeepTheirIndices` pins this.

### Warp
`Warp` reshapes the table read after mip selection; `Warp Amt` (with CV on the Warp jack) sets the depth. `Off` is index 0, so a preset saved before warps existed loads unwarped.

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

1. **Conservative mip selection.** `warpRateFactor(mode, amount)` reports the steepest phase-map slope a mode reaches — the factor by which it can outrun a plain 1× read. `selectMip()` is fed `dt × factor`, so the stored frame is already band-limited for the fastest read the warp will perform.
2. **Oversampling.** Modes whose output has a *step* or an *amplitude nonlinearity* generate harmonics no amount of input band-limiting can prevent. Those render at 4× and come back through a 33-tap Blackman-windowed sinc decimator (~−74 dB stopband). The whole voice — every unison sub-oscillator plus the sub — is summed at the oversampled rate and decimated once per stereo leg, which is valid because decimation is linear and costs two filters per voice instead of two per sub-oscillator.

When oversampling is active the mip is chosen against the **oversampled** Nyquist (`warpRate / kOversample`): those extra harmonics are representable while rendering at 4×, and the decimator removes them on the way down. Band-limiting to the base Nyquist there instead would collapse a hard-synced table to a sine before the warp ever saw it.

`PWM` is the interesting exception: subtracting two reads of the same band-limited table cannot introduce harmonics the table did not already have, so it is alias-free by construction and needs neither defence.

**`clampWarpAmount()`** covers what mips cannot. Mip selection band-limits the *harmonics* of a read, but `Sync` and `Formant` also multiply the read's own **fundamental** — at 8×, a 4 kHz note reads at 33 kHz. The clamp backs the amount off at extreme pitches so the sped-up read stays under the **base** Nyquist. Deliberately not the oversampled one: the oversampling headroom exists for the harmonics a discontinuity throws off and gets filtered away on the descent, so spending it here would let `Sync` push the slave past 22 kHz where the decimator removes it and the knob just fades to silence. At musical pitches the clamp never binds — 8× `Sync` only starts costing amount above ~1.8 kHz.

Every mode is swept at full warp on MIDI 108 by `WavetableWarpAliasTest.EveryWarpModeStaysCleanBelowTheFundamental` (parameterised over all 11) and held to bounded output by `WavetableWarpBoundsTest`. **A warp mode that aliases is a regression, not a feature** — extend both suites when adding one.

### Phase control
Every sub-oscillator restarts on note-on (MIDI note-on in mono; a voice going from silent to sounding in poly):

- **Phase** (0–360°) — where in the cycle the wave restarts.
- **Rand Phase** (0–1) — per-note jitter added on top.
- **Spread** (0–1) — walks the unison voices' start phases around the cycle.

Without `Spread` or `Rand Phase`, a unison stack and a poly chord attack perfectly phase-correlated and comb-filter themselves. All three are sampled **at the note-on instant** — they shape the attack, not the sustain — so their CV is read at the block's first sample rather than per-sample.

### Unison, stacking and the sub
- **Unison** (1–8) with **Detune** (0–100 cents) spreads voices symmetrically about the root.
- **Stack** applies an interval on top of detune: `Detune` (none), `Octave`, `Power Chord`, `12th`, `Major`, `Minor`. Voice 0 always stays at the root so `Blend` has an unshifted centre to fade against.
- **Blend** (0–1) fades the detuned/stacked voices against that always-present centre voice, so thinning the chorus does not change the fundamental's level.
- **Sub** (0–1) adds a sub-oscillator one or two octaves down (`Sub Oct`), as a sine or square (`Sub Wave`). It is **read out of the built-in `Basic Shapes` table** rather than generated naively, so it inherits the same mip anti-aliasing as everything else — a naive square would alias at exactly the pitches the pyramid exists to protect.

### Stereo output
The module has two output jacks. `Width` pans the unison voices across the stereo field; `Pan` places the whole voice.

- **Mono**: `Audio L` on ch0, `Audio R` on `kRightBase` (ch23).
- **Poly**: `Audio L` on ch0-7, `Audio R` on ch23-30. Both blocks are poly-bus heads spanning 8 voices, so a stereo unison stack reaches the Voice Mixer as two cables rather than sixteen.

`Audio R` sits on a **dedicated block above every input channel**, not on ch1 — ch1 is `Position` CV, and putting the right leg there would have made the module's flagship parameter unroutable.

The pan law is **balance, not equal-power**: centre leaves both legs at unity and panning only attenuates the leg you move away from. An equal-power centre sits at 1/√2, which would have quietened every existing mono patch by 3 dB the moment the module grew a second jack. At `Width` 0 / `Pan` 0, `Audio L` carries bit-for-bit what the mono build carried (`DefaultsKeepAudioLIdenticalToAudioR`).

### Sync input
The `Sync` jack takes an audio-rate signal from another oscillator. `Sync In` selects what it does:

- `Hard Sync` — resets every sub-oscillator on the master's rising zero crossing. Crossings are computed **once per block** into a shared array, because each voice renders in its own pass and a running per-sample state would be consumed by voice 0 and wrong for voice 1. Note that at an exact integer frequency ratio the reset is a no-op — the slave already completes a whole number of cycles per master period.
- `Ring Mod` — multiplies the finished voice by the input.
- `AM` — multiplies by `0.5 + 0.5·input`.

### Anti-aliasing: mip pyramid
Every frame is stored as an **11-level mip pyramid** instead of being filtered at render time.

- Mip `m` is band-limited to `mipHarmonicLimit(m)` harmonics: 1023, 511, 255, 127, 63, 31, 16, 8, 4, 2, 1.
- Mip `m` is stored at `mipLength(m)` samples — `max(64, 2048 >> m)` — so the shortest mips stay long enough for linear interpolation to be well conditioned.
- `selectMip(dt)` picks the finest mip whose highest harmonic still clears Nyquist for that note. The mip is chosen from the **highest-frequency end of the block's frequency ramp**, so a rising glide cannot alias mid-block.
- Storage is ~17 KB per frame. Built-in tables (~557 KB each) are built once per process and **shared by every module instance**; only file-loaded tables are per-instance.
- Verified by `WavetableOscillatorModuleTest.HighNotesDoNotAlias` (square wave at MIDI 108 — asserts the band below the fundamental stays >26 dB down) and `WavetableMipGeometry.LimitsDecreaseMonotonicallyToTheFundamental`.

### Table synthesis
Tables are synthesised from harmonic spectra via inverse FFT (`TableBuilder`), one FFT per distinct mip length. The builder **self-calibrates** its inverse-transform gain at construction (a unit fundamental must come out as a unit-amplitude sine), so table amplitudes do not depend on the platform FFT engine's normalisation convention, and every mip of a frame shares one consistent gain. Each finished table is peak-normalised to 1.0 across mip 0, preserving relative frame levels.

### Interpolation quality
`Interp` chooses how a stored frame is read at a fractional phase:

- `Linear` — two taps, the original behaviour.
- `Hermite` — 4-point Catmull-Rom. The coarse mips are only 64–256 samples long, where linear interpolation between stored points visibly droops the top harmonics; the cubic fit follows the curve instead of chording it, for three extra taps per read.

Frame-to-frame (scan) interpolation stays linear in both modes.

### Loading a wavetable file
- `loadWavetableFile(const juce::File&)` — message thread only. Accepts anything `juce::AudioFormatManager::registerBasicFormats()` can read (WAV, AIFF, FLAC, Ogg); stereo files are summed to mono.
- **Import modes** (`Import` parameter) decide how the file is cut:

  | Mode | Behaviour |
  |---|---|
  | `Auto` | whole 2048-sample blocks when the file is long enough, else one resampled cycle (the pre-#180 behaviour) |
  | `256` / `512` / `1024` / `2048` | whole blocks of that size |
  | `Single Cycle` | the entire file resampled to one cycle |
  | `Pitch Detect` | normalised autocorrelation finds the period, then blocks of that period |
  | `Spectral` | like `Auto`, but every frame is resynthesised zero-phase |

- A power-of-two frame **at or below** `kFrameSize` is analysed at its own size rather than resampled to 2048 first, so a 256-sample table imports with none of the HF droop linear upsampling would add. Anything else (an odd pitch-detected period, a whole-file single cycle, a frame longer than `kFrameSize`) is resampled into `kFrameSize` first. That upper bound is load-bearing — the analysis buffer is exactly `kFrameSize` long, so copying a longer frame into it verbatim overflows the heap.
- `detectPeriod()` replaces its best lag **only on a clearly better score** (margin `1e-3`). A periodic signal correlates just as well at 2× and 3× its period, so without the margin floating-point noise decides which multiple wins and a 512-sample sine imports as 1536-sample frames.
- `Spectral` collapses each frame's spectrum onto sine phase, keeping magnitudes. Scanning then cross-fades magnitudes instead of beating phase-incoherent frames against each other — which is what makes a table sampled from unrelated cycles morph smoothly instead of cancelling at the midpoint.
- At most `kMaxFrames` (64) frames are kept, chosen **evenly spaced** across the file so a 256-frame table still spans its whole morph range.
- DC (bin 0) is discarded during analysis, so a loaded table cannot introduce a DC offset.
- The call does **not** touch any parameter. The UI's "Load Wavetable..." button selects the `Loaded File` choice itself after a successful load.

### Wavetable folder browser
Rather than reopening a file chooser per table, point the module at a directory once and step through it:

- `setWavetableFolder(dir)` scans for readable audio files (non-recursive), sorts them by name and parks the cursor on the currently loaded file if it lives there. **Scanning loads nothing on its own.**
- `nextWavetable()` / `previousWavetable()` / `stepWavetable(delta)` walk the list, wrapping at both ends and **skipping entries that fail to load**, so one unreadable wav cannot wedge the browser.
- `selectWavetableAt(index)` jumps directly.
- The folder path persists two ways: per-module through `getExtraState()`/`getStateInformation` (so a preset reopens pointed at the right place), and app-wide through `ApplicationProperties` — `GraphEditor` holds the last-used folder so a newly dropped Wavetable card seeds its browser from it, and `MainComponent` owns the settings round trip via `onWavetableFolderChanged`. This is the same split the cable-colour config uses: `GraphEditor` stays settings-free.
- Cards also accept **drag-and-drop** of an audio file, which goes through the same import path as the Load button.
- Returns `false` and leaves the current table untouched on any failure; the `Loaded File` choice falls back to `Basic Shapes` when nothing is loaded, so a broken preset never goes silent.
- **State**: the source path is published through `getExtraState()`/`setExtraState()` as a `wavetableFile` property. This is the mechanism presets and undo/redo actually use — `AIStateMapper::graphToJSON` persists parameters plus `getExtraState()` and never calls `getStateInformation`, so a path stored only in the binary `ModuleState` blob would be silently dropped on every preset load. `setExtraState` is reached **only on the trusted path**: untrusted model-authored JSON must never name a file for the app to open (same guard as the Sampler).
- `getStateInformation`/`setStateInformation` also carry the path, for the plain `juce::AudioProcessor` contract; `setStateInformation` reloads the file first so the restored `table` choice stays authoritative, and silently skips a file that no longer exists.

### Thread-safe table handoff
Built-in tables are immutable and shared, so they need no synchronisation. A file-loaded table is built on the message thread and handed over through a **pending / retired slot pair** guarded by a `juce::SpinLock`:

- `publishLoadedTable()` (message thread) reclaims whatever the audio thread retired, then stores the new table in `pendingTable`. The lock is held for three pointer moves; the actual frees happen after it is released.
- `adoptPendingTable()` (audio thread, once per block) try-locks; on success it moves `pendingTable` into `audioLoadedTable` and the displaced table into `retiredTable`. Pointer moves only — **the audio thread never allocates and never frees a table**. A failed try-lock simply keeps the current table and retries next block.
- Because every publish reclaims one retired slot before filling the pending slot, the retired slot is always empty when the audio thread needs it.

## Noise Module
- **Noise Types**: White, Pink, Brown.
- **Features**: 
    - DJ-style `Color` filter (-1 to 1) for smooth low-pass (<0) and high-pass (>0) sweeps.
    - Sample-rate aware 1-pole filter cutoff calculations.
    - Level control and integrated visual buffer.
    - Mono and Poly mode support (8 voices).
- **CV Channels**: Channel 8 = Color CV, Channel 9 = Level CV.
- **Smoothing**: Color and Level are both smoothed over 10 ms into per-block ramps (poly mode renders one voice after another). Color is not only a filter cutoff — it also ends in a make-up gain — so stepping it steps the level.

## Sampler Module
Loads an audio file from disk and plays it back one of two ways.

- **Modes** (`playMode`): `Sample` — one-shot / looping playback; `Granular` — a cloud of short windowed grains read from around the `start` position.
- **Formats**: whatever JUCE's basic readers handle (WAV, AIFF, FLAC, Ogg Vorbis). The file chooser wildcard comes from `SamplerModule::getSupportedFormatWildcard()`; drag-and-drop gates on `SamplerModule::isSupportedAudioFile()` (an extension check, so hovering a folder of files stays cheap).
- **Loading a sample** — three ways:
  1. The **Load Sample…** button (a `juce::FileChooser`).
  2. **Drop an audio file onto the module** — replaces its sample. `ModuleComponent` implements `juce::FileDragAndDropTarget` and returns `false` from `isInterestedInFileDrag` for every non-Sampler module, so a file dropped on, say, an Oscillator falls through to the canvas instead of being silently swallowed.
  3. **Drop an audio file onto empty canvas** — `GraphEditor` creates a Sampler already holding it (dropping several files cascades one Sampler each). The file is loaded into the processor *before* it joins the graph, because `recordStructuralChange` snapshots the graph afterwards and that snapshot is what undo/redo replays.
- **Waveform overview**: peaks are cached per (sample, width) and drawn as a single filled path, not one `drawVerticalLine` per column — the canvas renders module cards under GraphEditor's zoom transform, and per-column 1 px lines do not tile at any zoom ≠ 1 (visible gaps and moiré striping). The 15 Hz timer repaints only when the sample changes or the playhead crosses a whole pixel.
- **Parameters**: `playMode` (choice), `pitch` (±24 semitones), `rootNote` (0-127, default 60), `loop` (bool, default on), `start` (0-1), `grainSize` (5-500 ms), `density` (1-100 grains/sec), `spray` (0-1), `level` (0-1, default 0.8).
- **Channel layout** (mono module — no poly mode): in ch0 = Trigger/Gate, ch1 = Pitch CV, ch2 = Position CV, ch3 = Grain Size CV, ch4 = Density CV, ch5 = Spray CV, ch6 = Level CV, ch7 = Root Note CV. Out ch0/ch1 = Audio L/R; ch2-7 are silent pass-throughs.
- **Buffer aliasing note**: 8 outputs are declared even though only ch0-1 carry audio, so JUCE copies the CV input channels instead of letting the post-cache clear scribble on a buffer another node still needs — the same constraint as the Oscillator's 22-channel declaration. Root Note CV (ch7, FRO312) was appended last rather than inserted earlier, so no existing saved-patch routing shifts.
- **Gate precedence**: trigger CV > MIDI note > free-run. "A trigger cable is connected" is *latched* on the first non-zero sample rather than re-derived per block: a legitimately-low gate is an all-zero channel, indistinguishable from an unpatched jack, so re-deriving it would let a closed gate silently fall back to free-running. With nothing patched and no MIDI, a loaded sample plays immediately — dropping the module in and picking a file makes sound with no wiring.
- **MIDI gate is a `heldNotes` bitset, and every Note-On retriggers** (FRO246): the gate reads open while *any* note is held, and a Note-On event always cuts and restarts the sample (playhead back to `start`), even if the gate never reads a falling edge — a legato Note-On with no Note-Off first, or a Note-Off immediately followed by a Note-On in the same block (a transport loop restart landing inside one block), both used to be silently dropped; the very first MIDI note after the module had been free-running is likewise now a real retrigger rather than a continuation of the free-run playhead at the new pitch. Two consequences: with two notes held and the more recently pressed one released, playback keeps going rather than stopping — but at the *released* note's pitch (`midiNote` only updates on a Note-On, so nothing hands pitch back to the still-held note); there is no last-note-priority.
- **Pitch**: `2^((pitch + pitchCV×24 + (midiNote − rootNote)) / 12)`, times the file-rate/device-rate ratio so a 48 kHz file plays at the right speed on a 44.1 kHz device. Reads are 4-point Catmull-Rom interpolated.
- **Granular engine**: 24-grain pool, Hann-windowed, spawned every `sampleRate / density` samples while the gate is open; grain start positions wrap rather than clamp so `spray` keeps scattering near either end. Output is scaled by `1/sqrt(density × grainSize)` so loudness stays roughly constant as the cloud thickens, then hard-limited to [-1, 1]. When the pool is exhausted new grains are dropped.
- **Anti-click**: a 64-sample linear ramp on trigger and release; a one-shot ramps out at the last frame rather than cutting.
- **Smoothing**: Level is smoothed over 10 ms, but *snapped* on every rising gate — a new note starts at the knob's current value rather than ramping up to it from whatever the last note left behind (the 64-sample fade-in above is what keeps the note start click-free). Pitch is a playback rate and Start / Grain Size / Density / Spray are only consulted when a grain spawns or a loop wraps, so none of them is smoothed.
- **Bypass**: clears its output — the documented pure-source exception to the bypass/mute contract (every input is CV/gate, so there is no dry signal to pass through).
- **Sample lifetime**: `loadSampleFile()` (message thread) publishes a reference-counted `SampleData` under a `SpinLock`; `processBlock` takes the *try*-lock, so the audio thread never blocks — a block that races a load renders silence. Replaced samples stay alive in a message-thread-owned array so no destructor ever runs on the audio thread. Files longer than `kMaxSampleSeconds` (120 s) are truncated, with one log line.
- **Persistence**: the loaded path is *not* a parameter, so it round-trips through `ModuleBase::getExtraState()` / `setExtraState()`, which `AIStateMapper` serialises as the node's `"state"` object. Restored **only on the trusted path** (our own undo/redo snapshots and presets) — untrusted model output must never be able to name a file for the app to open. See [`ai/patch-format.md`](../ai/patch-format.md).
- **Not a `PatchEval` sound source**: `evaluatePatch` deliberately does *not* count a Sampler towards `sourceReachesOutput`, because it is silent until a file is loaded and nothing in a model-authored patch can load one — counting it would let `AIIntegrationService`'s structural gate accept a patch that can only ever play silence. A patch whose output is fed *only* by a Sampler is rejected with a reason that names the Sampler and says to add an Oscillator or Noise module. This gate only applies to AI-authored patches; dragging a Sampler in by hand is unaffected.

## Filter Module
- **Types**: 7 filter types — `LPF24`, `LPF12`, `HPF24`, `HPF12`, `BPF24`, `BPF12`, `Notch`.
- **Implementation**: `LPF24/12`, `HPF24/12`, `BPF24/12` use `juce::dsp::LadderFilter`; `Notch` uses `juce::dsp::StateVariableTPTFilter` (notch computed as input minus bandpass).
- **Parameters**: Cutoff (20–20000 Hz), Resonance (0–1), Drive (1–10), Filter Type (choice), Poly (bool), Level (0–1, default 1.0 — the shared output-level stage; see [`fx-modules.md § Output Level`](fx-modules.md#output-level-shared-stage)). Level scales ch0 in mono mode and all 8 voice channels in poly mode, plus the matching `Audio R` block, never the CV inputs. It goes through `ModuleBase::applyOutputLevelSplit` so **one** smoothing ramp covers both legs — two `applyOutputLevel` calls would advance the smoother twice and leave the right leg a block behind the left.
- **Stereo (issue #219)**: two audio input jacks (`Audio L`, `Audio R`) and two output jacks. Unlike a pure source, a filter needs the right leg as an **input** as well as an output, so `kRightBase` (ch11, fanning to ch11-18 in poly) is both. Each leg has its own `juce::dsp::LadderFilter` (and notch SVF) per voice — `ladders[leg][voice]` — because a stereo signal through a single shared ladder collapses back to mono. Both legs get identical per-sample coefficients; only their state is separate.
- **Why not FX-style Dual I/O**: the Filter carries the same Dual I/O toggle as the FX (defaulting to *on*), but not their channel layout. The FX collapse assumes a contiguous ch0/ch1 audio pair, and ch1 here is the Cutoff CV input — moving Cutoff would break every saved patch that modulates it, so the right leg goes on its own block above the CV inputs instead. Consequence: "off" on this module shows the **left leg only** (the right block is hidden and unpatchable), where "off" on an FX shows one jack owning *both* legs. See [`fx-modules.md § Split-block modules share the toggle, not the channel layout`](fx-modules.md#split-block-modules-share-the-toggle-not-the-channel-layout).
- **Collapsing hands the right leg's cables back, it does not silence them**: a cable on the hidden `kRightBase` block is re-pointed onto the matching channel of the left block wherever the far end still exposes it (`GraphEditor::dropHiddenRightLegConnections`), and splitting again takes it back off the left leg. Without that, collapsing the default patch's voice chain left every right channel downstream rendering silence — the mix jumped left.
- **Splitting a module that was wired while collapsed brings both legs up wired** (`GraphEditor::completeStereoPairConnections`): a real right leg on the peer wins, otherwise the input leg copies the feed the left leg has and the output leg is summed into the peer's mono jack. A jack already carrying a summed pair from one upstream **migrates** the second cable onto `Audio R` rather than adding a third. Collapsing takes the stand-ins away again, so on/off round-trips exactly. Full rule, and why the peer's hidden `kRightBase` block stays off limits either way: [`fx-modules.md § Splitting a split-block module that was wired mono`](fx-modules.md#splitting-a-split-block-module-that-was-wired-mono).
- **A silent `Audio R` costs nothing.** The right leg is skipped per block when its input is silent, so a mono insert performs as it did before #219 and emits silence on `Audio R`. Patch both jacks to get a stereo path.
- **CV inputs (mono mode)**: Cutoff = ch1, Resonance = ch2, Drive = ch3 — unchanged by #219. Visible jack *order* changed (Audio L, Audio R, then the CV jacks), but connections persist by raw channel index, so no saved patch is affected.
- **CV inputs (poly mode)**: Cutoff = ch8, Resonance = ch9, Drive = ch10.
- **Poly mode**: 8 per-voice audio inputs (ch0-7) + 3 shared CV inputs (ch8-10) + 8 per-voice `Audio R` inputs (ch11-18). Shared CV is computed once per block and applied to all active voices on both legs.
- **The end-of-block CV clear is bounded at `kRightBase`** — it used to run to `getNumChannels()`, which would now erase the filtered right leg. Pinned by `FilterStereo.CVClearDoesNotEraseAudioR`.
- **Declared 19 in / 19 out** (was 11 in / 8 out). Raising the output count above every CV input index also means JUCE hands this node private copies of shared CV buffers, so the CV clear can only ever zero its own copy.
- **Smoothing**: Cutoff, Resonance and Drive are each smoothed over 5 ms — per sample in mono mode, a block at a time in poly mode (one ladder per voice, coefficients set once per block). Resonance and Drive scale the ladder's feedback and saturation, so an automated step lands straight on the output level.
- **Atomic modulated params**: `modulatedCutoff`, `modulatedResonance`, and `modulatedDrive` are `std::atomic<float>` members updated every `processBlock` (before voice processing in poly mode; per-sample in mono mode). `FrequencyResponseComponent` reads `getCurrentCutoff()` / `getModulatedResonance()` on the UI thread without locks.
- **`isAutoPromotableModTarget`**: Returns `false` in poly mode (poly CV connections stay plain `DirectCV`, not auto-wrapped in attenuverters).

## ADSR (Envelope) Module
- **Engine**: `synth::EnvelopeGenerator` (`Source/Modules/Envelope/EnvelopeGenerator.h`), a
  progress-based AHDSR generator with no rate/coefficient state cached from one sample to the
  next — every stage owns a `p` in `[0, 1]` advanced by `1 / (time * sampleRate)`, and level is
  always `stageStart + (stageTarget - stageStart) * shape(p, curve)`. This is what removed the
  cliffs a rate-based envelope (the previous `juce::ADSR`-backed implementation) is prone to: a
  stage's duration and endpoints are exact regardless of curve, a 0 ms Hold costs no samples of
  its own (it cascades straight into the next stage rather than sitting for one flat sample),
  and changing a stage's time mid-ramp only changes slope, never the level. Attack/Decay/Release
  floor their *effective* time to a fixed, sub-millisecond/millisecond minimum instead of
  cascading for free (see below) -- a one-sample full-scale level step is an audible click, and
  Hold is exempt only because it is pinned flat (start == target) and so has no level to step
  across.
- **Stages**: Attack, Hold, Decay, Sustain, Release. Hold is new (FRO110) — a flat segment
  pinned at 1.0 between Attack finishing and Decay starting, default 0 s (no hold).
- **Curves**: `attackCurve` (default -0.3), `decayCurve` (default 0.65), `releaseCurve` (default
  0.65), each -1..+1. `0` is linear; `> 0` is fast-first/slow-tail (the natural decay/release
  shape); `< 0` is slow-first/fast-finish. Exact endpoints hold for every curve amount:
  `shape(0, c) == 0`, `shape(1, c) == 1`. Not exposed to the AI few-shot examples.
- **Times retired their minimum clamps (FRO110)**: `attack`/`hold`/`decay`/`release` move to a
  **linear** `NormalisableRange(0.0, 5.0)` — the parameter's own minimum is genuinely 0 s (an
  explicit 0 is honoured and displayed, not silently raised the way the old 2 ms / 5 ms clamps
  did), and the maximum stays 5.0 (widening it would quadruple the existing misfire of
  `AIStateMapper`'s in-`[0,1]` rescale heuristic for untrusted patches — see below). New
  defaults: attack 0.001 s, hold 0 s, decay 1.0 s, **sustain 1.0**, release 0.015 s — a held note
  sustains by default now, matching how most other synths default.
  **Click floor (FRO116)**: a 0 s Attack/Decay/Release parameter is a real, displayed value, but
  `synth::EnvelopeGenerator` internally floors that stage's *effective* time to a fixed
  click-free minimum — 0.1 ms for Attack, 1 ms for Decay and Release — before turning it into a
  per-sample ramp rate. A one-sample full-scale level step is audible regardless of whether the
  user dialled in 0 explicitly or an automation lane swept down to it, the same way an analog
  envelope circuit has its own sub-millisecond physical floor. Hold is the one stage still
  genuinely instant at 0 s: it is pinned flat (its start and target are both 1.0), so there is no
  level to step across. The floor is internal to the generator only — the parameter range, its
  displayed "0 ms", and the AI-authorable schema are all unchanged by it.
  The knob feel at the new 1 ms attack default lives on the **slider**, not the parameter: the
  0.3 skew that used to sit on these four `NormalisableRange`s now lives purely in the UI layer
  (`ModuleComponent.cpp`'s ADSR special case, `applyAdsrTimeSliderSkew`, runs on the four time
  sliders, identified by `paramID`). It has to run **after** the slider's
  `SliderParameterAttachment` is built, not before: that constructor always installs its own
  `NormalisableRange<double>` built from lambda `convertFrom0to1`/`convertTo0to1` functions, and
  JUCE's `NormalisableRange::convertFrom0to1` returns through that lambda immediately whenever one
  is installed — its own `skew` field is never consulted once a lambda is present, so a plain
  `Slider::setSkewFactor()` call, whether before or after the attachment, is a silent no-op.
  Installing a plain (function-free), already-skewed `NormalisableRange<double>` on the slider
  after the attachment restores a real pixel-to-value skew curve while leaving
  `slider.getValue()`/`setValue()` — what the attachment reads and writes — exchanging real units
  exactly as before, with no effect on the parameter's stored or automated value. Keeping the
  parameter range linear means `AIStateMapper`'s untrusted-apply heuristic (`AIStateMapper.cpp`'s
  in-`[0,1]` rescale — the AI few-shot examples emit envelope times as real seconds like
  `attack: 0.01`, `decay: 0.15`–`0.3`, all of which land inside `[0,1]`) behaves exactly as it did
  before FRO110's range change: a linear `convertFrom0to1` against `0..5`, not the badly-distorted
  curve a range-level 0.3 skew would produce (a requested 0.3 s decay would land around 0.02 s
  instead of roughly 1.5 s). The one place a **host's own** VST3/AU automation lane still shifts is
  the negligible range-start change from the old clamped minimum (0.01) to 0.0 — a linear offset,
  not a curve change. Acceptable pre-V1.
- **Retrigger is two different rules, on purpose**: a MIDI note-on always calls `noteOn()` at
  the event itself — driven by the event, not by an edge in a held/not-held flag — so a
  gapless back-to-back note sequence (a note-off and the next note-on landing on the very same
  sample) still re-articulates. `noteOn()` enters Attack from the envelope's *current* level,
  over the *full* attack time, from any stage including mid-release — continuous, no click, no
  jump; a retrigger from above where Attack would otherwise be at that point in time can only
  climb toward Attack's target (never dip below where it started). A Gate CV that stays high is
  legato by modular convention and does **not** retrigger on its own; only a rising edge (via
  the shared `SchmittTrigger`) or a MIDI note-on starts a new envelope. Mono note-offs are
  tracked per note number in a `std::bitset<128>` (channel-agnostic); release only fires once
  every held note is gone **and** the Gate CV is low — the OR between the two is unchanged.
  CC123 (all notes off) and CC120 (all sound off) clear every held note.
- **Mono output**: Generates a single control signal (0.0 to 1.0) on channel 0. The envelope is
  held while **either** MIDI is down **or** the Gate CV is high, and releases only when both are
  low. Unpatched Gate stays silent, so existing MIDI-only patches are unchanged.
- **Gate CV**: a Schmitt trigger on ch0 (same `SchmittTrigger` helper as Sample & Hold /
  Comparator). Arms above `Threshold` and only re-arms once the signal falls a fixed 0.05 below
  it. **Sampled per sample in both mono and poly mode** — a mid-block retrigger is seen on the
  exact sample it happens, wherever it falls in the block.
- **Threshold**: `gateThreshold` (0.0–1.0, default 0.5) plus Threshold CV on ch8. The id is `gateThreshold` rather than `threshold` / `trigThreshold` because Compressor / Limiter / Gate own `threshold` as dB and Sample & Hold / Comparator own `trigThreshold` as bipolar CV.
- **Poly mode**: 8 gate CV inputs (ch0-7) drive 8 independent envelope generators; outputs 8
  per-voice envelopes (ch0-7). Threshold CV (ch8) is shared across voices, and one
  `EnvelopeParameters` is built once per sample (not once per voice) since every voice shares
  the same attack/hold/decay/sustain/release/curve values.
- **Stage-time/level CV (FRO285)**: Attack ch9, Hold ch10, Decay ch11, Sustain ch12, Release ch13
  — appended after Threshold, shared across every voice exactly like Threshold, and read the
  normalised-CV convention every jack added since (`ModuleBase::blockCV` +
  `ModuleBase::modulateNormalised`, once per block; see
  [`modulation.md#cv-in-normalised-units`](modulation.md#cv-in-normalised-units)). Because
  attack/hold/decay/release use a **linear** `[0, 5]` range, their CV moves them in knob units
  (e.g. +1.0 sweeps all the way to 5 s), not a fixed ms/s amount. Sustain CV always applies — it's
  a level, not a stage time. **Attack/Hold/Decay/Release CV is ignored while `tempoSync` is on**:
  a synced stage's effective time already comes from its `*Div` param and the live tempo, and a
  CV value expressed in the linear `[0, 5]` range has no meaning overlaid on a beat-locked
  division, so the jack stays visibly patchable but is a no-op until the module goes back to MS
  mode. Declaring the output count to match (17, matching the highest CV channel read) keeps
  these silent per [`poly-channel-layout.md`](poly-channel-layout.md#rule-for-new-poly-modules).
- **Curve CV (FRO314)**: `attackCurve` ch14, `decayCurve` ch15, `releaseCurve` ch16 — appended
  after the stage-time/level block, same shared-across-every-voice / `blockCV`+
  `modulateNormalised` convention as the stage times above. These three curve amounts have **no
  generic rotary knob** — `ModuleComponent.cpp`'s `shouldSkipGenericFloatSlider` skips them on
  purpose because they are edited only via the envelope graph's bend handles (FRO112) — so a
  dropped cable currently has no visible knob to ring for them (patching the jack itself still
  works through the mod matrix / AI-authored connections). Giving them a bend-handle drop anchor,
  matching how `getModTargetPortForPoint` already special-cases the Threshold control, is tracked
  as a UI follow-up.
- **Smoothing**: Sustain is smoothed over 20 ms, read fresh **per sample** (not a block at a
  time) and fed to `EnvelopeGenerator` as both Decay's live target and the flat Sustain output,
  so an in-flight decay or a held note retargets smoothly instead of stepping. It is the one
  parameter that is a *level*, not a stage time; attack/hold/decay/release are deliberately left
  unsmoothed — `EnvelopeGenerator` turns a time change mid-ramp into a slope change on its own.
- **Tempo sync (FRO113, BPM | MS toggle)**: `tempoSync` (bool, default false/MS mode) is REAL
  sync, not a display-only snap — each timed stage keeps its own note-division choice parameter
  (`attackDiv`/`holdDiv`/`decayDiv`/`releaseDiv`, sharing one six-entry division list, "1/1"
  down to "1/32", the same entries and order as [LFO](#lfo-module)'s `rateSync`) and the
  effective stage time is recomputed from that division and the current tempo **every block**,
  so it follows a live tempo change rather than snapping once. The ms parameters
  (`attack`/`hold`/`decay`/`release`) are untouched and stay in sole control whenever `tempoSync`
  is off; both sets of parameters always exist and round-trip in every patch, so toggling the
  mode never loses the other mode's values. Tempo comes from `getPlayHead()->getPosition()`
  (falling back to 120 BPM with no playhead, mirroring LFOModule's own sync mode) — read at the
  same per-block cadence as LFO's rate, not the Sequencer's per-sample beat-locked stepping,
  since a stage's duration only needs "how long in seconds right now", not a beat-grid position.
  Switching `tempoSync` mid-note is click-safe for the same reason a `attack`/`decay`/`release`
  automation move already was: `EnvelopeGenerator` turns any stage-time change mid-ramp into a
  slope change, never a level jump, regardless of which parameter set produced the new time.
  Division-derived defaults are the closest available division to each ms default at 120 BPM —
  `decayDiv`'s "1/2" default lands on exactly 1.0 s (the ms `decay` default) at 120 BPM;
  `attackDiv`/`holdDiv`/`releaseDiv` default to the fastest division ("1/32", 62.5 ms at 120 BPM)
  since nothing in the shared six-entry list gets closer to their sub-20-ms ms defaults — an
  accepted resolution tradeoff of reusing LFO's division list rather than a reason to invent a
  finer one. See `Source/Modules/Envelope/EnvelopeTempoSync.h` for the division table/conversion.
- **UI playhead**: `getPlayheadStage()` / `getPlayheadProgress()` / `getPlayheadLevel()` expose
  the stage/progress/level of the most recently (re)triggered voice, written lock-free
  (`std::atomic`, relaxed) once per block. Consumed by the envelope graph's playhead marker (see
  the card section below) — `getPlayheadLevel()` remains unused, held for a future level readout.
- **Uses**: Modulation of VCA gain, Filter cutoff, or Oscillator Level.
- **Threshold control**: `ThresholdControlComponent` in slider+meter mode — a live unipolar bar of the Gate jack with the Threshold slider attached, so the slice can be set by eye.
- **Card UI (FRO112)**: `ModuleComponentEnvelopeCard.cpp`. Five rotary knobs
  (attack/hold/decay/sustain/release, styled and readout-boxed identically to the generic auto-UI
  — see below) flow through the same 3-per-row knob grid as every other module
  (`ModuleComponentLayout.cpp`'s `layoutDefaultContent`), wrapping 3+2 at the shared 280px width;
  `attackCurve`/`decayCurve`/`releaseCurve` are no longer sliders at all — they're edited only via
  the breakpoint curve editor's bend handles (`Source/UI/ModuleViews/CurveEditor/`, FRO111), on a
  fixed 5-node topology (origin, attack peak, hold end, sustain, release end) whose node x is
  cumulative time and whose ripple contract (`CurveModel::setNodeX`) preserves every other
  segment's own duration across a single-node drag.
  - **Readout formatting**: `AudioParameterFloatAttributes` on all eight float params
    (`ADSRModule.h`'s `adsrTimeAttributes()`/`adsrSustainAttributes()`/`adsrCurveAttributes()`) —
    the four stage times show as `"<n> ms"` below 1 s and `"<n> s"` at or above it; sustain shows
    as dB (`0.0 dB` at unity, `-inf dB` at exactly zero, never `log10(0)`); the three curve
    amounts show as a plain 2-decimal value. Display-only: every `NormalisableRange` (including
    the 0.3 UI-side skew on the four time sliders, unchanged from FRO110 — see above) stays
    exactly what it was, for patch compatibility and the `AIStateMapper` rescale heuristic. Since
    `SliderParameterAttachment` installs `param.getText()`/`getValueForText()` as the slider's own
    `textFromValueFunction`/`valueFromTextFunction`, this is also what a host's generic automation
    UI shows for these params now.
  - **Two-way sync**: a node/bend drag on the graph writes the matching parameter(s) via
    `setValueNotifyingHost` (epsilon-gated, one write per value that actually moved), bracketed
    into ONE undo entry per whole drag gesture (`captureBeforeState`/`pushSnapshotFromCapture`
    around the curve's own `onGestureStart`/`onGestureEnd`, mirroring the Parametric EQ card's
    `wireEqGestureCallbacks`) — never `beginChangeGesture`/`endChangeGesture`, which would let
    `parameterGestureChanged` push a second undo entry per parameter. The reverse direction (a
    knob drag, host automation, undo/redo, or a preset load) rebuilds the curve model from the
    module's current parameter values on every relevant `parameterValueChanged` callback, except
    while a graph gesture is itself in flight (the graph is its own source of truth for that
    span) — it snaps once more at the gesture's end to settle on the quantised/clamped values.
  - **Graph section**: collapsed by default behind a disclosure toggle, sizing the card via the
    same `setVisible()` -> `updateLayout()` idiom the scope/frequency-response toggles use. The
    expanded/collapsed state is **not persisted** — it resets to collapsed on every construction,
    matching the scope and frequency-response toggles rather than Macro Group's persisted
    `collapsed` flag; persisting would need `ModuleBase::setExtraState`, which the root
    `CLAUDE.md` flags as a trusted-path-only, security-sensitive surface not worth spending on a
    view toggle.
  - **Playhead marker**: maps `EnvelopeStage` to the curve's `(segment, progress)` — Attack/Hold/
    Decay/Release map to their own segment 0-3; Sustain parks at `(segment 2, progress 1.0)`,
    which is exactly the sustain node's own position, needing no special geometry. Polled from
    `ModuleComponent`'s existing gated 15 Hz `timerCallback` (only while the graph is visible) —
    no new `juce::Timer` — since `CurveEditorComponent::setPlayhead` already no-ops on an
    unchanged value, an idle or collapsed card costs nothing beyond that one guard check. See
    [`layout/visualizers.md`](../layout/visualizers.md).
  - **BPM | MS toggle**: a segmented control beside the graph's disclosure toggle, wired to
    FRO113's `tempoSync` bool param (FRO117) — clicking either button writes `tempoSync` via
    `setValueNotifyingHost`, and an external write (automation/undo/preset load) syncs the pair
    back via `parameterValueChanged`, the same reverse-sync shape as the envelope graph itself.
    FRO113 also added one `AudioParameterChoice` note-division param per stage time
    (`attackDiv`/`holdDiv`/`decayDiv`/`releaseDiv`, sharing LFO's rateSync division list) and the
    tempo-following DSP behind them; both `tempoSync` and the four division params are excluded
    from the generic per-param UI (`shouldSkipGenericBoolToggle`/`shouldSkipGenericChoiceCombo` in
    `ModuleComponent.cpp`) so they don't leak extra rows onto the card, but the four division
    params have no user-facing control of their own yet — tracked separately (FRO118) as a real
    BPM-mode UI design (e.g. swapping each knob for a division picker), not a quick follow-up.
- **Default instrument-track chain (P9-3i, FRO43)**: "+ Track -> Instrument -> {Oscillator/Wavetable}" auto-wires one ADSR (MIDI-gated, forced non-poly, `sustain` overridden to 0.7) driving a VCA ahead of the rest of the chain — see [`docs/mixer/mixer.md#envelope-and-vca-for-a-raw-instrument`](../mixer/mixer.md#envelope-and-vca-for-a-raw-instrument) for the full wiring and why it's forced non-poly. When the instrument is poly (P9-3j, FRO46), the ADSR is genuinely poly instead — gated by a Poly MIDI node's per-voice Gate CV rather than raw MIDI — see [`docs/mixer/mixer.md#a-poly-instrument-gets-a-per-voice-envelope`](../mixer/mixer.md#a-poly-instrument-gets-a-per-voice-envelope).

## Envelope Follower Module
- **Role**: A *detector*, not a generator — tracks the amplitude contour of an audio input and emits it as unipolar `[0, 1]` modulation CV. Deliberately separate from ADSR: its input is audio (not a gate), its times are milliseconds (not seconds), and it has no decay/sustain stages.
- **Detection**: `Peak` (rectify) or `RMS` (mean-square, square-rooted on output), selected by the `detection` choice parameter.
- **Parameters**: Attack (0.1–200 ms), Release (1–2000 ms), Sensitivity (0–1), Detection (choice), Mute.
- **Sensitivity**: A unit-interval control mapped to 0.25×–4.0× detector gain (`sensitivityToGain`, exponential, 0.5 = unity). It is *not* a raw gain or a dB value on purpose: `AIStateMapper::applyParamsToProcessor` rescales an in-`[0,1]` value when the parameter's range is wider than `[0,1]`, so a non-unit range here would silently misread AI-authored patches.
- **Channels**: inputs ch0 = Audio, ch1 = Attack CV, ch2 = Release CV, ch3 = Sensitivity CV. Outputs ch0 = Env CV; ch1-3 are silent (declared only so JUCE cannot alias CV input ch3 onto another node's output buffer).
- **CV modulation of times**: Attack/Release coefficients are recomputed once per block from the CV value at the block start — one `exp()` per block instead of per sample. Time constants only change how fast the follower tracks, so block-rate updates cannot zipper the output level. Sensitivity *is* applied per sample (smoothed).
- **Bypass**: Clears its output rather than passing the dry signal through — see the exception note in [architecture/module-base.md](../architecture/module-base.md#bypassmute-contract). The module has an audio input but no audio output, so a dry pass-through would push audio-rate samples into a CV destination.
- **Uses**: Sidechain-style ducking (follow a drum bus → VCA gain), dynamic auto-wah (follow a signal → Filter cutoff).

## VCA (Amplifier) Module
- **Inputs**: 
    - Mono mode: `Audio L` (ch0), CV (ch1), `Audio R` (ch16).
    - Poly mode: Per-voice `Audio L` ch0-7, per-voice envelope/CV ch8-15, per-voice `Audio R` ch16-23.
- **Stereo (issue #219)**: `Audio L` / `Audio R` jacks on both sides, with the right leg on a dedicated block at `kRightBase` (ch16). It is not on ch1 — that is the gain CV input. Both legs are gated by the **same** gain ramp and the same CV, so the stereo image cannot drift while Gain moves. A silent `Audio R` is skipped per block. This is what carries the default preset's stereo path from the Filter into the FX chain.
- **Poly summing**: In poly mode, multiplies each voice's audio by its corresponding envelope CV, then sums all 8 voices with `tanh` soft saturation and 1/8 normalization — the left block to ch0, the right block to ch16. Follower voices in both blocks are zeroed so they don't leak downstream.
- **Legacy ch0→ch1 duplicate, deliberately preserved**: mono still copies the gated ch0 onto ch1 after processing, and the poly left sum is still written to both ch0 and ch1. That was the module's old "mono to stereo" affordance and `VCAModuleTest.MonoToStereoCopy` / `PolyMode_MultiVoice` / `MonoMode_BackwardsCompatible` pin it. It is vestigial now that there is a real right leg — prefer the `Audio R` jack.
- **Features**: Parameter smoothing for click-free gain changes.
- **Default instrument-track chain (P9-3i, FRO43)**: auto-wired ahead of the rest of an Oscillator/Wavetable instrument track's chain (`gain` overridden to 1.0 so the ADSR envelope alone governs level) — see [`docs/mixer/mixer.md#envelope-and-vca-for-a-raw-instrument`](../mixer/mixer.md#envelope-and-vca-for-a-raw-instrument). When the instrument is poly (P9-3j, FRO46), the VCA is genuinely poly instead, doing its own 8-voice sum in place of a separate Voice Mixer stage — see [`docs/mixer/mixer.md#a-poly-instrument-gets-a-per-voice-envelope`](../mixer/mixer.md#a-poly-instrument-gets-a-per-voice-envelope).

## Poly MIDI Module
- **Capacity**: 8 simultaneous voices.
- **Allocation**: a note-on re-uses the voice already holding that note, else the first free voice, else steals one per the **Voice Steal** parameter.
- **Parameters**: `voiceSteal` — `Oldest` (default) | `Round-Robin` | `Random`; `velToGate` ("Vel → Gate", default **off**).

| Mode | Which voice loses its note |
|---|---|
| `Oldest` | Least recently used — the voice whose last note-on is furthest in the past. |
| `Round-Robin` | The next voice in a 0→7 cycle, regardless of age. The cursor advances only on a steal. |
| `Random` | A voice picked by a module-owned PRNG. |

- **Voice ages are sample counts, never wall-clock time** (issue #198). `processBlock` advances a monotonic `sampleCounter_` (before the bypass check, so ages stay ordered across a bypass toggle) and each note-on is stamped with `blockStartSample + its MIDI sample offset`. Two consequences the module depends on: a chord delivered inside one block steals in arrival order rather than collapsing onto voice 0, and offline renders are reproducible. Stamps are additionally clamped to `lastStamp_ + 1` so notes sharing one sample offset still order strictly. `Random` seeds its `juce::Random` from a fixed constant in `prepareToPlay` and advances it only on a steal, so it too renders identically every run — **never** reintroduce `juce::Time`, `std::random_device`, or an unseeded PRNG on this path.
- **Outputs**: 16 total channels — ch0-7 = per-voice pitch (Hz), ch8-15 = per-voice gate (0..1).
- **Visible ports**: 1 output jack ("Poly Out") representing the entire poly bus.
- **Voice mask atomic**: `voiceMaskAtomic_` (`std::atomic<uint8_t>`) is written at the end of every `processBlock` with `std::memory_order_relaxed` — one bit per voice (bit 0 = voice 0, … bit 7 = voice 7), set when `voices[i].active` is true. `AudioEngine::getDisplayVoiceCount()` reads it lock-free and counts set bits via `std::popcount` (C++20 `<bit>`).
- **Default instrument-track chain (P9-3j, FRO46)**: "+ Track -> Instrument -> {Oscillator/Wavetable}" auto-wires a Poly MIDI node between Track In and the instrument when the instrument is poly, so the auto-wired ADSR+VCA (see the ADSR/VCA entries above) can be genuinely poly too instead of forced non-poly — see [`docs/mixer/mixer.md#a-poly-instrument-gets-a-per-voice-envelope`](../mixer/mixer.md#a-poly-instrument-gets-a-per-voice-envelope) for the full wiring.

### Poly note contract (machine MIDI)

Hand-played MIDI rarely repeats a pitch inside an envelope's attack; **machine-generated MIDI does it constantly** — this is the contract the timeline's Track In node schedules its clips against, so it is stated in samples, not in "should sound fine".

- **Every re-articulation produces a gate edge.** All three paths that start a note on a voice — same-pitch retrigger, voice steal, and reuse of a voice released moments earlier — go through one function (`startNote`). If that voice's gate CV is not already at rest (`smoothedGate.getCurrentValue() > 1e-4`, deliberately *not* the `active` flag — a just-released voice is inactive but still emitting), the gate drops to **0 instantly** at the event sample via `setCurrentAndTargetValue(0.0f)`. An instant CV step is a real edge; the usual 5 ms smoothing still shapes the rise back up.
- **Minimum 1 ms low-gate gap.** The voice records `gateReopenAtSample` (absolute, off the same monotonic `sampleCounter_` the age stamps use) = event sample + `jmax(16, sampleRate * 0.001)`. `renderChunk` checks it per sample, so a gap that ends mid-chunk still reopens exactly on time. Without the gap a drop followed immediately by a rise can smooth into a dip too shallow and short for anything downstream to register — that is precisely the clip-boundary case, where a note-off and the next note-on for one pitch land on the **same** sample.
- **A fresh note on an idle voice has no gap** — its gate is already 0, so the rise starts on the event sample itself.
- **Note-off is unchanged**: gate target 0 with the 5 ms smoothing, pitch held.
- **`Vel → Gate` (`velToGate`, default off).** Off: the gate rises to exactly 1.0, as it always has — a preset saved before this parameter existed loads with the default and renders byte-identically. On: the gate rises to the note-on velocity (0..1), stored per voice, so held voices keep their own levels.
- **Chord larger than the voice count.** Nine same-sample note-ons on eight voices in `Oldest` mode drop the chord's **first** note: stamps are strictly increasing (`stampFor` clamps to `lastStamp_ + 1`), so note 1 is the oldest by one count and note 9 steals it. Notes 2–9 sound, and the stolen voice re-attacks by the rule above rather than gliding.
- **Downstream reality check (`ADSRModule`)**: the ADSR's poly branch samples its gate input **per sample** (FRO110 fixed the stale once-per-block sampling), so the 1 ms gap re-articulates it on the exact sample it happens, wherever it falls in the block — a mid-block retrigger is seen identically to one that spans a block boundary. Pinned end-to-end by `PolyMidiToAdsrTest.RetriggerReArticulatesAdsrRegardlessOfBlockAlignment`.

## Voice Mixer Module
- **Purpose**: Explicit 8-to-stereo voice summing with level control and soft saturation. An alternative to VCA's internal poly summing for patches that need a separate mix stage.
- **Inputs**: ch0-7 (up to 8 voice signals).
- **Outputs**: ch0 (Left), ch1 (Right) — stereo copy of the mono sum.
- **Dual I/O**: output-only (the inputs are eight voice jacks, not a stereo pair), off by default — one `"Audio"` jack owning ch0/ch1. Same shape as the Ring Modulator's; see [`fx-modules.md § Stereo I/O`](fx-modules.md#stereo-io-dual-io-toggle).
- **Processing**: Sums all 8 input channels, scales by Level parameter (0–1, default 0.125), then applies `std::tanh` soft saturation for gentle clip protection. Level smoothed over 10 ms to prevent clicks.
- **Parameter**: `Level` (0.0–1.0, default 0.125).

## Math Module
- **Source file**: `Source/Modules/MathModule.h`
- **Purpose**: Dual-input CV/audio math and logic utility, inspired by Make Noise Maths and Mutable Instruments Kinks. All five outputs are computed simultaneously every block — there is no mode selector.
- **Inputs**: ch0 = **A** (signal or CV), ch1 = **B** (signal or CV).
- **Outputs**: 5 channels, all computed simultaneously from A and B:

| Channel | Output | Formula |
|---|---|---|
| ch0 | Sum | A + B |
| ch1 | Diff | A - B |
| ch2 | Min | min(A, B) |
| ch3 | Max | max(A, B) |
| ch4 | Mult | A * B |

- **Processing**: Computes Sum/Diff/Min/Max/Mult per-sample from the raw A/B inputs, then applies the `Clip` stage to all five outputs before writing them out.
- **Parameters**:
    - `Clip` (choice: `Off` / `Hard` / `Soft`, default **Off**) — applied uniformly to all five outputs.
        - `Off` — transparent; sums may exceed the nominal [-1, 1] CV range (e.g. two unipolar envelopes sum to 0..2). Intentional default: a math utility shouldn't silently destroy magnitude, and the graph's attenuverters give the user downstream scaling.
        - `Hard` — `jlimit(-1, 1)`, the Eurorack "clips at the rails" behaviour.
        - `Soft` — `tanh()` saturation. Also attenuates in-range signals (tanh(1) ~= 0.762) — inherent to saturation, not a bug.
- **Behaviour notes**:
    - With nothing patched into **B**, B reads as 0: `Min`/`Max` become negative/positive half-wave rectifiers of A, `Sum` and `Diff` both pass A unchanged (A ± 0), and `Mult` is silent. This is the classic Kinks rectifier trick.
    - `Min`/`Max` double as analog logic on gate/CV signals — min = AND, max = OR — hence "Math / Logic".
    - `Mult` is clean four-quadrant multiplication (ring modulation) at audio rate. It is **not oversampled** and **not a diode-ring emulation** — audio-rate multiplication of bright sources will alias.
    - Integrated visual buffer displays the **Sum** output.
    - **A**/**B** are signal inputs, not parameter-CV destinations — connections into them are never auto-wrapped in a hidden attenuverter (see [Attenuverter Module](#attenuverter-module-hidden)).
    - **Bypass**: dry pass-through of A on ch0; ch1-4 (Diff/Min/Max/Mult) cleared. **Mute**: `buffer.clear()` silences all five outputs.
- **Patch ideas**: Sum two LFOs for a richer composite modulation shape; use Min/Max as analog AND/OR gate logic; leave B unpatched and take Max (or Min) as a half-wave rectifier of A; patch two audio-rate oscillators into A/B and take Mult as clean ring modulation.

## MIDI Keyboard Module
- **Purpose**: Provides an interactive on-screen keyboard for MIDI input.
- **Features**:
    - **Octave Shift**: Shift the keyboard range by ±2 octaves.
    - **Visual Feedback**: Real-time display of pressed keys.
    - **MIDI Output**: Generates standard MIDI messages for driving oscillators or other MIDI-capable modules.

## LFO Module
- **Source file**: `Source/Modules/LFOModule.h`
- **Waveforms**: 5 shapes — Sine, Triangle, Sawtooth, Square, S&H (Sample-and-Hold).
- **Rate modes**:
    - **Hz mode**: Free-running, 0.01–20.0 Hz (default 1.0 Hz, skewed range). Rate CV (ch0) applies here.
    - **Sync mode**: Tempo-locked to host BPM (falls back to 120 BPM if no PlayHead). Subdivisions: 1/1, 1/2, 1/4 (default), 1/8, 1/16, 1/32. The rate is the tempo division, not the `rateHz` knob, so Rate CV is deliberately ignored in this mode.
- **Parameters**: Shape (choice), Sync (bool mode toggle), Rate Hz (float), Sync Rate (choice), Bipolar (bool, default true), Retrig (bool), Level (0.0–1.0, default 1.0), Glide (0.0–1.0, S&H only).
- **Bipolar/Unipolar**: When `Bipolar` is true, output range is −1 to +1. When false (unipolar), mapped to 0 to +1.
- **S&H Glide**: On each phase wrap a new random value is drawn; `Glide > 0` ramps to it over up to 0.5 s using `juce::LinearSmoothedValue`.
- **Retrig**: A MIDI Note On resets the phase to 0.0 when `Retrig` is enabled — unaffected by the CV jacks below.
- **CV inputs** (FRO284): Rate ch0, Level ch1, Glide ch2 — all `PortRole::ModCV`, mono. Each follows the normalised-CV convention (`ModuleBase::blockCV` + `modulateNormalised`, read once per block; see [`modulation.md`](modulation.md#cv-in-normalised-units)): Rate moves `rateHz` in its own skewed range (Hz mode only, see above); Level moves the `level` knob before the 10 ms smoother; Glide moves the `glide` knob at the same S&H step-edge read the knob itself uses. ch0 doubles as the CV output on the way out (read before overwrite, the same shared-channel convention as Oscillator/Wavetable ch0 and Sample & Hold ch0); ch1/ch2 are cleared each block so the raw CV doesn't leak downstream as output. `getModulationTargets()` sets `paramId` on all three (`rateHz`/`level`/`glide`).
- **Output**: Single CV channel (ch0), the module's only visible output jack. Declares 3 raw output channels (docs/modules/poly-channel-layout.md — ch1/ch2 exist only to keep JUCE from aliasing them onto ch0's buffer) but ch1/ch2 carry no signal. Pushes to `VisualBuffer` for scope display.
- **Smoothing**: Level is smoothed over 10 ms per sample — it scales the emitted CV, so a step there steps every destination downstream. Rate is a frequency (phase-continuous) and Glide is read only at an S&H step edge, so neither is smoothed.
- **Width**: SINGLE (280 px). See [docs/layout/module-card.md](../layout/module-card.md#width-buckets).

## Sequencer Module
- **Source file**: `Source/Modules/SequencerModule.h`
- **Purpose**: 8-step monophonic step sequencer. Generates MIDI messages (Note On/Off + CC) — it does **not** output a raw CV pitch channel.
- **Parameters**:
    - `Run` (bool) — starts/stops sequencer; sends Note Off for the active note on stop.
    - `BPM` (30–300, default 120) — internal tempo.
    - `Pitch 1–8` (`AudioParameterInt`, 0–127, MIDI note number) — per-step pitch. Displayed as note names (e.g. "F3"). Default sequence: F3 F4 Gb3 Db4 F3 A3 Gb3 C4.
    - `Gate 1–8` (0.1–1.0, default 0.5) — gate length as fraction of one beat.
    - `F.Env 1–8` (0.0–1.0, default 0.5) — per-step filter envelope amount, sent as MIDI CC 74.
- **Timing**: One step per beat at the configured BPM. `currentActiveStep` (`std::atomic<int>`) is written each block for UI step-highlight.
- **Sync to Transport** (`syncToTransport`, bool, default **off**): opt-in. Off (default): behaves exactly as above — `BPM` stays authoritative and every existing preset produces a byte-identical event schedule (`AIStateMapperTest.ParamIdsGolden` and `SequencerModuleTest.LegacyScheduleIsByteIdenticalWithSyncOff` pin this). On: the module locks to the graph transport instead — tempo comes from the transport (`BPM` is ignored), the step index is a pure function of the beat (beat *B* plays step `B % 8`), note-on/off land at sample-accurate crossing offsets within the block (not the legacy 0/1-sample hack), a loop wrap fires the wrapped range's beats (e.g. the loop-start step) at `loopWrapSample + <offset>` with no double-fire or skipped beat, and a stopped transport emits one note-off for any held note and goes silent without advancing. `Run` still gates everything in both modes. The transport is read via `dynamic_cast<synth::TransportService*>(getPlayHead())`; only this app's own `AudioEngine` installs a `TransportService` as the playhead, so a foreign host (or a null playhead) falls back to the legacy free-running clock for that block instead of going silent.
- **Width**: DOUBLE (560 px). See [docs/layout/module-card.md](../layout/module-card.md#width-buckets).

## Poly Sequencer Module
- **Source file**: `Source/Modules/PolySequencerModule.h`
- **Purpose**: 8-step polyphonic chord sequencer. Generates MIDI chord events — it does **not** output per-voice pitch CV channels.
- **Parameters**:
    - `Run` (bool) — starts/stops; sends Note Off for all active chord notes on stop.
    - `BPM` (30–300, default 120) — internal tempo.
    - `Step 1–8 Root` (`AudioParameterInt`, 0–127) — root note for the step. Defaults: C3 E3 G3 C4 C3 G3 E3 C4.
    - `Step 1–8 Chord` (choice) — chord voicing: Unison, Major (0/4/7), Minor (0/3/7), Maj7 (0/4/7/11), Min7 (0/3/7/10), 5ths (0/7), Octs (0/12), Random (±12 semitones).
    - `Gate 1–8` (0.1–1.0, default 0.5) — gate length as fraction of one beat.
- **Timing**: One step per beat. `currentActiveStep` (`std::atomic<int>`) written each block for UI step-highlight.
- **Sync to Transport** (`syncToTransport`, bool, default **off**): same contract as the Sequencer module above — off keeps `BPM` authoritative with a byte-identical legacy schedule; on locks the whole chord (fire and kill together) to the transport's BPM and beat-locked step index (`B % 8`), with sample-accurate crossing offsets, correct loop-wrap behaviour, and one note-off per held chord note on stop. Same `TransportService` downcast caveat: a foreign host's playhead falls back to the legacy clock for that block.
- **Width**: DOUBLE (560 px). See [docs/layout/module-card.md](../layout/module-card.md#width-buckets).

## Sample & Hold Module
- **Source file**: `Source/Modules/SampleHoldModule.h`
- **Purpose**: Latches the value of a source signal on every clock edge and holds it until the next one — the stepped CV behind generative sequences and "R2D2" style bleeps.
- **Parameters**:
    - `Source` (choice: Input / **Random**) — sample the Signal input, or an internal white-noise generator.
    - `Mode` (choice: **Sample** / Track, param id `holdMode`) — Sample latches one value per rising edge; Track follows the source while the gate is high and freezes when it falls. The id is `holdMode` rather than `mode` because `AIStateMapper::getPatchSchema` constrains choice parameters globally by id and `LFOModule` already owns a boolean `mode`.
    - `Clock` (choice: **Internal** / External) — free-running internal oscillator, or the Trigger input.
    - `Threshold` (-1.0–1.0, default 0.5, param id `trigThreshold`) — level the Trigger input must exceed to fire. The id is `trigThreshold` rather than `threshold` because Compressor, Limiter and Gate all own a `threshold` float meaning dB.
    - `Rate` (0.1–50 Hz, default 8, skewed) — internal clock speed. Ignored when `Clock` is External.
    - `Slew` (0.0–1.0, default 0.0) — one-pole lag toward each new value, up to 0.5 s. 0 snaps instantly.
    - `Level` (0.0–1.0, default 1.0) — output scaling.
    - `Offset` (-1.0–1.0, default 0.0) — output offset; use +0.5 with Level 0.5 for a unipolar 0–1 CV.
- **Output**: bipolar CV on ch0, clamped to [-1, 1].
- **Smoothing**: Level and Offset are smoothed over 10 ms per sample — they scale and shift the emitted CV. Rate (a clock frequency), Slew (a one-pole coefficient) and Threshold (a comparator level that only decides *whether* an edge fires) cannot put a step in the output and are deliberately read raw.
- **Why `Source`/`Clock` are explicit choices**: the module deliberately does *not* infer "is anything patched in?" from channel activity. A gate signal sits at 0 most of the time and a slow LFO crosses zero, so activity detection misfires. Defaults (Internal clock + Random source) make the module produce stepped random CV the moment it is dropped on the canvas, with nothing patched.
- **Trigger detection**: a Schmitt trigger (`Source/Modules/SchmittTrigger.h`, shared with ADSR and Comparator). It arms when the Trigger input rises above `Threshold` and only re-arms once the signal falls a fixed `kHysteresis` (0.05) *below* it. Gate state is carried across block boundaries — a gate that stays high spanning two blocks is one edge, not two. The hysteresis is deliberately not user-exposed (see the Module Development Guide on not crowding modules with knobs); without it, any signal loitering near the threshold — a slow sine, anything with dither on it — would retrigger every sample.
- **Trigger meter**: `ThresholdControlComponent` in meter-only mode draws the live Trigger level as a bipolar bar with a marker at the effective threshold, so the threshold can be set by eye against the real signal. The module publishes `getTriggerLevel()` / `getEffectiveThreshold()` / `isTriggerHigh()` / `getTriggerCount()` as atomics for it. The meter is tracked **whichever clock is selected**, so the threshold can be dialled in before switching to External.
- **CV inputs**: `Rate` maps raw CV exponentially over ±4 octaves (per the Module Development Guide convention); `Slew`, `Level` and `Offset` are additive over their native ranges. Only these four jacks are auto-promotable mod targets — Signal and Trigger connections stay direct rather than being wrapped in an attenuverter.
- **Width**: SINGLE (280 px).

## Comparator Module
- **Source file**: `Source/Modules/ComparatorModule.h`
- **Purpose**: A *switch*, not a detector and not an envelope generator — emits a 0/1 gate while Signal is above Threshold, and the inverted gate on a second output. Slice an LFO into a pulse, a kick into a gate, any CV into a window. Envelope Follower tracks amplitude continuously; ADSR shapes a gate into A/D/S/R; Comparator only answers "is it over the line?".
- **Parameters**: `Threshold` (-1.0–1.0, default 0.5, param id `trigThreshold`) — same id and meaning as Sample & Hold. Hysteresis is the shared `SchmittTrigger::kHysteresis` (0.05), not user-exposed.
- **Channels**: in ch0 = Signal, ch1 = Threshold CV. Out ch0 = Gate, ch1 = Inverse.
- **Bypass / mute**: both clear. There is no dry audio path — passing Signal through would push audio-rate samples into a Gate destination.
- **Threshold control**: `ThresholdControlComponent` in slider+meter mode on a bipolar scale.
- **Width**: SINGLE (280 px).

## Macro Control Module ("Macros")
- **Purpose**: A bank of assignable CV knobs. Patch one macro jack to several destinations and a single knob movement sweeps all of them at once — filter cutoff, distortion drive and oscillator wave together.
- **Inputs**: none.
- **Outputs**: ch0–15 carry macros M1–M16. Only the first `Knobs` channels are audible; the rest are cleared every block and their jacks are hidden.
- **Parameters**:
    - `Knobs` (`macroCount`, int 1–16, default 8) — how many macros the bank exposes. See *Resizing* below.
    - `Bipolar` (`macroBipolar`, bool, default off) — maps the knobs to −1…+1 instead of 0…1, so a centred knob means "no change".
    - `M1`…`M16` (`macro1`…`macro16`, float 0.0–1.0, default 0.0) — the knobs themselves.
- **Processing**: each active channel is filled with its knob value, smoothed over 20 ms so a macro sweep never clicks. Bypass and mute both silence every channel (pure source module — there is no dry signal to pass through).
- **Routing**: no N-to-M matrix of its own. Depth and polarity per destination come from the Attenuverter the graph inserts on every CV cable, so the same macro can push one target up while pulling another down.

### Why 16 channels but a variable knob count

JUCE fixes an `AudioProcessor`'s bus layout at construction, and rebuilding it would drop every graph connection the node already has. The bank therefore always declares 16 output channels and 16 knob parameters; `Knobs` only changes how many are *exposed* (`getVisibleOutputPortCount()`) and how many are driven. Hidden knobs keep their values, so shrinking and re-growing the bank is lossless.

### Resizing

Changing `Knobs` resizes the module in place, anchored at its top-left, at `LayoutUtil::kMacroRowH` (44 px) per macro:

- The bank never moves — it is the module under the user's cursor. Instead `GraphEditor::handleModuleResized` pushes any neighbour that the new footprint would cover straight down and re-settles it on the grid (`LayoutUtil::resolveOverlapsAfterResize`). Shrinking moves nothing back.
- **Shrinking disconnects the macros that disappear.** A jack that is no longer drawn cannot be unplugged, and the module already silences its channel, so any cable or mod routing on a hidden macro is removed rather than left as a dead entry in the mod matrix. The whole change — count, layout and disconnects — is one undo step.

---

## Track In Module (Timeline MIDI Source, Hidden)

`Source/Modules/TimelineMidiSourceModule.h`. One node per timeline MIDI track: it turns that track's notes into MIDI events, so everything downstream (Poly MIDI → oscillators → FX) is an ordinary patch that neither knows nor cares that a timeline exists.

- **Ports**: none. 0 audio in, 0 audio out, MIDI **out** only (`acceptsMidi() == false`, `producesMidi() == true`). It **replaces** the graph-supplied `midiMessages` buffer rather than appending to it — the `ExternalMidiModule` contract for a source.
- **Parameters**: none beyond the inherited `bypassed`. There is nothing to tune: what it plays is the timeline's business.
- **Pull, not push.** Nothing schedules PLAYBACK events into it. Every block it downcasts `getPlayHead()` to `synth::TransportService`, reads `getCurrentBlockInfo()` and `getCurrentTimelineSnapshot()` (see [docs/architecture/audio-engine.md](../architecture/audio-engine.md#audioengine)/[docs/architecture/timeline.md](../architecture/timeline.md#timelinesnapshot-the-audio-threads-view-of-the-timeline)), and emits the note edges falling inside this block's beat range. There is no per-module state to keep in sync with the document, so a locate, a tempo change or an edit to the notes takes effect on the very next block with no invalidation step.
- **Audition is the ONE thing pushed in.** `pushAuditionNote(pitch, velocity, noteOn, channel = 1)` sounds a preview note from this node — the piano roll's "clicking a note plays it" (see [timeline/piano-roll.md](../timeline/piano-roll.md#note-audition)). It emits from **this** node deliberately: a track's MIDI destinations are graph connections whose SOURCE is this node's MIDI output, so a preview reaches exactly the modules the clip's notes reach, by construction rather than by re-deriving the destination list. The transfer is a fixed-capacity `juce::AbstractFifo` of POD events (`kAuditionFifoCapacity = 64`) plus a slot array — `TransportService`'s command-FIFO idiom — written from the message thread and drained once per block: no lock, no allocation, no logging on the audio thread, and a full FIFO **drops** the event rather than blocking. Every drained event lands at **sample 0**, the right granularity for a gesture made between two callbacks.
    - **A held preview carries an infinite end beat** (`kAuditionEndBeat`), so `emitRange`'s end-beat release scan can never touch it — and that same sentinel is what tells an audition note apart from a timeline one. A second note-on for a pitch already being auditioned releases the old one first (off-then-on), so the held table cannot drift; a note-off for a pitch that is **not** being auditioned emits nothing, which is load-bearing — a stray note-off would cut a timeline note of the same pitch short.
    - **It survives a stop, a locate and a loop wrap.** Those hygiene flushes go through `flushTimelineNotes()` (non-audition notes only): they say something about where the transport is, and a preview is not on the transport's clock. Audition is not gated on mute/solo either — it is a MONITOR path, like clicking a key on a MIDI Keyboard module. Bypass is the one thing that DOES end it (`flushActiveNotes()` releases everything), because a bypassed source emits nothing and a queued note-off would never be delivered; anything pushed while bypassed is discarded rather than replayed on resume.
- **Binding is by node uuid.** It finds "its" track by scanning the snapshot for the first **MIDI** track whose `bindingUuid` `strcmp`s equal to `ModuleBase::getNodeUuid()` (the audio-safe mirror of the graph node's `"uuid"` property — see [architecture/module-base.md](../architecture/module-base.md#node-uuid-mirror-setnodeuuid--getnodeuuid)). A module with **no** uuid matches nothing: an unsaved node has no identity yet, and `""` would otherwise adopt every unbound track in the document.
- **Mute/solo**: the track is silent when `muted`, or when `snapshot.anySoloed && !track.soloed`. `anySoloed` is precomputed by `TimelineSnapshot::buildFrom` so this costs one branch, not a rescan.
- **Sample-accurate offsets**: a note edge at absolute beat `b` lands at `jlimit(base, numSamples-1, base + llround((b - rangeStart) / info.beatsPerSample()))`, where `base` is 0 for the block's primary range and `info.loopWrapSample` for the wrapped range below. At 48 kHz / 512 / 120 BPM one beat is 24000 samples, so beat 1.0 is offset 448 of block 46 — pinned exactly by `Tests/Timeline/TimelineMidiSourceTests.cpp`.
- **Held-note hygiene — a note-on is a promise to emit the matching note-off.** Anything that could break that promise releases every held **timeline** note as note-offs at **sample 0** of the block it happened in, before anything else (an auditioned note is exempt from the positional flushes and released only by bypass or its own note-off — see the audition bullet above): the transport stopping, a block-start discontinuity (`info.blockStartSample != expectedNextBlockStart` — a locate, a tempo change re-deriving the sample position, a host repositioning us), the module being bypassed, the bound track disappearing or being unbound, and mute/solo suppression turning on. Continuity is only tracked *while playing*, since a stopped transport republishes the same start sample every block. The release is always **per note, never a blanket all-notes-off (CC 123)** — one MIDI cable can carry several sources downstream, and a CC 123 would silence notes this module never started. It emits note-ons and note-offs and nothing else, ever.
- **Bypass** is the two-branch contract for a pure source with no dry audio path: the **first** bypassed block still emits the pending note-offs, then clears and returns; subsequent bypassed blocks just clear and return.
- **No allocation on the audio path.** Held notes live in a fixed `kMaxActiveNotes = 128` array of `{pitch, channel, endBeat}`. On overflow the **note-on is dropped** — never a resize, and never a note-off for something already sounding.
- **Internal-only.** Not in the module library, not in the replace-with menu, not AI-authorable: it is in `kNonAuthorableModuleTypes`, and `validatePatch` on the untrusted path rejects it outright with `PatchValidationError::InternalModuleNotAllowed` (see [docs/ai/patch-safety.md](../ai/patch-safety.md)). It **is** in the factory, unconditionally, purely so our own saves round-trip it. The timeline's add-track flow is the only thing that creates one.
- **Loop wrap is two ranges, not one**. When `info.loopWrapSample >= 0` the block is split: the **primary** range `[startPpq, loopEndPpq)` at offsets `[0, loopWrapSample)`, then the **wrapped** range starting at `loopStartPpq` at offsets from `loopWrapSample` on. Note that `info.endPpq` is the *unwrapped virtual* end and overshoots `loopEndPpq` — using it for the primary range would emit beats the transport never plays.
    - **Release at the wrap.** At exactly `loopWrapSample`, every note still held is released (per note, before the wrapped range's note-ons at that same offset). Crossing the boundary ends the pass; the wrapped range then starts the next one from scratch. So **one block can end a note and start the same note again** — each loop pass re-articulates, which is what a looping sequencer sounds like, and a note whose end beat lies outside the loop (its note-off would never be reached) is released here instead of hanging.
    - **The wrapped range ends where the transport actually lands**, not at a derived beat: `predictNextBlockStart` mirrors `TransportService::tick`'s loop fold (same inputs, all published in `BlockTimeInfo`, same rounding), and the wrapped range stops there. That is what makes the emitted ranges tile the timeline exactly across a wrap — no beat emitted twice, none skipped. The same prediction feeds the continuity check, so **a wrap is not a discontinuity**: get it wrong and the block after every wrap reads as a locate and kills the notes the wrapped range just started.
    - **Multi-wrap bound.** `BlockTimeInfo` reports only the **first** wrap in a block. If the loop is shorter than the block — possible, since the minimum loop is `TransportService::kMinLoopLengthBeats` = 1/16 beat = 1500 samples at 48 kHz / 120 BPM, against a block that may be 2048 or more — the whole repetitions *between* the first wrap and the block's end are **not emitted**: the module plays the primary range and the final partial pass and drops the middle. What it never does is leak a note; the release-at-wrap runs regardless, so a pathologically short loop degrades to "some repeats are missing", never to "an envelope is stuck open". Pinned by `TimelineMidiSourceTest.TinyLoopMultiWrapNeverSticksNotes`.
- **Off-before-on at a shared offset.** Where a note-off and a note-on land on the same sample, the off is always emitted first — `juce::MidiBuffer` keeps insertion order among events sharing a sample position, and three separate places rely on it: the hygiene flushes run before any emission in the block, each range emits the note-offs of already-held notes in a pass of its own before any note-on, and the wrap release runs before the wrapped range's note-ons. Within a range, notes are visited in start-beat order, so a note's own end-of-range off is inserted before any later-starting note's on at that offset. Downstream this is exactly what the [poly note contract](#poly-note-contract-machine-midi) needs: a release, then the retrigger, never the other way round.
- **Fuzzed.** `TimelineMidiSourceTest.TransportFuzz1000OpsNoStuckNotes` drives 1000 seeded random transport operations (play / stop / locate / set loop / set tempo) against a polyphonic multi-channel track and models the emitted stream: a note-on for a key already sounding, a note-off for one that is not, any non-note message, or anything left down after the final stop is a failure.
- **Downstream**: see [Poly note contract](#poly-note-contract-machine-midi) above — machine-generated MIDI repeats pitches inside an envelope's attack constantly, and that contract is what makes each repeat produce a real gate edge.

---

## Rec Tap Module (Audio Take Recorder, Hidden)

`Source/Modules/RecordTapModule.h/.cpp`. A transparent stereo pass-through that copies what flows through it into a WAV plus a peaks sidecar, on a background thread. The record flow auto-splices one in front of Audio Output when an **audio** track is armed; see [`architecture/app-wiring.md`](../architecture/app-wiring.md#audio-recording) (Audio recording) for the flow itself.

- **Ports**: 2 audio in, 2 audio out, no MIDI (`acceptsMidi() == false`, `producesMidi() == false`). Jacks are `Left`/`Right` on both sides. The channel count is **fixed for the node's lifetime** (JUCE settles the bus layout in the `ModuleBase` constructor), so a wider-than-stereo master is not re-routed through it — the splice leaves any channel above 1 connected straight to the output.
- **Parameters**: none beyond the inherited `bypassed`. There is deliberately **no `muted`**: muting the tap would silence the whole master bus, which is not what a recorder is for.
- **Pass-through is literally nothing.** The graph hands a node its input in the same buffer it wants the output in, so the samples are already where they belong; the module only ever *reads* them. Recording is therefore audibly free, and a patch with a tap in it sounds exactly like the same patch without one — asserted bit-exactly, armed and disarmed, by `RecordTapTest.PassThroughAlways`.
- **Bypass passes the dry signal through** — the ordinary two-branch contract, since this module HAS a dry path (contrast Track In and Audio Input, which clear). There are no CV channels to clear. A bypassed tap also stops capturing: it is out of the signal chain, so recording what flows past it would put audio in the take the user could not hear.
- **Three threads.** `processBlock` (audio) pushes into a pre-allocated SPSC ring; a `juce::TimeSliceThread` the module owns drains it into a `juce::AudioFormatWriter` and accumulates peaks; `startCapture`/`stopCapture` run on the message thread. `stopCapture` detaches the writer client (`juce::TimeSliceThread::removeTimeSliceClient` blocks until any in-flight callback returns, which is what makes the message thread the ring's sole reader afterwards), drains the remainder itself, closes the WAV and writes the sidecar. The thread is **never joined there** — it stays alive and idle for the next take, and is stopped in the destructor.
- **Ring**: one **interleaved** `juce::AbstractFifo` of `1 << 17` = **131072 frames** — 2.7 s at 48 kHz, exactly 1 MiB of float storage for stereo, allocated once in the constructor. Interleaved so a block is one contiguous copy per FIFO region rather than one per channel, and so "frame" is the same unit the sample counter and the WAV length use. Sized well past the ~1 s of slack the writer needs so it can be descheduled for seconds without dropping a sample. The capacity is constructor-injectable purely so tests can provoke an overrun deterministically.
- **Overrun = drop + flag.** A push that does not fit keeps what it can and sets an atomic flag; the audio thread never blocks, never allocates, never locks and never logs. The take still commits, just short — the caller surfaces `"Dropped audio during recording"`. `TakeResult::lengthSamples` counts what was actually **written**, so it always equals the WAV's length even when a block landed after the final drain.
- **Not `AudioFormatWriter::ThreadedWriter`.** That class is its own ring plus its own time-slice client; stacking it on top of ours would mean two buffers, two overrun policies and two answers to "how long is the file". One ring, one writer, one counter.
- **Format**: 32-bit IEEE-float WAV (`juce::WavAudioFormatWriter` sets `usesFloatingPointData` for 32-bit), so a take is a bit-exact copy of what the graph produced — no clipping, no dither decision, and a read-back that compares equal.
- **Peaks sidecar (`.agpk`)**, accumulated during the drain so it is ready the moment the take stops and the clip view never has to re-read the WAV to draw the clip. Binary, little-endian: magic `AGPK`, `uint32` version (1), `uint32` bucketSize (256 source samples per channel), `uint32` numChannels, then per bucket per channel `float min`, `float max`. Bucket count is `ceil(lengthSamples / bucketSize)` — the final bucket is **short, never padded**. Written to `Peaks/<wav-stem>.agpk`.
- **Internal-only**, the same three exclusions as Track In: not in the module library, not in the *Replace with…* menu, and not AI-authorable — it is in `kNonAuthorableModuleTypes`, and `validatePatch` on the untrusted path rejects it outright with `PatchValidationError::InternalModuleNotAllowed`. The reason is sharper than Track In's: a Rec Tap **names a file path on disk**, so a model that could author one could aim a recording anywhere the app can write. It **is** in the factory, unconditionally, purely so our own saves round-trip a patch that has one. Cable-colour category: **Utility** (it is plumbing, like the device tap).

---

## Channel Strip Module (Mixer Channel, Hidden)

`Source/Modules/ChannelStripModule.h`. The end of one mixer channel (P9-2; the design lives in [`docs/mixer/mixer.md#node-types`](../mixer/mixer.md#node-types)). The mixer enumerates these; a column's fader, pan, mute and solo drive one.

- **Ports**: fixed **5 raw inputs, 16 raw outputs**. In: Left on ch0, Right on `kRightBase` = 4 (the split-block convention, never ch1; the same `kRightBase` the Macro In/Out port nodes use), ch1–3 **reserved** (future gain/pan CV) and cleared every block. Out: the main pair on ch0/ch4, ch5–7 reserved, then the four **send** slots (§5.15) — slot *k*'s Left on `kSendBase + k` (8–11) and its Right on `kSendBase + kMaxSends + k` (12–15), so each send's right leg sits on its own block too. Output is **always stereo**; the input is **Mono** (one `In` jack on ch0, feeding both output legs) or **Stereo** (`Left`/`Right`). No MIDI. The (5, 16) shape inherits no Dual I/O toggle.
- **Send slots are sparse and declared at the maximum** (the variable-port rule): all four `sendNLevel` parameters and all eight raw send channels always exist; `activeMask_` varies only the VISIBLE jack count. Removing a middle send leaves higher slots on their own raw channels and renumbers only the visible jacks — the jack LABEL keeps naming the slot, so a jack and its `sendNLevel` always agree. A send's TARGET is never stored: it is the graph edge itself.
- **Shape is fixed at creation** (§5.4): written once by `setShape()` (the channel-creation flow) or a trusted `setExtraState`, and locked from then on and from the first `prepareToPlay`. A later different shape is refused — changing width means replacing the strip.
- **Parameters**: `gain` (dB, −60…+12, default 0; the −60 floor is silence), `pan` (−1…+1, the shared `ModuleBase::panGains` balance law — unity at centre), `send1Level`…`send4Level` (dB, −60…+12, default 0 = unity), `muted`. Gain, pan and every send level are smoothed over 20 ms. A pre-fader send is tapped before gain/pan, a post-fader one after; **mute silences both**, and under bypass they coincide.
- **Solo is not a parameter.** `setSoloed`/`isSoloed` is an atomic flag persisted in extra state (`{"shape": "mono"|"stereo", "solo": bool, "isBus": bool, "sends": [{"slot", "pre"}]}`); the engine counts soloed strips and, while the count is nonzero, each strip silences the output legs its published **audible-leg mask** says do not reach the soloed path ([`docs/mixer/sends-and-buses.md#solo-is-a-per-leg-audible-mask`](../mixer/sends-and-buses.md#solo-is-a-per-leg-audible-mask)) — see [`architecture/audio-engine.md`](../architecture/audio-engine.md#mixer-solo-gate) (Mixer solo gate). Change it through `AudioEngine::setChannelStripSoloed`.
- **Bypass** is dry (no gain/pan; a mono strip still feeds both legs); **mute** clears — two branches. The solo gate applies in both the dry and the normal branch: a bypassed non-soloed strip must not leak into a soloed mix.
- **Meter**: `getMeterPeak(leg)` — the last block's post-fader peak, a plain store (not consume-on-read) so the mixer column and a track header's channel chip can both read it.
- **Internal-only**, the same three exclusions as Rec Tap; in `kNonAuthorableModuleTypes` (§6). Cable-colour category: **Utility**.

## Master Module (Mix Bus, Hidden)

`Source/Modules/MasterModule.h`. The mix bus every channel strip feeds, spliced in front of Rec Tap / Audio Output by `synth::ensureMasterNode` when the first channel is created (see [`architecture/app-wiring.md`](../architecture/app-wiring.md#audio-recording) (Audio recording) for the splice).

- **Ports**: 4 in — `Mix L`/`Mix R` (ch0/1, where strips land) and `Direct L`/`Direct R` (ch2/3, whatever went straight to the output before the splice); 2 out, `Left`/`Right`. No MIDI. Opts out of the inherited Dual I/O toggle (`StereoAudio::None`): its inputs are two stereo blocks, not an FX pair plus CV.
- **Parameters**: `gain` (dB, −60…+12), `muted`. Direct is summed into Mix **before** the fader.
- **Direct is gated while any strip is soloed** — it is not a channel, so a soloed mix silences it.
- **Bypass** is a unity sum of Mix + Direct (dropping Direct would silence every unchanneled cable); **mute** clears. Meter as for Channel Strip.
- **Internal-only**, a singleton by construction, same exclusions as Channel Strip.

---

## Track Audio Module (Timeline Audio Source, Hidden)

`Source/Modules/TimelineAudioSourceModule.h`. One node per timeline **audio** track: it plays that track's audio clips by **streaming them off disk**, so everything downstream is an ordinary stereo signal that neither knows nor cares that a timeline exists. The twin of Track In, for the other kind of track.

- **Ports**: 0 in, 2 audio out (`Left`/`Right`), no MIDI (`acceptsMidi() == false`, `producesMidi() == false`).
- **Parameters**: none beyond the inherited `bypassed`. Every playback value — gain, fades, source trim — lives on the **clip**, not the node, so there is nothing here to tune (and deliberately no `muted`: a muted *track* is a document state the module already honours).
- **Pull, not push**, exactly as Track In is: every block it downcasts `getPlayHead()` to `synth::TransportService` and reads three things off it — `getCurrentBlockInfo()`, `getCurrentTimelineSnapshot()` and `getAudioClipStreamer()`. No per-clip state is held anywhere, so a locate, a tempo change or a clip edit takes effect on the very next block with no invalidation step.
- **Binding is by node uuid**, and by **kind**: it finds "its" track by scanning the snapshot for the first **Audio** track whose `bindingUuid` `strcmp`s equal to `ModuleBase::getNodeUuid()`. An empty uuid matches nothing. (This is why the binding chip's menu is kind-aware — see [timeline/tracks.md](../timeline/tracks.md#binding-chips).)
- **Mute/solo**: silent when `muted`, or when `snapshot.anySoloed && !track.soloed`. Audio-track support also made a soloed **audio** track count towards `anySoloed`, which it did not before — audio tracks render now, and solo that excluded them would not be solo.
- **THE RAM CONTRACT.** The module and the streamer behind it never hold more than the rings. A ten-minute take costs **one ring — `AudioClipStreamer::kRingFrames`, ~1 MiB — not 100 MB**, whatever the file's length. This is the opposite of [Sampler](#sampler-module)'s whole-file-in-RAM model, which is right for a one-shot a voice retriggers from arbitrary offsets and wrong for an arrangement. Nothing in `processBlock` touches a file API: it copies out of a ring the prefetch thread filled, or it gets silence. Pinned by `AudioClipPlaybackTest`/`AudioClipStreamerTest.RamStaysBounded`, which plays a 60-second take and asserts the resident figure never moves off the ring size.
- **A clip with no stream renders silence** — just added, just seeked to, unresolvable, or past `AudioClipStreamer::kMaxStreams`. Never stale audio, never a stall.
- **Sample rate: the v1 policy, stated loudly.** Source positions are file frames computed with the **engine's** sample rate, and **nothing resamples**. A file whose header rate differs from the session's therefore plays **transposed** — a 44.1 kHz file in a 48 kHz session is ~8.8 % sharp and correspondingly short. Record-taps are written at the session rate, so takes recorded in the app are always right; an imported file may not be. The alternative for v1 (nearest-frame mapping at fill time) would alias audibly, which is worse than a uniform, documented pitch offset. `TODO(resample)`: a real SRC belongs in the prefetch thread's fill step, where the audio thread pays nothing for it. `AudioClipStreamer::getStreamFileSampleRate()` is what a UI warning would read.
- **Loop wrap is two ranges, not one** — the same decomposition Track In makes, from the same `BlockTimeInfo` fields and the same `predictNextBlockStart` fold, so a MIDI track and an audio track can never disagree about which beats belong to a block. The same **multi-wrap bound** applies (only the first wrap in a block is reported).
- **Overlapping clips SUM.** Overlap is legal in the model and crossfading it is an editor decision, not a playback one. Each clip is scaled by its own `gainLinear` (converted from dB **once**, at flatten time) times a per-sample **linear** fade envelope from `fadeInBeats`/`fadeOutBeats`, measured from the clip's own edges. Fades are **not** clamped against the clip length — a fade longer than the clip never reaches unity, which is exactly what `TimelineDoc::setClipFades` stores and what a later resize must not silently rewrite.
- **Hard cuts.** A stop, a locate or a loop wrap cuts: the module renders what the new position says and nothing else, so a clip cut mid-waveform clicks. Deliberate here — declick ramps and the loop-boundary crossfade are a later polish pass, and a hidden fade here would make the sample-exactness the tests pin unassertable. What is **never** heard is garbage: a position the ring cannot serve is silence, never a mix of two places in the file.
- **Bypass** clears (the documented exception in the bypass/mute contract: a pure source with no dry audio path). The buffer a source node is handed is scratch with undefined contents, so it is cleared on *every* path out of `processBlock`.
- **No allocation on the audio path.** One `kScratchFrames = 2048` stereo mixing scratch, sized in the constructor; a longer block costs another pass over it, never a resize.
- **Internal-only**, the same three exclusions as Track In and Rec Tap: not in the module library, not in the *Replace with…* menu, not AI-authorable (`kNonAuthorableModuleTypes`, plus `validatePatch`'s untrusted-path rejection with `PatchValidationError::InternalModuleNotAllowed`). The reason matches Rec Tap's from the other direction — a Track Audio node plays whatever clips the track bound to it names, so authoring one is choosing what gets read off disk. It **is** in the factory, unconditionally, purely so our own saves round-trip it. Cable-colour category: **Sources** (what leaves it is audio read off a file, exactly like a Sampler; Track In stays under Sequencing because what leaves *it* is notes).
- **The service behind it**: [`architecture/app-wiring.md`](../architecture/app-wiring.md#audioclipstreamer-disk-streaming-clip-playback) (AudioClipStreamer) — the pool, the ring's fill ordering, the retire policy, the asset roots and the deterministic test pump.

---

## Hosted Plugin Module (Third-Party VST3 / AU, Hidden)

`Source/Plugin/Hosting/HostedPluginModule.h` · `ModuleType::HostedPlugin` · factory key `"Hosted Plugin"` (scanning + load UX)

A third-party VST3 — or, on macOS, AU — instrument or effect, wrapped as an ordinary graph module. Hosting is done with **JUCE's built-in `juce_audio_processors` support only** (a licensing decision): no new dependency pins, and therefore no new licences to clear. **CLAP is deferred** — supporting it would mean vendoring the CLAP headers or `clap-juce-extensions`, i.e. exactly the new dependency that decision avoids. The formats are enabled as Core `PUBLIC` compile definitions in the root `CMakeLists.txt`: `JUCE_PLUGINHOST_VST3=1` everywhere, `JUCE_PLUGINHOST_AU=1` on Apple.

- **Channel layout: 16 in / 16 out, ALWAYS.** `ModuleBase("Hosted Plugin", kMaxPluginChannels, kMaxPluginChannels)` with `kMaxPluginChannels = 16`, whatever plugin the node ends up hosting and whether or not it ever hosts one. This is **forced, not chosen**: JUCE settles a node's bus layout in the `ModuleBase` constructor and renegotiating it would drop every graph connection the node already has — but a hosted plugin's channel count is not known until an async load completes, which is necessarily *after* the node exists and is already wired. So the node must be able to carry the widest plugin we will host from the moment it is created. Same fixed-maximum / varying-visible-count pattern as the [Macro bank](#macro-control-module-macros) and [Audio Input](#audio-input):
    - `getVisibleInputPortCount()` / `getVisibleOutputPortCount()` report the **instance's real counts** (0 inputs for an instrument is real and is shown as such), floored at **1 a side while empty** — a bare module still has to show where audio goes in and comes out.
    - Every channel at or above the instance's output count is **cleared every block**. Without it, whatever an upstream node put on channel 9 of a stereo plugin's node would sail on downstream.
    - When the visible count shrinks, the owner drops routings left on the hidden jacks (`GraphEditor::dropRoutingsOnHiddenJacks`) — an invisible jack cannot be unplugged.
- **Over-max is refused, never truncated.** An instance reporting more than 16 in or 16 out is discarded and `getStatusMessage()` says how wide it was and what the limit is. Truncating a surround plugin down to 16 would silently drop channels with nothing to tell the user.
- **`rightAudioLegChannel()` (FRO42) is derived from the published instance, not from `ModuleBase`'s default.** Ch1 here is never a CV input — this module has none, only the instance's own contiguous audio channels — so the base class's `hasDualIOParameter() ? 1 : -1` (which reads **-1 forever**: this module's fixed 16/16 shape never grants a Dual I/O parameter) is overridden: `1` once the instance publishes 2+ real outputs (a stereo or wider instrument/effect, same raw ch1 an FX pair uses); `0` (both legs read the one channel) for a genuinely mono instance; `-1` (no leg) for a bare/silent module. A caller wiring a stereo chain off this node (`MainComponent::addInstrumentPluginTrack`, [`docs/mixer/mixer.md#a-hosted-plugin-as-the-instrument`](../mixer/mixer.md#a-hosted-plugin-as-the-instrument)'s P9-3h entry) must read this only **after** the load completes — a bare module's 1-in/1-out placeholder shape is not the real instance.
- **Pass-through until ready.** Loading is asynchronous (JUCE's own contract, and a plugin load can take seconds). Until an instance is published — and *forever*, if the patch names a plugin this machine does not have — the module passes audio through **unmodified**. A chain does not go silent because one link is still loading, and a patch opened without a plugin installed still plays the rest of itself. That dry path is also why the bypass branch is trivial: bypass and "no instance" are the same code, and neither touches the buffer. `muted` clears, as the contract requires.
- **Instance lifetime: the audio thread never frees.** The [Sampler](#sampler-module)'s retained-instance discipline. `activeInstance_` is an atomic pointer the audio thread acquire-loads **once per block and never caches**. A load, a reload or an unload *retires* the old instance into a message-thread-only list; nothing is deleted on the audio thread, and a retired instance is freed only once the audio thread has demonstrably started two later blocks (a block counter bumped on every path out of `processBlock`, including bypass). Publication happens **after** `prepareToPlay` has run on the new instance and after its state blob has been applied, so the audio thread never renders an unprepared instance or one block of the plugin's factory default.
- **Stream format.** `prepareToPlay` re-prepares the live instance in place (the graph prepares its nodes with the audio callback stopped, so the published pointer stays valid), and a plugin loaded after a device change gets the new rate and block size, not a constructor default. The instance's `getLatencySamples()` is republished as the module's — see **Latency** below for what makes the graph actually compensate for it.
- **State, and its trust posture.** `getExtraState()` carries the **identity** plus the plugin's own opaque state blob, base64'd, and rides the existing **trusted-only** `setExtraState` path.
    - The identity is **format + `uniqueId` + name, and never a path**. A patch naming `/Users/someone/Library/Audio/Plug-Ins/VST3/Foo.vst3` would leak the author's disk layout into a file meant to be shared, would not survive moving the plugin or opening the patch on another machine, and would hand anything that can write a patch a lever on which file the host opens. Paths live only in the local scan list, never in a patch. Matching is uid-first (stable across machines for both formats, and across a rename); name is only the fallback for uid-less entries, and is always carried so an unresolved identity can still be *shown* to the user.
    - **A pending blob belongs to one plugin.** `setExtraState` is the only load allowed to carry a state blob into a publish; every other load (a library drop, a user-chosen replacement) clears it first. A blob is opaque bytes destined for third-party `setStateInformation`, and handing plugin A's bytes to plugin B is a fault the user cannot see.
    - A module holding an identity with **no instance is a valid state**, not an error — it is the "plugin not installed" placeholder, and `getExtraState` keeps re-serializing both the identity and the last known blob so re-saving the patch on a machine without the plugin does not destroy it.
- **Card layout override.** The trusted extra state may also carry `"cardLayout"` — the per-instance override of which parameters the card shows as knobs (a `CardLayout` object; [`control/plugin-card-layout.md`](../control/plugin-card-layout.md)). Absent means no override, and a module with no identity writes no state at all. It is cleared by every load that is not a state restore (like the blob, it belongs to the plugin it was made for). `HostedPluginModule::setExtraState` treats an object with `cardLayout` and **no identity keys** as a *layout-only patch* that changes the override and nothing else (no reload, blob untouched) — the shape `AppUndoManager::recordNodeExtraStateChange` replays on undo/redo. `onCardLayoutChanged` (message thread, single slot) fires when the override changes.
- **Internal-only**, the same three exclusions as Track In / Rec Tap / Track Audio: not in the module library, not in the *Replace with…* menu, not AI-authorable (`kNonAuthorableModuleTypes`, plus `validatePatch`'s untrusted-path rejection with `PatchValidationError::InternalModuleNotAllowed`). The reason is the **strongest on that list**: the node's `"state"` is an opaque byte string handed verbatim to third-party `AudioPluginInstance::setStateInformation`, and its identity selects *which binary the host loads*. Neither may ever be chosen by a model. It **is** in the factory, unconditionally, the same as Track In / Rec Tap / Track Audio — hosting has always been independent of the timeline feature, it simply never needed a gate to prove it. Cable-colour category: **Utility** — every other entry in `categoryFor` names a module with one DSP role, but this one names a *host*, and what it hosts is unknowable from the `ModuleType` alone; Utility is the neutral bucket rather than a guess that would be wrong about half the time.
- **Card body (FRO128).** The card shows the parameters of its resolved `CardLayout` — per-instance override, else the plugin's stored default, else the first eight automatable parameters — as ordinary knobs, toggles and choice combos, bound live in both directions to the hosted parameter (`HostedParameterAttachment`), under one row holding **Open Editor** and **Choose knobs...** (`ModuleComponent::onChooseKnobsRequested`, `Source/UI/Graph/PluginKnobPicker/`). A parameter that no longer resolves is not drawn. The body rebuilds whenever the instance goes live, when the per-instance override changes, and when the plugin's stored default changes; it empties when the instance goes away. It grows like any many-parameter card. Design and lifetime rules: [`control/plugin-card-layout.md`](../control/plugin-card-layout.md#card-rendering-as-built-fro128).
- **MIDI Learn and Automate on a card knob (FRO137).** Every knob/toggle/choice the card draws is registered with `ModuleComponent`'s own MIDI Learn registry (`registerHostedMidiLearnable`) exactly like a built-in module's controls, so right-click MIDI Learn / Forget / Edit assignment work on it; a knob also gets "Automate..." (opens the same lane picker a built-in knob does). `MidiLearnController` resolves a paramId that isn't one of the node's `RangedAudioParameter`s through `synth::resolveLaneParameter`'s hosted rules, so the assignment survives a plugin version moving its parameter set the same way an automation lane does. Removing a knob from the card layout never forgets its MIDI mapping or deletes its automation lane — the layout is presentation, never a binding. See [`control/plugin-card-layout.md`](../control/plugin-card-layout.md#interaction-with-midi-remote-and-automation).
- **Editor window.** The "Open Editor" button (`ModuleComponent`'s `HostedPluginModule` branch, `ModuleComponentHostedPluginCard.cpp`) is enabled only while `hasInstance()` — refreshed on the card's existing 15 Hz poll, since an async load can flip it at any moment. The click routes `GraphEditor::onOpenPluginEditorRequested(nodeId)` to `MainComponent`, which resolves the live module and calls `synth::HostedPluginWindowManager::openEditorFor`. The window (`synth::HostedPluginEditorWindow`) is a native `juce::DocumentWindow` — one per node, re-opening focuses the existing one — hosting `instance->createEditorIfNeeded()`, or a `juce::GenericAudioProcessorEditor` when the plugin reports no editor of its own. The editor drives the window's size (`setContentOwned(editor, true)`, which is what makes `ResizableWindow::childBoundsChanged` track the editor's own resizes) and `setResizable()` mirrors the editor's own `isResizable()`. The window observes `HostedPluginModule::onInstanceChanged` (fired on every edge of `hasInstance()`; the card observes the same edges through `HostedPluginModule::InstanceObserver`) to rebuild against a reload or close on a real unload — see the window manager's ownership/close rules in [`architecture/plugin-layer.md`](../architecture/plugin-layer.md#editor-windows) (Plugin hosting).
- **The seam behind it**: `synth::HostedPluginBackend` — see [`architecture/plugin-layer.md`](../architecture/plugin-layer.md#synthhostedpluginbackend--the-seam) (Plugin hosting).

### The scan list, and how an identity becomes a plugin

`Source/Plugin/Hosting/PluginScanService.h` owns a `juce::KnownPluginList` plus its blacklist. It is the **only** place a plugin's `fileOrIdentifier` is ever stored — the counterpart to "never a path in a patch" above.

- **Scanning is out of process.** Probing a plugin means loading a stranger's binary and calling into it, and a fair number of shipping plugins crash, hang, or pop a modal window when probed. So each candidate is scanned by a separate process: the app re-launches its own executable with `--scan-plugin <format> <fileOrIdentifier> <token>`, that process loads exactly one plugin, prints its description XML between sentinel lines stamped with that per-launch token, and exits. The parent reads only the last block carrying its own token, so a plugin that prints a forged description while it loads cannot write the scan list. A crash kills the child; a hang is killed on a 15 s timeout. Design and seams: [`architecture/plugin-layer.md`](../architecture/plugin-layer.md#plugin-scanning--a-crash-must-kill-a-child-not-the-app) (Plugin scanning).
- **A failure blacklists the candidate and the scan continues.** A crash, a timeout, a non-zero exit and "ran fine but described nothing" are indistinguishable from the parent and are all treated the same: the `fileOrIdentifier` goes on the blacklist and is **never launched again** by a later scan. Otherwise one bad plugin becomes a crash loop the user cannot escape. `clearBlacklist()` is the only way back in — for a plugin they have since updated or repaired.
- **Identity → description precedence** (`PluginScanService::resolve`), in order:
    1. **format + `uniqueId`**, and only when it matches *exactly one* entry. The uid is stable across machines and survives a rename, so it is the strongest key.
    2. **format + exact name**, used when the identity has no uid (an old or hand-written patch), when the uid matched nothing, or when **several** entries share the uid — which is real: VST3 shells and some vendors' families collide, and picking arbitrarily between two plugins the user can tell apart by name is worse than using the name. Name matching is exact and case-sensitive.
    3. Otherwise **nothing**, which is what leaves the module a placeholder that still remembers what it wants.
- **Persistence is the owner's job.** Core never touches settings. The owner restores the list from the `"pluginScanList"` user setting on startup, installs the service on the process-wide `DefaultHostedPluginBackend`, and writes `toXml()` back after every scan. The blacklist rides in the same document, so a crashing plugin is not rediscovered on the next launch. In the app the owner is `MainComponent`; in a plugin build it is `AgentSynthAudioProcessor`, whose constructor does the restore+install so a session restored with the window closed still resolves. An editor built on an external engine adopts the installed service rather than replacing it — see [`architecture/plugin-layer.md`](../architecture/plugin-layer.md#synthhostedpluginbackend--the-seam).
- **A hosted build resolves but never scans.** `startPluginScan()` is refused outright when the engine is `Hosted`: the scan re-launches `currentExecutableFile`, which inside a VST3/AU is the **host's** binary, i.e. one extra copy of the DAW per candidate. The host owns plugin discovery in that world anyway.
- **The scan is eager, not just click-triggered (FRO44).** `PluginScanService::ensureScanned(formatNames)` is a shared "make sure this has happened at least once" entry point: the first caller (any consumer, not only the sidebar) actually starts a scan that skips any candidate already in the persisted list (so a warm launch only probes what's newly installed, not the whole install base), every later caller is a no-op, and every registered `Listener` hears `pluginScanCompleted` when the one real scan finishes regardless of who triggered it. `Source/Main.cpp` calls `MainComponent::maybeStartEagerPluginScan()` once, right after building the real window, so the standalone app's list is populated in the background without anyone opening the Plugins section first — a hosted build's `isHosted()` guard keeps this a no-op there too, same as the manual button, which still forces a full re-probe of everything. Full design: [`architecture/plugin-layer.md`](../architecture/plugin-layer.md#plugin-scanning--a-crash-must-kill-a-child-not-the-app) (Plugin scanning).

### Load UX

The module library grows a **Plugins** section (last, below I/O) containing:

- a **"Scan for plugins..."** command row (`RowKind::Action`), always present. When nothing has been scanned it is the only row in the section, so it doubles as the empty-state hint. Clicking it runs `scanAsync` over every format this build hosts, with per-plugin progress in the status bar; `MainComponent::pluginScanCompleted` (a `PluginScanService::Listener`, registered once for the component's lifetime) does the row refresh + list save on completion — the same handler the eager startup scan above uses, so both paths produce identical UX.
- one **plugin row** (`RowKind::Plugin`) per scanned plugin, name on the left and the **format tag** on the right — the same plugin often ships as both VST3 and AU and they are different entries with different state.

A plugin row can be **dragged** onto the canvas or **clicked** to drop one in the middle of the view. Both go through `GraphEditor::addHostedPluginAtCanvasPosition`, a thin wrapper over the ordinary `addModuleAtCanvasPosition` whose `configure` hook sets the identity **before the node joins the graph** — so the identity is inside the undo snapshot and redo brings back the same plugin, not a bare module. Drag payloads ride the one DragAndDrop channel and are told apart by prefix: plain text is a module type, `snippet:` a saved group, `plugin:` a `PluginIdentity` (format + uid + name — **not** a path, for exactly the reasons a patch carries none).

`"Hosted Plugin"` itself is still **not** a library module row: added bare it would host nothing and have no way to become anything.

- **A second entry point, and a second completion signal (FRO42, P9-3h).** "+ Track -> Instrument -> Plugin -> <name>" ([`docs/timeline/add-track.md`](../timeline/add-track.md#the-plugin-sub-submenu), [`docs/mixer/mixer.md#a-hosted-plugin-as-the-instrument`](../mixer/mixer.md#a-hosted-plugin-as-the-instrument)'s P9-3h entry) is the other way a `HostedPluginModule` gets loaded, alongside the library-sidebar drop above — it REUSES the same `PluginScanService`/`HostedPluginBackend`/`HostedPluginModule::loadPlugin` machinery, never a parallel instantiation path, but stages the module OFF the graph and defers joining it (inside one undo transaction, with a whole default channel already wired around it) until the load's outcome is known. Neither `onInstanceChanged` nor `onInstancePublished` fires on a failed or over-max-refused load (the class's own comment on each explains why), so this needed a THIRD callback, `onLoadCompleted(bool success)` — fired once per load attempt, `success` read as `hasInstance()` right after, covering publish, an outright backend failure and the silent over-max refusal inside `publishInstance()` alike. `onInstancePublished`/`onInstanceChanged` are unchanged; this is additive.

- **Instance parameters as automation lanes.** The inner plugin's own parameters — never `HostedPluginModule`'s own `getParameters()`, which only ever carries `muted` — are exposed for lane resolution via `findInstanceParameter(paramId)` / `findInstanceParameterByIndex(index)` / `getInstanceParamIndexFallback(paramId)` / `getInstanceParameters()`, all message-thread-only and all reading the live instance. A hosted parameter is `juce::AudioProcessorParameter`, **not** `juce::RangedAudioParameter` — a persistent string id, where the format has one, comes from `juce::HostedAudioProcessorParameter::getParameterID()`, a sibling interface with no `NormalisableRange`; its native domain is always `0..1` (JUCE's own host contract). A lane's `paramId` + `paramIndexHint` resolve against the live instance by this rule, enforced identically everywhere a lane resolves (`synth::resolveLaneParameter`, `Source/Timeline/AutomationBinding.h` — see [`architecture/plugin-layer.md`](../architecture/plugin-layer.md#automation-lanes-on-hosted-plugin-parameters) (Automation lanes on hosted-plugin parameters)):

    | Situation | Outcome |
    | --- | --- |
    | `paramId` matches a live parameter's `getParameterID()` | **Binds** to it, whatever index it is now at. |
    | No id match, but `paramIndexHint` names a parameter with **no stable id at all** (a legacy format) | **Binds by index** — the only case an index match is trusted. |
    | No id match, and the hinted index names a **different, still-identified** parameter (a plugin update moved the parameter set) | **Orphans.** Never silently rebinds to whatever is there now. |
    | No live instance (still loading, unloaded, or the plugin isn't installed) | **Orphans.** A `HostedPluginModule` node with nothing loaded cannot currently vouch for any parameter. |
    | Hinted index no longer exists at all | **Orphans.** |

  Orphaned lanes are retained and re-bindable, never auto-deleted. The automation strip's lane picker offers "Add lane…" entries for not-yet-automated instance parameters; a created lane's `RangeSnapshot` is always `{0, 1, default}` and its `paramIndexHint` is captured from the parameter's index at that moment, never re-derived afterwards. Because the load is asynchronous, a lane always *starts* orphaned when a project opens — the node exists long before its parameter set does — and what un-orphans it is the completed load itself: `onInstancePublished` fires at the end of every publish and `MainComponent` reconciles there. Before that hook existed, such a lane stayed orphaned until an unrelated graph edit happened to trigger the next reconcile pass.
- **Latency.** The module mirrors the instance's `getLatencySamples()` onto the node at publish, drops it to 0 on unload, and — the part that needs a listener — follows a plugin that changes its own latency at runtime (a lookahead mode toggled in its editor). That notification arrives on whatever thread the plugin was on, so it is hopped to the message thread through a `juce::AsyncUpdater` before anything is touched. Reporting the number is not compensating for it: `juce::AudioProcessorGraph` only re-derives its parallel-path delays when the render sequence is **rebuilt**, so the module fires `onLatencyChanged` / `onInstancePublished` and `MainComponent` calls `rebuild()`. Details, edge-by-edge, in [`architecture/plugin-layer.md`](../architecture/plugin-layer.md#latency-compensation) (Latency compensation); the acceptance test is an impulse split down a dry and a hosted path that must land on one output sample.

---

## Audio Input / Audio Output (Graph I/O)

Both are **singletons**: at most one of each per patch (`GraphEditor::isSingletonIOModule`), because JUCE ties the graph's channel count to the output node and every lookup in the app (auto-connect, `PatchEval`, auto-arrange) takes the first match and stops. A second drop is a no-op, the library row greys out, neither is snippet-eligible (a snippet carrying one would insert a duplicate into whatever patch it lands in), and neither offers the *Replace with…* menu.

They are **not** the same kind of object, though, and have not been since Audio Input became a real module:

- **Audio Output** — everything you want to hear ends here. Still a `juce::AudioProcessorGraph::AudioGraphIOProcessor`, so no parameters, no level control, no bypass/mute contract. It stays one because the graph's output channel count is defined by it.
- **Audio Input** — a real module (`Source/Modules/AudioInputModule.h`, `ModuleType::AudioInput`), so it has a bypass, a port model, a cable colour and an ordinary module card. It carries **real device input** (the device-input work got the input into the engine, then gave it a module) and behaves identically standalone and in a plugin.

### Audio Input

- **Where the samples come from.** Not from an input bus — the graph feeds a node's inputs from other nodes, and the device is not a node. Every render pass `AudioEngine` copies the device's (or host's) input into a **private snapshot buffer** and parks pointers to it on the transport (`TransportService::setDeviceInputForBlock`); the module downcasts `getPlayHead()` and copies from there. Same route Track In uses for the timeline snapshot, and the same block-only lifetime — the pointers are never cached in a member.
  The snapshot copy is load-bearing, not defensive: the graph renders **in place** over the very buffer that carried the input in, so a node reading those channels mid-graph would see partly-rendered output. See [`architecture/audio-engine.md`](../architecture/audio-engine.md#audioengine) (AudioEngine).
- **Max channels fixed, visible jacks varying.** The node always has `AudioInputModule::kMaxChannels` (= `TransportService::kMaxDeviceInputChannels` = **8**) output channels, because JUCE settles a bus layout in the `ModuleBase` constructor and renegotiating it would drop every connection the node already has. Only `getVisibleOutputPortCount()` follows the device — the same max-channel/visible-port pattern as the [Macro bank](#macro-control-module-macros), with the device standing in for the `macroCount` parameter. Channel *i* is device input channel *i*; jacks are labelled `Left`/`Right` then `In 3`…`In 8`.
    - Channels at or above the visible count are **cleared every block**.
    - At least **one** jack is always visible, even with no input device: a patch still has to show where input would arrive, and a zero-jack card is not something a cable can be drawn to.
    - The card's height tracks the jack count: the port gutter alone sets it, since the module has no controls — 100 px (the floor) up to two jacks, 217 px at eight. `GraphEditor::estimateModuleSize` returns the resting 100 px for the drop estimate, exactly as it does for the Macro bank's default knob count.
- **Owner drop on shrink.** Switching from an 8-in interface to a 2-in one hides jacks that may still have cables on them, and an invisible jack cannot be unplugged by hand. The owner drops them: `MainComponent`'s device-state-changed callback calls `GraphEditor::refreshIoModulesAfterDeviceChange()`, which pushes `AudioEngine::getDeviceInputChannelCount()` into every Audio Input module (so the card resizes immediately rather than one rendered block later), calls `GraphEditor::dropRoutingsOnHiddenJacks` for it, and re-measures the card. Attenuverter chains are removed whole, both legs, exactly as on a Macro-bank shrink.
- **Bypass** clears, like every pure source with no dry audio path (Oscillator, LFO, Noise, Poly MIDI) — the documented exception to the two-branch bypass/mute contract in [`architecture/module-base.md`](../architecture/module-base.md#bypassmute-contract). There is no `muted` parameter: bypass already silences everything there is to silence, so the module's only parameter is `bypassed`.
- **Opt-in, still.** Inputs are off until the user chooses them in *Settings → Audio*; a fresh install (or any install whose user has never chosen a device setup) opens output-only exactly as before, and the module then shows its single silent jack. Nothing is audible until you patch it onward — the default patch leaves it unconnected.
- **Monitoring gate.** Even once patched onward, the module's `processBlock` clears its own output — the graph never hears the device input — unless `TransportService::isInputMonitoringEnabledForBlock()` reads true. `MainComponent`'s 10 Hz poll is what flips that flag: any `TrackKind::Audio` track armed enables it, none armed disables it. This is deliberately the *same* signal that gates recording — "monitoring enabled" **is** the record path for input chains, so an idle rack (nothing armed) keeps the mic out of the speakers with no separate mute to remember. A sustained near-clip output (post-graph, ≥ 0.97 peak for ≥ 0.25 s) trips a feedback guard that disables monitoring itself and latches a status-bar warning until the armed-track set is explicitly disarmed and re-armed — see [`architecture/audio-engine.md`](../architecture/audio-engine.md#input-monitoring--feedback-guard) (Input monitoring & feedback guard) for the full mechanism and thresholds.
- **Compatibility.** The factory key, the display name and the serialized `"type"` string are all still `"Audio Input"`, and channel indices still start at 0 — so a patch saved before Audio Input became a module, whose node *was* the raw `audioInputNode`, loads onto the module with its wires intact and saves back out identically (`AudioInputModuleTest.LegacyPatchLoads`). It remains model-authorable: it is a benign source with nothing but a bypass.

---

## Attenuverter Module (Hidden)
- **Purpose**: Invisible gain/polarity stage automatically inserted on every mono CV connection routed via the mod matrix.
- **Parameters**: `Amount` — ranges from -1.0 (full inversion) to +1.0 (full depth), **constructor default 0.0**.
    - `AudioEngine::addModRouting()` immediately sets Amount to 1.0 when a connection is created through the mod matrix.
    - `AudioEngine::addEmptyModRouting()` leaves Amount at 0.0 (empty routing placeholder).
- **Interaction**: Controlled via the **Smart Cable knob** on the graph or the **Mod Matrix** slider.
- **Serialization**: Saved and restored as part of every preset alongside the connection it belongs to.

---

## Poly Channel Layout

This table shows the raw `AudioProcessorGraph` channel assignments for each poly-capable module. See [docs/modules/modulation.md](modulation.md) for how `getModulationRoutings()` and the logical-port API collapse these into visible wires.

### Rule for new poly modules

In poly mode, voices occupy channels 0-7 (audio/pitch/gate) and the shared-CV block starts at channel 8. **Declare `numOutputs >= highest CV input channel index you read**, to avoid JUCE `AudioProcessorGraph` buffer aliasing when `inputChan >= getTotalNumOutputChannels()`.

Declare your per-voice **output** fan in `mapOutputChannel()` if the module actually emits one signal per voice — Oscillator, Filter and Noise all fan raw ch0-7 onto a single Audio jack this way. Don't, if the module sums voices down instead: VCA has no `mapOutputChannel()` override because its poly mode sums all 8 voices to stereo on ch0-1, a plain jack rather than a fan. Getting this wrong misdirects `GraphEditor::resolvePolyLink` (see [docs/modules/modulation.md](modulation.md#creating-poly-connections)) into fanning a dragged cable out of an output that only ever produces one signal.

| Module | Channel | Direction | Content |
|--------|---------|-----------|---------|
| **PolyMidi** | ch0-7 | Out | Pitch CV per voice (Hz) |
| **PolyMidi** | ch8-15 | Out | Gate CV per voice (0/1) |
| **Oscillator (poly)** | ch0-7 | In | Per-voice pitch CV (Hz) |
| **Oscillator (poly)** | ch8 | In | Shared Waveform CV |
| **Oscillator (poly)** | ch9 | In | Shared Octave CV |
| **Oscillator (poly)** | ch10 | In | Shared Coarse CV |
| **Oscillator (poly)** | ch11 | In | Shared Fine CV |
| **Oscillator (poly)** | ch12 | In | Shared Level CV |
| **Oscillator (poly)** | ch13 | In | Shared Pan CV |
| **Oscillator (poly/mono)** | ch14 | In | Unison CV (FRO314, same channel both modes) |
| **Oscillator (poly/mono)** | ch15 | In | Detune CV (FRO314, same channel both modes) |
| **Oscillator (poly)** | ch0-7 | Out | Per-voice audio — `Audio L` |
| **Oscillator (poly)** | ch8-13 | Out | Silent pass-throughs (prevent buffer aliasing) |
| **Oscillator (poly)** | ch14-21 | Out | Per-voice audio — `Audio R` (`kRightBase`); ch14/15 alias the Unison/Detune CV inputs above |
| **Oscillator (mono)** | ch0 | In/Out | Pitch CV in / `Audio L` out (shared channel, CV saved before clear) |
| **Oscillator (mono)** | ch1 | In | Waveform CV |
| **Oscillator (mono)** | ch2 | In | Octave CV |
| **Oscillator (mono)** | ch3 | In | Coarse CV |
| **Oscillator (mono)** | ch4 | In | Fine CV |
| **Oscillator (mono)** | ch5 | In | Level CV |
| **Oscillator (mono)** | ch6 | In | Pan CV |
| **Oscillator (mono)** | ch14 | Out | `Audio R` (`kRightBase`); aliases the Unison CV input above |
| **Wavetable (poly)** | ch0-7 | In | Per-voice pitch CV (Hz) |
| **Wavetable (poly)** | ch8 | In | Shared Position CV |
| **Wavetable (poly)** | ch9 | In | Shared Octave CV |
| **Wavetable (poly)** | ch10 | In | Shared Coarse CV |
| **Wavetable (poly)** | ch11 | In | Shared Fine CV |
| **Wavetable (poly)** | ch12 | In | Shared Level CV |
| **Wavetable (poly)** | ch13-22 | In | Shared Warp, Phase, Rand, Detune, Spread, Width, Blend, Sub, Pan, Sync CV |
| **Wavetable (poly)** | ch0-7 | Out | Per-voice audio **L** |
| **Wavetable (poly)** | ch8-22 | Out | Silent pass-throughs (prevent buffer aliasing) |
| **Wavetable (poly)** | ch23-30 | Out | Per-voice audio **R** (`kRightBase`) |
| **Wavetable (mono)** | ch0 | In/Out | Pitch CV in (inert) / Audio L out (shared channel) |
| **Wavetable (mono)** | ch1 | In | Position CV |
| **Wavetable (mono)** | ch2 | In | Octave CV |
| **Wavetable (mono)** | ch3 | In | Coarse CV |
| **Wavetable (mono)** | ch4 | In | Fine CV |
| **Wavetable (mono)** | ch5 | In | Level CV |
| **Wavetable (mono)** | ch6-15 | In | Warp, Phase, Rand, Detune, Spread, Width, Blend, Sub, Pan, Sync CV |
| **Wavetable (mono)** | ch23 | Out | Audio R (`kRightBase`) |
| **Filter (poly)** | ch0-7 | In/Out | Per-voice audio — `Audio L`, filtered in place |
| **Filter (poly)** | ch8 | In | Shared Cutoff CV |
| **Filter (poly)** | ch9 | In | Shared Resonance CV |
| **Filter (poly)** | ch10 | In | Shared Drive CV |
| **Filter (poly)** | ch11-18 | In/Out | Per-voice audio — `Audio R` (`kRightBase`), own ladder per voice |
| **Filter (mono)** | ch0 | In/Out | `Audio L` in / filtered out |
| **Filter (mono)** | ch1 | In | Cutoff CV |
| **Filter (mono)** | ch2 | In | Resonance CV |
| **Filter (mono)** | ch3 | In | Drive CV |
| **Filter (mono)** | ch11 | In/Out | `Audio R` (`kRightBase`) in / filtered out |
| **VCA (poly)** | ch0-7 | In | Per-voice audio — `Audio L` |
| **VCA (poly)** | ch8-15 | In | Per-voice envelope/CV |
| **VCA (poly)** | ch16-23 | In | Per-voice audio — `Audio R` (`kRightBase`) |
| **VCA (poly)** | ch0 | Out | `Audio L` — sum of the left voice block |
| **VCA (poly)** | ch1 | Out | Vestigial duplicate of the left sum (pre-#219 mono→stereo affordance) |
| **VCA (poly)** | ch16 | Out | `Audio R` — sum of the right voice block |
| **VCA (mono)** | ch0 | In/Out | `Audio L` in / gated out |
| **VCA (mono)** | ch1 | In | Gain CV (also overwritten on the way out — see above) |
| **VCA (mono)** | ch16 | In/Out | `Audio R` (`kRightBase`) in / gated out |
| **ADSR (poly)** | ch0-7 | In | Per-voice gate CV |
| **ADSR** | ch8 | In | Threshold CV (shared) |
| **ADSR** | ch9-13 | In | Attack/Hold/Decay/Sustain/Release CV (shared, FRO285) |
| **ADSR** | ch14-16 | In | Attack/Decay/Release Curve CV (shared, FRO314; no generic knob — see above) |
| **ADSR (poly)** | ch0-7 | Out | Per-voice envelope (0–1) |
| **Sample & Hold** | ch0 | In/Out | Signal in / held CV out (shared channel; read before overwrite) |
| **Sample & Hold** | ch1 | In | Trigger / gate |
| **Sample & Hold** | ch2 | In | Rate CV |
| **Sample & Hold** | ch3 | In | Slew CV |
| **Sample & Hold** | ch4 | In | Level CV |
| **Sample & Hold** | ch5 | In | Offset CV |
| **Sample & Hold** | ch6 | In | Threshold CV |
| **Sample & Hold** | ch1-6 | Out | Silent (cleared each block so CV does not leak downstream) |
| **Comparator** | ch0 | In/Out | Signal in / Gate out (shared channel; read before overwrite) |
| **Comparator** | ch1 | In/Out | Threshold CV in / Inverse gate out |

---

## Modulation System

Agent Synth uses a **hidden Attenuverter architecture** for modulation depth control, inspired by Serum's mod matrix. The engine derives a first-class `ModulationRouting` model from the graph at runtime. See [docs/modules/modulation.md](modulation.md) for the full reference.

### Smart Cables
- Every mono CV cable (AttenuverterChain routing) renders a circular knob at the midpoint of the bezier curve.
- **Drag up/down** to sweep depth from -100% to +100%.
- **Double-click** to instantly delete the connection.

### Poly Bus Wires
- When N per-voice `DirectCV` connections share the same source module and destination visible jack, the GraphEditor collapses them into a single wire with an "xN" badge (e.g. "x8").
- These `PolyBus` wires have no midpoint knob because there is no attenuverter in the path.
- Dragging a cable between two equally-wide poly jacks creates all N per-voice connections directly — no need to hand-author each voice in preset JSON.
- Toggling a module's `poly` parameter re-anchors its existing cables to the new channel layout: mono cables fan out when both ends go poly, and fans collapse back to one wire when poly is switched off.
- A mono modulator (e.g. an LFO) dropped on a per-voice **mod-CV** fan such as the poly VCA's CV jack is broadcast to every voice — one source channel, N wires. This applies to `ModCV` only: `Pitch`/`Gate` fans and audio fans still take a single head-to-head wire, since duplicating those would stack N identical voices.

### Mod Matrix Panel
- Sits on the right edge of the Graph Editor (toggleable).
- Lists every active CV connection as a labelled row with a bipolar slider.
- Sliders and smart cable knobs are **bidirectionally synced** in real time via a 30 Hz timer.

### Panel Toggles
- **Hide AI / Show AI** — collapses the right-side AI chat panel.
- **Hide Matrix / Show Matrix** — collapses the Mod Matrix panel.
- Both buttons live in the top application bar, and the Graph Editor canvas expands to fill reclaimed space.
