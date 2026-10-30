# Drum machine

`Source/UI/DrumMachine/DrumMachineComponent.h` (`synth::ui::DrumMachineComponent`) is the step-grid editor for a
clip on a Drum Kit track. Double-clicking such a clip opens it where the piano roll would open; any other track
opens the roll. It edits the same notes the roll shows, so a clip can be viewed either way.

## What it shows

- A header strip: the clip name, a "Drum machine | Piano roll" switch, "Play clip", "Show all drums", the
  Steps per bar dropdown (8, 16, 32, 64), the Swing and Humanise knobs and the Ideas button.
- One row per drum: a coloured dot, the name (a button: click it to pick the row the lanes under the grid edit, drawn
  in the accent colour while picked, Kick at first), and Mute (M) and Solo (S) toggles. The default rows are Kick, Snare,
  Clap, Closed hat, Open hat, Tom, Cymbal, Cowbell; "Show all drums" adds the rest of the General MIDI drum map.
  A pitch that holds a note always has a row, and a pitch outside the table is named "Note N".
- Cells are rounded squares with every other beat group (the cells of beats 2 and 4 in 4/4) shaded with text-primary at 3%, as the step sequencer shades its groups. A hit fills the cell to its velocity; an accent adds
  a white tick on top. While the transport plays the playing column is tinted and a hit flashes as it plays. A hit that carries per-hit settings shows [marks](#grid-marks).

## A Drum Kit's pads as rows

When the clip's track plays a Drum Kit, the kit's pads decide the rows, not the General MIDI table:

- One row per **shown** pad within the pad count, in pad order, named and coloured by the pad. A pad the person hid
  with its Shown switch (see [Drum Kit pads](../modules/drum-kit.md#hiding-a-pad)) has no row, though its hits stay in the
  clip and still play. A note on a pad that is hidden and has no shown twin is not drawn either; a note no pad owns still
  gets a "Note N" row.
- The "Show all drums" button becomes **Show all**: enabled while a pad is hidden, titled with the count ("Show all,
  6 of 8 drums · 2 hidden"), and it switches every pad on as one undo step.
- The rows follow the kit while the editor is open (the timeline's 15 Hz tick re-reads them), so hiding a pad on the
  card removes its row at once, and undo brings it back.

- The **timeline clip** follows the same rows: its note preview spreads the shown pads evenly down the clip, a hidden
  pad's hits are not drawn, and the rows below close up. A clip shows only its name (no caption line), so the count of
  hidden drums is not repeated there.

A clip on a track with no Drum Kit keeps the General MIDI rows above.

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

The Swing knob in the header reads 0% (straight, stored `Clip::swing` 0.5) to 100% (the full shuffle, stored 0.75), so
the stored value is `0.5 + percent / 400` and the knob shows `(swing - 0.5) * 400` (a clip stored at 0.66 reads 64%). It
sets the clip's swing, one undo step per turn: a drag or key press commits on release, a double-click resets to 0%. A
linked group shares it (`syncLinkedClips`); split, duplicate and copy/paste carry it. It is saved only when it is not 0.5.
The knob's text, tooltip ("Swing: push every second step later (0% is straight)") and spoken value all use the percent.

Swing is applied in `TimelineSnapshot::buildFrom` and never written into the notes. The steps of the clip's grid are
counted from 0; a note whose clip-relative start is on an ODD step (within 1e-6 of a step) starts later by
`(swing - 0.5) * 2 * step`, so at 0.75 the off-beat of a pair sits at 75% of the pair (a triplet feel). Notes off the
grid are untouched, the note keeps its length (its end follows, clamped to the clip end as before), and a shift that
would push the start past the clip end is dropped. The clip's run is always stable-sorted again, since a swung or nudged
note can pass an off-grid one. A step is `beatsPerBar / stepsPerBar` with the bar length of the transport's requested time signature (a change republishes through `TransportService::onTimeSignatureChanged`),
which the engine passes to `buildFrom` on every publish (a time-signature change reaches playback at the next
publish, not at once).

The grid keeps its cells on the grid. A hit that plays off its cell start shows a small tick inside its cell, at how far
into the step it plays ([marks](#grid-marks)). The playhead column is real time, and a swung hit flashes when it plays,
not when the playhead enters its column.

## Humanise

The Humanise knob beside Swing reads 0% to 100% and sets `Clip::humanise` (0 to 1, `p / 100`) through
`TimelineDoc::setClipHumanise`: small, deterministic random changes to each hit's loudness and timing, applied only in
`TimelineSnapshot::buildFrom`, never written into the notes. A drag or key press commits on release as ONE undo step, a
double-click resets to 0% (and does nothing, with no undo step, when it is already 0%). It is a Tab stop named
"Humanise" with the tooltip "Humanise: small random changes to each hit's loudness and timing (0% is exact)". A take the
Ideas panel adds as a new clip keeps the clip's humanise as it keeps its swing.

## Per-hit settings and per-drum length in playback

A hit's chance, repeat, flam, nudge and condition, the clip's humanise and a drum's own length and swing are applied in
`TimelineSnapshot::buildFrom` (`appendClipNotes`) and by Track In. They are never written into the notes, so the
piano roll shows the notes as the user left them. All of it is deterministic: the same document gives the same events.

For every note that survives mute and solo, with `step = beatsPerBar / stepsPerBar`:

- **Per-drum length** (`Clip::drumRows`, `lengthSteps` L). When `L * step` is shorter than the clip, only notes whose
  cell (`floor(start / step)`) is below L play, and they repeat every `L * step` across the clip (repetition
  r = 0, 1, ...; an occurrence at or past the clip end is dropped). A note of that pitch stored at a cell of L or more
  stays in the clip but does not play. A length of 0, or one at least as long as the clip, changes nothing. Example:
  Closed hat (42) at length 12 in a one-bar 16-step clip plays steps 1..4 again at 13..16, and a note stored at step 14
  stays silent (step 14 plays the copy of step 2).
- **Swing**: the drum's own swing when set, else the clip's, by the usual odd-step rule on the occurrence's start.
- **Start** (clip-relative) = occurrence start + swing delay + `nudge * step` + humanise timing. A start below 0 plays
  at 0; a start at or past the clip end is dropped.
- **Humanise** h (0..1): a hash of the note id and repetition gives timing within +-0.08 * h * step and loudness within
  +-round(24 * h), loudness clamped 1..127.
- **Repeat** n: n hits at `start + j * step / n`, each as long as the note or `step / n`, whichever is shorter.
- **Flam**: one grace hit 0.15 steps before the first hit, at 60% of its loudness (at least 1) and at most 0.15 steps
  long; skipped when it would start before the clip.
- Each event's end is clamped to the clip end, and the clip's run is stable-sorted by start.
- **Chance and condition** travel on the event (`rollSeed`, `chance`, `condHit`, `condOf`, `cycle`, `cyclesPerPass`)
  and are decided per pass by `TimelineSnapshot::notePlays(event, pass)`: with `c = cycle + pass * cyclesPerPass`, a
  condition (`condOf > 1`) plays only when `c % condOf == condHit - 1`; a chance below 100 then plays when
  `hash(rollSeed, pass) % 100 < chance`. `cycle` is the repetition r for a drum with its own length, else the bar the
  note sits in; `cyclesPerPass` is how many such cycles one pass over the clip spans. Linked clips continue each other's
  cycle: the k-th clip of a link group on the track (by position) adds `k * cyclesPerPass`. A repeat and its flam share
  one seed, so they play or skip together. The roll is deterministic: the same hit, linked copy and pass always roll the same way,
  so a song played from the start (and a bounce) sounds the same every time, and the hits vary from bar to bar, copy to
  copy and loop pass to loop pass.

Track In counts the pass: in the clip preview, the clip wraps completed since the preview started (reset on start and
restart); during playback, the transport loop wraps since play started or the playhead was located. A hit that
`notePlays` rejects emits no note-on and owes no note-off.

## Lanes under the grid

A row of lane chips (`LaneChipRow`, [lane-chips](../layout/lane-chips.md)) sits under the grid with a "for <row>" note
beside it. There are five lanes, in chip order: Loudness, Chance, Repeat, Nudge and Condition. Any set can be on at
once; each is its own lane, 72 px tall, stacked under the grid in chip order (whatever order they were turned on) and
above the chips row.

Turning a chip on grows its lane under the grid over 160 ms (`easeOutCubic`, 80 ms under Reduce Motion, at once with
Animations Off or off screen) and turning it off folds it back; lanes moving together share one motion, and a chip
pressed mid-motion carries on from where each lane is. The grid area above the lanes scrolls when the panel is short:
the lanes squeeze (down to 36 px each) before the rows lose their last two rows of room. The lanes edit the row picked
by its name (Kick at first); the "for <row>" note says which.

All five are one class, `DrumHitLane` (`Source/UI/DrumMachine/DrumHitLane.h`), driven by a `DrumLaneSpec`
(`DrumLaneSpec.h`: name, value range, key steps, bipolar or not, the options of a discrete lane, and how a value reads).
A lane draws one bar per hit of the picked row, as tall as the hit's value, with the value on hover; a bar is at least a
pixel tall so a value of 0 still has something to see and focus. A lane's value for a cell is the cell's first note's; an
edit writes every note of the cell.

| Lane | Range | Keys (Up / Down, Shift) | Reads as |
|---|---|---|---|
| Loudness | 1 to 127 | 5, Shift 25 | "Kick, step 5, loudness 100" |
| Chance | 0 to 100% | 5, Shift 25 | "Kick, step 5, chance 50%" |
| Repeat | Off, Flam, 2 to 8 | one option | "Kick, step 5, repeat 3" |
| Nudge | -50% to +50% of a step, drawn up or down from a centre line | 5, Shift 25 | "Kick, step 5, nudge -10%" |
| Condition | Always, 1/2, 2/2, 1/3 to 3/3, 1/4 to 4/4, 1/8 to 8/8 | one option, Shift four | "Kick, step 5, condition 2/4" |

Repeat: Off is repeat 1 without a flam, Flam is repeat 1 with one, and 2 to 8 set that repeat without a flam. A stored
value that is not an option still shows its own text ("3 + flam", "3/5"), keeps it until it is edited, and the next key
press moves to the option above or below it.

Press and drag across a lane to set each hit under the pointer to the pointer's height (a discrete lane steps through its
options); a drag is a straight ramp between its pointer samples, previewed in the lane and written once on release as ONE
undo step, with exactly the values it previewed. Each lane is one Tab stop titled "<Lane> lane" with a tooltip: Left and
Right move between the row's hits, Up and Down change the value, each press one undo step. The grid's cell text also
reads a hit's non-default settings ("Kick, step 5, hit, loudness 100, chance 50%, condition 2/4").

The per-hit values are applied by the snapshot and Track In, never written into the notes' timing.

## Grid marks

A hit's cell shows what its settings do, small and in theme colours (`DrumCellMarks`, painted by
`DrumMachineComponent::paintCellMarks`), drawn with the hit so they grow in and shrink out with it; a dense grid
(cells under 3 px) draws none.

| Setting | Mark |
|---|---|
| Chance below 100% | the fill is drawn dimmer and a small "%" sits at the cell's lower left |
| Repeat above 1 | that many thin ticks across the top of the cell, below the accent tick |
| Flam | a short grace tick at the cell's left |
| Condition other than always | the text ("2/4") at the lower right when the cell is wide enough, else a dot |
| Nudge, or swing on an odd step | one tick where the hit plays (swing and nudge add up); an early hit's tick sits at the cell's left edge |

## Per-drum length and swing

Each drum can have its own length in steps (`Clip::drumRows`, set with `TimelineDoc::setClipRowLength`) and its own
swing (`setClipRowSwing`); a drum without them follows the clip. Playback (the snapshot repeats a shorter drum every
`length` steps and swings it by its own amount) is in `TimelineSnapshot`; the grid only edits and shows it.

**Display.** A drum with a length draws an orange edge (the theme's warning colour) after its last step with the step
count beside it. Cells past the edge are ghosts: cell `c` shows what plays there, the hit of cell `c mod length` at 30%
alpha with that hit's marks (a ghost under a row swing reads the row's swing). Notes stored past the edge do not play,
so they are hidden behind the ghosts, and the lanes skip them; lengthening the drum brings them back. A length at or
past the clip's step count is no length. A ghost is not a hit: **a click, Return, Shift+Return, Delete or a loudness key
on a ghost does nothing** (the cell only takes the focus), and the screen reader says "Closed hat, step 13, repeats
step 1, hit, loudness 100".

**Gestures.** Every row has a handle at its end (its own edge, else the clip's end) that shows on hover with a
left-right resize cursor and the tooltip "Drag to change how long this drum's pattern is" plus the two step keys.
Dragging snaps to steps (at least 1) and previews live: the ghosts follow and nothing is written. Releasing commits ONE
undo step with exactly the previewed length; dragging to or past the clip's end clears the length (a clip longer than
256 steps stops at 256, the model's cap); Escape drops the drag. Right-clicking a row's name (or Shift+F10, the shared
`openContextMenu` action, on a focused name or on the grid's focused row) opens a menu: Length (Clip length, then 1 step
to the clip's step count, the current one ticked; the clip's own count is the same as Clip length), Swing (Clip swing,
then 0% to 100% in tens, the same mapping as the Swing knob) and Reset (clears both, one undo step). Each choice is one
undo step and is announced. A right-click on a name opens the menu without selecting the row.

**Keys.** Four rebindable actions in the Drum Machine category act on the row of the focused name button, else the
grid's focused row: `drumMachineRowShorter` (Alt+Left) and `drumMachineRowLonger` (Alt+Right) move the end by a step,
`drumMachineRowShorterBar` (Alt+Shift+Left) and `drumMachineRowLongerBar` (Alt+Shift+Right) by a bar of the clip's
grid. Each press is one undo step and is announced; shortening stops at 1 step, lengthening to the clip's end clears the
length, and a press that changes nothing makes no undo step. The grid claims these chords before the timeline's clip-move
chords (also Alt+arrows).

**Names.** A row's name button reads "Closed hat row, length, 12 steps, swing 40%" with what is set.

**Motion.** Ghost cells fade in over 110 ms when a drum is shortened (also while dragging), and the edge slides to its
new place over 160 ms when the change is not a drag. Under Reduce Motion the ghosts take an 80 ms fade and the edge
jumps; with Animations off or off screen both land at once. Menus use the shared popup reveal.

## Ideas

The Ideas button opens a popover grown out of it (`DrumIdeasPanel`, a `CallOutBox` with the shared reveal) holding
four parts. The pattern logic is plain functions over notes in `DrumIdeas.cpp`, deterministic for a seed (real use
seeds from the time); every result is ONE undo step, and the cells it changed keep an orange outline that fades over
1.2 s. New hits grow in from left to right, 8 ms per column (none under Reduce Motion).

- **Starters by style**: House, Techno, Hip hop, Trap, Drum and bass, Breakbeat, Rock, Funk, Disco, Reggaeton,
  Afrobeat, Lo-fi. A style's one-bar pattern (16 sixteenths on the default rows, Trap with hat rolls on 32nds) is
  repeated across the clip and replaces its notes. The grid is lifted to 16 steps per bar (32 for Trap) in the same
  undo step when it is coarser. The tempo is not changed.
- **Fills**: "Snare roll" and "Tom fill" buttons and an "Every 4th bar" toggle (`drumideas::addFill`). A fill replaces
  every note except kicks (35, 36) in the last beat (snare roll: four rising sixteenths on the snare, loudness 60 to 120,
  the last two with repeat 2) or the last two beats (tom fill: eight sixteenths down the toms 50, 48, 47, 45, two each)
  of the clip's last bar; with the toggle on, of bars 4, 8, 12 and so on (the last bar when the clip is under 4 bars).
  A bar the clip cuts short keeps only the hits that start inside the clip. The grid is lifted to 16 steps per bar in
  the same undo step when coarser. One undo step, the changed-cell outline, and an announcement ("Added a snare roll").
- **Vary**: an Amount slider (Subtle to Wild) and "Make 3 takes" show three mini grids side by side. Pressing a mini
  grid plays one pass of that take at the project tempo through the single-note audition path (a timer; it stops after
  the pass or on the next press). "Pick" under a take adds it as a new clip right after this one (same grid, swing, humanise and per-drum length and
  swing; the grid opens it) or, with "Replace this clip" on, swaps this clip's notes. The rules move, add and remove
  ghost hits on the snare and hats, push the kick, swap a closed hat for an open one and humanise some velocities;
  the amount scales how many; a kick on a bar's first beat and the snare backbeats stay unless the amount is Wild.
- **Randomize**: a Density slider (Sparse to Busy) and "Keep kick and snare"; with it on the existing kick (35, 36) and
  snare (38, 40) notes stay exactly as they are and only the other drums are generated.

Escape closes the popover and the focus returns to the Ideas button; each result is announced ("Filled with House
starter").

## Keyboard and accessibility

The grid is one Tab stop titled "Drum machine grid". Arrow keys, Home and End move a focus cell (drawn with the
focus ring); Return toggles a hit, Space plays or stops the clip, Shift+Return toggles accent, Shift+Up and Shift+Down change loudness
by 10, Delete removes the hit, Escape closes. Return, Space (Play clip), Shift+Return, Shift+Up/Down and Alt(+Shift)+Left/Right (a drum's
[length](#per-drum-length-and-swing)) are rebindable (Drum Machine category in Settings). The focused cell is read out as, for example, "Kick, step 5, hit, loudness 100, accent" or
"Kick, step 5, empty". Header controls (the Swing and Humanise knobs, Ideas) and each row's name, M and S are Tab-reachable with names and tooltips.

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
