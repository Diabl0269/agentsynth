# Accessibility

Every control in the app must work for someone who never touches a mouse or who hears the UI through
a screen reader. The coverage test (below) keeps the gaps from growing.

## The rule

Every new or changed control:

1. **Is keyboard-reachable** - either it is its own Tab stop (`setWantsKeyboardFocus(true)`), or it
   sits inside a focus region that moves between its items with the arrow keys (the mixer panel is
   one focusable leaf; its columns and faders are reached with Left/Right/Up/Down, and Tab moves from a
   column into its send and insert rows; the small icon buttons that take no key of their own -- a column's colour dot
   and sources badge, and each row's bypass button -- are Tab stops too). It acts on
   Space/Enter or the arrow keys the way a native control would, and every new key is a rebindable
   action (below). The exceptions are the arrow keys of a list of controls (below), the Settings window's
   Cmd+1..9 (a fixed positional key; see Switching tabs and Tab strips), and a key inside a focused editor that a native control would also
   own (the arrows, Return and their modifiers in the EQ curve and the curve editor): that is the
   control's own behaviour, not a global shortcut, and is documented with the view
   ([`visualizers.md`](../layout/visualizers.md)).
2. **Shows the accent focus ring** when it holds keyboard focus.
3. **Has a screen-reader name** - `setTitle("...")` on the component (a button's text counts), plus a
   value or role where it applies (a slider's `textFromValueFunction`, a meter's accessibility value).
4. **Has a tooltip** that names its shortcut when it has one (`Play (Space)`).

## Lists with painted rows or messages

A list whose rows are painted (the module library) is one element with a `list` handler whose
read-only value text follows the keyboard-focused row, and it posts a value-changed event only when
that text changes; the wording lives in a pure header with a unit test
([`docs/layout/module-library.md`](../layout/module-library.md#screen-reader)). A list of real
components (the AI chat's messages) is a `list`-role container whose children are `listItem`-role
components titled with their text ([`docs/ai/chat-component.md`](../ai/chat-component.md#screen-reader)).

A stock JUCE list whose rows ignore the selection when they paint (the Settings Audio tab's channel and
MIDI input lists inside `juce::AudioDeviceSelectorComponent`) gets `synth::ui::ListBoxFocusRing`
(`Source/UI/Layout/ListBoxFocusRing.h`): while such a list has keyboard focus the accent ring is drawn
around its selected row, and a list that takes focus with nothing selected selects row 0 (selecting never
flips a tick; Return does). The selector rebuilds its lists when the device changes, so the helper re-scans on
child changes and focus changes, and polls the selected row with a light timer only while a list has focus.

## Focus order

Focus moves between regions with Tab/Shift+Tab (`control/focus-regions.md`) and inside a region with that region's own
keys. A region whose root is a plain container (the Timeline's routing pane, the piano roll's scale pane) takes focus
itself, enters its controls with Down, steps with Up/Down in on-screen order and returns with Escape
(`Source/UI/Layout/FocusStepWithin.h`); its controls are Tab-reachable through the region cycle, not as stops of the region
before it. The timeline's track column is one Up/Down walk of track rows and their open lanes. Arrows only navigate where the
focused control does not use them itself (a combo box or text field keeps its Up/Down).

A track row also takes Cmd+D (rebindable `timelineDuplicateFocusedTrack`) to duplicate its track, named in the row's tooltip and
beside "Duplicate Track" in its menu; focus follows the copy. It takes Ctrl+E (`timelineShowFocusedTrackModule`) to show its module on the canvas and Ctrl+Cmd+E (Ctrl+Alt+E off the Mac, `timelineToggleFocusedTrackPluginWindow`) to open or close a hosted plugin's window, both named in its tooltip; the row's target button does the same, is a Tab stop with the focus ring, and is named "Show <track> module". A row that has a menu (a track row, an automation lane header, a modulator row) has no "..." button: the menu is a
right-click, Shift+F10 (`KeyboardContextMenuProvider`) or Return on the focused row, the row is named for what it is
("Cutoff automation lane") and its tooltip lists the three ways in. A picture that only shows state (the modulator
shape icon) is named and tooltipped but is not a Tab stop; the control it mirrors stays the one that edits.

The track row's Cmd+Backspace question is an alert window: Delete is the default button (Return confirms; Cmd+Z undoes it),
Escape cancels, Tab reaches the **Don't ask again** box (named, tooltipped, with the preference's way back) and **Delete**,
and every button draws the shared focus ring. It appears and disappears with the popup motion
([animation](../layout/animation.md#popup-windows)); the shared window is `showConfirmDontAsk` (`ConfirmDontAskDialog.h`).

## Module cards

The canvas is one focus region; inside it the arrows move between cards, Return steps into the selected card,
Tab/Shift+Tab walk that card's controls, and Escape (or Tab past the last control, Shift+Tab before the first) goes back out with the card still selected, so the next Tab moves to the next region
([shortcuts](../control/shortcuts.md#canvas-card-keys)). The selected card's accent border is its ring;
each control inside draws its own (`AppLookAndFeel`). A card is a `group` named after its title
(`ModuleComponentAccessibility.cpp`, through `TooltipHelpHandler`). Every knob, combo and toggle on it is named
after its parameter and gets a tooltip naming it, by one pass that runs after the card has built its controls
(`applyControlAccessibility`; it also tells the MIDI Learn registry, which composes a mapped control's
tooltip from the base one). A knob's spoken value is the parameter's own text through the slider's text
function, so a frequency parameter that formats itself (`Source/Modules/FrequencyText.h`, the Filter cutoff)
reads "Cutoff, 1.2 kHz". Card knobs are `CardKnobSlider`s: they take focus and each key step is one change
gesture (one undo step). A card fader (`CardFader`) is a slider too, with the same keys (arrows, Shift for a
fine step, Page Up/Down, Home/End) and its focus ring drawn round the cap by the fader painter. A
segmented switch (`CardSegmentedSwitch`) is ONE Tab stop that Left/Right/Home/End move; to a screen reader it
is a `group` named after the parameter holding one titled radio button per value, the selected one checked.
A stepper (`CardStepper`) is two buttons, each a Tab stop titled after the parameter ("Octave down",
"Octave up"), and the arrows step while either has focus. A composite control (the stepper) is registered for
MIDI Learn as a whole: a right click on any part of it resolves to the registered ancestor
(`ModuleComponent::mouseDown`). None of these keys is global, so none is a `ShortcutManager` action. The painted jacks get invisible, click-through, unfocusable stand-ins named
"Audio L input" and so on, for the accessibility tree only; there is no keyboard cable creation.

## Which focus helper

| Thing that takes focus | Helper |
| --- | --- |
| A whole panel or focus region | `paintFocusRegionOutline` (`Source/UI/Layout/FocusRegion.h`) - translucent, panel-weight outline |
| A small control (button, toggle, knob, slider, custom widget) | `paintFocusRing` (`Source/UI/Layout/FocusRing.h`) - solid accent ring, 1.5x the theme border weight (2.5x on a light theme, where a thin ring vanishes against a white control) |

A ring that follows a tracked item rather than the component's own focus (the piano roll's focused
note) calls `paintFocusRingAlways`, which draws the same ring without the focus check.

A control that is only painted (no child component), such as the piano roll's header chips, gets a
transparent child button over it: it is the Tab stop and carries the name, role and tooltip, while
the parent keeps handling the pointer (`PianoRollHeaderChip`).

`AppLookAndFeel` already draws the ring for buttons, combo boxes, text editors, toggles, rotary
knobs and sliders, so a stock control needs nothing. A custom-painted control calls
`paintFocusRing(g, area, *this, cornerRadius)` at the end of its `paint()` and repaints in
`focusGained`/`focusLost`.

## Adding a rebindable key

1. Add a row to `ShortcutManager::getActionTable()` (`Source/ShortcutManager/ShortcutManagerActionTable.cpp`),
   keeping each category's rows contiguous.
2. Give it a default in its category's `add…DefaultBindings()` in
   `Source/ShortcutManager/ShortcutManagerDefaults.cpp`.
3. Add its `getActionDescription` text in `Source/ShortcutManager/ShortcutManagerActionNames.cpp`.
4. Update the row-order pin in `Tests/UI/Settings/ShortcutsSettingsTabTests.cpp`.
5. Add it to [`docs/control/shortcuts.md`](../control/shortcuts.md).
6. A command action also needs its `AppCommands.h` enumerator and `getCommandForAction` mapping, and a row
   in `MainComponentCommandTable.cpp` (the row-order pin is `MainComponentCommandTableTests.cpp`).

## The coverage test and its baseline

`Tests/UI/Accessibility/AccessibilityCoverageTests.cpp` audits every visible, accessible interactive
control (buttons, sliders, combo boxes, text editors, anything that wants keyboard focus) on a
headless `MainComponent` (panel state pinned: library open, bottom dock open on the Timeline tab, AI chat and mod matrix closed, so the count does not depend on saved settings), one card for every built-in module type (`ModuleCards`, one aggregate entry; types needing a plugin binary, a timeline track or mixer/macro plumbing are skipped, the list is in the test), each Settings tab, the module library, the AI chat (one message in the list), the MIDI Remote panel (a controller from a template with one control selected), the Mod Matrix (two routings, `ModMatrix`) and its search picker (`ModMatrixPicker`), the mod dot's panel on both its pages (`ModDotPopover`), the piano roll (a clip loaded, the velocity strip shown, the scale-assist panel open on its custom-scale editor), the mixer panel (`Mixer`: two tracks, a bus, a send and an EQ insert, with sends, inserts and EQ shown), and every dialog and popup that can be built without a window (Export Audio, Sign in, Configure I/O for a macro, the macro auto-port prompt, the per-module Dual I/O popup, the EQ window, the module views built on their own (`ModuleViews`: the EQ curve, the curve editor and the threshold control, the three that take focus, with the read-only visualizers beside them), the welcome screen, the colour picker, the module library help popover), and counts two gaps per
surface: **missingName** (empty `setTitle`, and for a button empty text; a custom accessibility handler is not consulted because it does not exist without a native window) and **missingTooltip**. The counts must equal the
entry in `Tests/UI/Accessibility/AccessibilityBaseline.h`:

- More gaps than the entry fails and prints every gap path: fix the control you just added.
- Fewer gaps than the entry passes but prints a `[ NOTE ]` line: lower the entry in the same change
  that fixed the gap. (Equality would be stricter, but a few controls exist only on some machines, so
  it would fail on one CI platform or another.)
- Never raise an entry. A new surface is added with its real counts.

## Dialogs: Tab order, hidden stops, Escape

Every dialog, tab and popup is held to three more things, all in `Source/UI/Layout/DialogKeyboard.h`
and pinned by `Tests/UI/Accessibility/DialogKeyboardTests.cpp`:

- **Tab and Shift+Tab visit every control once, in visual order, and wrap.** JUCE sorts the siblings of
  one parent by their pixel `y` and then `x`, so controls centred at different heights in one visual
  row come out in the wrong order (the macro port rows do): give them `setExplicitFocusOrder`. A
  control that is visible but has no bounds (a hidden-by-layout combo that was never `setVisible(false)`)
  is a Tab stop nobody can see. The tests walk `juce::KeyboardFocusTraverser` (what Tab asks) with
  `walkTabOrder()` from `TabOrderHelpers.h` and assert the exact sequence of names.
- **No invisible Tab stops.** `removeHiddenTabStops(editor)` takes every focus-wanting part out of a
  `juce::TextEditor` (stock JUCE already keeps its viewport out, so this is the safety net; call it on
  each text field). The stops that really do hide are elsewhere: a `juce::Viewport` hosting a tab's rows
  wants focus by default (`setWantsKeyboardFocus(false)`, and `ScrollIntoViewOnFocus` scrolls the
  focused control into view instead), and an editable slider text box is its own stop, which an
  `ExpandingRangeSlider`-style subclass names (`<title> value`). `TabbedButtonBar` is a keyboard focus
  container whose buttons Tab never reaches, so the tab buttons are not stops at all: the strip is one stop
  (see [Tab strips](#tab-strips)). A tab's content wrapper never wants focus, and the window puts focus on
  the strip when it first shows.
- **Escape closes with Cancel semantics.** The surface's own `keyPressed` handles Escape (an
  `onRequestClose` callback if the caller set one, else `closeHostingWindow()`, which presses the
  hosting `DialogWindow`'s close button or dismisses the `CallOutBox`). A `juce::TextEditor` swallows
  Escape, so each text field calls `bubbleEscapeToParents` and Escape travels up as it would from any
  other control. Return presses the dialog's default button where it has one (Export, Open in Browser).

## Verifying for real

The test proves names exist, not that they read well or that Tab reaches them. Run the app, Tab
through the area you changed and watch the ring, then dump the macOS accessibility tree of the running
process with pyobjc (`AXUIElementCreateApplication(pid)`, walk `AXChildren`, print `AXRole`,
`AXTitle`, `AXValue`, `AXDescription`) and check the new names appear.

## Tab strips

**Every tab strip is one Tab stop, and every new one follows this rule** (the bottom dock's strip and the
Settings window's strip both do):

- One focusable leaf lays over the strip (`BottomDockComponent::TabStripFocus`, `SettingsTabs::StripFocus`);
  the tab buttons themselves do not want keyboard focus. The leaf has a screen-reader name and role and a
  tooltip, and draws the accent ring (`paintFocusRingAlways`) around the open tab's button while it has focus.
- With the leaf focused, plain Left / Right switch to the previous / next tab at once and stop at the ends
  (no wrap), Home / End jump to the first / last. The decision is one pure function,
  `synth::ui::tabStripKeyTarget` (`Source/UI/Layout/TabStripKeys.h`); a strip never re-implements it.
- Return (and Space where the surface wants it) moves focus into the open tab's first control (the dock hands
  it to the panel's region root); Tab goes into the content by normal traversal, so the leaf is ahead of the
  content in the Tab order, and Shift+Tab from the content's first control returns to the leaf.
- Keys with a modifier, Tab and Escape are not the strip's: they bubble up to the window.
- There is no global "next tab" chord: the former rebindable `tabPrevious` / `tabNext` actions were removed
  because the focused strip's arrows do the job. A saved binding that still names them is ignored on load.

## Switching tabs

- **A text field may swallow the key.** A key press walks up from the focused control and each
  component's key listeners run before its own `keyPressed`, but a `juce::TextEditor` is at the bottom of
  that walk and answers first: where the platform delivers a text character with Ctrl+digit it types the
  digit of Cmd+1 and consumes the key before the window's handler is reached. (On macOS the peer drops the
  text character while Command is held, so there the key already bubbles.) `synth::ui::TabSwitchKeys`
  (`Source/UI/Layout/TabSwitchKeys.h`) is the one shared guard: attach it to every text field of the surface
  (`attachToTextEditorsIn`) with the surface's tab handler. Combo boxes, sliders, toggles and buttons let
  the key bubble up untouched.
- **Cmd+1..9 is the second fixed key.** The Settings window opens its Nth tab on Cmd+N (a number past the
  last tab does nothing). The key names a position, so there is nothing to rebind, and the tab button
  tooltips say "(Cmd+N)"; holding Cmd over the window also shows each tab's Cmd+N as a hint badge
  (`ShortcutHintOverlay::addFixedKeyTarget`). It is the second exception to "every new key is a rebindable
  action", beside the list arrow keys below. The bottom dock's Cmd+1/2/3 are ordinary rebindable actions, and
  they (like Cmd+T when it opens the pane) leave focus on the dock's strip.
- The Settings window remembers its open tab by name (`settingsTabName`), not by position, because the Audio
  tab exists in the app and not in the plugin.
- **Tests** deliver the key through the listener-then-`keyPressed` walk with a text field as the starting
  point (`Tests/UI/Settings/SettingsWindowTabKeysTests.cpp`).

## Arrow keys in lists of controls

A Settings tab or a dialog is a list of controls, so the arrow keys walk it the way they walk the module
library's rows (`ModuleLibraryInput.cpp`). One helper, `ArrowKeyNavigation`
(`Source/UI/Layout/ArrowKeyNavigation.h`), gives a "scope" component these keys; it is a
`juce::KeyListener`, so it only sees keys the focused control did not consume:

| Key (no modifiers) | Focus on | Does |
| --- | --- | --- |
| Up / Down | any control in the scope | moves focus to the previous / next control in the order Tab walks (`juce::KeyboardFocusTraverser`), skipping controls that are hidden, disabled or have no bounds; clamped at the ends (the key is consumed, nothing wraps) |
| Right / Left | a `juce::ToggleButton` | ticks / unticks it through `setToggleState(..., sendNotification)`, the call a click makes, so the setting is saved and undo steps are recorded as on a click; idempotent; Left does nothing on a radio button |
| Left / Right | a `synth::ui::FoldableHeader` (a Preferences section header, a Keyboard Shortcuts section header) | folds / unfolds the section |

Every other key and every arrow with a modifier passes through untouched. Scrolling the newly focused
control into view is the owner's `ScrollIntoViewOnFocus`.

- **Attached to**: every Settings tab (`SettingsWindow` creates one per tab content), the Export Audio
  dialog, the Sign in dialog and the Configure I/O (macro port) dialog. The mixer, timeline, piano roll,
  module library, canvas and the card layout editor's list have their own arrow handling and do not use
  it. The layout editor's list is like the module library's: each row (a control, or a group header) is
  one focus stop that takes its own keys (Up/Down between rows; Space, Cmd+Up/Down and Enter are the
  rebindable Layout Editor actions), so the arrows walk rows and never land on a row's widget combo,
  which Tab reaches ([module-card-layout.md](../layout/module-card-layout.md#accessibility)).
- **Native controls keep their arrows.** A `ComboBox`, `Slider` or `TextEditor` consumes the arrows before
  they bubble, so a list can walk onto one but not through it: Down lands on a combo box, the next Down
  changes the combo's selection, and Tab is what moves on. The macro port rows' swatch and Delete buttons
  likewise keep their own arrows, and the Keyboard Shortcuts rebind button, while it listens, takes every
  key (arrows included) as the new binding.
- **A `juce::Viewport` is watched too.** `Viewport::keyPressed` consumes Up/Down for scrolling when its
  scrollbar shows, so the helper also listens on each plain viewport inside the scope
  (`watchViewportsInScope()`, or `watchViewport()` for a dialog that builds its viewport itself), ahead
  of the viewport's own handler.
- **Not rebindable**, same as the module library and the track header rows
  ([`shortcuts.md`](../control/shortcuts.md#settings-and-dialog-arrow-keys)): list navigation is native
  control behaviour, not an action. This is one of two exceptions to "every new key is a rebindable
  action"; the other is the Settings window's Cmd+1..9 ([Switching tabs](#switching-tabs)).
- **Headless tests** cannot hold real keyboard focus, so `ArrowKeyNavigationTests.cpp` supplies the
  "focused" component and the focus landing through the helper's test hooks and delivers keys the way the
  native window does (each ancestor's key listeners, then its `keyPressed`).

### The focus ring on a ticked check box

A ticked check box is filled with the accent colour, so a ring drawn against its edge merged into it.
While a toggle has keyboard focus `AppLookAndFeel::paintToggleButton` (behind `drawToggleButton`) strokes a
1 px separator in the page colour just outside the box and the accent ring beyond that, ticked or not.
The box gives up size only when the row is too short for the ring to fit inside the component (under
about 23 px). `FocusRing.h` is unchanged: every other ring in the app keeps its geometry.

## No all-caps UI text

UI text is never written in ALL CAPS: not by typing it that way and not with `toUpperCase()`. Headers and
column heads use Title Case ("Modulation Matrix", "Source"), buttons and strips sentence case ("Collapse
all"). Abbreviations that are capitals everywhere (MIDI, ADSR, JSON, NRPN) are fine. `scripts/check-ui-caps.sh`
enforces it in the Lint job (`scripts/tests/check-ui-caps.test.sh`): under `Source/UI/` it rejects any
`toUpperCase()` call and any string literal with a 4+ letter capitals word that is not in its
abbreviation list. A line that is not UI text (a hex colour code) ends with `// not-ui-text: <reason>`.
Comments are not checked.
