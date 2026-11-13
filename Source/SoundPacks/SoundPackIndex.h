#pragma once

// The hosted list of downloadable sound packs (index.json): parsing and validation, no network.

#include <juce_core/juce_core.h>
#include <vector>

namespace synth::soundpacks {

struct IndexInstrument {
    juce::String id, name, category;
};

struct IndexPack {
    juce::String id, name, version, url, sha256, licence, licenceUrl, credit;
    juce::int64 downloadBytes = 0;
    juce::int64 installedBytes = 0;
    std::vector<IndexInstrument> instruments;
};

struct ParsedIndex;

struct SoundPackIndex {
    std::vector<IndexPack> packs;
    /** Entries the parser dropped because they were malformed (a bad id or url, a missing checksum). */
    int skippedPacks = 0;

    const IndexPack* find(const juce::String& packId) const;

    /** Reads `{"format":1,"packs":[...]}`. An unknown format or unreadable JSON fails; a single bad pack entry is
     *  skipped and counted, so one broken row never hides the rest. */
    static ParsedIndex parse(const juce::String& json);

    /** True for an id that is safe as a folder name: lower-case letters, digits, '-' and '_', 1 to 64 long. */
    static bool isValidId(const juce::String& id);
};

struct ParsedIndex {
    bool ok = false;
    SoundPackIndex index;
    juce::String error; ///< Plain words, set when !ok.
};

/** A download size in words: "33 MB", "1.1 GB". Decimal units, as the hosting page and the OS show them. */
juce::String formatPackSize(juce::int64 bytes);

} // namespace synth::soundpacks
