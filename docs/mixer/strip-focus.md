# Mixer strip focus

The mixer panel is the one focusable leaf of its region, and `MixerPanelComponent::focusedColumnIndex_` is the column
the keys act on ([panel](panel.md#keyboard-navigation-and-accessibility)). Left and Right move it; so does the mouse.

## A click focuses the column

A press anywhere in a column (a control, the header or the empty space beside the name) focuses that column, without
scrolling. A right-click does too, so the ring shows which strip the header menu acts on.
The panel hears every press through a nested mouse listener (`MixerPanelComponent::focusColumnFromClick`,
`MixerPanelKeyboard.cpp`) and takes the keyboard itself: left alone, JUCE would hand it to the column's first focusable
child, the colour dot, which is why the column's keys used to miss. A pressed control that takes the keyboard (the
rename editor, the colour dot, a bypass or sources button) keeps it and draws its own ring or caret, and the strip then
draws none; a press on a fader, knob, mute or solo hands the keys to the panel and the strip rings.

## One mark per state

A strip shows at most two marks, one for each state: its **selection** is the accent wash (14 percent, over the plain
surface), and **key focus** is the shared focus ring (`synth::ui::paintFocusRingAlways`, the same ring every control
draws; Direct and Master draw it too). There is no third "revealed" border: the channel chip's reveal
(`MixerPanelComponent::revealColumn(id, takeKeys = true)`) makes the strip the focused one and hands the mixer the keys,
so the ring is its "found it"; a control surface's select only scrolls. The strip's grey text (peak readout, meter
ticks, list captions) is painted through `readableOnStrip` (`MixerStripText.h`): moved toward white or black until it
reads at 4.5:1 on the plain strip and on the washed one alike, in every theme, so it does not shift as the wash fades.

A press in a strip's empty send area still opens the Add Send menu as before; the column is focused all the same.

## The outline follows the keyboard

When another control takes the keyboard (a click on the canvas, or a strip's own control) the ring goes; the column
keeps its place, and the ring comes back with the keyboard. A strip's control that holds the keys makes its strip the
focused one (`focusOfChildComponentChanged`), and a strip key it lets bubble (M, Cmd+Backspace, Option+arrows) acts on
that strip and hands the keys back to the panel (`MixerPanelComponent::keyPressed`), so the ring shows where it acted. A pop-up window (a menu) holding the keyboard, or the app going to the background,
keeps it (`MixerPanelComponent::noteKeyboardMoved`, `MixerPanelRowKeyboard.cpp`).

The ring is read from real state, never trusted from the last event: `isKeyboardAwayNow()` says the keys are away when the
panel is hidden (a swapped dock tab: JUCE drops its focus while nothing is focused yet, which looks like the app in the
background, and the focus then lands on a sibling that never tells the panel) or when the side pane's list holds
them (it shows its own cursor). `noteKeyboardMoved()` runs on every focus change, from `visibilityChanged()` (so a tab
that comes back shows no ring until the keys are on it) and from `paintOverChildren()`, so no event can leave a stale
ring. A track row's focus outline already reads `hasKeyboardFocus(true)` at paint time and needs no such cache.
Tests: `ReachSweepShell.ARingedMixerStripLosesItsRingWhenAnotherTabTakesTheKeysAndTheMixerComesBack`
(`Tests/UI/Layout/ReachSweepTests.cpp`) and `StripPressReach.APressMovesTheStripRingToWhereTheKeysActAndAStripKeyBringsItBack`
(real presses through the window's peer, as test mode sends them; `Tests/UI/Layout/KeyboardReachTests.cpp`).

## Duplicate

**Cmd+D** resolves the track row's `timelineDuplicateFocusedTrack` (one rebind covers both) and copies the focused
strip's track: only a strip fed by exactly one track, else the key bubbles. It is the same `onDuplicateTrack` path as
the header menu's **Duplicate Track**, which names the key. From the mixer, `MainComponent::duplicateOneTrack` copies
that one track (never the timeline's selection) and leaves the keyboard on the strip; the strip keeps its focus across
the rebuild by uuid, so every press copies it again.

## Select, delete and move strips

A strip fed by exactly one track selects through the timeline's selection model (`TimelinePanelComponent`'s
`TrackSelectionModel`, reached through `MixerPanelComponent::trackSelectionHost`, which `MainComponent` wires). A press
on the strip itself selects the track like a row does: any spot that is not a control counts, the header and name, the
meter, the space between sections and a list's empty area alike. Nothing lists those parts: `isSelectionSurface` walks
from the pressed part up to the strip (`pressSelectsSurface`, `UI/Layout/SurfacePress.h`) and the press is a control's
when a part on the way is a button, slider, text field or combo box, or one of the strip's parts that act on a press
only in places and say so in `MixerColumnComponent::partHandlesPress` (a list row or its add space, a divider, a hidden
section's strip, the EQ curve). `MixerTrackSelection.EveryPointOfAStripEitherSelectsItOrBelongsToAControl` presses a
grid over a strip, so a new part that acts on a press but is missing there fails it. Plain selects, **Cmd** toggles,
**Shift** extends from the anchor; a Cmd or Shift press only edits the selection and does not also pick the macro on
the canvas. The same press works in the detached mixer window, which shows the same selection. The arrow keys carry
the selection with the focus, **Shift+Left / Shift+Right** extend it one strip at a time from the anchor (the strip the
key started on) and the other way shrinks it back, as Shift+Up / Shift+Down do on the timeline's rows (a fixed chord;
the pan keys took Option+Shift+arrows for it), and **Cmd+Space** toggles the focused strip's track. A selected strip is
washed with the accent at 14 percent (`MixerColumnComponent::setTrackSelected`), over its border.

The wash belongs to the strips' own selection: it shows only while the selection was last set from a strip
(`TimelinePanelComponent::isTrackSelectionFromMixer`), so a row picked on the timeline washes no strip. The model is
still the one shared set; the timeline's pick just is not "selected in the mixer", and a delete or move from a strip then
acts on that strip's track alone (`claimTrackSelection`). Buses, Direct, Master and a strip several tracks feed are not
selectable.

**Cmd+Backspace** (`timelineDeleteFocusedTrack`) and the header menu's **Delete Track** / **Delete N Tracks** delete the
selected set, or just the focused strip's track when it is not in the set. They go through `deleteTrackAfterConfirm`:
the same question (Remove or Keep macro when one would go) and the same "ask before deleting" preference, one undo step.

**Option+Left / Option+Right** moves the set one place (fixed chords, not rebindable). The order change is the
timeline's own (`TimelineDoc::moveTrack` inside one `performTrackEdit`, one undo step), and the strips glide with the
column drag's `ReorderDragAnimator`; under reduced motion they jump. At the edge nothing moves, the key is still taken.
The detached mixer window follows every selection change as the docked one does.

## Colour

**C** (`mixerOpenColour`, Mixer category, rebindable) opens the focused strip's colour picker on its dot, the same
`MixerPanelComponent::openColourPicker` a click on the dot calls (`openFocusedColour`, `MixerPanelColour.cpp`). A
column with nothing to recolour (Direct, Master) lets the key bubble. The dot's tooltip names the key and follows a
rebind. The dot itself takes Return only, so after a click lands on it Space still plays: it is the one mixer button
`Tests/UI/Mixer/MixerSpaceKeyTests.cpp` expects to leave Space alone.

## Pan and hide

**Option+Shift+Left / Option+Shift+Right** (`mixerPanLeft`, `mixerPanRight`; Shift+arrows extend the strip selection, [below](#select-delete-and-move-strips)) move the focused strip's pan a tenth of the way left or
right per press, clamped at either end, through the pan knob itself (`MixerColumnComponent::nudgePan`), so each press is
one undo step, as a click on the knob is. Master's pan is its pan law and Direct has none, so the keys bubble there.
**H** (`mixerToggleChannelHidden`) hides the focused column (not Master) as one undo step and hands the focus to its
right-hand neighbour, or the left one at the end (`MixerPanelComponent::hideFocusedChannel`, through `focusFallback_`).
In the side pane's list the same key shows or hides the cursor row, as Space does. The pan knob's and the eyes' tooltips
name the keys and follow a rebind.

## Test mode

`getState` on `mixerFocusedColumn` (a hidden child of the panel) reports `focusedColumn`, the title of the column whose outline shows (empty when none), and `focusedRow` / `focusedRowIndex`, the focused row's screen-reader text and index (empty and -1 in column mode), and `zonesCursor`, the channel the side pane's list cursor is on while the list holds the keys.
`Tools/ui-verify/scenarios/mixer-strip-click-focus.yaml` clicks a strip, duplicates it three times, clicks the canvas
and presses Cmd+Up; `Tests/UI/Mixer/MixerPanelKeyboardFocusTests.cpp` drives the same through the real ComponentPeer.
`mixer-colour-key.yaml` opens the picker with C and with Return on the dot; `MixerColourDotTests.cpp` pins the keys.
`mixer-pan-hide-keys.yaml` pans and hides a strip and shows it again; `MixerZonesKeyboardTests.cpp` pins those keys.
