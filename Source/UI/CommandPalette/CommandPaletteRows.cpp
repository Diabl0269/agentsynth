#include "CommandPaletteRows.h"

#include "UI/Layout/SearchMatch.h"
#include <algorithm>
#include <array>

namespace synth::ui {

namespace {

constexpr const char* kTopMatchHeader = "Top match";
constexpr const char* kRecentHeader = "Recent";

// The group order under the top match. The same order breaks a tie for the top match: a view or a card the user
// named beats an action that merely mentions it.
constexpr std::array<PaletteKind, 8> kGroupOrder = {PaletteKind::Place,   PaletteKind::Module, PaletteKind::Track,
                                                    PaletteKind::Marker,  PaletteKind::Action, PaletteKind::Setting,
                                                    PaletteKind::Project, PaletteKind::Preset};

int groupRank(PaletteKind kind) {
    return (int)(std::find(kGroupOrder.begin(), kGroupOrder.end(), kind) - kGroupOrder.begin());
}

juce::String groupHeader(PaletteKind kind) {
    switch (kind) {
    case PaletteKind::Place:
        return "Views";
    case PaletteKind::Module:
        return "Modules";
    case PaletteKind::Track:
        return "Tracks";
    case PaletteKind::Marker:
        return "Markers";
    case PaletteKind::Action:
        return "Actions";
    case PaletteKind::Setting:
        return "Settings";
    case PaletteKind::Project:
        return "Recent projects";
    case PaletteKind::Preset:
        return "Presets";
    }
    return {};
}

ModMatrixPicker::Item itemFor(const PaletteEntry& entry, int index, const juce::String& category) {
    ModMatrixPicker::Item item;
    item.id = kPaletteIdBase + index;
    item.category = category;
    item.text = entry.text;
    item.detail = entry.detail;
    item.searchText = entry.keywords;
    item.tag = paletteKindTag(entry.kind);
    item.hint = entry.hint;
    item.switchOn = entry.switchOn;
    item.action = entry.action;
    item.enabled = entry.enabled;
    return item;
}

struct Hit {
    int index = 0;
    int score = 0;
};

// Lower first: the match score, then the kind's place in the group order, then the shorter label, then list order.
bool better(const Hit& a, const Hit& b, const std::vector<PaletteEntry>& entries) {
    const auto& ea = entries[(size_t)a.index];
    const auto& eb = entries[(size_t)b.index];
    if (a.score != b.score)
        return a.score < b.score;
    if (groupRank(ea.kind) != groupRank(eb.kind))
        return groupRank(ea.kind) < groupRank(eb.kind);
    if (ea.text.length() != eb.text.length())
        return ea.text.length() < eb.text.length();
    return a.index < b.index;
}

ModMatrixPicker::QueryRows blankQueryRows(const std::vector<PaletteEntry>& entries,
                                          const juce::StringArray& recentKeys) {
    ModMatrixPicker::QueryRows rows;
    std::vector<bool> used(entries.size(), false);
    for (const auto& key : recentKeys)
        for (size_t i = 0; i < entries.size(); ++i)
            if (!used[i] && entries[i].key == key) {
                rows.items.push_back(itemFor(entries[i], (int)i, kRecentHeader));
                used[i] = true;
                break;
            }
    for (size_t i = 0; i < entries.size(); ++i)
        if (!used[i] && entries[i].kind == PaletteKind::Place)
            rows.items.push_back(itemFor(entries[i], (int)i, groupHeader(PaletteKind::Place)));
    return rows;
}

// The Add list's own rows under their own headers, the best match lifted out on top. A row's haystack is the one
// the Add list's picker searches, so "+ osc" finds what typing "osc" in the Add list finds.
ModMatrixPicker::QueryRows addListRows(const std::vector<ModMatrixPicker::Item>& addRows, const juce::String& rest) {
    ModMatrixPicker::QueryRows rows;
    rows.highlight = rest;
    const auto words = searchQueryWords(rest);
    std::vector<size_t> matches;
    int best = -1;
    int bestScore = 0;
    for (size_t i = 0; i < addRows.size(); ++i) {
        const auto& row = addRows[i];
        const auto haystack = row.text + " " + row.detail + " " + row.searchText + " " + row.tag;
        if (!searchWordsMatch(haystack, words))
            continue;
        const int score = searchWordsScore(haystack, words);
        if (!rest.isEmpty() && row.enabled && (best < 0 || score < bestScore)) {
            best = (int)i;
            bestScore = score;
        }
        matches.push_back(i);
    }
    if (best >= 0) {
        auto top = addRows[(size_t)best];
        top.category = kTopMatchHeader;
        rows.items.push_back(std::move(top));
    }
    for (const auto i : matches)
        if ((int)i != best)
            rows.items.push_back(addRows[i]);
    return rows;
}

} // namespace

std::optional<int> paletteEntryIndexForId(int id) {
    if (id < kPaletteIdBase)
        return std::nullopt;
    return id - kPaletteIdBase;
}

juce::String paletteKindTag(PaletteKind kind) {
    switch (kind) {
    case PaletteKind::Action:
        return "Action";
    case PaletteKind::Place:
        return "View";
    case PaletteKind::Module:
        return "Module";
    case PaletteKind::Track:
        return "Track";
    case PaletteKind::Marker:
        return "Marker";
    case PaletteKind::Setting:
        return "Setting";
    case PaletteKind::Project:
        return "Project";
    case PaletteKind::Preset:
        return "Preset";
    }
    return {};
}

bool isAddListQuery(const juce::String& query) { return query.trimStart().startsWithChar('+'); }

ModMatrixPicker::QueryRows buildCommandPaletteRows(const std::vector<PaletteEntry>& entries,
                                                   const std::vector<ModMatrixPicker::Item>& addRows,
                                                   const juce::StringArray& recentKeys, const juce::String& query) {
    const auto trimmed = query.trim();
    if (isAddListQuery(trimmed))
        return addListRows(addRows, trimmed.substring(1).trim());
    if (trimmed.isEmpty())
        return blankQueryRows(entries, recentKeys);

    const auto words = searchQueryWords(trimmed);
    std::vector<Hit> hits;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const auto haystack = e.text + " " + e.detail + " " + e.keywords + " " + paletteKindTag(e.kind);
        if (searchWordsMatch(haystack, words))
            hits.push_back({(int)i, searchWordsScore(haystack, words)});
    }
    ModMatrixPicker::QueryRows rows;
    rows.highlight = trimmed;
    if (hits.empty())
        return rows;
    std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) { return better(a, b, entries); });
    // The top match is the best hit that can run; a disabled row never leads.
    const auto top =
        std::find_if(hits.begin(), hits.end(), [&](const Hit& h) { return entries[(size_t)h.index].enabled; });
    if (top != hits.end())
        rows.items.push_back(itemFor(entries[(size_t)top->index], top->index, kTopMatchHeader));
    for (const auto kind : kGroupOrder)
        for (const auto& hit : hits)
            if (entries[(size_t)hit.index].kind == kind && (top == hits.end() || hit.index != top->index))
                rows.items.push_back(itemFor(entries[(size_t)hit.index], hit.index, groupHeader(kind)));
    return rows;
}

juce::StringArray pushRecentPaletteKey(juce::StringArray recent, const juce::String& key, int limit) {
    if (key.isEmpty())
        return recent;
    recent.removeString(key);
    recent.insert(0, key);
    while (recent.size() > limit)
        recent.remove(recent.size() - 1);
    return recent;
}

} // namespace synth::ui
