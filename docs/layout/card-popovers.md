# Popovers a card or a main-window panel opens

A popover a card opens (a sequencer's Randomize panel, its key and chord pickers, the instrument and control-style
panels, Replace with, a hosted plugin's modulation target list) or a panel of the main window opens (the timeline's track
colour, key, add-track, automation and MIDI destination pickers, the mixer's colour and insert pickers, the MIDI Remote
panel's pickers, the Mod Matrix pickers, Scale assist's key picker, the pen's shape flyout, the library's help popover)
is a `juce::CallOutBox`, its own top-level window, so a key it does not use never reaches the main window: Cmd+Z and
Space would die there.

## Opening one

Open it with `launchCardCallOut(content, screenArea, anchor)` (`Source/UI/Layout/CardCallOut.{h,cpp}`), never
`CallOutBox::launchAsynchronously`. It puts a key listener on the box that hands the keys
`isWindowForwardableShortcut` allows (undo, redo, play/stop) to the main window that holds `anchor`, the way the shape
editor window does (`makeAppShortcutRelay`). A card's anchor reads the shortcut table from its canvas; a main-window
panel's from the main window's canvas, so `anchor` is the button or panel the
popover points at. A popover that needs its box back (MIDI Remote) uses the box `launchCardCallOut` returns; the pen's
flyout and the library's help popover build their own box and call `relayAppShortcutsFromCallOut` on it. The listener sees a key only after the focused child and its parents have
declined it, so a text field's own Cmd+Z and Escape keep working, and keys that act on the canvas selection (copy, paste,
select all) stay with the popover.

## After an undo

A popover that lists or shows project state built when it opened can go stale under an undo. Its content implements
`AppUndoAware` (`Layout/AppUndoAware.h`), which the relay calls before the app runs undo or redo and after. The pickers
that list modules, tracks or destinations (`ModMatrixPicker`, `KeyPicker`, `MidiDestinationPicker`, which the launch asks to close with `closeOnUndo`) close without picking.
`ColourPickerPopup` puts its live preview back first (as Escape does, so the undo works on the project as it was), then
closes without applying or restoring anything. Content that shows nothing from the project (the MIDI Remote popovers,
the shape flyout, the help popover) stays open; the Randomize panel re-syncs as below.

## Randomize panel

Undo and redo move the range while the panel stays open: the card tells its open panel (`syncOpenRandomizePanel`) to
show the range the pattern now has.

## Guard

`CardCallOutRelayTests` presses Cmd+Z and Cmd+Shift+Z from every focusable child of the Randomize panel and checks the
range. It also opens the main-window popovers through their real buttons in a real `MainComponent` (the timeline track
colour swatch, the transport Key field, the mixer colour dot and "+ Insert" row, Scale assist's key field;
`MidiRemotePanelTests` does the MIDI Remote one) and presses Space, Cmd+Z and Cmd+Shift+Z in each in real key order
(`MainPopoverKeys.h`): the app plays, undoes and redoes, canvas keys (Cmd+A, Cmd+C, Delete) stay with the popover, and a
popover that would be stale closes. It lists every remaining raw `CallOutBox::launchAsynchronously` call with the reason
it does not relay (a settings window, a layout editing session, a macro prompt, a modal dialog). A new raw call fails
there by name. The scenario `Tools/ui-verify/scenarios/main-popover-undo.yaml` shows it in the app.
