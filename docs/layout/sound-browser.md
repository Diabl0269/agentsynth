# The sound browser

One place to find a recording (and, in the Shapes place, a named curve): the **sound index** (Core, no UI) lists every sound in the installed packs and the user's own folders, and the **Sounds view** (the left sidebar's second view) browses and searches it. This doc covers the index, the view, its filters, the preview, dragging a sound out, the packs you can download from the view and your own folders.

## The sound index

`synth::soundbrowser::SoundIndex` (`Source/SoundBrowser/`, part of Core) owns one long-lived scan thread. Nothing it does runs on the message thread.

- **Roots**: every installed sound pack folder (`SoundPackLibrary::roots()`; a pack is a folder with a `pack.json` that has an id, and a repeated id is listed once) and each user folder passed to `setUserFolders`. The pack folders' own audio and `.sfz` files are the entries. A pack you have not downloaded has no entries; the view lists it from the installer's pack list instead ([Packs you do not have](#packs-you-do-not-have)).
- **Entry** (`SoundEntry`): path, place (pack id and name, or `folder:<path>` and the folder's name), the folder inside the place, size, length, sample rate, kind (`OneShot`, `Loop` or `Instrument` for `.sfz`), tags, tempo (0 = unknown), key (empty = unknown) and modification time.
- **Extensions**: `wav`, `aif`, `aiff`, `flac`, `ogg`, `mp3`, plus `.sfz` instruments. A file whose header no format reads is left out.
- **Headers only**: listing opens each file's reader (format header and metadata chunks) and never reads a sample, so a WAV whose audio is damaged still lists, with the length its header declares. A WAV `acid` chunk gives the tempo (or beats over length), the root note and the one-shot flag; the flag beats a "loop" in the file name.
- **Guessing from names** (`SoundNameGuess`): tag words (kick, snare, hat, clap, tom, cymbal, perc, drum become Drums; bass; keys, piano, rhodes; vox, vocal; fx; string) from the folder and file names; tempo from `96bpm`, `120 BPM` or a bare number between two other words (`dusty_120_Am`), 60 to 200 only; key from `Am`, `C#min`, `Ebm`, `F major`. A sound is a loop when its tempo is known or its name or folder says loop, else a one-shot.
- **Cache**: `<settings folder>/SoundBrowser/index.json`, loaded before the first scan so the list is there at once, then rescanned incrementally: a file whose size and modification time match its cached entry is not opened again. The index holds at most 50,000 entries; past that the scan stops and the snapshot says `truncated`.
- **Reading**: `snapshot()` returns an immutable `std::shared_ptr<const SoundSnapshot>`, swapped in under a mutex, so a reader never blocks the scan. `search(query, filter)` ranks name matches first (then folder, place and tag matches, then by name) with the same word rules as `Layout/SearchMatch.h` (a Core copy, because Core cannot use the UI's; a test keeps the two equal). `childrenOf(place, folder)` returns the immediate subfolders (with a sound count) and sounds. `find(path)` looks one entry up.
- **Listener**: `SoundIndex::Listener::soundIndexChanged()` is called on the message thread (an `AsyncUpdater`) after the cache is shown, as the scan progresses and when it ends.
- **Results while scanning**: the scan publishes when it has walked a place and at most every 200 ms inside one (a publish copies the list), so a new folder's sounds are searchable before the scan ends. A mid-scan snapshot keeps the cached entries of the places the walk has not reached. Each `SoundPlace` says whether the running scan has walked it (`scanned`), which is what makes a folder row read "Scanning" while a pack install's rescan leaves the others alone.
- **Folder names and missing folders**: `setUserFolderNames` renames a folder's place and its entries at once, with no scan (the id stays the path). A user folder whose path is not a folder is still listed, with `missing` set and no entries; `rescanIfFoldersChanged()` stat-checks the folders and rescans only when one appeared or vanished.
- **Favourites and Used recently**: `SoundLists` holds two path lists (the recent one capped at 50, newest first). Core never touches the settings file, so the view's owner round-trips them through `soundBrowserFavourites` and `soundBrowserRecent`.

## The Sounds view

The left column holds two views behind a **Modules / Sounds** switch at its top (`LibrarySidebarViews`, `Source/UI/Library/`). The switch is one Tab stop; Left and Right change view, and each segment's tooltip names its shortcut. The column is the "library" focus region's root.

- **Screens**: Places (Favourites, Used recently, each installed pack, "Get more" with the packs you lack, then "Your folders" ending in "Add folder..."), then folders inside a place, then the sounds. A path bar above the list shows where you are; every part but the last is a button that goes back there. A search field above it searches every place at once and shows sounds only.
- **Row detail**: the right-hand column of a sound always ends with its length (`formatSoundLength`): "0.8 s" for a one-shot, "120 BPM . 2 bars . 4.0 s" for a loop (the dot is U+00B7; no bars part when the length rounds to 0 bars, "120 BPM . 0.4 s"; "4.0 s" alone for a loop with no tempo), "instrument" for an .sfz. `soundDetailText` builds the parts and the screen-reader text reads the same fields in words ("loop, 120 BPM, 2 bars, 4.0 seconds"). The detail gets at most 60% of the row (`soundDetailMaxWidth`); a narrower row drops parts from the left, BPM first, then bars (`soundDetailTextFitting`), so the length stays and the name keeps its room at the 200 px minimum.
- **Keys** (list focused): Up, Down, PageUp, PageDown select; Right or Return opens a folder; Left or Cmd+Up goes up; Return on a sound activates it; Space plays or stops its preview; F adds or removes it from Favourites; a printable key jumps to the search field. In the search field Down or Return moves into the list and Esc clears the text, then returns to the list.
- **Screen reader**: the rows are painted, and ONE list element speaks the selected row ("snare, one-shot, 0.8 seconds, 2 of 23"), the same pattern as the module library. The wording lives in `SoundBrowserText` and is unit-tested.
- **Footer**: the selected sound's name and place, then the preview strip (see [Preview](#preview)).
- **Width**: while Sounds shows, the column is as wide as `soundBrowserWidth` (default 280, 200 to 400). Drag the right edge, double-click it to reset, or focus it and use Left and Right (16 px a step). The width eases between the Modules width and the Sounds width while the views slide.
- **Motion**: switching views slides 190 ms (a cross-fade under reduced motion); opening a folder slides the list in, going up slides it out.
- **Lazy start**: the index starts its first scan when Sounds is first shown, and rescans when the installed packs change.

## Packs you do not have

Under Places, a **Get more** heading lists every pack the hosted list offers that is not installed, greyed, each with its download size and a **Download** button painted at the row's right edge. At the end of search results the same rows appear for the packs the query finds by name or by an instrument's name (`Layout/SearchMatch.h` over the name and instrument names), after the sounds; with a filter on, none show. They need the installer's list to have loaded: showing the view fetches it when it has not (and again after a failed fetch), and until it loads there is no Get more.

- **One download**: the button starts `SoundPackInstaller::install`, the job Settings > Sound packs starts, and the view listens to the same installer. The row's percentage and the bar filling along its foot are the installer's progress, so a download started in either place shows in both. The button reads Cancel while the pack downloads (`cancel`), and goes away while it is checked and unpacked. The index scan after the install turns the row into the pack's normal Place; until the index lists the folder the row stays, reading Installing, so the pack is never in neither list.
- **Selecting** a pack row only selects it (no click downloads from the row body). The hosted pack list carries no demo or preview URL and no bundled demo, so there is nothing to play; the footer says "Not downloaded" and the size.
- **Greyed rows are not sounds**: they refuse drags (a press and drag on one does nothing) and their tooltips read "Download <pack> (<size>)", or "Download <pack> (<size>) to use this sound" when the search matched an instrument rather than the pack's name.
- **Keyboard**: the rows are list rows (Up and Down reach them). Return or Space starts the download; Delete or Backspace cancels a running one. The button is not a separate stop: the selected row's focus ring is its ring, and the list speaks "Core instruments, not downloaded, 33 MB, press Return to download, 4 of 7".

## Your folders

"Your folders" lists the folders you added, then **Add folder...** at the end of Places. Return, Space or a click on that row opens the folder chooser (an async `juce::FileChooser`), and dragging a folder from Finder onto the view adds it too: the view is a file-drop target for folders only (a file dragged over is refused, so cards and tracks keep taking audio files; sound dragging out is unaffected). An accent outline fades in around the list while a folder is over it.

- **Scanning**: the new folder's row reads "Scanning" until the scan has walked it, and its sounds are searchable as they are found ([Results while scanning](#the-sound-index)).
- **Folder menu**: right-click a folder Place, or Shift+F10 (the app's open-context-menu key) with it selected: **Rename**, **Show in Finder** (**Show in File Explorer** on Windows; `fileManagerName()` in `SoundBrowserText.h` names it in the menu and the tooltips), **Scan again**, **Remove**. A right-click on any row selects it and never opens it. Rename edits the name in place (`NonModalLabel` over the row: Return or leaving the field commits, Esc cancels) and changes only the label shown here, in the footer and in the Place filter; the folder and its files keep their names. Remove stops listing the folder and never touches a file on disk. Scan again rescans (a file whose size and time are unchanged is not opened again).
- **Missing folders**: a folder whose path is not there (an unplugged drive) stays listed, dimmed, with "Not found" instead of a count, and opening it says so. Showing the view and coming back to the app from another one stat-check the folders (no timer) and rescan only when one appeared or vanished, so the folder returns by itself when the drive is plugged in.
- **Persistence**: `SoundFolderBook` (`Source/SoundBrowser/`) holds the list and the names and keeps the index in step; `LibrarySidebarViews` saves it to `soundBrowserFolders` and `soundBrowserFolderNames`. Settings > Sound packs lists the same folders under "Your folders" (name, path, Remove).

## Short columns

Nothing overlaps at any height, and the search row is always whole. The footer gives way first: with room for the search, path bar, two list rows and the whole footer it shows as described under Preview; with less, a **compact footer** (one 36 px row: Stop, the sound's name cut with an ellipsis, the volume; no waveform and no Auto-play, whose setting still holds); with less than that, no footer at all (invisible, so a screen reader skips it). The open Filters panel is capped to what the footer and two list rows leave, and scrolls inside itself. Once it is open it is guaranteed 64 px: room for it comes first from the footer (whole, compact, none), then from the list (two rows, one, none), and only then does the panel shrink below 64 px. The tall layout is unchanged.

## Filters

Tab is the app's region cycle, so inside the Sounds view Up and Down always step focus (`SoundBrowserKeyboardReach.cpp`, the model of `FocusStepWithin.h`): from the search's neighbours through Filters, the chip row (while it shows) and the open panel's controls in on-screen order, then into the list; Up from the first sound climbs back to the last of them, and Up from Filters returns to the search. Down from the search still goes straight to the list. Down on the last sound enters the preview footer (Stop, Match key, Auto-play, volume; the compact footer has Stop and volume; a hidden footer has none), and Up from its first control returns to the list with the selection kept. Only a text field keeps its own Up and Down. A menu (Key, Length, Place) opens with Return or Space and no longer changes with Up or Down; a range handle and the preview volume move with Left and Right. A control that steps into view in a short panel scrolls the panel to show it.

A **Filters** button sits beside the search field and reads "Filters" or "Filters (2)". Space or Return opens or closes the panel, Esc closes it, and a screen reader hears "Filters, 2 on, collapsed" (or expanded). The tooltip is "Narrow by kind, sound, tempo, key, length or place".

- **The panel opens inline** under the search and pushes the list down; it never covers it. It grows open over 190 ms with the list sliding (an 80 ms fade under Reduce Motion, instant with Animations off). If the view is short the panel takes at most what leaves two list rows above the footer, and scrolls inside itself (its own viewport and scroll bar; a focused control scrolls into view) instead of being clipped.
- **Groups** (`SoundFilterPanel`): Kind (One-shots, Loops, Instruments), Sound (Drums, Bass, Keys, Vocals, FX, Strings), Tempo (a two-handle range slider, 60 to 200 BPM, plus a "Near song tempo" chip), Key (a "Song key" chip when the project has a key, and a menu of the 24 keys), Length (Any, Under 1 s, 1-4 s, Over 4 s) and Place (any pack or folder). Left and Right move across the chips of a group; Space toggles one. A menu opens with Return or Space; Escape closes the panel from anywhere in it. Within Kind, and within Sound, any one may match; the groups themselves all have to.
- **Near song tempo** keeps sounds within 10 BPM of the transport's tempo, or of half or double it. A sound with an unknown tempo or key never passes an active tempo or key filter.
- **Chips**: one row of small removable chips under the search ("Loops x", "90-100 BPM x"), then "Clear all" at the right. The row is one stop (Left and Right move, Space, Delete or Backspace remove). A chip's tooltip is "Remove Loops filter". Removing a chip or Clear all switches the same filter off in the panel. A new chip grows in at the end; a removed one shrinks out and the rest slide along (190 ms; an 80 ms fade under Reduce Motion).
- **What it filters**: search results, folder listings (a folder with no passing sound is left out and its count is of passing sounds), Favourites and Used recently. At the Places screen an active filter shows the matching sounds from every place as a flat list. When nothing passes, the list says so.
- **Code**: the filter is Core (`SoundFilter` in `SoundEntry.h`, `soundPassesFilter` and `parseSoundKey` in `SoundSearch.h`, `SoundIndex::search` and `childrenOf` take it); `SoundFilterModel` holds the switches and makes the chips. The range slider is the reusable `Source/UI/Controls/RangeSlider`: each handle is its own stop ("Tempo, lower handle, 90 BPM"), Left and Right move it one step (Shift or Page keys ten, Home and End to the limit), the handles never cross, and a click on the track moves the nearer one.

## Preview

Selecting a sound plays it when **Auto-play** is on (the default). The next selection cuts the old sound with a 5 ms fade. Space on a sound plays it, or stops it when it sounds; with Auto-play off, selecting is silent and Space plays. A Loop starts on the next bar when the song plays and at once when it does not, and it is fitted to the song ([Loops match the song](#loops-match-the-song)). Instruments, folders and files that are gone never play. Hiding the view stops the preview.

- **Footer** (`SoundPreviewFooter`): the sound's waveform (drawn in left to right over 190 ms, an 80 ms fade under Reduce Motion) with a moving playhead while it plays, a Stop button ("Stop the preview (Space)"), a volume slider and the **Match key** switch (tooltip "Play loops in the song's key", off by default) and the Auto-play switch (tooltip "Play a sound when you select it"), side by side on the Stop row. Both are keyboard stops with an accessibility title. Under a selected loop that has a key, the second line has the key, "Key Am . song Dm" (the dot is U+00B7; only the parts that are known, and a sound with no key shows nothing). With Match key on, a loop's second line leads with what the key match does (`matchKeyText`, from the same `LoopTarget` the preview and the drag use): "Plays 2 semitones up, to D minor" or "Plays 1 semitone down, to F major", "Already in D minor", "No key in the song", "This loop has no key"; with Match key off it says nothing about shifting. The Match key switch's tooltip and accessibility description are the same text when there is one (else the base tooltip). The waveform comes from a `juce::AudioThumbnail`, so decoding stays off the message thread.
- **Volume**: -60 to +6 dB, default -6, arrows nudge 1 dB, spoken as "Preview volume, -6 dB", tooltip "Preview volume, only for previews". It sets the preview's own gain only.
- **Playhead**: the moving line repaints a strip a few pixels wide at 30 Hz while the preview plays, and moves under Reduce Motion (it shows a position). It is a permitted exception in [animation](animation.md#the-time-bounded-animation-rule).
- **Audio**: the preview is summed after the master and never goes through the graph, so the song's volume, mute and effects do not touch it, a bounce never contains it, and plugin mode mixes it into the plugin's output. See [audio-engine](../architecture/audio-engine.md#sound-preview).

## Loops match the song

A Loop whose tempo is known (its `acid` chunk or file name, [Entry](#the-sound-index)) plays and drops at the song's tempo. A sound with no tempo is never stretched, and neither is a one-shot or an instrument.

- **Tempo**: the ratio is the transport's BPM over the loop's BPM, where the loop's BPM is the closest of itself, half and double (`bestStretchBpm`; the same half and double time the Near song tempo chip treats as near). A ratio within 0.5% of 1 plays as recorded. The tempo always follows the song.
- **Key**: with **Match key** on, and both the loop's key and the song's key known, the loop is shifted by the smallest number of semitones (-6 to +6, `planKeyMatch`) that moves its root onto the song's. When the modes disagree the loop goes to the song's relative key instead: a major loop in D minor lands in F major (root +3 from D), a minor loop in F major lands in D minor (root -3). A loop or song with no readable key is not shifted. `LoopTarget::keyMatch` says why (`Off`, `NoSongKey`, `NoLoopKey`, `InKey`, `Shifted`) and `targetKey` names the key the loop lands in; the footer, the preview and the drag all read that one `makeLoopTarget` result. The song key reaches the view as "D minor" or "F major" from `soundKeyForScale`, which reads the scale's third: a minor third (natural, harmonic and melodic minor, minor pentatonic, dorian, phrygian, blues, locrian) is minor, a major third (major, major pentatonic, lydian, mixolydian, phrygian dominant, whole tone) is major, and a scale with no third (or both, like chromatic) has no key. The setting is `soundBrowserMatchKey`.
- **Preview**: `SoundPreviewPlayer::play(file, alignToBar, LoopTarget)`. The decoder thread stretches the finished buffer after it converts it to the device rate, so the audio thread still only reads a finished clip. The preview uses the tempo and key of the moment the sound is selected; a tempo change while it plays takes effect at the next selection.
- **Drop on an audio track**: a stretched copy is written, and the clip is made from it. `SoundDropAdapter` offers the file target `{path}`; a target that overrides `takesStretchedLoops()` (the timeline's clip lanes and the "New audio track" strip) gets `stretchedLoopFile(...)`'s WAV instead of the original when the drag began with a target that needs stretching (`SoundDragState::loopTargetFor`). The copy is a 24-bit WAV at the file's own rate in `Agent Synth Stretched Loops` in the system temp folder, named `<name>-<song bpm>bpm[<+n>st]-<hash>.wav`, and reused when the same file, tempo and shift come again. Nothing re-pitches the clip afterwards: an audio clip carries no tempo or pitch of its own and plays its file as written, so the copy's shift is the only one. It is rendered on the message thread at the drop (about a tenth of a second for a four-bar loop); a file longer than 60 s, or one that cannot be read or written, drops as the original. The Sampler, a pad and the canvas always take the original file.
- **Engine**: [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) 1.4.0 and its FFT library [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear) 0.6.4, both MIT-licensed and header-only. `FetchContent` downloads them and only their include directories are used; the pins live in `cmake/DependencyVersions.cmake`, the licences in [third-party-licences](../development/third-party-licences.md). `Source/SoundBrowser/LoopStretch.cpp` is the one call site (`stretchLoop`).
- **Not here**: stretching an audio clip already on a track is the clip edge's Stretch-to-fit, a separate feature.
- **Tests**: `Tests/SoundBrowser/LoopStretchTests.cpp` (click spacing after a 120 to 96 BPM stretch, the measured pitch of a shifted sine, the identity case, the tempo and key arithmetic and the reason for every shift), `LoopStretchFileTests.cpp`, `SoundPreviewPlayerLoopTests.cpp`, and under `Tests/UI/Library/SoundBrowser/` the drop adapter and the Match key switch.

## Dragging a sound

Drag a Sound row (past 4 px) and a small chip with its name follows the pointer. The payload is the string `sound:` plus the file's absolute path (`SoundDragPayload.h`: `makeSoundPayload`, `isSoundPayload`, `soundFileFromPayload`), apart from module names, snippets and plugin identities. Every target is a file-drop target that also takes a Sound as a one-file drop, so the rules, the highlight and the undo are the OS file drop's own (`SoundDropAdapter` hands the sound to the component's `isInterestedInFileDrag`, `fileDragMove` and `filesDropped`).

| Drop on | What happens | Undo |
| --- | --- | --- |
| Empty canvas | A new Sampler holding the sound (an .sfz loads as its instrument). The ghost is a Sampler's size. `GraphDragDropController::addSamplerLoadedWith` is the one path the OS file drop uses too. | one step |
| A Sampler card | Replaces its sample (an .sfz, or the Load Sample button, goes through the same path); the old waveform fades out while the new one draws in left to right over 220 ms (80 ms cross-fade under Reduce Motion, instant with Animations off). | one step |
| An audio track row | A clip at the snapped beat under the pointer, like an OS file drop. A loop arrives as a copy fitted to the song ([Loops match the song](#loops-match-the-song)). | one step |
| Empty space under the last track | A "New audio track" strip lights up; the drop makes an audio track (with its mixer channel) and the clip at that beat. An OS file dropped there does the same. | one step for both |

An .sfz is an instrument, so only a Sampler card and the canvas take it; no track does. A drop on nothing flies the chip back to its row (the drag container's own snap-back).

**Outlines.** While a sound is dragged, every card and lane that would take it shows a faint accent outline (the audio rows and the "New audio track" strip in the lanes); the target under the pointer gets the full highlight. One app-wide flag (`SoundDragState`) tells the targets when a drag starts and ends, and each repaints once for each; nothing repaints per mouse move. None of the hints is a focus stop.

**Keyboard.** Return (or a double-click) on a Sound is the keyboard way to do the same: it loads the sound into the one selected Sampler on the canvas (one undo step); else adds a clip at the playhead to the selected audio track; else adds a new Sampler in the middle of the visible canvas. A short toast says what happened. The view's tooltip reads "Return loads it into the selected Sampler or audio track."

## Shapes

The Places screen has a **Shapes** place after Favourites and Used recently (its count is every listed shape). Opening it lists two groups, each under a heading row: **Filter shapes** then **LFO shapes**. A group lists your saved shapes of that kind first (marked "yours"), then the factory set; a row is a small curve thumbnail (the shape drawn in the accent colour) and the name. The library is `synth::shapes::ShapeLibrary` ([custom-filter.md](../modules/custom-filter.md#shape-library)); the view reads it when it changes (a Save shape in a shape window adds the row at once), never while painting. Searching and the Filters panel list sounds only (an active filter leaves shapes out of Favourites and Used recently). The tooltip of a shape row says what Return needs. The screen reader says "ah, filter shape, built in, 3 of 20" (`describeShapeForAccessibility`).

### Footer, Favourites, Delete, Rename and Return

- **Footer**: a shape row has its own footer. The name, then "Filter shape, yours" or "LFO shape, factory", and the curve in a larger box (`ShapeCurveView`, drawn in from the left over 190 ms). Stop, Auto-play and the volume fade out over 120 ms (a fade of 80 ms under Reduce Motion, at once with Animations off) and are then hidden, so they are no focus stops; they fade back when a sound is selected. Selecting a shape ends a preview still playing.
- **Favourites and Used recently** hold shapes next to sounds, keyed `shape:<kind>:<factory|user>:<name>` (the drag payload's id; `ShapeLibrary::shapeId`) in the same `soundBrowserFavourites` and `soundBrowserRecent` lists. F stars a shape row (a star is painted like a sound's). A shape whose file is gone is not listed; deleting or renaming a shape removes or renames its entries. Stamping a shape (a drop, Start from, or Return) puts it first in Used recently (`ShapeLibrary::noteUsed`). Return or a double-click on a sound adds the sound there too.
- **Saved shapes only** (built-in shapes are read-only) have a right-click menu, or Shift+F10: **Rename**, **Show in Finder** (**Show in File Explorer** on Windows) and **Delete**. Delete or Backspace on the row is the same as the menu's Delete. Delete asks first ("Delete shape", with Delete and Cancel) and then calls `ShapeLibrary::remove`; the list fades to its new rows and the selection lands on the neighbour. Rename edits the name in place over the row (Return commits, Esc cancels); a name that is empty, unchanged or taken leaves the shape as it was, and the result is announced.
- **Return** stamps the selected shape onto the one selected card (`stampShapeOnSelectedCard`) when it is a Filter set to Custom or an LFO with a Custom wave of the same kind, onto the shape nearest the morph cursor, as one undo step, and a toast says so. With no fitting card selected nothing happens.

### Dragging a shape

Drag a Shape row (past 4 px) and the same name chip follows the pointer, carried by the payload `shape:<filter|lfo>:<factory|user>:<name>` (`ShapeDragPayload.h`; the curve is looked up by name when the drop lands, so a shape deleted meanwhile drops nothing). It reuses the sound drag: `SoundDragState` also says "a shape of this kind is being dragged" (`beginShape`; `isActive()` stays false, so the lanes' "New audio track" strip does not light), the chip is started with `allowDraggingToOtherJuceWindows` because the shape window is a window of its own, and every target is a `SoundDropAdapter` that returns a `ShapeDropTarget` from `shapeDropTarget()` (null for all that do not take shapes: tracks, Sampler cards, the canvas, which refuses the payload so no module is made from it).

| Drop on | What happens | Undo |
| --- | --- | --- |
| A chip in a Filter shapes window / an LFO shapes window | Replaces that shape's curve with the dragged one, when the kinds match; the chip is outlined while the pointer is over it and its tooltip reads "Drop to stamp ah onto oo". | one step |
| A Filter card on its Custom type / an LFO card whose shape is Custom | Replaces the curve of the shape nearest the morph cursor (the one the card's curve editor shows). The card shows a faint outline while a matching shape is dragged, and the full highlight with the label "Stamp ah here" while the pointer is over it. | one step |

A shape of the other kind, and a card that is not on Custom, are refused (no outline, no drop).

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
| `soundBrowserFolderNames` | the names given to those folders: a JSON object from path to name; absent while none is renamed |
| `soundBrowserFavourites`, `soundBrowserRecent` | file paths and shape ids, one per line |
| `soundBrowserAutoPlay` | play a sound when it is selected; default on |
| `soundBrowserPreviewGainDb` | the preview volume in dB; default -6, clamped -60 to +6 |
| `soundBrowserMatchKey` | play and drop loops in the song's key; default off |
