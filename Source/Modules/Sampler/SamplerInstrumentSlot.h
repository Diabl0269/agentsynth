#pragma once

// What the Sampler's Instrument mode has loaded (an SFZ, a sound pack entry or dropped files), how that is saved, and
// the message-thread load/restore path in front of the InstrumentPlayer.

#include "Modules/Sampler/SamplerInstrumentPlayer.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::sampler {

/** One recording's placement; `file` indexes InstrumentSource::files and `key` is the root key. The fields after the
    velocity range are RegionSpec's, so a converted SFZ or pack instrument loses nothing. */
struct DroppedRegion {
    int file = 0;
    int loKey = 0, hiKey = 127, key = 60, loVel = 1, hiVel = 127;
    float tuneCents = 0.0f, volumeDb = 0.0f, keytrackCents = 100.0f;
    RegionSpec::Loop loop = RegionSpec::Loop::None;
    juce::int64 loopStart = -1, loopEnd = -1;
    int seqLength = 1, seqPosition = 1;
    bool releaseTrigger = false;
    float ampReleaseSeconds = -1.0f;
};

struct InstrumentSource {
    enum class Kind { None, Sfz, Pack, Files };

    Kind kind = Kind::None;
    juce::File sfzFolder;
    juce::String sfzRelative; // '/'-separated, inside sfzFolder
    juce::String packId, instrumentId;
    float gainDb = 0.0f;     // Kind::Pack: added to every region's volume when decoded; 0 plays the pack as recorded
    juce::StringArray files; // an entry may be empty (a file a bundle no longer holds); indices stay valid
    std::vector<DroppedRegion> regions;
    juce::String name; // Kind::Files only; empty = named after the files

    static InstrumentSource fromSfz(const juce::File& sfz);
    static InstrumentSource fromPack(const juce::String& packId, const juce::String& instrumentId);
    /** Orders `absolutePaths` by name and places them with placeDroppedFiles. */
    static InstrumentSource fromFiles(const juce::StringArray& absolutePaths);

    /** The common start of the file names, or "My instrument". */
    static juce::String nameForFiles(const juce::StringArray& files);
};

/** The Kind::Files source holding `specs` (absolute sample paths) with every field; regions on one file share it. */
InstrumentSource sourceFromSpecs(const std::vector<RegionSpec>& specs, const juce::String& name);

/** The regions a Kind::Files source plays (a region whose file entry is empty is left out). */
std::vector<RegionSpec> specsFor(const InstrumentSource& source);

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

    /** Message thread. Rebuilds the instrument from `specs` (reusing the samples already decoded), publishes it and
        keeps it as a Kind::Files source whatever it was before. False, keeping the loaded instrument, when `specs`
        is empty or nothing in it can play. */
    bool replaceRegions(const std::vector<RegionSpec>& specs);

    /** The loaded instrument's regions as specs; empty when none is loaded. */
    const std::vector<RegionSpec>& regions() const noexcept { return specs_; }

    /** What the loaded instrument came from (Kind::None when nothing is loaded or a restore is still pending). */
    InstrumentSource::Kind loadedKind() const noexcept {
        return specs_.empty() ? InstrumentSource::Kind::None : loaded_.kind;
    }

    void clear();

    bool isPending() const noexcept { return pending_; }

    /** What a save should hold: the source still decoding if there is one, else the loaded one. */
    const InstrumentSource& sourceToSave() const noexcept { return pending_ ? pendingSource_ : loaded_; }

    /** Bumped on every install attempt (success or failure) and on clear(), so a UI can notice a failed restore. */
    int loadSerial() const noexcept { return loadSerial_; }

    /** Errors and warnings of the last load. */
    const juce::StringArray& messages() const noexcept { return messages_; }

private:
    bool install(const InstrumentSource& source, InstrumentLoad&& result, bool isRestore);
    bool deferRestore(const InstrumentSource& source);

    InstrumentPlayer& player_;
    const void* owner_;
    InstrumentSource loaded_, pendingSource_;
    std::vector<RegionSpec> specs_; // the loaded instrument's regions, without their audio
    juce::StringArray messages_;
    bool pending_ = false;
    int serial_ = 0;
    int loadSerial_ = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE(InstrumentSlot)
};

} // namespace synth::sampler
