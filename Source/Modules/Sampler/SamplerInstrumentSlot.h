#pragma once

// What the Sampler's Instrument mode has loaded (an SFZ, a sound pack entry or dropped files), how that is saved, and
// the message-thread load/restore path in front of the InstrumentPlayer.

#include "Modules/Sampler/SamplerInstrumentPlayer.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::sampler {

/** One dropped file's placement; `file` indexes InstrumentSource::files and `key` is the root key. */
struct DroppedRegion {
    int file = 0;
    int loKey = 0, hiKey = 127, key = 60, loVel = 1, hiVel = 127;
};

struct InstrumentSource {
    enum class Kind { None, Sfz, Pack, Files };

    Kind kind = Kind::None;
    juce::File sfzFolder;
    juce::String sfzRelative; // '/'-separated, inside sfzFolder
    juce::String packId, instrumentId;
    juce::StringArray files; // an entry may be empty (a file a bundle no longer holds); indices stay valid
    std::vector<DroppedRegion> regions;

    static InstrumentSource fromSfz(const juce::File& sfz);
    static InstrumentSource fromPack(const juce::String& packId, const juce::String& instrumentId);
    /** Orders `absolutePaths` by name and places them with placeDroppedFiles. */
    static InstrumentSource fromFiles(const juce::StringArray& absolutePaths);

    /** The common start of the file names, or "My instrument". */
    static juce::String nameForFiles(const juce::StringArray& files);
};

/** Adds the instrument keys of `source` to `state` (nothing for Kind::None). */
void writeInstrumentState(const InstrumentSource& source, juce::DynamicObject& state);

/** The source `state` holds; Kind::None when it holds none. */
InstrumentSource readInstrumentState(const juce::DynamicObject& state);

struct InstrumentLoad {
    Instrument::Ptr instrument;
    juce::StringArray messages; // errors, and warnings prefixed "warning: "
};

/** Reads and decodes `source`; null instrument when nothing can play. Touches no module; any thread. */
InstrumentLoad decodeInstrument(const InstrumentSource& source);

class InstrumentSlot {
public:
    InstrumentSlot(InstrumentPlayer& player, const void* owner)
        : player_(player)
        , owner_(owner) {}

    /** Decodes now and publishes. False (keeping the loaded instrument) when nothing could be loaded. */
    bool load(const InstrumentSource& source);

    /** A load from saved state: decodes on the worker while a project opens on screen (DeferredAssetLoads), now
        otherwise. False when it failed now or could not be started. */
    bool restore(const InstrumentSource& source);

    void clear();

    bool isPending() const noexcept { return pending_; }

    /** What a save should hold: the source still decoding if there is one, else the loaded one. */
    const InstrumentSource& sourceToSave() const noexcept { return pending_ ? pendingSource_ : loaded_; }

    /** Errors and warnings of the last load. */
    const juce::StringArray& messages() const noexcept { return messages_; }

private:
    bool install(const InstrumentSource& source, InstrumentLoad&& result);
    bool deferRestore(const InstrumentSource& source);

    InstrumentPlayer& player_;
    const void* owner_;
    InstrumentSource loaded_, pendingSource_;
    juce::StringArray messages_;
    bool pending_ = false;
    int serial_ = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE(InstrumentSlot)
};

} // namespace synth::sampler
