# Fine drag: Shift slows every value drag

Shift is the only fine-drag key. Command, Control and Alt never slow a drag: `FineDragSlider` switches off
`juce::Slider`'s default of swapping to a velocity (slow) drag while Cmd, Ctrl or Alt is held, so the Level knob, a
mod-matrix amount and the mixer fader all behave the same. Cmd-click still resets a control to its default, and Alt on
a card knob is still the modulation-amount drag; neither changes the plain drag's rate.

Holding Shift while dragging any slider, knob, fader, number field or amount bar moves the value at a tenth
(`FineDrag::kRate`, `Source/UI/Controls/FineDrag/FineDrag.h`) of the normal rate, so a precise value needs no typing.
Pressing or releasing Shift in the middle of a drag never makes the value jump: only the travel after the change counts
at the new rate.

## How a control gets it

- **A control that hands the mouse to `juce::Slider`** (card knobs, the mod matrix amount, the Export tail, sends, pan,
  sampler fields, the drum ideas sliders, ...) derives from `FineDragSlider` (`FineDragSlider.h`). It feeds the base
  class a pointer that has travelled only the scaled distance, so JUCE's own maths, snapping and gesture
  notifications are untouched. Every `juce::Slider` in `Source` is a `FineDragSlider` or derives from one.
- **A control that works out its own value** (the card and mixer faders, the XY pad, the number field, the mod dot's amount bar,
  the tempo label, the colour picker's fields, range handles, the meter-stop editor, the modulator band's amount
  drag) multiplies its travel by `FineDrag::rateFor(mods)`, or keeps a `FineDrag` and reads `advance(e)` / `travelled(e)`.
- **The modulation-amount drags** (the cable's midpoint knob, a card knob's ring or Alt-drag, the mod dot's drag) scale
  each move by `rateFor`, so a modulation amount is as fine as a value.

## Where Shift already meant something

Shift is read per mouse move on a control that is already being dragged, so it never collides with a press-time use:
Shift-press on empty canvas still starts a marquee, Shift-click on a card still toggles its selection, and Shift on a
point editor (curve, lane, morph map, EQ band, piano roll) still means axis lock or extend, because those drags move
points, not values. The keyboard's Shift step (a finer arrow step) is unchanged.

## Guard

`FineDragEveryControl` (`Tests/UI/Controls/FineDragTests.cpp`) lists every class that drags a value, drives each with a
real press and drag with and without Shift and expects the Shift travel to be about a tenth. A second test drags them with Cmd, Ctrl and Alt and expects the plain
rate (a control that Cmd-click resets skips the Cmd drag). Source scans fail on a
`juce::Slider` that is not a `FineDragSlider`, on a `FineDragSlider` that stops switching the key swap off, on code that
turns a velocity or modifier-key drag mode back on, and on a new `mouseDrag` class that is in neither list. The list of
exceptions (drags that move something other than a value) may only shrink.
