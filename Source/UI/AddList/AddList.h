#pragma once

#include "UI/Graph/ModMatrixPicker.h"
#include <optional>
#include <vector>

namespace synth::ui {

// The rows of the one searchable "Add" list: tracks, modules, presets, snippets and markers together. This unit only
// orders, groups and tags rows; the Timeline supplies the track rows and the module library the library rows, so
// neither needs to know about the other.

/** Which button opened the list; it decides which groups come first. */
enum class AddListOrigin { Timeline, Canvas };

/** Library rows get ids from here up, far above every id the Timeline's add menu uses (those stay under 10000). */
constexpr int kAddListLibraryIdBase = 1 << 24;

enum class AddListLibraryKind { Module, Plugin, Snippet };

/** One module-library entry as the list needs it. `section` is the sidebar header it sits under ("Sources"). A plugin
 *  card carries its `format` ("VST3") and `pluginUid`, which with `text` identify it. A disabled row says why in
 *  `disabledReason`. */
struct AddListLibraryRow {
    juce::String text;
    juce::String section;
    AddListLibraryKind kind = AddListLibraryKind::Module;
    bool enabled = true;
    juce::String disabledReason;
    juce::String format;
    int pluginUid = 0;
};

/** The picker rows. `trackRows` are the Timeline's flattened add menu (ids and headers kept, kind tags added); library
 *  row n gets id kAddListLibraryIdBase + n. Timeline origin lists track groups, track preset groups, modules, snippets,
 *  then More; Canvas origin lists modules, snippets, tracks, track presets, then More. */
std::vector<ModMatrixPicker::Item> buildAddListItems(std::vector<ModMatrixPicker::Item> trackRows,
                                                     const std::vector<AddListLibraryRow>& libraryRows,
                                                     AddListOrigin origin);

/** The index into the library rows a picked id names, or nothing for a Timeline menu id. */
std::optional<int> libraryIndexForAddListId(int id);

} // namespace synth::ui
