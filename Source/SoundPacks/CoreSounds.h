#pragma once

// The instruments the song planner may name from the downloadable sound packs (core and animals), and the synth each
// one stands in for while its pack is not installed.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::soundpacks {

inline constexpr const char* kCorePackId = "core";
inline constexpr const char* kAnimalsPackId = "animals";

struct CoreSound {
    const char* id;      ///< "core/upright-piano": pack id, a slash, the pack's instrument id.
    const char* name;    ///< For the plan card's list of instruments.
    const char* standIn; ///< The module type a build uses until the pack is installed.
    bool drumKit;        ///< Played by the Drum Kit module, not a Sampler.
    float trimDb;        ///< Added to the recordings when a Sampler plays it, so it sits level with its stand-in.
};

/** The sound `id` names; null for anything that is not in the table. */
const CoreSound* findCoreSound(const juce::String& id);

/** The part of the id before the slash: the pack the sound is in ("core", "animals"). */
juce::String packIdOf(const CoreSound& sound);

/** The part of the id after the slash: the instrument id inside the pack. */
juce::String coreInstrumentId(const CoreSound& sound);

/** True when the op's `instrument` is the stand-in the id is paired with. */
bool coreSoundMatchesInstrument(const juce::String& id, const juce::String& instrument);

/** The distinct names of `ids`, in order, joined with ", " (ids that are not core sounds are left out). */
juce::String coreSoundNames(const std::vector<juce::String>& ids);

/** Loads `sound` into `sampler` (a Sampler module) with its trim; false when `sampler` is not one or the load fails. */
bool loadCoreSoundIntoSampler(juce::AudioProcessor& sampler, const CoreSound& sound);

/** True when the instrument is installed in a pack the sampler library can read. */
bool coreSoundInstalled(const CoreSound& sound);

} // namespace synth::soundpacks
