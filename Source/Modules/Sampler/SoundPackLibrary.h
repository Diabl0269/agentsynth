#pragma once

// Finds the sampled instruments that ship in sound packs: folders holding a pack.json and the SFZ files it lists.

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace synth::sampler {

struct PackInstrument {
    juce::String packId, packName, instrumentId, name, category, licence;
    juce::File sfz;
};

class SoundPackLibrary {
public:
    /** Every instrument of every readable pack, bundled packs first. A pack whose id was already seen in an earlier
        root is skipped; a malformed pack.json or entry is skipped with a log line. Message thread or a worker. */
    static std::vector<PackInstrument> listInstruments();

    static std::optional<PackInstrument> find(const juce::String& packId, const juce::String& instrumentId);

    /** The folders searched, in order: the app's bundled SoundPacks, then the user's. */
    static std::vector<juce::File> roots();

    /** The user's folder: where downloaded packs are installed. */
    static juce::File userRoot();

    /** Test-only: searches `roots` instead; nullopt restores the normal roots. Set before any lookup runs. */
    static void setRootsForTests(std::optional<std::vector<juce::File>> roots);

private:
    SoundPackLibrary() = delete;
};

} // namespace synth::sampler
