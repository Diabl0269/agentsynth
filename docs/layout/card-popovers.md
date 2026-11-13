# Popovers a card or a main-window panel opens

A popover a card opens (a sequencer's Randomize panel, its key and chord pickers, the instrument and control-style
panels, Replace with, a hosted plugin's modulation target list) or a panel of the main window opens (the timeline's track
colour, key, add-track, automation and MIDI destination pickers, the mixer's colour and insert pickers, the MIDI Remote
panel's pickers, the Mod Matrix pickers, Scale assist's key picker, the pen's shape flyout, the library's help popover)
is a `juce::CallOutBox`, its own top-level window, so a key it does not use never reaches the main window: Cmd+Z and
Space would die there.

## Opening one

A popover's content paints an opaque themed panel of its own (`windowBackgroundFor(themeOf(*this))`, the theme's `border` outline and corner radius), because the call-out's bubble behind it is JUCE's translucent dark scheme: dark rows on that bubble were unreadable in the light theme. `ThemeSweepPopovers` lists every content class in `PopoverRegistry.h` and renders the ones that have a fixture in every theme (`ThemeSweepPopoverTests.cpp`); a new content that is in neither the swept nor the exempt list fails the test.

Open it with `launchPopover(content, screenArea, anchor, options)` (`Source/UI/Layout/PopupHost.{h,cpp}`), never
`CallOutBox::launchAsynchronously`. A popover a button toggles lives in a `PopoverSlot`: `open` returns `Opened`,
`ClosedIt` (the popover was open, so the button closed it) or `Ignored` (it is already leaving). `options.trigger` is the
button: a press on it or on the area closes the popover with a fade and is consumed, so it cannot reopen it; a press
elsewhere closes it the JUCE way and commits. Esc runs the content's `PopoverContent::onPopoverCancel()` (the colour
picker puts its preview back) and fades it out. A popover rises 8 px out of what it points at, unless its content
overrides `PopoverContent::growsFromOpener()` (the searchable pickers do): it then grows out of the area it points at
and shrinks back into it, the box's own background scaling with the content ([animation](animation.md#popup-windows)). A key listener on the box hands the keys `isWindowForwardableShortcut`
allows (undo, redo, play/stop) to the main window that holds `anchor`, the way the shape editor window does
(`makeAppShortcutRelay`). A card's anchor reads the shortcut table from its canvas; a main-window panel's from the main
window's canvas, so `anchor` is the button or panel the popover points at. A popover that needs its box back (MIDI
Remote) uses the box `launchPopover` returns; the library's help popover builds its own box and calls
`relayAppShortcutsFromCallOut` on it. The listener sees a key only after the focused child and its parents have
declined it, so a text field's own Cmd+Z and Escape keep working, and keys that act on the canvas selection (copy, paste,
select all) stay with the popover.

Every popover hosts its own tooltip window (the call-out is its own native window, so the main window's tip would draw under it): any control in any content gets its hover tip, a greyed one's reason included, with no work in the content. See [Tooltips](animation.md#tooltips).

## After an undo

A popover that lists or shows project state built when it opened can go stale under an undo. Its content implements
`AppUndoAware` (`Layout/PopupHost.h`), which the relay calls before the app runs undo or redo and after. The pickers
that list modules, tracks or destinations (`ModMatrixPicker`, `KeyPicker`, `MidiDestinationPicker`, which the launch asks to close with `closeOnUndo`) close without picking.
`ColourPickerPopup` puts its live preview back first (as Escape does, so the undo works on the project as it was), then
closes without applying or restoring anything. Content that shows nothing from the project (the MIDI Remote popovers,
the shape flyout, the help popover) stays open; the Randomize panel re-syncs as below.

## Escape gives the keyboard back

Escape closes the popover through the live fade and then gives keyboard focus back to `PopoverOptions::trigger` when
it takes focus. A content that returns focus itself (the mixer picker, the morph map) does so after the box is gone and
wins. `ColourPickerEscapeTests` sends Escape from every control of the colour picker and records the alpha frames: one
fall, no leaving picture on top of the live fade, the colour put back, focus on the opener.

## Randomize panel

The Randomize button owns a `PopoverSlot` (`StepGridComponent::randomizeSlot_`), so it toggles the panel: a press while it
is open closes it through one fade, one while it fades does nothing, and the keyboard shortcut that opens Randomize
toggles it the same way (`StepGridRandomizeToggleTests`, `randomize-toggle.yaml`).

Undo and redo move the range while the panel stays open: the card tells its open panel (`syncOpenRandomizePanel`) to
show the range the pattern now has.

## Guard

`PopoverRelayTests` presses Cmd+Z and Cmd+Shift+Z from every focusable child of the Randomize panel and checks the
range. It also opens the main-window popovers through their real buttons in a real `MainComponent` (the timeline track
colour swatch, the transport Key field, the mixer colour dot and "+ Insert" row, Scale assist's key field;
`MidiRemotePanelTests` does the MIDI Remote one) and presses Space, Cmd+Z and Cmd+Shift+Z in each in real key order
(`MainPopoverKeys.h`): the app plays, undoes and redoes, canvas keys (Cmd+A, Cmd+C, Delete) stay with the popover, and a
popover that would be stale closes. `PopupOwnPathTests` fails by name when a file opens a call-out outside `PopupHost`, or opens a popover that
`PopoverRegistry.h` does not list; `PopupHostBehaviourTests` opens every popover content in Full, Reduce Motion and
Animations Off and checks it fades in and out once, toggles on its button, closes on Esc and on a click away, and never
flickers. The scenario `Tools/ui-verify/scenarios/main-popover-undo.yaml` shows it in the app. `popup-host-toggle-esc.yaml` and `randomize-toggle.yaml` show the toggle and Esc.
