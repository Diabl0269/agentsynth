# Mixer strip focus

The mixer panel is the one focusable leaf of its region, and `MixerPanelComponent::focusedColumnIndex_` is the column
the keys act on ([panel](panel.md#keyboard-navigation-and-accessibility)). Left and Right move it; so does the mouse.

## A click focuses the column

A press anywhere in a column (a control, the header or the empty space beside the name) focuses that column, without
scrolling, and shows its outline. A right-click does too, so the outline shows which strip the header menu acts on.
The panel hears every press through a nested mouse listener (`MixerPanelComponent::focusColumnFromClick`,
`MixerPanelKeyboard.cpp`) and takes the keyboard itself: left alone, JUCE would hand it to the column's first focusable
child, the colour dot, which is why the column's keys used to miss. A pressed control that takes the keyboard (the
rename editor, the colour dot) keeps it.

A press in a strip's empty send area still opens the Add Send menu as before; the column is focused all the same.

## The outline follows the keyboard

When another control takes the keyboard (a click on the canvas) the outline goes; the column keeps its place, and the
outline comes back with the keyboard. A pop-up window (a menu) holding the keyboard, or the app going to the background,
keeps it (`MixerPanelComponent::noteKeyboardMoved`, `MixerPanelRowKeyboard.cpp`).

## Duplicate

**Cmd+D** resolves the track row's `timelineDuplicateFocusedTrack` (one rebind covers both) and copies the focused
strip's track: only a strip fed by exactly one track, else the key bubbles. It is the same `onDuplicateTrack` path as
the header menu's **Duplicate Track**, which names the key. From the mixer, `MainComponent::duplicateOneTrack` copies
that one track (never the timeline's selection) and leaves the keyboard on the strip; the strip keeps its focus across
the rebuild by uuid, so every press copies it again.

## Colour

**C** (`mixerOpenColour`, Mixer category, rebindable) opens the focused strip's colour picker on its dot, the same
`MixerPanelComponent::openColourPicker` a click on the dot calls (`openFocusedColour`, `MixerPanelColour.cpp`). A
column with nothing to recolour (Direct, Master) lets the key bubble. The dot's tooltip names the key and follows a
rebind. The dot itself takes Return only, so after a click lands on it Space still plays: it is the one mixer button
`Tests/UI/Mixer/MixerSpaceKeyTests.cpp` expects to leave Space alone.

## Pan and hide

**Shift+Left / Shift+Right** (`mixerPanLeft`, `mixerPanRight`) move the focused strip's pan a tenth of the way left or
right per press, clamped at either end, through the pan knob itself (`MixerColumnComponent::nudgePan`), so each press is
one undo step, as a click on the knob is. Master's pan is its pan law and Direct has none, so the keys bubble there.
**H** (`mixerToggleChannelHidden`) hides the focused column (not Master) as one undo step and hands the focus to its
right-hand neighbour, or the left one at the end (`MixerPanelComponent::hideFocusedChannel`, through `focusFallback_`).
In the side pane's list the same key shows or hides the cursor row, as Space does. The pan knob's and the eyes' tooltips
name the keys and follow a rebind.

## Test mode

`getState` on `mixerFocusedColumn` (a hidden child of the panel) reports `focusedColumn`, the title of the column whose outline shows (empty when none).
`Tools/ui-verify/scenarios/mixer-strip-click-focus.yaml` clicks a strip, duplicates it three times, clicks the canvas
and presses Cmd+Up; `Tests/UI/Mixer/MixerPanelKeyboardFocusTests.cpp` drives the same through the real ComponentPeer.
`mixer-colour-key.yaml` opens the picker with C and with Return on the dot; `MixerColourDotTests.cpp` pins the keys.
`mixer-pan-hide-keys.yaml` pans and hides a strip and shows it again; `MixerZonesKeyboardTests.cpp` pins those keys.
