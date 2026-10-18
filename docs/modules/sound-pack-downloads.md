# Sound pack downloads

Sound packs are optional downloads, not part of the app. The Sampler reads them through `SoundPackLibrary` (see [`sampler-instruments.md`](sampler-instruments.md#sound-packs)); this page covers how they get onto the machine.

## The hosted list

`kSoundPackIndexUrl` (`Branding.h`) points at `index.json` in the `agentsynth-sound-packs` repository:

```json
{"format": 1, "packs": [{"id": "core", "name": "Core instruments", "version": "1.0.0", "url": "https://.../core.zip",
  "sha256": "...", "downloadBytes": 32548439, "installedBytes": 35913032, "licence": "CC0 1.0 (public domain)",
  "licenceUrl": "https://...", "credit": "...", "instruments": [{"id": "upright-piano", "name": "Upright piano", "category": "Keys"}]}]}
```

`SoundPackIndex::parse` rejects an unknown `format` with a plain-words message and skips (counts) any single pack with an unsafe id (`[a-z0-9_-]`, at most 64), a non-https url, a checksum that is not 64 hex digits, or missing sizes, so one bad entry never hides the rest. A licence link that is not https is dropped.

## The installer

`SoundPackInstaller` (`Source/SoundPacks/`, part of Core) is one object per app: `MainComponent` holds a `juce::SharedResourcePointer` to it and the Sampler's offer takes another, so a download started anywhere shows everywhere. Message thread only; it listens through `Listener::soundPacksChanged`.

`install(id)` runs `runInstall` on a worker thread (`SoundPackInstallWork.cpp`); the worker touches only a shared `InstallJob`, never the installer, and results hop back with `callAsync`. The steps:

1. Check free disk space against download plus installed size.
2. Download into `<SoundPacks>/.staging-<id>/pack.zip` (`juce::URL`, redirects followed, progress and cancel per chunk).
3. SHA-256 against the list (`SoundPackChecksum`, our own implementation checked against the FIPS vectors; `juce_cryptography` is not linked).
4. Validate every zip entry before extracting: relative, inside `<id>/`, no `..`, backslash or drive letter, `<id>/pack.json` present, unpacked size within twice the listed size plus 8 MB.
5. Unpack into the staging folder, check `pack.json` has the expected id and a version.
6. Move the folder into `<SoundPacks>/<id>`, parking an older version first and restoring it if the move fails.

The staging folder is always deleted, so any failure or cancel leaves either the old pack or nothing; the message for each failure is plain words (offline, 404, checksum, bad zip, disk full). `SoundPackLibrary` rescans the folders on every lookup, so listeners are the only "rescan" needed.

A download is restarted from the beginning after an interruption; HTTP Range resume was left out (packs are tens of megabytes, and a restart keeps the checksum step simple).

`remove(id)` deletes the pack folder, only for packs under the user folder; bundled packs are refused. An installed version below the listed one shows as "Update available".

## Settings > Sound packs

`SoundPacksSettingsTab` (`Source/UI/Settings/SoundPacks/`) is the last Settings tab. It fetches the list each time the tab is shown. One `SoundPackRow` per pack (hosted packs, then installed packs the list does not know): name, instruments, size, state, and Download / Cancel (with a bar) / Update / Remove buttons that fade where they stand. Remove asks first. Offline, the line "Couldn't reach the pack list." and a Retry link show while installed packs stay listed. Below the rows, Credits lists every pack (including CC0 ones): credit text, licence, "View licence" and, for an installed pack with `LICENSES/SOURCES.md`, "Show sources".

Accessibility: every button and link is a Tab stop with a name and tooltip naming the pack; a row reads "<pack>, downloading, 42 percent"; the bar's name carries the percentage.

## Song builds and the core instruments

A song plan may name core instruments (see [song steps](../ai/song-steps.md#real-instruments)). While the pack is missing:

- **The plan card** (`SongPlanCard`, `SongPlanCardSounds.cpp`) adds a line, "Uses real instruments: ... Download the core instruments (33 MB) first?" (the size comes from the loaded list and is left out when the list is not loaded). The Build button reads **Download first**: it starts the shared installer's `install("core")`, the line turns into the pack's progress text with a bar, and the song is built as soon as the pack is installed (`SongSession::buildAfterDownload` carries the intent across the chat rebuilding its bubbles). **Continue with synths** builds at once. A failed download says "Couldn't download the instruments." and keeps both choices. With the pack installed, or no sounds in the plan, the card is as before.
- **The build** makes the stand-in synth and stores `pendingSound` (e.g. `core/flute`) on the module. It is kept in the module's `CardViewState` (`Modules/PendingSound.h`), so it is saved in the node's `cardView` key, restored on the trusted path only, never sent to a model, and survives a save and an undo.
- **The swap** (`CoreSoundSwapper`, owned by `MainComponent`) listens to the shared installer. When the core pack becomes installed, and once when a project finishes loading, it replaces every module whose `pendingSound` the pack provides with a Sampler in Instrument mode loaded with that instrument, through the same code as Replace with (`GraphEditor::replaceModule`, which keeps cables, macro membership and the track), inside one `AppUndoManager::ScopedUndoGroup`: one undo puts all the stand-ins back, `pendingSound` included. A Wavetable stand-in's ADSR and VCA stay in the chain after the swap (the Sampler already shapes its notes, so they add a second, gentle envelope); a song built with the pack installed has the same chain, so the two end up alike. The stand-in's Level goes to the Sampler too, and so does the sound's level trim ([Levels](#levels)). A toast says "N tracks now play the real instruments", with Undo. A Drum Kit that names the core kit but has none loaded gets its kit loaded at the same moment (not part of the undo step; the kit reference is unchanged).

## Levels

The recordings are mastered quiet next to the synths, and a Sampler sums its voices with -9 dB of headroom, so a Sampler at the same Level as its stand-in played about 10 dB lower. Each melodic sound in the table (`CoreSounds.cpp`, `trimDb`) has a trim in dB, added to every recording's volume when the core instrument becomes a Sampler (`InstrumentSource::gainDb`, saved as `instrumentGainDb` only when not 0, so a hand-picked instrument and every saved project play as before). It is applied at the build with the pack installed and in the swap after the download (`loadCoreSoundIntoSampler`); the Drum Kit has none.

Measured offline with the real core pack (`CoreSoundLevelTests.cpp`, run with `AGENTSYNTH_CORE_PACK_DIR` set to the folder holding the unpacked `core`; skipped otherwise). A mid-velocity phrase (velocity 90; a C3-C4-E4-G4 chord, or a single E2 for the bass and the guitar) through the stand-in with the params the song planner usually sends (its Level too), and through a Sampler at the same Level with every other param at its default. RMS covers the first second of a held sound and the first quarter second of a struck or plucked one (piano, bass, guitar), in dBFS:

| Sound | Stand-in RMS | Sampler RMS, no trim | Trim | Sampler RMS, trimmed | Sampler peak, trimmed |
| :-- | --: | --: | --: | --: | --: |
| Upright piano | -20.3 | -32.9 | +12.5 | -20.4 | -8.5 |
| String section | -17.0 | -23.9 | +7.0 | -16.9 | -4.9 |
| Trumpet | -23.5 | -32.5 | +9.0 | -23.5 | -7.6 |
| French horn | -23.5 | -29.7 | +6.0 | -23.7 | -11.6 |
| Flute | -21.9 | -27.0 | +5.0 | -22.0 | -11.5 |
| Clarinet | -21.9 | -26.5 | +4.5 | -22.0 | -9.9 |
| Plucked double bass | -39.5 | -39.6 | 0 | -39.6 | -34.1 |
| Electric guitar | -40.0 | -38.0 | 0 | -38.0 | -25.5 |

The trim is the RMS gap, rounded to half a decibel. The two plucked sounds need none: the Pluck stand-in at the Level the planner sends (0.3) is just as quiet, and a negative trim would make the recordings quieter still. Their level is the planner's choice. A very large chord at full velocity can still reach the Sampler's soft limiter.

## Tests

No test reaches the network: the installer takes a `Fetcher`, an index URL and an install root, and `setRunInlineForTesting` runs its worker steps on the calling thread. `Tests/SoundPacks/SoundPackTestKit.h` builds small zips with `juce::ZipFile::Builder` and serves them from a fake. The test binary also blocks the real fetcher (`setHttpBlockedForTesting`).

`Tests/SoundPacks/CoreSoundLevelTests.cpp` renders the table above against the real pack (skipped without `AGENTSYNTH_CORE_PACK_DIR`); `ChannelFlowCoreSoundsTests.cpp` checks with a fake pack that a recorded song answer builds with the pack installed, with the same modules as without it, and that the trim is applied at the build and in the swap.
