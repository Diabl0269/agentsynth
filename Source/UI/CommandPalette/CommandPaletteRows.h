#pragma once

#include "UI/Graph/ModMatrixPicker.h"
#include <optional>
#include <vector>

namespace synth::ui {

// The command palette's rows: what one search bar finds anywhere in the app, ranked and grouped. Pure: the caller
// lists what exists (MainComponentCommandPalette.cpp) and runs what is picked.

enum class PaletteKind { Action, Place, Module, Track, Marker, Setting, Project, Preset };

/** One thing the palette finds and runs. `key` names it across sessions (the recents list stores keys); `text` is the
 *  row's label and starts with a capital letter; `keywords` are searched but not shown; `hint` is a muted note at the
 *  right (an action's key). A set `switchOn` makes it an on/off row; `action` gives it an icon button. */
struct PaletteEntry {
    PaletteKind kind = PaletteKind::Action;
    juce::String key;
    juce::String text;
    juce::String keywords;
    juce::String hint;
    juce::String detail;
    std::optional<bool> switchOn;
    std::optional<ModMatrixPicker::RowAction> action;
    bool enabled = true;
    bool opensChoices = false; // a pick opens the setting's choices in the same bar instead of running anything
};

/** One choice of a dropdown setting; `current` is the one the setting holds now (it gets the tick). */
struct PaletteChoice {
    juce::String text;
    bool current = false;
};

/** Entry n's row id is kPaletteIdBase + n, far above every Add list id, so a pick says which list it came from. */
constexpr int kPaletteIdBase = 1 << 28;

/** The ids of the choices level: the heading row that goes back, then choice n at kPaletteChoiceBase + 1 + n. */
constexpr int kPaletteChoiceBase = 1 << 29;
constexpr int kPaletteBackId = kPaletteChoiceBase;

/** The index into the entries a picked id names, or nothing for an Add list row or a choices-level row. */
std::optional<int> paletteEntryIndexForId(int id);

/** The index of the choice a choices-level id names; nothing for the back row or an id of another list. */
std::optional<int> paletteChoiceIndexForId(int id);

/** The choices level of a dropdown setting: a heading row (`settingName`, a back chevron, "Esc" at its right) over the
 *  `choices` the words of `query` match, the best match first and the list order among equals. The current choice is
 *  ticked. */
ModMatrixPicker::QueryRows buildPaletteChoiceRows(const juce::String& settingName,
                                                  const std::vector<PaletteChoice>& choices, const juce::String& query);

/** The muted word at a row's right end ("View", "Module"). */
juce::String paletteKindTag(PaletteKind kind);

/** The rows for `query`. Blank: the `recentKeys` entries (in that order, ones that no longer exist skipped) under
 *  "Recent", then every place. A query: the best match alone under "Top match", then every other match under its
 *  kind's header (Views, Modules, Tracks, Markers, Actions, Settings, Recent projects, Presets), best first. A query
 *  starting with "+" lists only the `addRows` (the Add list's rows) the rest matches, its best match first. */
ModMatrixPicker::QueryRows buildCommandPaletteRows(const std::vector<PaletteEntry>& entries,
                                                   const std::vector<ModMatrixPicker::Item>& addRows,
                                                   const juce::StringArray& recentKeys, const juce::String& query);

/** True for a query that narrows the palette to the Add list's rows. */
bool isAddListQuery(const juce::String& query);

/** `recent` with `key` moved to the front, at most `limit` long. */
juce::StringArray pushRecentPaletteKey(juce::StringArray recent, const juce::String& key, int limit = 8);

} // namespace synth::ui
