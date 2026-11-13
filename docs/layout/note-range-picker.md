# Note range picker

`synth::ui::NoteRangePicker` (`Source/UI/Layout/NoteRangePicker/`) picks a lowest and a highest note on a keyboard strip
that scrolls over every note from C-1 to G9. The piano roll's Generate panel ([scale-assist.md](../timeline/scale-assist.md))
and the sequencer's Randomize popover ([sequencer-design.md](../modules/sequencer-design.md#pattern-tools)) both use it.

## Drawing

Two rows, 45 px in all (11 px overview, 34 px strip); a picker given less than 45 px shows the strip alone.

- **Overview bar** (8 px, then a 3 px gap): the whole keyboard, with a tick at each C, the range as a small accent bar and
  a frame around the octaves the strip shows now.
- **Strip**: white keys at least 7 px wide, with the black keys over them. It shows as many white keys as fit at 7 px,
  between 7 and 28 (the piano roll's 158 px strip shows 22 keys of 7.2 px, Randomize's 230 px shows 28 of 8.2 px). With a scale
  set its keys are drawn lighter. The strip never draws a key above G9 or below C-1: it stops there.
- **Band**: the selected range, accent at 25 percent alpha with a 1 px accent outline, a 4 x 26 px handle on each edge
  and the low and high note names inside its ends. A name whose edge is off the strip is not drawn.

The strip's left side can sit on any white key (it scrolls smoothly, not by octaves).

## Mouse

- **Drag an edge** to add or remove notes. The edge goes to the key under the pointer, then to the nearest scale note when
  a scale is set (`MusicalScale::snapPitch`). The low edge never passes the high edge, so the band always holds one note.
- **Drag the middle** (grab anywhere inside the band): the range follows the note under the pointer, one note at a time
  (one scale note with a scale); hold Shift for whole octaves. It stops at the ends of the keyboard and keeps its width.
- **Click empty keys**: the band moves there at the same width (the same number of scale notes with a scale, edges on scale
  notes), as one undo step. Keep the button down and the band follows from there.
- **Scrolling under a drag**: while an edge or the band is dragged within half a key of either end of the strip, or past
  it (the pointer may leave the picker), the strip scrolls under the pointer, 4 keys a second at the end and 9 more for
  every key further out, up to 70. The key under the pointer keeps changing, so the edge or band keeps moving; every note
  is reachable in one drag. Scrolling starts only after the pointer has moved 3 px, so pressing an edge that sits at the
  strip's end does nothing by itself. `beginDragAutoRepeat` keeps the drag events coming while the pointer rests.
- **Escape during a drag** puts the range and the strip back where the press found them and commits nothing; the
  scroll stops at once, though the button is still down ([layout.md](layout.md#every-drag-follows-the-view)).
- **Overview bar**: click or drag to centre the strip on that spot of the keyboard. It never changes the range.
- **Note names**: click the low or high name (without dragging) and type an exact key such as `C#3` or `Bb2` (octave
  numbering follows `setMiddleCOctave`), then Enter. The edge goes exactly there, with no scale snap; if it passes the other
  edge, the other edge follows. A name that does not parse stays in the box, in the error colour; Escape or clicking away
  closes it. One undo step.
- A sideways swipe (horizontal wheel) over the picker scrolls the strip; a plain vertical wheel is left to the panel.
- The band and the keys glide to their place in 120 ms (`glideTo`: one animator moves the strip and both band edges). A
  scroll under a drag follows the pointer at once. With Reduce Motion everything lands at once and the band fades in over
  80 ms; with Animations Off it lands at once ([animation.md](animation.md)).

## Keyboard and accessibility

Three Tab stops, in this order: the lowest note, the highest note, the band. Each has a focus ring, a title ("Lowest
note", "Highest note", "Move the whole range"), a tooltip that names its keys, and reads its note aloud. Taking focus
scrolls the strip to show the part. The picker is a group that reads "Note range, C2 to C3".

| Key | On an edge | On the band |
| --- | --- | --- |
| Left / Right | the edge one scale note | the range one scale note |
| Shift + Left / Right | an octave | an octave |
| Enter | types a note (the name box) | |
| Page Up / Page Down | show the octave above or below (the range stays) | same |

Every key step that moves the range, and every typed note, is one undo step.

## API

`setRange(lo, hi)` (does not notify; the strip scrolls to centre the range only when it is not on screen), `getLow()`,
`getHigh()`, `setScale(const MusicalScale*)` (copied; null or chromatic means every note), `setMiddleCOctave(int)` (4 in the
piano roll, 3 in the step grid, so note names agree with each place), `onChange(lo, hi)` (every user change, live, for a
preview) and `onGestureEnd(lo, hi)` (once when a gesture ends with a different range than it began with, and once per key
step or typed note that moved the range; never by `setRange`; the owner records one undo step here). Hosts size it with
`kPreferredHeight` (45) and give it all the width they have.

The maths is pure, in `NoteRangeGeometry.h` (`visibleKeysFor`, `keySpan`, `pitchAtX`, `scrollToReveal`,
`autoScrollSpeed`, `edgePitchAt`, `dragBandRange`, `bandCentredOn`, `overviewScrollFor`, `parseNoteName`) and tested
without a window; the gestures are `NoteRangePickerGestures.cpp`. The shared note name is `synth::noteNameWithOctave` in
`Source/Music/NoteNames.h`. Tests: `Tests/UI/Layout/NoteRangePicker/NoteRangePickerTests.cpp`; scenarios
`Tools/ui-verify/scenarios/note-range-picker-*.yaml`.
