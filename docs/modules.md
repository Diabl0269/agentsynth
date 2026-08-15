# Core Modules Reference

Detailed specifications for Agent Synth's primary synthesis modules.

> **Level parameters.** There is no universal per-module gain. Modules that output audio expose a level control: Oscillator, LFO, Noise and Voice Mixer have their own `level`; VCA has `gain`; Filter and the FX modules use the shared opt-in output-level stage documented in [`fx_modules.md § Output Level`](fx_modules.md#output-level-shared-stage). Modules that output **pitch/gate CV or MIDI** (Sequencer, Poly Sequencer, ADSR, Poly MIDI, MIDI Keyboard, External MIDI) deliberately have none — scaling a V/oct pitch CV transposes it, and scaling a gate drops it below the `> 0.5f` trigger threshold. Attenuverter has none because it already is a gain stage.

## Oscillator Module
- **Waveforms**: Sine, Square, Saw, Triangle.
- **Features**: 
    - PolyBLEP anti-aliasing on Square and Saw; PolyBLAMP on Triangle.
    - Waveform crossfade (64-sample fade on waveform changes).
    - MIDI-to-Frequency tracking with unison and detune support.
    - Integrated visual buffer for real-time waveform display.
- **Poly mode**: 8 voices driven by pitch CV (Hz). See [Poly Channel Layout](#poly-channel-layout) for channel details.
- **Poly `processBlock` CV-save order**: In poly mode, both the per-voice pitch CVs (ch0-7) and the shared mod CVs (ch8-12) are copied into pre-allocated `std::array` caches (`pitchCVCache`, `waveformCVCache`, `octaveCVCache`, `coarseCVCache`, `fineCVCache`, `levelCVCache`) **before** the output buffer is cleared. This is necessary because ch0-7 carry both output audio (written after the clear) and input pitch CV, so they must be read first.
- **Buffer aliasing note**: Declared with 14 output channels so JUCE's `AudioProcessorGraph` correctly copies shared mod-CV input channels (8-13) when they fan out to multiple downstream nodes. Channels 8-13 of the output are silent pass-throughs.

## Wavetable Module
Serum / Vital-style wavetable oscillator (`Source/Modules/WavetableOscillatorModule.h`). Type-name string: `"Wavetable"`.

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
- **Channel layout** (mono module — no poly mode): in ch0 = Trigger/Gate, ch1 = Pitch CV, ch2 = Position CV, ch3 = Grain Size CV, ch4 = Density CV, ch5 = Spray CV, ch6 = Level CV. Out ch0/ch1 = Audio L/R; ch2-6 are silent pass-throughs.
- **Buffer aliasing note**: 7 outputs are declared even though only ch0-1 carry audio, so JUCE copies the CV input channels instead of letting the post-cache clear scribble on a buffer another node still needs — the same constraint as the Oscillator's 14-channel declaration.
- **Gate precedence**: trigger CV > MIDI note > free-run. "A trigger cable is connected" is *latched* on the first non-zero sample rather than re-derived per block: a legitimately-low gate is an all-zero channel, indistinguishable from an unpatched jack, so re-deriving it would let a closed gate silently fall back to free-running. With nothing patched and no MIDI, a loaded sample plays immediately — dropping the module in and picking a file makes sound with no wiring.
- **Pitch**: `2^((pitch + pitchCV×24 + (midiNote − rootNote)) / 12)`, times the file-rate/device-rate ratio so a 48 kHz file plays at the right speed on a 44.1 kHz device. Reads are 4-point Catmull-Rom interpolated.
- **Granular engine**: 24-grain pool, Hann-windowed, spawned every `sampleRate / density` samples while the gate is open; grain start positions wrap rather than clamp so `spray` keeps scattering near either end. Output is scaled by `1/sqrt(density × grainSize)` so loudness stays roughly constant as the cloud thickens, then hard-limited to [-1, 1]. When the pool is exhausted new grains are dropped.
- **Anti-click**: a 64-sample linear ramp on trigger and release; a one-shot ramps out at the last frame rather than cutting.
- **Bypass**: clears its output — the documented pure-source exception to the bypass/mute contract (every input is CV/gate, so there is no dry signal to pass through).
- **Sample lifetime**: `loadSampleFile()` (message thread) publishes a reference-counted `SampleData` under a `SpinLock`; `processBlock` takes the *try*-lock, so the audio thread never blocks — a block that races a load renders silence. Replaced samples stay alive in a message-thread-owned array so no destructor ever runs on the audio thread. Files longer than `kMaxSampleSeconds` (120 s) are truncated, with one log line.
- **Persistence**: the loaded path is *not* a parameter, so it round-trips through `ModuleBase::getExtraState()` / `setExtraState()`, which `AIStateMapper` serialises as the node's `"state"` object. Restored **only on the trusted path** (our own undo/redo snapshots and presets) — untrusted model output must never be able to name a file for the app to open. See [`AI_Engine.md`](AI_Engine.md).
- **Not a `PatchEval` sound source**: `evaluatePatch` deliberately does *not* count a Sampler towards `sourceReachesOutput`, because it is silent until a file is loaded and nothing in a model-authored patch can load one — counting it would let `AIIntegrationService`'s structural gate accept a patch that can only ever play silence. A patch whose output is fed *only* by a Sampler is rejected with a reason that names the Sampler and says to add an Oscillator or Noise module. This gate only applies to AI-authored patches; dragging a Sampler in by hand is unaffected.

## Filter Module
- **Types**: 7 filter types — `LPF24`, `LPF12`, `HPF24`, `HPF12`, `BPF24`, `BPF12`, `Notch`.
- **Implementation**: `LPF24/12`, `HPF24/12`, `BPF24/12` use `juce::dsp::LadderFilter`; `Notch` uses `juce::dsp::StateVariableTPTFilter` (notch computed as input minus bandpass).
- **Parameters**: Cutoff (20–20000 Hz), Resonance (0–1), Drive (1–10), Filter Type (choice), Poly (bool), Level (0–1, default 1.0 — the shared output-level stage; see [`fx_modules.md § Output Level`](fx_modules.md#output-level-shared-stage)). Level scales ch0 in mono mode and all 8 voice channels in poly mode, never the CV inputs.
- **CV inputs (mono mode)**: Cutoff = ch1, Resonance = ch2, Drive = ch3.
- **CV inputs (poly mode)**: Cutoff = ch8, Resonance = ch9, Drive = ch10.
- **Poly mode**: 8 per-voice audio inputs (ch0-7) + 3 shared CV inputs (ch8-10). Shared CV is computed once per block and applied to all active voices.
- **Atomic modulated params**: `modulatedCutoff`, `modulatedResonance`, and `modulatedDrive` are `std::atomic<float>` members updated every `processBlock` (before voice processing in poly mode; per-sample in mono mode). `FrequencyResponseComponent` reads `getCurrentCutoff()` / `getModulatedResonance()` on the UI thread without locks.
- **`isAutoPromotableModTarget`**: Returns `false` in poly mode (poly CV connections stay plain `DirectCV`, not auto-wrapped in attenuverters).

## ADSR (Envelope) Module
- **Stages**: Attack, Decay, Sustain, Release.
- **Mono output**: Generates a single control signal (0.0 to 1.0) on channel 0, triggered by MIDI.
- **Poly mode**: 8 gate CV inputs (ch0-7) drive 8 independent ADSR instances; outputs 8 per-voice envelopes (ch0-7).
- **Uses**: Modulation of VCA gain, Filter cutoff, or Oscillator Level.

## Envelope Follower Module
- **Role**: A *detector*, not a generator — tracks the amplitude contour of an audio input and emits it as unipolar `[0, 1]` modulation CV. Deliberately separate from ADSR: its input is audio (not a gate), its times are milliseconds (not seconds), and it has no decay/sustain stages.
- **Detection**: `Peak` (rectify) or `RMS` (mean-square, square-rooted on output), selected by the `detection` choice parameter.
- **Parameters**: Attack (0.1–200 ms), Release (1–2000 ms), Sensitivity (0–1), Detection (choice), Mute.
- **Sensitivity**: A unit-interval control mapped to 0.25×–4.0× detector gain (`sensitivityToGain`, exponential, 0.5 = unity). It is *not* a raw gain or a dB value on purpose: `AIStateMapper::applyParamsToProcessor` rescales an in-`[0,1]` value when the parameter's range is wider than `[0,1]`, so a non-unit range here would silently misread AI-authored patches.
- **Channels**: inputs ch0 = Audio, ch1 = Attack CV, ch2 = Release CV, ch3 = Sensitivity CV. Outputs ch0 = Env CV; ch1-3 are silent (declared only so JUCE cannot alias CV input ch3 onto another node's output buffer).
- **CV modulation of times**: Attack/Release coefficients are recomputed once per block from the CV value at the block start — one `exp()` per block instead of per sample. Time constants only change how fast the follower tracks, so block-rate updates cannot zipper the output level. Sensitivity *is* applied per sample (smoothed).
- **Bypass**: Clears its output rather than passing the dry signal through — see the exception note in [architecture.md](architecture.md). The module has an audio input but no audio output, so a dry pass-through would push audio-rate samples into a CV destination.
- **Uses**: Sidechain-style ducking (follow a drum bus → VCA gain), dynamic auto-wah (follow a signal → Filter cutoff).

## VCA (Amplifier) Module
- **Inputs**: 
    - Mono mode: Audio (ch0), CV (ch1).
    - Poly mode: Per-voice audio ch0-7, per-voice envelope/CV ch8-15.
- **Poly summing**: In poly mode, multiplies each voice's audio by its corresponding envelope CV, then sums all 8 voices to stereo (ch0/ch1) with `tanh` soft saturation and 1/8 normalization.
- **Features**: Parameter smoothing for click-free gain changes.

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

### Poly note contract (machine MIDI)

Hand-played MIDI rarely repeats a pitch inside an envelope's attack; **machine-generated MIDI does it constantly** — this is the contract the timeline's Track In node (TL3) schedules its clips against, so it is stated in samples, not in "should sound fine".

- **Every re-articulation produces a gate edge.** All three paths that start a note on a voice — same-pitch retrigger, voice steal, and reuse of a voice released moments earlier — go through one function (`startNote`). If that voice's gate CV is not already at rest (`smoothedGate.getCurrentValue() > 1e-4`, deliberately *not* the `active` flag — a just-released voice is inactive but still emitting), the gate drops to **0 instantly** at the event sample via `setCurrentAndTargetValue(0.0f)`. An instant CV step is a real edge; the usual 5 ms smoothing still shapes the rise back up.
- **Minimum 1 ms low-gate gap.** The voice records `gateReopenAtSample` (absolute, off the same monotonic `sampleCounter_` the age stamps use) = event sample + `jmax(16, sampleRate * 0.001)`. `renderChunk` checks it per sample, so a gap that ends mid-chunk still reopens exactly on time. Without the gap a drop followed immediately by a rise can smooth into a dip too shallow and short for anything downstream to register — that is precisely the clip-boundary case, where a note-off and the next note-on for one pitch land on the **same** sample.
- **A fresh note on an idle voice has no gap** — its gate is already 0, so the rise starts on the event sample itself.
- **Note-off is unchanged**: gate target 0 with the 5 ms smoothing, pitch held.
- **`Vel → Gate` (`velToGate`, default off).** Off: the gate rises to exactly 1.0, as it always has — a preset saved before this parameter existed loads with the default and renders byte-identically. On: the gate rises to the note-on velocity (0..1), stored per voice, so held voices keep their own levels.
- **Chord larger than the voice count.** Nine same-sample note-ons on eight voices in `Oldest` mode drop the chord's **first** note: stamps are strictly increasing (`stampFor` clamps to `lastStamp_ + 1`), so note 1 is the oldest by one count and note 9 steals it. Notes 2–9 sound, and the stolen voice re-attacks by the rule above rather than gliding.
- **Downstream reality check (`ADSRModule`)**: the ADSR's poly branch samples its gate input **once per block** (`gateData[0]`), so the 1 ms gap re-articulates it only when the gap covers a block boundary; a mid-block retrigger is invisible to *that* module even though the CV is correct. Pinned end-to-end by `PolyMidiToAdsrTest.RetriggerReArticulatesAdsrWhenTheGapSpansABlockBoundary`. Fix belongs in ADSR's edge detection, not by widening this gap.

## Voice Mixer Module
- **Purpose**: Explicit 8-to-stereo voice summing with level control and soft saturation. An alternative to VCA's internal poly summing for patches that need a separate mix stage.
- **Inputs**: ch0-7 (up to 8 voice signals).
- **Outputs**: ch0 (Left), ch1 (Right) — stereo copy of the mono sum.
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
    - **Hz mode**: Free-running, 0.01–20.0 Hz (default 1.0 Hz, skewed range).
    - **Sync mode**: Tempo-locked to host BPM (falls back to 120 BPM if no PlayHead). Subdivisions: 1/1, 1/2, 1/4 (default), 1/8, 1/16, 1/32.
- **Parameters**: Shape (choice), Sync (bool mode toggle), Rate Hz (float), Sync Rate (choice), Bipolar (bool, default true), Retrig (bool), Level (0.0–1.0, default 1.0), Glide (0.0–1.0, S&H only).
- **Bipolar/Unipolar**: When `Bipolar` is true, output range is −1 to +1. When false (unipolar), mapped to 0 to +1.
- **S&H Glide**: On each phase wrap a new random value is drawn; `Glide > 0` ramps to it over up to 0.5 s using `juce::LinearSmoothedValue`.
- **Retrig**: A MIDI Note On resets the phase to 0.0 when `Retrig` is enabled.
- **Output**: Single CV channel (ch0). Pushes to `VisualBuffer` for scope display.
- **Width**: SINGLE (280 px). See [docs/layout.md](layout.md).

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
- **Sync to Transport** (`syncToTransport`, bool, default **off**, TL1-8): opt-in. Off (default): behaves exactly as above — `BPM` stays authoritative and every existing preset produces a byte-identical event schedule (`AIStateMapperTest.ParamIdsGolden` and `SequencerModuleTest.LegacyScheduleIsByteIdenticalWithSyncOff` pin this). On: the module locks to the graph transport instead — tempo comes from the transport (`BPM` is ignored), the step index is a pure function of the beat (beat *B* plays step `B % 8`), note-on/off land at sample-accurate crossing offsets within the block (not the legacy 0/1-sample hack), a loop wrap fires the wrapped range's beats (e.g. the loop-start step) at `loopWrapSample + <offset>` with no double-fire or skipped beat, and a stopped transport emits one note-off for any held note and goes silent without advancing. `Run` still gates everything in both modes. The transport is read via `dynamic_cast<synth::TransportService*>(getPlayHead())`; only this app's own `AudioEngine` installs a `TransportService` as the playhead, so a foreign host (or a null playhead) falls back to the legacy free-running clock for that block instead of going silent.
- **Width**: DOUBLE (560 px). See [docs/layout.md](layout.md).

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
- **Sync to Transport** (`syncToTransport`, bool, default **off**, TL1-8): same contract as the Sequencer module above — off keeps `BPM` authoritative with a byte-identical legacy schedule; on locks the whole chord (fire and kill together) to the transport's BPM and beat-locked step index (`B % 8`), with sample-accurate crossing offsets, correct loop-wrap behaviour, and one note-off per held chord note on stop. Same `TransportService` downcast caveat: a foreign host's playhead falls back to the legacy clock for that block.
- **Width**: DOUBLE (560 px). See [docs/layout.md](layout.md).

## Sample & Hold Module
- **Source file**: `Source/Modules/SampleHoldModule.h`
- **Purpose**: Latches the value of a source signal on every clock edge and holds it until the next one — the stepped CV behind generative sequences and "R2D2" style bleeps.
- **Parameters**:
    - `Source` (choice: Input / **Random**) — sample the Signal input, or an internal white-noise generator.
    - `Mode` (choice: **Sample** / Track, param id `holdMode`) — Sample latches one value per rising edge; Track follows the source while the gate is high and freezes when it falls. The id is `holdMode` rather than `mode` because `AIStateMapper::getPatchSchema` constrains choice parameters globally by id and `LFOModule` already owns a boolean `mode`.
    - `Clock` (choice: **Internal** / External) — free-running internal oscillator, or the Trigger input.
    - `Threshold` (-1.0–1.0, default 0.5, param id `trigThreshold`) — level the Trigger input must exceed to fire. The id is `trigThreshold` rather than `threshold` because Compressor and Limiter both own a `threshold` float meaning dB.
    - `Rate` (0.1–50 Hz, default 8, skewed) — internal clock speed. Ignored when `Clock` is External.
    - `Slew` (0.0–1.0, default 0.0) — one-pole lag toward each new value, up to 0.5 s. 0 snaps instantly.
    - `Level` (0.0–1.0, default 1.0) — output scaling.
    - `Offset` (-1.0–1.0, default 0.0) — output offset; use +0.5 with Level 0.5 for a unipolar 0–1 CV.
- **Output**: bipolar CV on ch0, clamped to [-1, 1].
- **Why `Source`/`Clock` are explicit choices**: the module deliberately does *not* infer "is anything patched in?" from channel activity. A gate signal sits at 0 most of the time and a slow LFO crosses zero, so activity detection misfires. Defaults (Internal clock + Random source) make the module produce stepped random CV the moment it is dropped on the canvas, with nothing patched.
- **Trigger detection**: a Schmitt trigger. It arms when the Trigger input rises above `Threshold` and only re-arms once the signal falls a fixed `kTriggerHysteresis` (0.05) *below* it. Gate state is carried across block boundaries — a gate that stays high spanning two blocks is one edge, not two. The hysteresis is deliberately not user-exposed (see the Module Development Guide on not crowding modules with knobs); without it, any signal loitering near the threshold — a slow sine, anything with dither on it — would retrigger every sample.
- **Trigger meter**: `Source/UI/TriggerMeterComponent.h` draws the live Trigger level as a bipolar bar with a marker at the effective threshold, so the threshold can be set by eye against the real signal. The module publishes `getTriggerLevel()` / `getEffectiveThreshold()` / `isTriggerHigh()` / `getTriggerCount()` as atomics for it. The meter is tracked **whichever clock is selected**, so the threshold can be dialled in before switching to External.
- **CV inputs**: `Rate` maps raw CV exponentially over ±4 octaves (per the Module Development Guide convention); `Slew`, `Level` and `Offset` are additive over their native ranges. Only these four jacks are auto-promotable mod targets — Signal and Trigger connections stay direct rather than being wrapped in an attenuverter.
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

`Source/Modules/TimelineMidiSourceModule.h` — TL3-1. One node per timeline MIDI track: it turns that track's notes into MIDI events, so everything downstream (Poly MIDI → oscillators → FX) is an ordinary patch that neither knows nor cares that a timeline exists.

- **Ports**: none. 0 audio in, 0 audio out, MIDI **out** only (`acceptsMidi() == false`, `producesMidi() == true`). It **replaces** the graph-supplied `midiMessages` buffer rather than appending to it — the `ExternalMidiModule` contract for a source.
- **Parameters**: none beyond the inherited `bypassed`. There is nothing to tune: what it plays is the timeline's business.
- **Pull, not push.** Nothing schedules events into it. Every block it downcasts `getPlayHead()` to `synth::TransportService`, reads `getCurrentBlockInfo()` and `getCurrentTimelineSnapshot()` (see [docs/architecture.md §2/§4](architecture.md)), and emits the note edges falling inside this block's beat range. There is no per-module state to keep in sync with the document, so a locate, a tempo change or an edit to the notes takes effect on the very next block with no invalidation step.
- **Binding is by node uuid.** It finds "its" track by scanning the snapshot for the first **MIDI** track whose `bindingUuid` `strcmp`s equal to `ModuleBase::getNodeUuid()` (the audio-safe mirror of the graph node's `"uuid"` property — see architecture.md §6). A module with **no** uuid matches nothing: an unsaved node has no identity yet, and `""` would otherwise adopt every unbound track in the document.
- **Mute/solo**: the track is silent when `muted`, or when `snapshot.anySoloed && !track.soloed`. `anySoloed` is precomputed by `TimelineSnapshot::buildFrom` so this costs one branch, not a rescan.
- **Sample-accurate offsets**: a note edge at absolute beat `b` lands at `jlimit(0, numSamples-1, llround((b - startPpq) / info.beatsPerSample()))`. At 48 kHz / 512 / 120 BPM one beat is 24000 samples, so beat 1.0 is offset 448 of block 46 — pinned exactly by `Tests/TimelineMidiSourceTests.cpp`.
- **Held-note hygiene — a note-on is a promise to emit the matching note-off.** Anything that could break that promise releases **every** held note as note-offs at **sample 0** of the block it happened in, before anything else: the transport stopping, a block-start discontinuity (`info.blockStartSample != expectedNextBlockStart` — a locate, a loop wrap, a host repositioning us), the module being bypassed, the bound track disappearing or being unbound, and mute/solo suppression turning on. Continuity is only tracked *while playing*, since a stopped transport republishes the same start sample every block.
- **Bypass** is the two-branch contract for a pure source with no dry audio path: the **first** bypassed block still emits the pending note-offs, then clears and returns; subsequent bypassed blocks just clear and return.
- **No allocation on the audio path.** Held notes live in a fixed `kMaxActiveNotes = 128` array of `{pitch, channel, endBeat}`. On overflow the **note-on is dropped** — never a resize, and never a note-off for something already sounding.
- **Internal-only.** Not in the module library, not in the replace-with menu, not AI-authorable: it is in `kNonAuthorableModuleTypes`, and `validatePatch` on the untrusted path rejects it outright with `PatchValidationError::InternalModuleNotAllowed` (see [docs/AI_Engine.md](AI_Engine.md)). It **is** in the factory, gated on `SYNTH_ENABLE_TIMELINE`, purely so our own saves round-trip it. The timeline's add-track flow (TL5-3) is the only thing that creates one.
- **TODO(TL3-2)**: only the block's *primary* beat range is emitted — a block that wraps stops at `loopEndPpq` and the post-wrap remainder is dropped (nothing hangs, because the wrap is itself a discontinuity and flushes). TL3-2 adds the second range plus boundary fuzz.
- **Downstream**: see [Poly note contract](#poly-note-contract-machine-midi) above — machine-generated MIDI repeats pitches inside an envelope's attack constantly, and that contract is what makes each repeat produce a real gate edge.

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

This table shows the raw `AudioProcessorGraph` channel assignments for each poly-capable module. See [docs/modulation.md](modulation.md) for how `getModulationRoutings()` and the logical-port API collapse these into visible wires.

### Rule for new poly modules

In poly mode, voices occupy channels 0-7 (audio/pitch/gate) and the shared-CV block starts at channel 8. **Declare `numOutputs >= highest CV input channel index you read**, to avoid JUCE `AudioProcessorGraph` buffer aliasing when `inputChan >= getTotalNumOutputChannels()`.

Declare your per-voice **output** fan in `mapOutputChannel()` if the module actually emits one signal per voice — Oscillator, Filter and Noise all fan raw ch0-7 onto a single Audio jack this way. Don't, if the module sums voices down instead: VCA has no `mapOutputChannel()` override because its poly mode sums all 8 voices to stereo on ch0-1, a plain jack rather than a fan. Getting this wrong misdirects `GraphEditor::resolvePolyLink` (see [docs/modulation.md](modulation.md#creating-poly-connections)) into fanning a dragged cable out of an output that only ever produces one signal.

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
| **Oscillator (poly)** | ch0-7 | Out | Per-voice audio |
| **Oscillator (poly)** | ch8-13 | Out | Silent pass-throughs (prevent buffer aliasing) |
| **Oscillator (mono)** | ch0 | In/Out | Pitch CV in / Audio out (shared channel, CV saved before clear) |
| **Oscillator (mono)** | ch1 | In | Waveform CV |
| **Oscillator (mono)** | ch2 | In | Octave CV |
| **Oscillator (mono)** | ch3 | In | Coarse CV |
| **Oscillator (mono)** | ch4 | In | Fine CV |
| **Oscillator (mono)** | ch5 | In | Level CV |
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
| **Filter (poly)** | ch0-7 | In | Per-voice audio |
| **Filter (poly)** | ch8 | In | Shared Cutoff CV |
| **Filter (poly)** | ch9 | In | Shared Resonance CV |
| **Filter (poly)** | ch10 | In | Shared Drive CV |
| **Filter (poly)** | ch0-7 | Out | Filtered audio per voice |
| **Filter (mono)** | ch0 | In/Out | Audio in / filtered audio out |
| **Filter (mono)** | ch1 | In | Cutoff CV |
| **Filter (mono)** | ch2 | In | Resonance CV |
| **Filter (mono)** | ch3 | In | Drive CV |
| **VCA (poly)** | ch0-7 | In | Per-voice audio |
| **VCA (poly)** | ch8-15 | In | Per-voice envelope/CV |
| **VCA (poly)** | ch0-1 | Out | Stereo sum (L/R) |
| **ADSR (poly)** | ch0-7 | In | Per-voice gate CV |
| **ADSR (poly)** | ch0-7 | Out | Per-voice envelope (0–1) |
| **Sample & Hold** | ch0 | In/Out | Signal in / held CV out (shared channel; read before overwrite) |
| **Sample & Hold** | ch1 | In | Trigger / gate |
| **Sample & Hold** | ch2 | In | Rate CV |
| **Sample & Hold** | ch3 | In | Slew CV |
| **Sample & Hold** | ch4 | In | Level CV |
| **Sample & Hold** | ch5 | In | Offset CV |
| **Sample & Hold** | ch6 | In | Threshold CV |
| **Sample & Hold** | ch1-6 | Out | Silent (cleared each block so CV does not leak downstream) |

---

## Modulation System

Agent Synth uses a **hidden Attenuverter architecture** for modulation depth control, inspired by Serum's mod matrix. The engine derives a first-class `ModulationRouting` model from the graph at runtime. See [docs/modulation.md](modulation.md) for the full reference.

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
