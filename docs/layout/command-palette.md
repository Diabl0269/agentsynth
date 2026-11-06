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
| Settings | the on/off Preferences (natural scrolling, cables on top, autosave ...) and a few others (Theme, Audio device, Keyboard shortcuts ...) | flips the on/off ones in place; the others open Settings at their tab |
| Recent projects, Presets | the File menu's recent projects and the factory presets | opens or loads it, through the unsaved-changes guard |

Each row ends in its kind as a tag (View, Module, Track, Marker, Action, Setting, Project, Preset), searched and read out with the row. An on/off row shows a switch. Return or a click flips it, writes the preference with the same key and the same live
effect as its Preferences checkbox, and the palette stays open. An open Settings window does not refresh its
checkbox until it is reopened.

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
