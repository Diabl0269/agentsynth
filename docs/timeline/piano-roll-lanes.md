# Piano Roll Velocity and CC Lanes

The strip docked under the [piano roll](piano-roll.md)'s note canvas: one lane at a time, either
**Velocity** (one bar per note) or a **MIDI CC** lane stored on the open clip. The CC data is part
of the document, plays back through the track's "Track In" node, and survives save/load.

## Source layout

`Source/UI/PianoRoll/PianoRollControllerLanes/` — a collaborator class the roll owns
(`PianoRollComponent::controllerLanes_`), not more code in the roll:

| Unit | Concern |
|---|---|
| `PianoRollControllerLanes.h` | The class declaration |
| `PianoRollControllerLanes.cpp` | Lane selection, the selector and right-click menus, geometry, the one commit path |
| `PianoRollControllerLanesVelocity.cpp` | Velocity gestures (bar drag, freehand, line) |
| `PianoRollControllerLanesCC.cpp` | CC gestures (freehand, click-add, move, line, erase) |
| `PianoRollControllerLanesPainting.cpp` | Lane header, guides, bars / curve, hover states, the drag readout, the playhead line |
| `ControllerLaneEdits.h/.cpp` | Pure edit maths (velocity ramp, span replace, stroke thinning, lane names) |

