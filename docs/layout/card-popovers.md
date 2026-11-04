# Popovers a card opens

A popover a card opens (a sequencer's Randomize panel, its key and chord pickers, the instrument and control-style
panels, Replace with, a hosted plugin's modulation target list) is a `juce::CallOutBox`, its own top-level window, so a
key it does not use never reaches the main window: Cmd+Z and Space would die there.

## Opening one

Open it with `launchCardCallOut(content, screenArea, anchor)` (`Source/UI/Layout/CardCallOut.{h,cpp}`), never
`CallOutBox::launchAsynchronously`. It puts a key listener on the box that hands the keys
`isWindowForwardableShortcut` allows (undo, redo, play/stop) to the main window that holds `anchor`, the way the shape
editor window does (`makeAppShortcutRelay`). The listener sees a key only after the focused child and its parents have
declined it, so a text field's own Cmd+Z and Escape keep working, and keys that act on the canvas selection (copy, paste,
select all) stay with the popover.

## Randomize panel

Undo and redo move the range while the panel stays open: the card tells its open panel (`syncOpenRandomizePanel`) to
show the range the pattern now has.

## Guard

`CardCallOutRelayTests` presses Cmd+Z and Cmd+Shift+Z from every focusable child of the Randomize panel and checks the
range, and lists every remaining raw `CallOutBox::launchAsynchronously` call with the reason it does not relay (a
settings window, a layout editing session, a main-window panel). A new raw call fails there by name.
