// AddList.cpp -- orders and tags the rows of the "Add" list.
#include "AddList.h"
#include <algorithm>

namespace synth::ui {
namespace {

enum class Group { Tracks, TrackPresets, Modules, Snippets, More };

bool isTrackHeader(const juce::String& header) {
    return header == "Tracks" || header == "Instrument Tracks" || header == "Plugins";
}

Group groupOfTrackRow(const ModMatrixPicker::Item& row) {
    if (isTrackHeader(row.category))
        return Group::Tracks;
    if (row.category.endsWith("from Preset"))
        return Group::TrackPresets;
    return Group::More; // Create Channels, Add Marker and Insert Track Preset from File stay together, last
}

juce::String tagOfTrackRow(const ModMatrixPicker::Item& row) {
    if (isTrackHeader(row.category))
        return "Track";
    if (row.category == "Bus from Preset")
        return "Bus preset";
    if (row.category.endsWith("from Preset") || row.text.startsWith("Insert Track Preset"))
        return "Track preset";
    if (row.text == "Add Marker")
        return "Marker";
    return {};
}

// "AudioUnit" reads as "AU", like the Timeline's plugin rows.
juce::String shortFormat(const juce::String& format) { return format == "AudioUnit" ? juce::String("AU") : format; }

ModMatrixPicker::Item itemForLibraryRow(const AddListLibraryRow& row, int index) {
    ModMatrixPicker::Item item;
    item.id = kAddListLibraryIdBase + index;
    item.enabled = row.enabled;
    if (!row.enabled)
        item.detail = row.disabledReason;
    switch (row.kind) {
    case AddListLibraryKind::Snippet:
        item.text = row.text;
        item.category = "Snippets";
        item.tag = "Snippet";
        item.searchText = "snippet group";
        break;
    case AddListLibraryKind::Plugin:
        item.text = row.format.isEmpty() ? row.text : row.text + " (" + shortFormat(row.format) + ")";
        item.category = "Modules " + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " Plugins";
        item.tag = "Module";
        item.searchText = "plugin vst au";
        break;
    case AddListLibraryKind::Module:
        item.text = row.text;
        item.category = "Modules " + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " " + row.section;
        item.tag = "Module";
        item.searchText = moduleSearchAliases(row.text);
        break;
    }
    return item;
}

int rankOf(Group group, AddListOrigin origin) {
    const int timeline[] = {0, 1, 2, 3, 4};
    const int canvas[] = {2, 3, 0, 1, 4};
    return (origin == AddListOrigin::Timeline ? timeline : canvas)[(int)group];
}

struct Ranked {
    int rank = 0;
    ModMatrixPicker::Item item;
};
} // namespace

std::vector<ModMatrixPicker::Item> buildAddListItems(std::vector<ModMatrixPicker::Item> trackRows,
                                                     const std::vector<AddListLibraryRow>& libraryRows,
                                                     AddListOrigin origin) {
    std::vector<Ranked> ranked;
    for (auto& row : trackRows) {
        const auto group = groupOfTrackRow(row);
        row.tag = tagOfTrackRow(row);
        ranked.push_back({rankOf(group, origin), std::move(row)});
    }
    for (int i = 0; i < (int)libraryRows.size(); ++i) {
        const auto group =
            libraryRows[(size_t)i].kind == AddListLibraryKind::Snippet ? Group::Snippets : Group::Modules;
        ranked.push_back({rankOf(group, origin), itemForLibraryRow(libraryRows[(size_t)i], i)});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.rank < b.rank; });

    std::vector<ModMatrixPicker::Item> items;
    items.reserve(ranked.size());
    for (auto& r : ranked)
        items.push_back(std::move(r.item));
    return items;
}

std::optional<int> libraryIndexForAddListId(int id) {
    if (id < kAddListLibraryIdBase)
        return std::nullopt;
    return id - kAddListLibraryIdBase;
}

} // namespace synth::ui