The roll's side is `PianoRollComponent/PianoRollControllerLaneGlue.cpp`: the carve-up, the "Lanes"
chip, `canvasBottom()`, `repaintNotesForLanes()`, and two read-only views the strip needs
(`getSelection()`, `snapBeatForLanes()`). `PianoRollComponent/PianoRollMidiFile.cpp` is the "MIDI"
chip (see [MIDI files](#midi-files)).

## Layout and collapse

The strip is a child spanning the roll's **full width**, so strip x equals roll x and every beat
maps through the roll's own `beatToX`/`xToBeat` — the lanes scroll and zoom with the notes. Its
left gutter, `[0, leftGutterWidth())`, holds the lane selector, so it lines up with the keys column
and follows the scale panel's slide. Height is `PianoRollControllerLanes::kStripHeight` (96 px),
carved from the bottom of the roll **before** the left gutter; `canvasBottom()` is the note canvas's
bottom edge and every grid/row read that used to take `getHeight()` takes it instead.

The **"Lanes"** header chip (the seventh chip, lit while open) toggles the strip. It starts
**collapsed**, and collapsed means nothing is carved: the roll's geometry is exactly what it is
without the feature. The state (and the selected lane) lives on the roll's child, so it is
remembered for the session, not persisted. Showing or hiding it is never an undo step.

The strip never takes keyboard focus (`setWantsKeyboardFocus(false)`,
`setMouseClickGrabsKeyboardFocus(false)`), so the roll's keys — J (snap), Q (quantise), Delete,
the arrows — keep working while the pointer works the lanes.

## Lane selector

Click the gutter for the menu: **Velocity**, **CC1 Mod Wheel**, **CC2 Breath**, **CC11
Expression**, **CC64 Sustain**, then every other CC lane the open clip already has, then **Other
CC...** (four submenus covering 0..127). Picking a CC lane the clip does not have mutates nothing;
the lane is created by the first committed edit.

## Gestures

Every gesture previews locally and commits **once**, on mouse-up, through
`AppUndoManager::recordTimelineChange` — one gesture is one undo step and one snapshot republish.
Gestures are independent of the roll's edit tool; modifiers pick the gesture.

**Velocity lane** (values clamped to `[1, 127]`; commit is `TimelineDoc::setNoteVelocities`):

| Gesture | Effect |
|---|---|
| Drag on a bar (within `kBarHitPx` of a note start) | That note's velocity follows the pointer. A chord moves together unless some of its notes are selected, then only those |
| Drag elsewhere (freehand) | Every note whose start the pointer passes takes the value under the pointer, interpolated between drag events so a fast drag skips nothing |
| **Shift**+drag (line) | A straight ramp from press to release across every note whose start lies between them |
| Right-click | "Set velocities to 100" (the selection, or all notes) |

With a note selection, freehand and line only change **selected** notes. Muted notes are drawn
dimmed and are still editable. Selected notes are drawn in the accent colour.

**CC lane** (values `0..127`; commit is `TimelineDoc::setControllerLanePoints`):

| Gesture | Effect |
|---|---|
| Click on empty lane | Adds one point at the snapped beat |
| Drag on empty lane (freehand) | Raw samples, thinned on mouse-up with `AutomationRecorder::thinPoints` (the recorder's epsilon, floored at one pixel of the lane), replacing the points in the dragged span |
| Drag a handle | Moves the point — beat snapped, value follows the pointer |
| **Shift**+drag (line) | Replaces the span with its two snapped endpoints |
| **Alt**+click/drag (erase) | Deletes every handle the pointer passes over |
| Right-click | Delete point, point curve Hold / Linear (on a handle); Clear lane; Remove lane. The target (clip, lane, point beat) is captured when the menu opens and re-verified when the async answer arrives; a stale one does nothing |

Snap is the shared view-state snap (J toggles it), the same magnetism note edits use. New points on
CC 64..69 (the pedals) default to **Hold**; everything else to **Linear**.

The gesture code is separate from `AutomationLaneEditor`'s: reusing it would have meant changing
that editor's target model, and the two differ in data (clip-relative CC points vs track lanes in
parameter units) and in bindings (modifiers here, a tool strip there).

## Look and feel

Theme tokens only (`synth::theme::Colors`), and no timers.

- **Lane header** (the left gutter): a pill with the lane name and a drawn caret, the value range
  underneath; it brightens on hover and opens the selector.
- **Guides**: velocity 32 / 64 / 96, CC 0 / 64 / 127, each labelled at the lane's left edge.
- **Velocity bars** are filled in the note's own colour — `PianoRollComponent::notePaintFor`, i.e.
  `resolveNoteColour` at the (previewed) velocity, so a bar and its note always match, muted notes
  included. Each bar has a rounded cap and a dot handle; selected bars carry the selection ring; the
  hovered bar and every bar a live gesture touches grow slightly.
- **Live recolour.** During a velocity drag the NOTES recolour too: `effectiveGeometryFor` reads
  the strip's `previewVelocityFor(id)`, and the strip asks the roll to repaint only the notes whose
  preview changed (`repaintNotesForLanes`).
- **CC curve**: stroked in the theme accent over a vertical accent gradient that fades toward the
  lane floor; handles are rings that fill and grow on hover.
- **Readout**: while a gesture is live, a small pill beside the pointer shows the value under it.
- **Repaint budget**: hover is state-change gated and repaints only the old and new bar column /
  handle / header; a drag step repaints the strip (the preview can change anywhere in it).

## Data model

`synth::Clip::controllers` — `std::vector<ClipControllerLane>`, sorted by `ccNumber`, at most one
lane per CC number. Each lane is `{ccNumber 0..127, points}`; a `ControllerPoint` is
`{beat, value, curve}` with `beat` **clip-relative** (like notes), `value` `0..127` and `curve`
Hold (0) or Linear (1). The reserved Bezier curve is refused. Points stay sorted with unique beats
(same-beat: last one wins); values are clamped by the mutation API and the loader. Caps:
`kMaxControllerLanesPerClip` (128), `kMaxControllerPointsPerLane` (= `kMaxBreakpointsPerLane`).

Mutators (`Source/Timeline/TimelineDoc/TimelineDocControllers.cpp`, one mutation each):
`setNoteVelocities`, `addControllerLane`, `removeControllerLane`, `setControllerLanePoints`
(replace-all, creates the lane when absent). Structural edits carry the lanes: **split** cuts each
lane with a boundary point on both halves so both keep playing the same curve (a half that would
pass the point cap drops its boundary point — that only happens when every point of a full lane is
on that side, where the lane is flat across the cut, so nothing audible changes and the split is
never refused); **join** re-bases
b's points and merges by CC number; **duplicate** copies them.

Serialised as an additive `"controllers"` array on each clip (written always, absent loads empty,
`kFormatVersion` stays 1). The untrusted gate's rules are in
[`ai/timeline-safety.md`](../ai/timeline-safety.md).

Every clip-cloning path carries the lanes: split / join / duplicate (the doc), repeat (built on
duplicate) and the panel's clip clipboard (copy / cut / paste, `TimelinePanelClipClipboard.cpp`,
restored through `setControllerLanePoints`).

## MIDI files

`synth::MidiClipFile` reads and writes CC lanes:

- **Export** writes each lane as controller events inside the clip window, on the clip's note
  channels (channel 1 without notes): the lane's value at beat 0, every Hold step at its point, and
  every Linear segment sampled each `kExportCcStepBeats` (1/32 beat), evaluated with
  `AutomationKernel` and written only when the 7-bit value changes. CCs come before notes at an
  equal tick.
- **Import** turns each track's controller events into one **Hold** lane per CC number (channels
  merged), drops unchanged values, and skips 120..127 (channel-mode messages). A lane over the point
  cap is thinned with `AutomationRecorder::thinPoints` at a growing tolerance. A track holding only
  CC data (a format-1 file's controller track) is kept. The same CC arriving from several tracks is
  MERGED into one lane (sorted, unique beats, capped), never last-track-wins. `importIntoTrack`
  makes one clip per note track, merges CC-only tracks into the first note clip, and turns a file
  with CC but no notes into one CC clip. `importIntoTrack` takes `withControllers` (default **false**:
  the AI `placeMidiClip` path stays notes-only); `importIntoClip` merges a file into an existing
  clip, growing it to fit and replacing any lane the file also has.
- **The "MIDI" chip** (eighth header chip) offers "Import MIDI file into this clip..." and "Export
  this clip as a MIDI file..." through async `juce::FileChooser`s. A file with CC data asks first:
  `promptMidiControllerImport` (protected virtual, async `AlertWindow`, SafePointer-guarded, clip id
  captured when asked) with "Import with controller data" / "Notes only"; the answer lands in
  `applyMidiImportAnswer(clipId, result, withControllers)` — one undo step, and the headless test
  seam. A notes-only file imports without asking.

## Playback

`TimelineSnapshot` flattens each unmuted MIDI clip's non-empty lanes into `ControllerInfo` entries
(absolute beats, the clip window, a channel mask) — see
[`architecture/timeline.md`](../architecture/timeline.md). `TimelineMidiSourceModule` plays them
through `synth::TimelineControllerPlayer` (`Source/Modules/TimelineMidiSourceModuleCC.h`) into the
same MIDI buffer as the notes, so CCs reach exactly the track's MIDI destinations (hosted plugins
included):

