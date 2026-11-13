#include "SoundBrowser/SoundSearch.h"
#include <algorithm>
#include <cmath>

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

bool passesTempo(const SoundEntry& e, const SoundFilter& filter) {
    if (!filter.tempo.has_value() && !filter.nearTempo.has_value())
        return true;
    if (e.tempo <= 0.0)
        return false;
    if (filter.tempo.has_value() && (e.tempo < filter.tempo->first || e.tempo > filter.tempo->second))
        return false;
    if (filter.nearTempo.has_value()) {
        constexpr double kWindow = 10.0;
        const double song = *filter.nearTempo;
        const bool near = std::abs(e.tempo - song) <= kWindow || std::abs(e.tempo - song * 0.5) <= kWindow ||
                          std::abs(e.tempo - song * 2.0) <= kWindow;
        if (!near)
            return false;
    }
    return true;
}

bool passesLength(const SoundEntry& e, SoundLength length) {
    switch (length) {
    case SoundLength::Any:
        return true;
    default:
        break;
    }
    if (e.lengthSeconds <= 0.0)
        return false; // an unknown length (an instrument) fails an active length filter
    switch (length) {
    case SoundLength::Any:
        return true;
    case SoundLength::Under1:
        return e.lengthSeconds < 1.0;
    case SoundLength::OneToFour:
        return e.lengthSeconds >= 1.0 && e.lengthSeconds <= 4.0;
    case SoundLength::Over4:
        return e.lengthSeconds > 4.0;
    }
    return true;
}

bool passesKey(const SoundEntry& e, const juce::String& key) {
    if (key.isEmpty())
        return true;
    const auto want = parseSoundKey(key);
    const auto have = parseSoundKey(e.key);
    if (want.pitchClass < 0 || have.pitchClass != want.pitchClass)
        return false;
    return want.mode == 0 || have.mode == 0 || want.mode == have.mode;
}

} // namespace

bool soundPassesFilter(const SoundEntry& e, const SoundFilter& filter) {
    if (!filter.kinds.empty() && filter.kinds.count(e.kind) == 0)
        return false;
    if (!filter.tags.isEmpty()) {
        bool any = false;
        for (const auto& tag : filter.tags)
            any = any || e.tags.contains(tag, true);
        if (!any)
            return false;
    }
    if (filter.placeId.isNotEmpty() && e.placeId != filter.placeId)
        return false;
    return passesTempo(e, filter) && passesLength(e, filter.length) && passesKey(e, filter.key);
}

SoundKeyId parseSoundKey(const juce::String& text) {
    const auto t = text.trim().toLowerCase();
    static const int semitones[7] = {9, 11, 0, 2, 4, 5, 7}; // a..g
    if (t.isEmpty() || t[0] < 'a' || t[0] > 'g')
        return {};
    int pc = semitones[t[0] - 'a'];
    int pos = 1;
    if (pos < t.length() && (t[pos] == '#' || t[pos] == 'b')) {
        pc += t[pos] == '#' ? 1 : -1;
        ++pos;
    }
    const auto mode = t.substring(pos).trim();
    SoundKeyId id;
    id.pitchClass = (pc + 12) % 12;
    if (mode == "m" || mode == "min" || mode == "minor")
        id.mode = 2;
    else if (mode == "maj" || mode == "major")
        id.mode = 1;
    else if (mode.isNotEmpty())
        return {};
    return id;
}

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
        if (!soundPassesFilter(e, filter))
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

SoundListing listFolder(const SoundSnapshot& snapshot, const juce::String& placeId, const juce::String& relativeFolder,
                        const SoundFilter& filter) {
    SoundListing listing;
    const juce::String prefix = relativeFolder.isEmpty() ? juce::String() : relativeFolder + "/";
    std::map<juce::String, int> subfolders;
    for (const auto& e : snapshot.entries) {
        if (e.placeId != placeId || !soundPassesFilter(e, filter))
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
