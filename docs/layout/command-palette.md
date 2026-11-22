# Command palette

Cmd+K (`openCommandPalette`, rebindable) or the top bar's **Search** button opens one search bar over the whole app.
A second press closes it. Code: `Source/UI/CommandPalette/` (pure ranking in `CommandPaletteRows`, the floating bar in
`CommandPaletteComponent`) and `Source/MainComponent/MainComponentCommandPalette.cpp` (what it lists and runs).

## What it finds

The rows are listed fresh each time it opens:

| Kind | Rows | A pick |
|---|---|---|
| Views | Canvas, Timeline, Mixer, MIDI Remote, AI panel, Mod Matrix, Library, Sounds, and each Settings tab ("Audio settings") | shows and focuses it; a Settings tab opens Settings there |
| Modules | every card, by its title ("LFO 6") | selects the card and centres the view on it (`showNodeOnCanvas`, at once, no glide) |
| Tracks | every track by name | shows the Timeline, then focuses the track's row |
| Markers | every marker, with its bar | moves the playhead to it |
| Actions | every rebindable command (`ShortcutManager` names), with its key at the right | runs the command; one that cannot run now is greyed out |
| Settings | every row of Settings > Preferences, by the name it has there: the on/off ones (natural scrolling, cables on top, autosave, Colour controls by module family, the macro and Timeline options ...), the dropdown ones (Theme, Smart connections, Connect side by side, Autosave interval, Timeline: dropping a section on other music, Animations, Duplicate points, Macro auto-ports, panel detach, Mixer placement, and the Appearance pickers Depth, Knob style and Fader style) and the few that need more than a switch or a short list (Audio device, Keyboard shortcuts, AI model, Split Left/Right jacks, number fields, per-type presets, Patch save location, usage statistics) | flips the on/off ones in place; a dropdown one opens its choices in the same bar ([below](#dropdown-settings)); the others open Settings at their tab |
| Recent projects, Presets | the File menu's recent projects and the factory presets | opens or loads it, through the unsaved-changes guard |

Each row ends in its kind as a tag (View, Module, Track, Marker, Action, Setting, Project, Preset), 11 px and coloured by kind (one hue per kind, [theming](theming.md#colour-code-kinds)), searched and read out with the row. An on/off row shows a switch. Return or a click flips it, writes the preference with the same key and the same live
effect as its Preferences checkbox, and the palette stays open. The switch glides: the knob slides across and the
pill's fill fades, 110 ms switching on and 90 ms switching off (`SwitchGlide`, see [theming](theming.md#themed-widgets));
80 ms under Reduce Motion and none under Animations: Off. A row bound for the first time (the list opening, a scroll, a
different entry on the row) shows its switch at rest with no motion; only a flip of the entry that stays bound glides. An open Settings window does not refresh its
checkbox until it is reopened.

## Every Preferences row is here

A Preferences row with no row in the palette is a bug: `CommandPaletteMainComponentTests` (`EveryPreferencesControlIsFoundByNameInThePalette`) walks the real Preferences
tab (every checkbox, dropdown and number field) and the Appearance tab's Depth and knob-style pickers (which are not dropdowns) and fails when one has no Setting row of the same name. A new Preferences
row therefore gets its palette row in the same change: an on/off one is a `switchSpecs()` entry (its key, default and the
live effect of its checkbox; `forgetOnEnable` lists the remembered answers its checkbox forgets when turned on), a
dropdown with a short list is a `ChoiceSetting` in `MainComponentCommandPaletteChoices.cpp` (written through the same key
as its Settings combo), anything else is a `TabSetting` that opens Settings > Preferences.

## Dropdown settings

A setting that is a dropdown in Settings shows its current choice at the row's right (muted, before the Setting tag). Return
or a click opens its choices **in the same bar**: the list swaps (fades in, 160 ms, `ModMatrixPicker::reloadRows`) to a
heading row (a back chevron, the setting's name in bold, "Esc" at its right) over the choices, the current one ticked. The
search field starts empty and filters the choices as you type, the best match first, and the highlight starts on the
first choice (never the heading), so "theme", Return, "warm", Return sets Warm Console. Picking writes the same key
and runs the same live effect as the Settings dropdown (`MainComponentCommandPaletteChoices.cpp`), then the bar steps back
to the first list with the search text it had, so the row shows its new value. Escape (or the heading row, or Up then
Return) steps back one level without picking; Escape on the first list closes the bar. A pick joins the recents. The
choices' ids sit above the palette entries' (`kPaletteChoiceBase`), so a pick says which level it came from.

Theme lists every theme; a pick becomes the default of its kind (dark or light) and the mode follows, as the Settings
Appearance tab does. Timeline: dropping a section on other music offers Insert and Replace the sections under it, as its Preferences dropdown does.
Depth offers Flat, Soft and Rich through `ThemeManager::setDepthLevel`, the call the Depth picker makes, so the window cross-fades as it does there; Knob style and Fader style
offer the five looks of the two rows of the Appearance picker and write the same keys.
Autosave interval offers 1, 2, 5, 10, 15, 30 and 60 minutes (the Settings field takes any whole
number: another stored value shows at the row's right with no tick). An open Settings window does not refresh until it
is reopened.

## Order

- **A query**: the best match alone under "Top match", then every other match under its kind's header, in the order
  of the table above. Ties for best go to the kind higher in that order, then the shorter label. A greyed-out row
  never leads.
- **No query**: recent picks under "Recent" (the last 8, newest first, kept as row keys one per line in
  `commandPaletteRecent`), then the views.
- **"+" first**: only the [Add list's](../timeline/add-track.md#the-picker) own rows, the best match first and the rest
  under their own headers. A pick runs the Add list's own pick (`MainComponentCommandPalette::runAddListPick`, which
  `MainComponent::openAddList` calls too), so it adds exactly what the Add list would.

Matching is the [shared search](chrome.md#shared-search): letters typed against digits split, so "lfo6" finds "LFO 6".

## Keyboard and screen readers

The search field keeps focus. Up/Down move, Return runs, Escape closes. On a row with a button (a plugin track's
window button), Tab or Right at the end of the text moves the highlight onto it, with the accent focus ring, and
Return presses it; Shift+Tab or Left moves back. Closing hands keyboard focus back to what had it when the palette
opened. The bar's (i) hint and the Search button's tooltip both say "type + to add".

Screen readers hear the bar as "Search everything", each row by its label (an on/off row adds ", on" or ", off"), and
the window button as "Show plugin window, <track>". Its tooltip names the
`timelineToggleFocusedTrackPluginWindow` key (Cmd+Ctrl+E).

## The bar

A 560 x 400 frame centred under the top bar by default, built on `ModMatrixPicker` (hosted in the frame instead of a
call-out: `onDismissRequested` closes it). It fades in and out (`FadeVisibility`). Drag its frame to move it; it stays
inside the window and the next open starts where it was dropped (`commandPalettePosition`, "x,y").

## The window button on plugin tracks

A track whose instrument is a hosted plugin gets a small window icon at the right of its row. Pressing it opens or
closes that plugin's window (`MainComponent::toggleTrackPluginWindow`), as Cmd+Ctrl+E does on a focused track row.
The mechanism is general: any `ModMatrixPicker::Item` with an `action` gets the button and reports it through
`onRowAction`.
