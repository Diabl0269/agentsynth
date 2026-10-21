# Drum machine

`Source/UI/DrumMachine/DrumMachineComponent.h` (`synth::ui::DrumMachineComponent`) is the step-grid editor for a
clip on a Drum Kit track. Double-clicking such a clip opens it where the piano roll would open; any other track
opens the roll. It edits the same notes the roll shows, so a clip can be viewed either way.

## What it shows

- A header strip: the clip name, a "Drum machine | Piano roll" switch, "Play clip", "Show all drums", and the
  Steps per bar dropdown (8, 16, 32, 64).
- One row per drum: a coloured dot, the name, and Mute (M) and Solo (S) toggles. The default rows are Kick, Snare,
  Clap, Closed hat, Open hat, Tom, Cymbal, Cowbell; "Show all drums" adds the rest of the General MIDI drum map.
  A pitch that holds a note always has a row, and a pitch outside the table is named "Note N".
- Cells are rounded squares with alternating beat-group shades. A hit fills the cell to its velocity; an accent adds
  a white tick on top. While the transport plays the playing column is tinted and a hit flashes as it plays.

## Opening it

Besides double-clicking a clip, a "Drum machine" button opens it from the Drum Kit card on the canvas and from the
header of every Drum Kit track (only those; other tracks show no button). The button opens the clip under the
playhead on the track; with none there, it adds a one-bar clip named "Beat N" (the first free N) at the start of the
bar the playhead is in, as ONE undo step. On the canvas the button is disabled, with a tooltip saying why, while no
track plays that kit; it opens the timeline tab and the first track playing the kit.

## Play clip

"Play clip" loops the open clip even with the song stopped, and reads "Stop clip" in the accent colour while it
loops. The grid's playhead follows the loop and hits flash as it reaches them. Space toggles it (the rebindable
`drumMachinePlayClip` action, Drum Machine category). Edits are heard on the next pass of the loop.

It stops on its own when the transport plays, the grid closes or switches to the piano roll, the clip is deleted, or
the project changes. A change to the clip's start or length restarts the loop on the new range.

`TimelineMidiSourceModule` (the track's Track In) plays the loop: the message thread publishes a start or stop
request through atomics (a version counter, odd while being written) and the audio thread reads it at the top of a
block. It reuses the module's note-hygiene (every note-on gets its note-off, also at the wrap, a stop, a mute and a
lost track). The loop only runs while the transport is stopped, and the module ends it the moment the transport
plays. The panel reads the loop's beat back through `TrackHeaderHost::clipPreviewBeat` at the playhead's 30 Hz frames,
which keep running while a loop plays (a NaN beat means the module ended it).
## Cell mapping

A row is one General MIDI pitch. A step is `beatsPerBar / stepsPerBar` beats (`beatsPerBarFor(transport)`). A note
belongs to the cell containing its clip-relative start beat (floor), whatever its length. Changing Steps per bar
keeps every note where it is in time and only changes which cell it falls in.

| Gesture | Result |
|---|---|
| Click an empty cell | Adds one note at the cell start, one step long, velocity 100; plays it once |
| Click a hit | Removes every note of that pitch starting in the cell |
| Shift-click an empty cell | Adds a hit at velocity 127 |
| Shift-click a hit | Toggles accent (127 and 100) |
| Drag a hit up or down | Sets velocity 1 to 127, previewed live, committed once on release |

An accent is a velocity of 120 or more. Every edit is one undo step through the `TimelineDoc` mutators.

## Mute and solo

Per-drum Mute and Solo are stored on the clip (`mutedPitches`, `soloPitches`) and applied in
`TimelineSnapshot::buildFrom`: a note is skipped when its pitch is muted, or when any pitch is soloed and its
pitch is not. They never write `MidiNote::muted`, which stays the piano roll's per-note mute.

## Keyboard and accessibility

The grid is one Tab stop titled "Drum machine grid". Arrow keys, Home and End move a focus cell (drawn with the
focus ring); Return toggles a hit, Space plays or stops the clip, Shift+Return toggles accent, Shift+Up and Shift+Down change loudness
by 10, Delete removes the hit, Escape closes. Return, Space (Play clip), Shift+Return and Shift+Up/Down are rebindable (Drum Machine
category in Settings). The focused cell is read out as, for example, "Kick, step 5, hit, loudness 100, accent" or
"Kick, step 5, empty". Header controls and each row's M and S are Tab-reachable with names and tooltips.

## Motion

Hits grow in over 110 ms and shrink out when removed; rows arriving or leaving animate too. Everything is
time-bounded, lands in its final state at once when off screen or with Animations off, and becomes a plain 80 ms
fade under Reduce Motion.

## How it is hosted

`TimelinePanelComponent` holds the grid in a `DrumMachineHost` beside `pianoRoll_`, at the same rect with the same
cross-fade. `clipEditorOpen()` is the one test for "an editor covers the clip lanes"; the ruler mapping, the toolbar
carve, the playhead's local client and the track-list motion gate all go through it. The piano roll shows a "Drum
machine" chip on Drum Kit tracks that switches back, and `TrackHeaderHost::isDrumKitTrack` is how the panel asks
whether a track's instrument is a Drum Kit.
