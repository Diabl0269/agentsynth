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

## A press outside what it draws

A popover's native window is a rectangle around its bubble, so the strip on each side of the arrow and the shadow margin
are inside the window but outside anything it draws. JUCE's call-out takes only the bubble (`CallOutBox::hitTest`), and a
press the window's components all decline is not passed through to the window underneath: the OS gives it to the
popover's window, where it reached nothing, and the popover stayed open. So the box takes its whole window
(`PopoverBox::hitTest`) and sorts a press by the bubble's shape: on the bubble or its arrow it is the popover's; anywhere
else in the window it is a press elsewhere. On its release the box closes the JUCE way (the leaving picture fades it) and
`PressOutsideShape::land` (`UI/Layout/PressBeneath.h`) sends the click to what is under it, through that window's peer,
so it lands where it was aimed as a press on another window would. A press there on the trigger or the area the box
points at closes it through the fade and is consumed, as on the trigger itself. The two other windows with a transparent
margin do the same: the mod dot panel frame (`ModDotPanelFrame`) and the note-length popup (`NoteLengthPopup`). The rule:
a window or popup never has a clickable area larger, or smaller, than what it draws.

## Where a menu opens

A menu a key opened (Return on a focused item, Shift+F10, the menu key) anchors to the focused item; a menu the pointer
opened opens at the pointer. `contextMenuOptions(keyboardAnchor)` (`UI/Layout/ContextMenuPlacement.h`) is the one place
that decides it. A hook that forwards a menu to an owner must pass the anchor on only for a key press (the morph map
tells it with `MorphMap::menuOpenedFromKeyboard()`), never drop the area. `MenuPlacementRegistryTests` scans Source: every
`showMenuAsync` names its placement (or is on a shrink-only list) and no `showMenu` hook ignores its area; the card's
dot menu is checked by key and by right-click in `ModuleComponentMorphShapesCardTests`.

## From a card's right-click menu

A popover a card's right-click menu item opens (Outputs..., Control Style..., Replace with..., a hosted plugin's Edit
Layout...) points at where the menu was opened, not at the card or the button the same panel has: `ModuleComponent::showContextMenu`
records the pointer in `menuPopoverAnchor()` (`popoverAreaAtPoint`, `UI/Layout/ContextMenuPlacement.h`, the same small
square `HostedModTargetPicker` uses), and the item's action reads it, because the pointer is on the item by then. The call-out flips
and clamps it to the screen, so a tall panel opened from a card at the top of the canvas still fits. Opened from the
panel's own button, or from the keyboard (a menu opened with the keyboard has no pointer, so `menuPopoverAnchor()` is empty), the popover
keeps its usual anchor: the button, the card, the split button. A new card menu item that opens a popover takes
`menuPopoverAnchor()` the same way; `HostedPluginOutputsPanelTest.EveryPopoverTheCardMenuOpensPointsAtTheClick...` lists the
menu's "..." items and fails on one it does not know.

## Panels in a frame window

The mod dot's panel (knob), the output jack's connections panel and the modulation curve popover open through
`launchInFrame` (`Source/UI/Graph/ModDot/ModDotPanelLaunch.cpp`): a `ModDotPanelFrame`, its own top-level window, not a
`CallOutBox`. They relay the app's shortcuts too: `launchInFrame` sets the frame's `onAppShortcut` to
`makeAppShortcutRelay(&anchor)` (anchor = the dot, knob, card or chip the panel points at), and the frame offers it every
key but Esc after the focused child and its parents declined it. Space, Cmd+Z, Cmd+Shift+Z, Cmd+J and the other keys
`isWindowForwardableShortcut` allows reach the app with the panel open; typing in its search box, Esc, and the canvas
selection keys (copy, paste, select all, Delete) stay with the panel. These panels do not implement `AppUndoAware`:
they follow the graph on the canvas's tick (`PortPanelController::tick`, `ModDotController::tickPopover`,
`tickModCurvePopover`), so an undo re-syncs the open panel, or closes it when its connection or card is gone. A new
window put on the desktop with `showOnDesktop()` must go through `launchInFrame` (`FrameRelayScan` fails otherwise).

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

`ModDotFrameRelayTests.cpp` (`FrameRelay`) opens each frame panel through its real owner in a real `MainComponent` and presses Space, Cmd+Z, Cmd+Shift+Z and Cmd+J through the frame's key order (the toggle shortcut and the window kind that `PopoverRelayTests` never covered); the scenario `moddot-panel-shortcuts.yaml` shows it in the app.

`PopoverRelayTests` presses Cmd+Z and Cmd+Shift+Z from every focusable child of the Randomize panel and checks the
range. It also opens the main-window popovers through their real buttons in a real `MainComponent` (the timeline track
colour swatch, the transport Key field, the mixer colour dot and "+ Insert" row, Scale assist's key field;
`MidiRemotePanelTests` does the MIDI Remote one) and presses Space, Cmd+Z and Cmd+Shift+Z in each in real key order
(`MainPopoverKeys.h`): the app plays, undoes and redoes, canvas keys (Cmd+A, Cmd+C, Delete) stay with the popover, and a
popover that would be stale closes. `PopupOwnPathTests` fails by name when a file opens a call-out outside `PopupHost`, or opens a popover that
`PopoverRegistry.h` does not list; `PopupHostBehaviourTests` opens every popover content in Full, Reduce Motion and
Animations Off and checks it fades in and out once, toggles on its button, closes on Esc and on a click away, and never
flickers. It also presses, through the box's window, every corner outside the bubble and the strip on each side of the
arrow for every content: the popover closes and the click lands on the window under it, while a press on the bubble or
the arrow keeps it open. `PopupDismissPathsTests` presses the margins of the mod dot panel and the note-length popup the
same way. The scenario `Tools/ui-verify/scenarios/main-popover-undo.yaml` shows it in the app. `popup-host-toggle-esc.yaml` and `randomize-toggle.yaml` show the toggle and Esc.
