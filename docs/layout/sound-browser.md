# The sound browser

One place to find a recording: the **sound index** (Core, no UI) lists every sound in the installed packs and the user's own folders, and the **Sounds view** (the left sidebar's second view) browses and searches it. Previewing, dragging a sound onto a module or track, filters and downloads arrive in later steps; this doc covers the index and the view as built.

## The sound index

`synth::soundbrowser::SoundIndex` (`Source/SoundBrowser/`, part of Core) owns one long-lived scan thread. Nothing it does runs on the message thread.

- **Roots**: every installed sound pack folder (`SoundPackLibrary::roots()`; a pack is a folder with a `pack.json` that has an id, and a repeated id is listed once) and each user folder passed to `setUserFolders`. The pack folders' own audio and `.sfz` files are the entries; there is no separate list for packs you have not downloaded yet.
- **Entry** (`SoundEntry`): path, place (pack id and name, or `folder:<path>` and the folder's name), the folder inside the place, size, length, sample rate, kind (`OneShot`, `Loop` or `Instrument` for `.sfz`), tags, tempo (0 = unknown), key (empty = unknown) and modification time.
- **Extensions**: `wav`, `aif`, `aiff`, `flac`, `ogg`, `mp3`, plus `.sfz` instruments. A file whose header no format reads is left out.
- **Headers only**: listing opens each file's reader (format header and metadata chunks) and never reads a sample, so a WAV whose audio is damaged still lists, with the length its header declares. A WAV `acid` chunk gives the tempo (or beats over length), the root note and the one-shot flag; the flag beats a "loop" in the file name.
- **Guessing from names** (`SoundNameGuess`): tag words (kick, snare, hat, clap, tom, cymbal, perc, drum become Drums; bass; keys, piano, rhodes; vox, vocal; fx; string) from the folder and file names; tempo from `96bpm`, `120 BPM` or a bare number between two other words (`dusty_120_Am`), 60 to 200 only; key from `Am`, `C#min`, `Ebm`, `F major`. A sound is a loop when its tempo is known or its name or folder says loop, else a one-shot.
- **Cache**: `<settings folder>/SoundBrowser/index.json`, loaded before the first scan so the list is there at once, then rescanned incrementally: a file whose size and modification time match its cached entry is not opened again. The index holds at most 50,000 entries; past that the scan stops and the snapshot says `truncated`.
- **Reading**: `snapshot()` returns an immutable `std::shared_ptr<const SoundSnapshot>`, swapped in under a mutex, so a reader never blocks the scan. `search(query, filter)` ranks name matches first (then folder, place and tag matches, then by name) with the same word rules as `Layout/SearchMatch.h` (a Core copy, because Core cannot use the UI's; a test keeps the two equal). `childrenOf(place, folder)` returns the immediate subfolders (with a sound count) and sounds. `find(path)` looks one entry up.
- **Listener**: `SoundIndex::Listener::soundIndexChanged()` is called on the message thread (an `AsyncUpdater`) after the cache is shown, as the scan progresses and when it ends.
- **Favourites and Used recently**: `SoundLists` holds two path lists (the recent one capped at 50, newest first). Core never touches the settings file, so the view's owner round-trips them through `soundBrowserFavourites` and `soundBrowserRecent`.

## Settings keys

| Key | Meaning |
| --- | --- |
| `librarySidebarVisible` | the left column is open (existing) |
| `librarySidebarView` | `modules` (default) or `sounds`: the view the column shows, and the view it reopens on |
| `soundBrowserWidth` | the column's width while Sounds shows; default 280, clamped 200 to 400 |
| `soundBrowserFolders` | the user's own sound folders, one absolute path per line |
| `soundBrowserFavourites`, `soundBrowserRecent` | file paths, one per line |
