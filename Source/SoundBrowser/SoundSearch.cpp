#include "SoundBrowser/SoundSearch.h"
#include <algorithm>

namespace synth::soundbrowser {

namespace {

juce::StringArray wordsOf(const juce::String& query) {
    juce::StringArray words;
    words.addTokens(query, " \t\r\n", "");
    words.removeEmptyStrings();
    return words;
}

int wordRank(const juce::String& text, const juce::String& word) {
    int best = -1;
    int from = 0;
    while (from + word.length() <= text.length()) {
        const int hit = text.indexOfIgnoreCase(from, word);
        if (hit < 0)
            break;
        const int rank = hit == 0 ? 0 : (juce::CharacterFunctions::isLetterOrDigit(text[hit - 1]) ? 2 : 1);
        if (best < 0 || rank < best)
            best = rank;
        if (best == 0)
            break;
        from = hit + 1;
    }
    return best;
}

bool passes(const SoundEntry& e, const SoundFilter& filter) {
    if (filter.kind.has_value() && e.kind != *filter.kind)
        return false;
    if (filter.tag.isNotEmpty() && !e.tags.contains(filter.tag, true))
        return false;
    return filter.placeId.isEmpty() || e.placeId == filter.placeId;
}

} // namespace

int soundSearchScore(const juce::String& text, const juce::String& query) {
    int worst = 0;
    for (const auto& word : wordsOf(query)) {
        const int rank = wordRank(text, word);
        if (rank < 0)
            return -1;
        worst = std::max(worst, rank);
    }
    return worst;
}

std::vector<SoundEntry> searchSnapshot(const SoundSnapshot& snapshot, const juce::String& query,
                                       const SoundFilter& filter) {
    struct Hit {
        int rank;
        const SoundEntry* entry;
    };
    std::vector<Hit> hits;
    for (const auto& e : snapshot.entries) {
        if (!passes(e, filter))
            continue;
        int rank = soundSearchScore(e.name, query);
        if (rank < 0) {
            // Words may be spread over the folder, the place and the tags; those rank after any name match.
            const int wide =
                soundSearchScore(e.name + " " + e.folder + " " + e.placeName + " " + e.tags.joinIntoString(" "), query);
            if (wide < 0)
                continue;
            rank = 3 + wide;
        }
        hits.push_back({rank, &e});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        const int byName = a.entry->name.compareNatural(b.entry->name, false);
        return byName != 0 ? byName < 0 : a.entry->path < b.entry->path;
    });
    std::vector<SoundEntry> out;
    out.reserve(hits.size());
    for (const auto& h : hits)
        out.push_back(*h.entry);
    return out;
}

SoundListing listFolder(const SoundSnapshot& snapshot, const juce::String& placeId,
                        const juce::String& relativeFolder) {
    SoundListing listing;
    const juce::String prefix = relativeFolder.isEmpty() ? juce::String() : relativeFolder + "/";
    std::map<juce::String, int> subfolders;
    for (const auto& e : snapshot.entries) {
        if (e.placeId != placeId)
            continue;
        if (e.folder == relativeFolder) {
            listing.entries.push_back(e);
        } else if (e.folder.startsWith(prefix) && (relativeFolder.isNotEmpty() || e.folder.isNotEmpty())) {
            const auto rest = e.folder.substring(prefix.length());
            ++subfolders[rest.upToFirstOccurrenceOf("/", false, false)];
        }
    }
    for (const auto& [name, count] : subfolders)
        listing.folders.push_back({name, count});
    std::sort(listing.folders.begin(), listing.folders.end(),
              [](const SoundFolder& a, const SoundFolder& b) { return a.name.compareNatural(b.name, false) < 0; });
    std::sort(listing.entries.begin(), listing.entries.end(),
              [](const SoundEntry& a, const SoundEntry& b) { return a.name.compareNatural(b.name, false) < 0; });
    return listing;
}

} // namespace synth::soundbrowser
