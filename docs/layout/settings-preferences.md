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
- **All** (first entry in the drop-down) shows every category on one page, each under a clickable
  header with a chevron. Click a header (or Tab to it and press Space) to fold or unfold that
  category; the one **Collapse all** / **Expand all** button in the strip above the rows (the same
  button, and the same place, as in Keyboard Shortcuts) does every header at once: it reads "Collapse
  all" while any section is open and "Expand all" once every section is folded. Folds are remembered
  across Settings windows and launches (user setting `preferencesFolded`, the folded categories by stable
  name, as for `preferencesCategory`; none saved means everything is expanded). They are restored in the
  constructor before the first layout, so the tab opens as it was left with nothing animating into place.
  The Keyboard Shortcuts tab does the same under `shortcutsFolded`; both go through
  `UI/Settings/SettingsFoldState.h`, and the window itself remembers the open tab by name
  (`settingsTabName`). → [`shortcuts.md`](../control/shortcuts.md)
- Section headers and column heads are in normal case ("Graph", "Files & Autosave"), never all caps.
- The picker row is sized by its content, not fixed: the drop-down is as wide as its longest entry (measured
  with the drop-down's own font plus the look-and-feel's text insets, `AppLookAndFeel::comboBoxWidthToFitItems`),
  so the filter field gets the rest. The Settings window cannot be dragged narrower than
  `SettingsWindow::kMinWidth`, where the field still shows its whole hint.
- **Keyboard**: Tab reaches every control; Up / Down also walk them (clamped at the ends), Right / Left tick
  or untick a focused check box, and Left / Right fold or unfold a focused section header in the All view.
  Down onto a drop-down, then Down again changes its selection (Tab moves on). The keys are fixed, not
  rebindable. → [`accessibility.md`](../development/accessibility.md#arrow-keys-in-lists-of-controls)
- Typing in the filter searches **every** category (a row matches on its label, button text or tooltip)
  and disables the drop-down until the filter is cleared. Esc clears it.
- The tab opens on the category you last picked and remembers it across Settings windows and launches
  (user setting `preferencesCategory`, saved as the name "All", "Graph", "Timeline", "Files", "Mixer",
  "Panels" or "MidiRemote"). With nothing saved, or a value that names none of them, it opens on All.
- **Tab keys**: Cmd+1..9 switches the Settings window's tabs, also from the filter field (Left / Right on the
  focused tab strip step through them). → [`shortcuts.md`](../control/shortcuts.md#switching-tabs)

| Category | Rows |
| --- | --- |
| Graph | smart connections, double-click to disconnect, alignment guides, Dual I/O default + per-module overrides, macro auto-ports, macro toggles, reconnect the chain on delete |
| Timeline | loop-locator toggles, ask before removing an LFO's last destination, natural scrolling, zoom direction, piano roll key labels |
| Files & Autosave | autosave on/interval/backups, patch save location (per project / shared folder / chosen folder + Choose...) |
| Mixer | auto-create channel on connect, default track presets, mixer placement |
| Panels & Windows | panel detach mode, animations (Follow system / Full / Reduced / Off; [`animation.md`](animation.md#reduced-motion)), show info tooltips (off hides tooltips that explain a control; helper tips such as the mixer sources badge keep showing; [`animation.md`](animation.md#tooltips)) |
| MIDI Remote | default takeover, badges |

## How it is built

- `PreferencesSettingsTab::Category` is the enum; `categoryCombo` lists it (combo id = enum value + 1).
  Its `onChange` (in `PreferencesSettingsTabCategories.cpp`) is the only place the selection changes; it
  saves the choice under `preferencesCategory`, re-lays the rows and resets the scroll position. The
  constructor restores the saved choice (`setupCategorySelector`) before the first layout.
- Each category has one `layout*Groups` function in the unit named for its concern
  (`layoutGraphGroups` in `...GraphBehaviour.cpp`, `layoutTimelineGroups`, `layoutAutosaveGroup`,
  `layoutMixerGroups`; the Panels and MIDI Remote groups are `layoutPanelDetachModeGroup`,
  `layoutInfoTooltipsGroup` and `layoutMidiRemoteGroup`, chained from the mixer group). Each sets `layoutCategory` first.
- `layoutContent` builds three closures (`groupMatches`, `setGroupVisible`, `beginGroup`) and calls the
  per-category functions in order. With an empty filter `groupMatches` is true only for groups whose
  `layoutCategory` equals `selectedCategory`; with a filter it is the text match, across all categories.
  Hidden groups are never given bounds, so they cost no height and leave no divider behind.
- **All view** (`PreferencesSettingsTabSections.cpp`): `Category::All` is the last enumerator but the
  first drop-down entry, and is never a `layoutCategory`. `groupMatches` calls `categoryShown`, which is
  true for every category that is not folded. Each `layout*Groups` unit calls `enterCategory(category, y)`
  (instead of assigning `layoutCategory`), which records where the category's rows begin; once
  `layoutContent` is done, `placeSectionHeaders` walks those bands in order, puts a `SectionHeader` above
  each, drops the divider that would sit under it and shifts the band down. A filter turns the headers
  and the fold-all button off and ignores folds, so a match can never be trapped inside one. The fold-all
  button is `synth::ui::FoldAllButton` (`Source/UI/Layout/FoldAllButton.h`), the class the Keyboard Shortcuts
  tab uses too; `resized()` gives it a strip between the picker row and the rows only in the All view.
  A header is never hidden and re-shown during a fold: that would drop the keyboard focus it holds. A new category
  also needs a `kSections` entry in that unit.
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

Planned row: a drag-inside-a-macro-hull preference (Graph, in `layoutGraphGroups`).

Worked example: the patch save location row (`PreferencesSettingsTabPatchSaveLocation.cpp`) is a label, a combo, a
"Choose..." button and a hint; its group is laid out from the tail of `layoutAutosaveGroup`, its controls are
chained from `setupMidiRemoteControls()`, and the folder logic lives in `synth::PatchSaveLocation`, not in the tab.

A new **category** needs a `Category` enumerator (before `All`; bump `kNumSections`), a `categoryName` case, an entry in `kCategoriesInOrder`
(`...Categories.cpp`) and its own `layout*Groups` that sets `layoutCategory`, called from `layoutContent`.
