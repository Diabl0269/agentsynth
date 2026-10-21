# Drum machine

`Source/UI/DrumMachine/DrumMachineComponent.h` (`synth::ui::DrumMachineComponent`) is the step-grid editor for a
clip on a Drum Kit track. Double-clicking such a clip opens it where the piano roll would open; any other track
opens the roll. It edits the same notes the roll shows, so a clip can be viewed either way.

## What it shows

- A header strip: the clip name, a "Drum machine | Piano roll" switch, "Play clip", "Show all drums", the
  Steps per bar dropdown (8, 16, 32, 64), the Swing knob and the Ideas button.
- One row per drum: a coloured dot, the name (a button: click it to pick the row the lanes under the grid edit, drawn
  in the accent colour while picked, Kick at first), and Mute (M) and Solo (S) toggles. The default rows are Kick, Snare,
  Clap, Closed hat, Open hat, Tom, Cymbal, Cowbell; "Show all drums" adds the rest of the General MIDI drum map.
  A pitch that holds a note always has a row, and a pitch outside the table is named "Note N".
- Cells are rounded squares with every other beat group (the cells of beats 2 and 4 in 4/4) shaded with text-primary at 3%, as the step sequencer shades its groups. A hit fills the cell to its velocity; an accent adds
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

## Swing

The Swing knob in the header (50% straight to 75% shuffle, 56% reads "56%") sets `Clip::swing` (0.5 to 0.75), one
undo step per turn: a drag or key press commits on release, a double-click resets to 50%. A linked group shares it
(`syncLinkedClips`); split, duplicate and copy/paste carry it. It is saved only when it is not 0.5.

Swing is applied in `TimelineSnapshot::buildFrom` and never written into the notes. The steps of the clip's grid are
counted from 0; a note whose clip-relative start is on an ODD step (within 1e-6 of a step) starts later by
`(swing - 0.5) * 2 * step`, so at 0.75 the off-beat of a pair sits at 75% of the pair (a triplet feel). Notes off the
grid are untouched, the note keeps its length (its end follows, clamped to the clip end as before), and a shift that
would push the start past the clip end is dropped. The clip's run is sorted again when swing is on, since a swung note
can pass an off-grid one. A step is `beatsPerBar / stepsPerBar` with the bar length of the transport's requested time signature (a change republishes through `TransportService::onTimeSignatureChanged`),
which the engine passes to `buildFrom` on every publish (a time-signature change reaches playback at the next
publish, not at once).

The grid keeps its cells on the grid. A hit on a swung step shows a small tick inside its cell, at how far into the
step it plays. The playhead column is real time, and a swung hit flashes when it plays, not when the playhead enters
its column.

## Lanes under the grid

A row of lane chips (`LaneChipRow`, [lane-chips](../layout/lane-chips.md)) sits under the grid with a "for <row>" note
beside it. The only lane so far is Loudness; the lane host (the chips row, the lane band above it) takes more.

Turning Loudness on grows a lane under the grid over 160 ms (`easeOutCubic`, 80 ms under Reduce Motion, at once with
Animations Off or off screen); the grid area above it scrolls when the panel is short. The lane draws one bar per hit
of the picked row, as tall as the hit is loud, with the value on hover. Press and drag across it to set each hit
under the pointer to the pointer's height; a drag is a straight ramp between its pointer samples, previewed in the
lane and written once on release as ONE undo step. The lane is one Tab stop: Left and Right move between the row's
hits, Up and Down change loudness by 5 (Shift 25), each press one undo step, read out as "Kick, step 5, loudness
100".

## Ideas

The Ideas button opens a popover grown out of it (`DrumIdeasPanel`, a `CallOutBox` with the shared reveal) holding
three parts. The pattern logic is plain functions over notes in `DrumIdeas.cpp`, deterministic for a seed (real use
seeds from the time); every result is ONE undo step, and the cells it changed keep an orange outline that fades over
1.2 s. New hits grow in from left to right, 8 ms per column (none under Reduce Motion).

- **Starters by style**: House, Techno, Hip hop, Trap, Drum and bass, Breakbeat, Rock, Funk, Disco, Reggaeton,
  Afrobeat, Lo-fi. A style's one-bar pattern (16 sixteenths on the default rows, Trap with hat rolls on 32nds) is
  repeated across the clip and replaces its notes. The grid is lifted to 16 steps per bar (32 for Trap) in the same
  undo step when it is coarser. The tempo is not changed.
- **Vary**: an Amount slider (Subtle to Wild) and "Make 3 takes" show three mini grids side by side. Pressing a mini
  grid plays one pass of that take at the project tempo through the single-note audition path (a timer; it stops after
  the pass or on the next press). "Pick" under a take adds it as a new clip right after this one (same grid and
  swing, the grid opens it) or, with "Replace this clip" on, swaps this clip's notes. The rules move, add and remove
  ghost hits on the snare and hats, push the kick, swap a closed hat for an open one and humanise some velocities;
  the amount scales how many; a kick on a bar's first beat and the snare backbeats stay unless the amount is Wild.
- **Randomize**: a Density slider (Sparse to Busy) and "Keep kick and snare"; with it on the existing kick (35, 36) and
  snare (38, 40) notes stay exactly as they are and only the other drums are generated.

Escape closes the popover and the focus returns to the Ideas button; each result is announced ("Filled with House
starter").

## Keyboard and accessibility

The grid is one Tab stop titled "Drum machine grid". Arrow keys, Home and End move a focus cell (drawn with the
focus ring); Return toggles a hit, Space plays or stops the clip, Shift+Return toggles accent, Shift+Up and Shift+Down change loudness
by 10, Delete removes the hit, Escape closes. Return, Space (Play clip), Shift+Return and Shift+Up/Down are rebindable (Drum Machine
category in Settings). The focused cell is read out as, for example, "Kick, step 5, hit, loudness 100, accent" or
"Kick, step 5, empty". Header controls (the Swing knob, Ideas) and each row's name, M and S are Tab-reachable with names and tooltips.

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
