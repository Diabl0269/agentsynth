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

## Tests

No test reaches the network: the installer takes a `Fetcher`, an index URL and an install root, and `setRunInlineForTesting` runs its worker steps on the calling thread. `Tests/SoundPacks/SoundPackTestKit.h` builds small zips with `juce::ZipFile::Builder` and serves them from a fake. The test binary also blocks the real fetcher (`setHttpBlockedForTesting`).
