# Note range picker

`synth::ui::NoteRangePicker` (`Source/UI/Layout/NoteRangePicker/`) picks a lowest and a highest note on a strip of four octaves. The piano roll's
Generate panel ([scale-assist.md](../timeline/scale-assist.md)) and the sequencer's Randomize popover
([sequencer-design.md](../modules/sequencer-design.md#pattern-tools)) both use it.

## Drawing

A horizontal keyboard of 28 white keys (default 240 x 34 px, it fills whatever bounds it is given) with the black keys
over it. With a scale set its keys are drawn lighter. The selected range is a band: accent at 25 percent alpha with a 1 px
accent outline, a 4 x 26 px handle on each edge and the low and high note names inside the band's ends. The strip shows
four octaves starting at a C and scrolls by octaves to keep the band in view, but only when a drag ends, never under the
pointer.

## Mouse

- Drag an edge to add or remove notes. The edge goes to the key under the pointer, then to the nearest scale note when a
  scale is set (`MusicalScale::snapPitch`). The low edge never passes the high edge, so the band always holds one note.
- Drag the middle to move the whole band by octaves; hold Option to move it one note at a time (one scale note when a
  scale is set). The band stays inside the octaves shown.
- The band glides to its place in 120 ms. With Reduce Motion it lands at once and fades in over 80 ms; with Animations
  Off it lands at once.

## Keyboard and accessibility

Three Tab stops, in this order: the lowest note, the highest note, the band. Left and Right move the focused edge one
scale note (Shift: an octave); on the band they move it an octave (Option: one note). Each stop has a focus ring, a title
and tooltip ("Lowest note", "Highest note", "Move the whole range") and reads its note aloud. The picker is a group that
reads "Note range, C2 to C3".

## API

`setRange(lo, hi)` (does not notify), `getLow()`, `getHigh()`, `setScale(const MusicalScale*)` (copied; null or chromatic
means every note), `setMiddleCOctave(int)` (4 in the piano roll, 3 in the step grid, so note names agree with each place),
`onChange(lo, hi)` (every user change, live, for a preview) and `onGestureEnd(lo, hi)` (once when a drag ends with a different range than it began with, and once per key step that moved the range; never by `setRange`; the owner records one undo step here, so every keyboard step is one step too). The pointer maths is pure, in `NoteRangeGeometry.h`; the shared note name is
`synth::noteNameWithOctave` in `Source/Music/NoteNames.h`.
