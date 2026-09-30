# Settings > Preferences: categories and scrolling

The Preferences tab (`Source/UI/Settings/PreferencesSettingsTab/`) holds editor behaviour that is not
appearance. Its rows are grouped into **categories** picked from a drop-down under the title; only the
selected category's rows are shown, inside a `juce::Viewport`, so every row is reachable however short
the Settings window is.

## What the user sees

- A title, then one line with the category drop-down (left) and the filter field (right), both pinned
  above the scrolled region.
- The rows of the selected category, top to bottom, in a viewport with a vertical scrollbar. Picking
  another category scrolls back to the top.
- Typing in the filter searches **every** category (a row matches on its label, button text or tooltip)
  and disables the drop-down until the filter is cleared. Esc clears it.
- The tab opens on Graph.

| Category | Rows |
| --- | --- |
| Graph | smart connections, double-click to disconnect, alignment guides, Dual I/O default + per-module overrides, macro auto-ports, macro toggles, reconnect the chain on delete |
| Timeline | loop-locator toggles, natural scrolling, zoom direction, piano roll key labels |
| Files & Autosave | autosave on/interval/backups |
| Mixer | auto-create channel on connect, default track presets, mixer placement |
| Panels & Windows | panel detach mode |
| MIDI Remote | default takeover, badges |

## How it is built

- `PreferencesSettingsTab::Category` is the enum; `categoryCombo` lists it (combo id = enum value + 1).
  Its `onChange` (in `PreferencesSettingsTabCategories.cpp`) is the only place the selection changes; it
  re-lays the rows and resets the scroll position.
- Each category has one `layout*Groups` function in the unit named for its concern
  (`layoutGraphGroups` in `...GraphBehaviour.cpp`, `layoutTimelineGroups`, `layoutAutosaveGroup`,
  `layoutMixerGroups`; the Panels and MIDI Remote groups are `layoutPanelDetachModeGroup` and
  `layoutMidiRemoteGroup`, chained from the mixer group). Each sets `layoutCategory` first.
- `layoutContent` builds three closures (`groupMatches`, `setGroupVisible`, `beginGroup`) and calls the
  per-category functions in order. With an empty filter `groupMatches` is true only for groups whose
  `layoutCategory` equals `selectedCategory`; with a filter it is the text match, across all categories.
  Hidden groups are never given bounds, so they cost no height and leave no divider behind.
- Persistence is untouched: each row's getter/setter/`persist*` code and settings key is exactly what it
  was, so closing and reopening Settings (a new tab constructed from the same properties) reloads every
  value regardless of category.

## Adding a preference (a row) to a category

1. **Control**: declare it in `PreferencesSettingsTab.h` next to its neighbours and construct it in the
   category's setup path (for a plain toggle: `contentHost.addAndMakeVisible(...)`, set its state from
   `appProperties.getUserSettings()`, set a tooltip, wire `onClick` to a `persist*` method). The
   constructor is on the function-size ratchet, so chain new setup from a `setup*Controls()` step rather
   than growing it.
2. **Getter / setter / persist**: add them in the unit for that concern (the existing rows show the shape),
   with the key constant in `PreferencesSettingsTabInternal.h`.
3. **Layout**: add one group block to that category's `layout*Groups` function, copying an existing block:
   `groupMatches({...})`, `setGroupVisible({...}, visible)`, `beginGroup(visible)`, set bounds and advance
   `y`, then `pendingDivider = pendingDivider || visible`. That is the whole change; the category picker,
   scrolling and search pick the row up from there.
4. **Push live** (if a `GraphEditor` should hear about it) in `setGraphEditor`.
5. **Test**: extend the matching `Tests/UI/Settings/PreferencesSettingsTab/*Tests.cpp` (round-trip) and,
   for a new category, add a probe row to `PreferencesSettingsTabCategoryTests.cpp`.

Planned rows: a patch save location (Files & Autosave, in `layoutAutosaveGroup`) and a drag-inside-a-macro-hull
preference (Graph, in `layoutGraphGroups`).

A new **category** needs a `Category` enumerator, a `categoryName` case, an entry in `kCategoriesInOrder`
(`...Categories.cpp`) and its own `layout*Groups` that sets `layoutCategory`, called from `layoutContent`.
