# The sound browser

One place to find a recording: the **sound index** (Core, no UI) lists every sound in the installed packs and the user's own folders, and the **Sounds view** (the left sidebar's second view) browses and searches it. Dragging a sound onto a module or track and downloads arrive in later steps; this doc covers the index, the view, its filters and the preview as built.

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

## The Sounds view

The left column holds two views behind a **Modules / Sounds** switch at its top (`LibrarySidebarViews`, `Source/UI/Library/`). The switch is one Tab stop; Left and Right change view, and each segment's tooltip names its shortcut. The column is the "library" focus region's root.

- **Screens**: Places (Favourites, Used recently, each installed pack, then "Your folders"), then folders inside a place, then the sounds. A path bar above the list shows where you are; every part but the last is a button that goes back there. A search field above it searches every place at once and shows sounds only.
- **Keys** (list focused): Up, Down, PageUp, PageDown select; Right or Return opens a folder; Left or Cmd+Up goes up; Return on a sound activates it; Space plays or stops its preview; F adds or removes it from Favourites; a printable key jumps to the search field. In the search field Down or Return moves into the list and Esc clears the text, then returns to the list.
- **Screen reader**: the rows are painted, and ONE list element speaks the selected row ("snare, one-shot, 0.8 seconds, 2 of 23"), the same pattern as the module library. The wording lives in `SoundBrowserText` and is unit-tested.
- **Footer**: the selected sound's name and place, then the preview strip (see [Preview](#preview)).
- **Width**: while Sounds shows, the column is as wide as `soundBrowserWidth` (default 280, 200 to 400). Drag the right edge, double-click it to reset, or focus it and use Left and Right (16 px a step). The width eases between the Modules width and the Sounds width while the views slide.
- **Motion**: switching views slides 190 ms (a cross-fade under reduced motion); opening a folder slides the list in, going up slides it out.
- **Lazy start**: the index starts its first scan when Sounds is first shown, and rescans when the installed packs change.

## Filters

A **Filters** button sits beside the search field and reads "Filters" or "Filters (2)". Space or Return opens or closes the panel, Esc closes it, and a screen reader hears "Filters, 2 on, collapsed" (or expanded). The tooltip is "Narrow by kind, sound, tempo, key, length or place".

- **The panel opens inline** under the search and pushes the list down; it never covers it. It grows open over 190 ms with the list sliding (an 80 ms fade under Reduce Motion, instant with Animations off). If the view is short the panel takes at most what leaves 96 px of list.
- **Groups** (`SoundFilterPanel`): Kind (One-shots, Loops, Instruments), Sound (Drums, Bass, Keys, Vocals, FX, Strings), Tempo (a two-handle range slider, 60 to 200 BPM, plus a "Near song tempo" chip), Key (a "Song key" chip when the project has a key, and a menu of the 24 keys), Length (Any, Under 1 s, 1-4 s, Over 4 s) and Place (any pack or folder). Tab moves group to group; Left and Right move across the chips of a group; Space toggles one. Within Kind, and within Sound, any one may match; the groups themselves all have to.
- **Near song tempo** keeps sounds within 10 BPM of the transport's tempo, or of half or double it. A sound with an unknown tempo or key never passes an active tempo or key filter.
- **Chips**: one row of small removable chips under the search ("Loops x", "90-100 BPM x"), then "Clear all" at the right. The row is one Tab stop (Left and Right move, Space, Delete or Backspace remove). A chip's tooltip is "Remove Loops filter". Removing a chip or Clear all switches the same filter off in the panel. A new chip grows in at the end; a removed one shrinks out and the rest slide along (190 ms; an 80 ms fade under Reduce Motion).
- **What it filters**: search results, folder listings (a folder with no passing sound is left out and its count is of passing sounds), Favourites and Used recently. At the Places screen an active filter shows the matching sounds from every place as a flat list. When nothing passes, the list says so.
- **Code**: the filter is Core (`SoundFilter` in `SoundEntry.h`, `soundPassesFilter` and `parseSoundKey` in `SoundSearch.h`, `SoundIndex::search` and `childrenOf` take it); `SoundFilterModel` holds the switches and makes the chips. The range slider is the reusable `Source/UI/Controls/RangeSlider`: each handle is its own Tab stop ("Tempo, lower handle, 90 BPM"), arrows move it one step (Shift or Page keys ten, Home and End to the limit), the handles never cross, and a click on the track moves the nearer one.

## Preview

Selecting a sound plays it when **Auto-play** is on (the default). The next selection cuts the old sound with a 5 ms fade. Space on a sound plays it, or stops it when it sounds; with Auto-play off, selecting is silent and Space plays. A Loop starts on the next bar when the song plays and at once when it does not; loops are not time-stretched. Instruments, folders and files that are gone never play. Hiding the view stops the preview.

- **Footer** (`SoundPreviewFooter`): the sound's waveform (drawn in left to right over 190 ms, an 80 ms fade under Reduce Motion) with a moving playhead while it plays, a Stop button ("Stop the preview (Space)"), a volume slider and the Auto-play switch (tooltip "Play a sound when you select it"). The waveform comes from a `juce::AudioThumbnail`, so decoding stays off the message thread.
- **Volume**: -60 to +6 dB, default -6, arrows nudge 1 dB, spoken as "Preview volume, -6 dB", tooltip "Preview volume, only for previews". It sets the preview's own gain only.
- **Playhead**: the moving line repaints a strip a few pixels wide at 30 Hz while the preview plays, and moves under Reduce Motion (it shows a position). It is a permitted exception in [animation](animation.md#the-time-bounded-animation-rule).
- **Audio**: the preview is summed after the master and never goes through the graph, so the song's volume, mute and effects do not touch it, a bounce never contains it, and plugin mode mixes it into the plugin's output. See [audio-engine](../architecture/audio-engine.md#sound-preview).

## Shortcuts

| Key | Action |
| --- | --- |
| Cmd+B (`toggleLibrary`) | closed: open on Modules; Sounds showing: switch to Modules; Modules showing: close |
| Cmd+Opt+B (`toggleSoundBrowser`) | the same for Sounds |
| `focusLibrary`, `focusLibrarySearch` | act on the view showing; a closed column opens on the view it last showed |

The toolbar's library button opens or closes the column on the last-used view, and its tooltip names both shortcuts. The current view persists under `librarySidebarView`.

## Settings keys

| Key | Meaning |
| --- | --- |
| `librarySidebarVisible` | the left column is open (existing) |
| `librarySidebarView` | `modules` (default) or `sounds`: the view the column shows, and the view it reopens on |
| `soundBrowserWidth` | the column's width while Sounds shows; default 280, clamped 200 to 400 |
| `soundBrowserFolders` | the user's own sound folders, one absolute path per line |
| `soundBrowserFavourites`, `soundBrowserRecent` | file paths, one per line |
| `soundBrowserAutoPlay` | play a sound when it is selected; default on |
| `soundBrowserPreviewGainDb` | the preview volume in dB; default -6, clamped -60 to +6 |