- **Channels.** A lane plays on every channel the clip's notes use; channel 1 for a clip with no
  notes.
- **When.** Per beat range, each lane overlapping it is evaluated with `AutomationKernel` at the
  range start (or the clip start inside the range) and at every breakpoint inside the range. A CC
  is sent only when its rounded 7-bit value differs from the last one sent on that
  (channel, controller). A Linear ramp therefore steps once per block. At most 512 messages per
  range; anything beyond is re-derived at the next range start. CCs are emitted **before** that
  range's notes.
- **Outside every clip window** nothing is sent; the controller keeps its last value. A point past
  the clip end never plays.
- **Chase.** Every positional flush — stop, locate, loop wrap, bypass, track lost / muted /
  soloed away — forgets the sent values, so the next evaluation re-sends the lane's current value:
  starting mid-clip, locating and wrapping the loop all land the controller where the lane says.
- **Pedals.** That same flush first sends 0 on CC 64 / 66 / 69 wherever this source last sent a
  value of 64 or more, so a stop or locate cannot leave a sustain held over the released notes.
  During playback a held pedal is also released (0, at that beat) the moment no lane for that
  controller and channel covers the playhead any more — its lane or clip ended, or the clip was
  muted, deleted or edited out of the snapshot. An adjacent lane that continues the hold keeps it.
- **Cost.** The per-track run carries a monotonic `runMaxEndBeat`, so each range binary-searches
  its first live lane instead of scanning every lane behind the playhead.
- All of it is allocation- and lock-free: a fixed 16 x 128 table of last-sent values.

## Tests

- `Tests/Timeline/TimelineDoc/TimelineDocControllersTests.cpp` — mutators, one-mutation batching,
  rejects, split / join / duplicate, `toVar`/`fromVar` round trip and loader rejects.
- `Tests/Timeline/TimelineControllerLaneTests.cpp` — `validateTimeline` accept and every reject
  code; snapshot flattening (absolute beats, channel mask, muted clip / audio track excluded).
- `Tests/Timeline/TimelineMidiSource/TimelineMidiSourceControllerTests.cpp` — emitted CCs per
  block: Hold steps, Linear ramp, channels, CC-before-note order, chase on locate / restart / loop
  wrap, sustain release on stop, clip window and muted clip.
- `Tests/UI/PianoRoll/PianoRollControllerLanesTests.cpp` — collapse keeps the old layout, focus,
  lane menu, every velocity and CC gesture, one undo step per gesture, playback velocity after a
  ramp, live note recolour, hover and readout, and the pure edit maths.
- `Tests/Timeline/MidiClipFileControllerTests.cpp` — CC export (Hold, sampled Linear, dedupe,
  channels) and import round trips, `importIntoTrack` / `importIntoClip` with and without CCs.
- `Tests/UI/PianoRoll/PianoRollMidiFileTests.cpp` — the ask-only-with-CC rule and both answers.
- `Tests/UI/Timeline/TimelinePanel/TimelinePanelClipClipboardControllerTests.cpp` — copy / cut /
  paste / duplicate / repeat carry CC lanes.
