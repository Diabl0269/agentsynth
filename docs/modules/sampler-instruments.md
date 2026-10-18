# Sampler Instrument mode

The Sampler's third `playMode`, `Instrument`: a polyphonic, MIDI-only player for a **multi-sample instrument** (many recordings mapped over the keyboard and the velocity range). The other two modes (`Sample`, `Granular`) are described in [`modules.md`](modules.md#sampler-module). Code: `Source/Modules/Sampler/` (engine: `SamplerInstrument`, `SamplerVoicePool`, `SfzReader`, `SamplerNoteNames`; module side: `SamplerInstrumentPlayer`, `SamplerInstrumentSlot`, `SoundPackLibrary`), wired into `SamplerModule`.

## What plays

- **Regions.** An instrument is a list of regions: a recording, a key range with a root key, a velocity range, and optional loop, tune, volume and release settings. A note plays every region whose ranges contain it (at most four per note); the sample is repitched from the root key by `2^((note - root + tune/100 + Pitch + Fine/100 + CV) / 12)`.
- **Velocity layers and crossfade.** Layers that meet at a velocity border crossfade over 8 velocity steps on each side with an equal-power curve, so a note near the border blends both layers instead of jumping. A range edge with no neighbouring layer is a hard edge.
- **Round robin.** Regions with `seq_length` / `seq_position` take turns: a counter advances on every press and a region plays when its position is the current slot.
- **Release sounds.** A region marked `trigger=release` plays when the key comes up, at the velocity of the press, when the `releaseSound` parameter is on (default on). Release-trigger voices never steal sounding notes.
- **Sustain pedal.** CC 64 at 64 or above holds released notes (they keep sounding at their sustain level) until it lifts; `loop_sustain` regions keep looping while held. One-shot regions ignore key and pedal release and play to the end.
- **Voices.** `voices` (1-32, default 16) caps the notes that are not already releasing. At the cap the oldest note gets a 5 ms fade and the new one takes a free slot, so stealing never clicks. The same note struck again releases its earlier voice over 30 ms.
- **Envelope.** `attack` (1 ms-2 s, default 2 ms) and `release` (10 ms-8 s, default 0.6 s) shape every voice; a region's own `ampeg_release` wins over the knob. Both are skewed so the short end has resolution.
- **Tone and Feel.** `tone` (0-1, default 1 = open) is a one-pole low-pass per voice from 200 Hz to 20 kHz. `feel` (0-1, default 0.7) is how much velocity shapes level (`(1 - feel) + feel * velocity^1.6`) and brightness (the cutoff moves by up to one octave either way). Neither can raise the output above the unity-gain case: tone only removes level, and the Feel factor is at most 1.
- **Pitch.** `pitch`, `fine` and the Pitch CV (ch1, 24 semitones per unit) and Fine CV (ch9, one semitone per unit) transpose every voice live, read once per block. `rootNote` and the Trigger input (ch0) are not used: Instrument mode plays from MIDI only. `level` and the Level CV (ch6) scale the output as in Sample mode.
- **Output.** The sum is scaled by Level and then rounded only above 0.9 by a soft limiter (like Pluck), so one note never changes while a loud chord stays under full scale. A hard clamp would distort chords.
- **Bypass, mute, mode switch.** Bypass, mute and leaving Instrument mode each clear the output and stop every voice at once (the module has no dry path, so both branches clear). Loading an instrument never makes a sound by itself.

## The SFZ subset

`SfzReader` reads the part of SFZ that free sample packs use: `<control>` (`default_path`), `<global>`, `<master>`, `<group>` and `<region>` with `sample`, `lokey`, `hikey`, `key`, `pitch_keycenter`, `lovel`, `hivel`, `tune`, `transpose`, `volume`, `loop_mode` (`no_loop`, `one_shot`, `loop_continuous`, `loop_sustain`), `loop_start`, `loop_end`, `seq_length`, `seq_position`, `trigger` (`attack`, `release`) and `ampeg_release`, plus `#define`. Anything else is reported once as a warning and ignored; `#include` is skipped with a warning.

