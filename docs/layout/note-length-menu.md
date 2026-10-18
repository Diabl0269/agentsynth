# Note-length menu

Every box that picks a musical length (the LFO's sync rate, the ADSR stage divisions, the Delay's time division, the
custom-wave grid, the modulator row's rate) is one shared control, `NoteLengthBox`
(`Source/UI/Layout/NoteLengthMenu/`), never a plain `juce::ComboBox`. It offers straight, triplet and dotted values in
one small popup.

## What the user sees

The closed box looks like any combo and shows the full name of the choice (`1/8T`). Opening it shows a compact popup of
two rows:

- **Row 1**: three small letter buttons, `S` (Straight), `T` (Triplet), `D` (Dotted), each with a tooltip of that name.
  The popup opens on the kind of the current value.
- **Row 2**: one row of narrow borderless chips. `1/` is written once, as a small prefix, and each chip carries only
  the bottom number: `1 2 4 8 16 32 64 128`. Hovering a chip shows the full name (`1/8T`). Names that are not note
  lengths and come before the notes (`Off`, `Bar`) are chips in front of the numbers behind a divider; the LFO's bar
  lengths (`2/1`, `4/1`, `8/1`, shown as `2 4 8`, tooltip `2/1 (2 bars)`) and any extra after the notes sit behind a
  divider after them.
- A kind the list has no value for (the custom-wave grid has no dotted steps: a dotted step does not divide the cycle
  evenly) is disabled, and its tooltip says why (`NoteLengthBox::setKindUnavailableTip`).

## Names and math

`Source/Modules/Envelope/NoteLength.h` is the one helper: a name is `count/denominator` with an optional `T` or `D`.
A triplet is the straight length times 2/3, a dotted value times 1.5 (`1/8T` = 1/3 beat, `1/4D` = 1.5 beats, `1/1D` = 6
beats). `envelopeNoteDivisions()` is the eight straight names (`1/128` .. `1/1`, their original indices), then
`1/128T` .. `1/1T`, then `1/128D` .. `1/1D`; `lfoRateDivisions()` puts `2/1`, `4/1`, `8/1` after the straight eight and
then the same sixteen. Projects save the name, never the index; both lists keep their old names at their old indices, and
`envelopeNoteDivisionBeats` / `lfoRateDivisionBeats` read a table built from the names, so any index gives the right
beats (and the audio thread never parses a name).

The chips are read from the list, not assumed (`NoteLengthMenuModel`, unit-tested without a component): `1/N` and
`1/NT` / `1/ND` become the numbered chips of their kind, a count above 1 (`2/1`) is a trailing chip, and any other name is
a leading chip when it comes before the first note length in the list and a trailing one after it. So the same popup
serves a parameter's list and, later, a list with `Off` and `Bar`.

## Using it

- **Parameter**: `NoteLengthBox` *is a* `juce::ComboBox` (item ids 1..N in list order), so `CardBody` builds one for
  `attackDiv`, `holdDiv`, `decayDiv`, `releaseDiv`, `timeDiv` and `rateSync` and binds it with the usual
  `juce::ComboBoxParameterAttachment` (same gesture and undo behaviour as every combo), MIDI Learn registers it, the
  card's measuring, accessibility names and automation reflection treat it as a combo. `setChoices(names, textPrefix)`
  fills it; `setCurrentName` / `getCurrentName` read and write by name.
- **Not a parameter**: set `onChange` and call `setChoices`; `setCurrentName(name, sendNotificationSync)` from the
  callback side.
- **Text prefix**: the grid box writes `Grid ` before every name in the closed box (`Grid 1/8`); names stay unprefixed.

## Keys

The box is a Tab stop. Space, Enter or Down opens the popup; Left and Right step through the choices without opening.
In the popup `S`, `T`, `D` switch the kind (ignored for a disabled kind), Left and Right move between the letters and
chips, Up and Down walk the same order (`ArrowKeyNavigation` is attached to the popup), Enter or Space picks the focused
chip, Esc closes without a change. A close from the keyboard hands the focus back to the box; a click elsewhere leaves the
focus where the click put it. `ArrowKeyNavigation` only moves Up/Down, so Left/Right are the popup's own.

## Accessibility

The popup has a title (`<box title> note length`), each letter and each chip a `setTitle` (`1/8 triplet`, `2 bars`) and a
tooltip, focus rings come from `paintFocusRing`, and colours from `themeOf`. Chips never appear or vanish without
motion: hover and the selected chip ease with `ButtonStateEase`. The popup is its own window, so it owns an
`AppTooltipWindow` (`NoteLengthBox::setApplicationProperties` supplies the "Show info tooltips" preference; without it
info tooltips stay on).

## Motion

The popup attaches `PopupMotion` with a `Style` whose body scales from 60% about the point of the box it hangs from (the
middle of the box's edge) while fading in (160 ms) and returns to 70% toward it on the way out (110 ms); Reduce Motion is
the shared 80 ms fade, Animations Off is instant. Every app-decided close (a pick, Esc, a click away, the app losing
focus) goes through `PopupMotion::dismiss`. See [animation](animation.md#popup-windows).

## Tests

`Tests/UI/Layout/NoteLengthMenu/NoteLengthMenuTests.cpp` (model, popup, box, attachment, the keyboard path),
`Tests/UI/Graph/CardBody/NoteLengthChoiceTests.cpp` (the card wiring), `Tests/Modules/Envelope/TempoDivisionTests.cpp`
(names, beats, legacy state).

## The ADSR stage divisions

Each ADSR stage swaps its time fader for its division in the same cell, so the division is a **compact** box
(`NoteLengthBox::setCompact`, set by `CardBody::createChoice` for any note-length box in a swap cell): no chevron, the
full name centred in the cell's width in the plain label size. The cell is as wide as the fader (40 px); the box is one
row tall and sits in the middle of the fader's height, its caption on the cell's top edge. The default layout therefore
no longer promotes a division beside a fader to a stepped fader (a 24-step fader would run straight, then triplet, then
dotted, which is not monotonic in time).
