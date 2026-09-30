# Accessibility

Every control in the app must work for someone who never touches a mouse or who hears the UI through
a screen reader. The coverage test (below) keeps the gaps from growing.

## The rule

Every new or changed control:

1. **Is keyboard-reachable** - either it is its own Tab stop (`setWantsKeyboardFocus(true)`), or it
   sits inside a focus region that moves between its items with the arrow keys (the mixer panel is
   one focusable leaf; its columns and faders are reached with Left/Right/Up/Down). It acts on
   Space/Enter or the arrow keys the way a native control would, and every new key is a rebindable
   action (below).
2. **Shows the accent focus ring** when it holds keyboard focus.
3. **Has a screen-reader name** - `setTitle("...")` on the component (a button's text counts), plus a
   value or role where it applies (a slider's `textFromValueFunction`, a meter's accessibility value).
4. **Has a tooltip** that names its shortcut when it has one (`Play (Space)`).

## Which focus helper

| Thing that takes focus | Helper |
| --- | --- |
| A whole panel or focus region | `paintFocusRegionOutline` (`Source/UI/Layout/FocusRegion.h`) - translucent, panel-weight outline |
| A small control (button, toggle, knob, slider, custom widget) | `paintFocusRing` (`Source/UI/Layout/FocusRing.h`) - solid accent ring, 1.5x the theme border weight |

`AppLookAndFeel` already draws the ring for buttons, combo boxes, text editors, toggles, rotary
knobs and sliders, so a stock control needs nothing. A custom-painted control calls
`paintFocusRing(g, area, *this, cornerRadius)` at the end of its `paint()` and repaints in
`focusGained`/`focusLost`.

## Adding a rebindable key

1. Add a row to `ShortcutManager::getActionTable()` (`Source/ShortcutManager/ShortcutManager.h`),
   keeping each category's rows contiguous.
2. Give it a default in `resetToDefaults()`.
3. Add its `getActionDescription` text in `Source/ShortcutManager/ShortcutManagerActionNames.cpp`.
4. Update the row-order pin in `Tests/UI/Settings/ShortcutsSettingsTabTests.cpp`.
5. Add it to [`docs/control/shortcuts.md`](../control/shortcuts.md).

## The coverage test and its baseline

`Tests/UI/Accessibility/AccessibilityCoverageTests.cpp` audits every visible, accessible interactive
control (buttons, sliders, combo boxes, text editors, anything that wants keyboard focus) on a
headless `MainComponent` (panel state pinned: library open, bottom dock open on the Timeline tab, AI chat and mod matrix closed, so the count does not depend on saved settings), one card for every built-in module type (`ModuleCards`, one aggregate entry; types needing a plugin binary, a timeline track or mixer/macro plumbing are skipped, the list is in the test), each Settings tab and the Export Audio dialog, and counts two gaps per
surface: **missingName** (empty `setTitle`, and for a button empty text; a custom accessibility handler is not consulted because it does not exist without a native window) and **missingTooltip**. The counts must equal the
entry in `Tests/UI/Accessibility/AccessibilityBaseline.h`:

- More gaps than the entry fails and prints every gap path: fix the control you just added.
- Fewer gaps than the entry fails too: lower the entry in the same change (a strict ratchet, like
  `scripts/file-size-baseline.txt`).
- Never raise an entry. A new surface is added with its real counts.

## Verifying for real

The test proves names exist, not that they read well or that Tab reaches them. Run the app, Tab
through the area you changed and watch the ring, then dump the macOS accessibility tree of the running
process with pyobjc (`AXUIElementCreateApplication(pid)`, walk `AXChildren`, print `AXRole`,
`AXTitle`, `AXValue`, `AXDescription`) and check the new names appear.