- Opcodes inherit down the headers; a more specific header wins, and within one header the later opcode wins. A `key=` sets `lokey`, `hikey` and `pitch_keycenter` together and replaces ranges inherited from above.
- **Note names.** In an SFZ, C4 is MIDI 60. The app displays and names notes with C3 = 60, so a dropped file called `piano_C3.wav` lands on 60. A plain integer is a MIDI number in both.
- Sample paths resolve against the SFZ's folder (and `default_path`); backslashes are accepted.
- Parse warnings come back in the load messages, prefixed `warning: `, next to real errors (a sample that could not be read drops its region).

## Dropped files

`placeDroppedFiles` lays the files out, in name order:

- A note in the file name (`C3`, `c#4`, `Db2`, glued like `kalimbaC2`) is the root key. Each root covers the keys from halfway to the previous root to halfway to the next one; the lowest extends down to 0 and the highest up to 127.
- Several files on one root become velocity layers, split evenly over 1-127. For two files, `soft`/`pp`/`quiet`/`v1` and `loud`/`ff`/`hard`/`v2+` in the name decide which is which.
- Files without a note take one key each, upward from just above the highest named root (a lone unnamed file covers the whole keyboard at root 60).
- The instrument is named after the common start of the file names, or `My instrument`.

## What is saved

Instrument mode saves exactly one source next to the Sampler's `sampleFile` (both can be held; the mode picks which plays). Keys are in `Source/Modules/ModuleFileStateKeys.h`:

| Source | Keys |
| :-- | :-- |
| SFZ | `instrumentFolder` (absolute folder of the `.sfz`) and `instrumentSfz` (the `.sfz` path relative to that folder, `/` separators) |
| Sound pack | `instrumentPack` = `<packId>/<instrumentId>`; no path is saved, the pack library resolves it at load |
| Dropped files | `instrumentFiles` (array of absolute paths) and `instrumentRegions` (array of `{file, lokey, hikey, key, lovel, hivel}`; `file` indexes `instrumentFiles`, `key` is the root) |

`instrumentFolder` and `instrumentFiles` are registered in `synth::ModuleFileRefs`: inside a saved project they become bundle-relative refs, and Collect & Archive copies the SFZ folder tree (the `.sfz` files and audio below it) into `Instruments/` and the dropped files into `Samples/` (see [`project-bundle.md`](../architecture/project-bundle.md#projectbundle-agsproj)). Samples an SFZ names outside its own folder are not collected. A list element that is missing or escapes the bundle becomes empty, so the indices in `instrumentRegions` stay valid; regions pointing at an empty element are skipped.

Restoring state decodes through `synth::DeferredAssetLoads` while a project opens on screen, like a sample: the module keeps the pending source in its state, a load serial stops a stale decode from overwriting a newer choice, and a missing pack or file leaves the module without an instrument. Only the trusted path restores it; an AI patch can select the mode (`playMode: "Instrument"`) but never reaches `setExtraState`.

## Sound packs

`SoundPackLibrary` finds instruments in pack folders. A pack is `<root>/<packId>/pack.json`:

```json
{"id": "keys", "name": "Keys", "version": "1", "licence": "CC0", "credit": "...",
 "instruments": [{"id": "grand", "name": "Grand Piano", "sfz": "grand/grand.sfz", "category": "Keys"}]}
```

`sfz` is relative to the pack folder and must stay inside it. Roots, in order: the app's bundled packs (macOS: `<app>/Contents/Resources/SoundPacks`; elsewhere a `SoundPacks` folder beside the executable), then the user's `<user app data>/Agent Synth/SoundPacks`. A later root never overrides an earlier one for the same pack id. A malformed `pack.json` or entry is skipped with a log line. Tests redirect the roots with `SoundPackLibrary::setRootsForTests`.

## The card

`SamplerChrome` (`Source/UI/Graph/ModuleComponent/SamplerChrome/`) owns everything the Sampler card draws above its knobs. In Sample and Granular mode that is the waveform and the Load Sample row, exactly as before. In Instrument mode two sets of components share the same 112 px and cross-fade when the mode changes (`FadeVisibility`; a card that is not on screen switches at once), so the card never changes height:

- **Instrument picker.** The instrument name with a dim "built in" or "your files" and a caret ("Choose an instrument" when none is loaded). It is a Tab stop; click, Return or Space open a searchable list (`ModMatrixPicker` in a call-out, the shared picker; the list eases in out of the picker, and the caret turns while it is open). The search field has focus, typing filters, Up/Down move, Return picks, Escape closes. Rows: **Built in** (every `SoundPackLibrary::listInstruments()` entry, pack name dim under it; one disabled row "No instruments installed yet" when there are none), then **Your files**: "Load an instrument file (.sfz)..." (async chooser, `*.sfz`) and "Load recordings..." (async chooser, several audio files). New groups (the sound-packs ticket's Downloaded and Get more) join in `collectInstrumentChoices`. Accessible name "Instrument, <name>" (or "Instrument, none"), tooltip "Choose an instrument (Enter)".
- **Key strip.** A caption ("<N> recordings · soft and loud", or "· one layer") over a small keyboard across the instrument's key span, with one band above it per recording: the loud layer in the accent colour on the upper half, the soft layer in a darker accent on the lower half, a recording covering the whole velocity range at full height. Release-sound recordings add no band. The notes sounding (`getSoundingNotes()`) are lit on the keys. The card's existing gated 15 Hz tick polls the strip (`SamplerChrome::poll`): it re-reads the notes only while one is lit or the module is playing and repaints only when the set changes, so a quiet card costs one comparison. The strip is not a Tab stop (it does nothing when activated); it has the accessible name "<N> recordings, soft and loud" and a tooltip.
- **Knobs.** Mode, then Attack, Release, Level; Pitch, Fine; Tone, Feel, Voices; footer toggle Release Sound. Start, End, Root Note, the grain trio, Loop and Reverse are hidden in this mode, and the Instrument-only controls are hidden in the other two (layout: [module-card-layout.md](../layout/module-card-layout.md#default-layouts)). The five Instrument knobs take a CV cable on their knob only (no gutter jack): Attack CV ch10, Release CV ch11, Tone CV ch12, Feel CV ch13, Voices CV ch14, appended after Fine CV so saved routings keep their jacks. Each is read once per block over the knob's own span (CV 1.0 moves the knob from where it sits to its maximum, like every other CV in the app), and only in Instrument mode.
- **One undo step.** Every load from the card (a pick, a chooser, a drop) runs between `AppUndoManager::captureBeforeState` and `pushSnapshotFromCapture`, so Cmd+Z restores the previous instrument and the mode together in one step, and a load that changed nothing records nothing.
- **Messages.** A load that failed or warned puts its first line on the caption (the full list in its tooltip), the way a sample that could not be read shows on the file-name label. No dialog.
- **File drop.** On the card, one audio file is a Sample load as before; two or more audio files become an instrument (`loadInstrumentFromFiles`, sorted by name) and switch to Instrument mode; an `.sfz` loads as an SFZ. While two or more audio files are dragged over the card the drop highlight carries the hint "<N> files: placed by the notes in their names". Dropping on the canvas still makes one Sampler per file.

## Threading

The message thread decodes and publishes an `Instrument` under a `SpinLock`; the audio thread takes a try-lock once per block and renders silence on contention. Replaced instruments stay in a message-thread array until the audio thread has let go of them, so no destructor runs on the audio thread. The notes sounding at the end of each block are published as two atomic 64-bit words for the UI key strip (`SamplerModule::getSoundingNotes`).
