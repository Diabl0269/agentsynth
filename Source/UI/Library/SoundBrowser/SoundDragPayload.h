#pragma once

#include "Modules/SamplerModule.h"
#include <juce_core/juce_core.h>

// What a Sound row carries while it is dragged: a "sound:" prefix and the file's absolute path. The prefix keeps it
// apart from the other drag payloads (module names, snippets, plugin identities), none of which start with it.
// Rules for the targets: docs/layout/sound-browser.md#dragging-a-sound
namespace synth::ui {

constexpr const char* kSoundPayloadPrefix = "sound:";

inline juce::String makeSoundPayload(const juce::String& absolutePath) {
    return juce::String(kSoundPayloadPrefix) + absolutePath;
}

/** True for a payload made by makeSoundPayload (a string var with the prefix and a non-empty path). */
inline bool isSoundPayload(const juce::var& description) {
    return description.isString() && description.toString().startsWith(kSoundPayloadPrefix) &&
           description.toString().length() > (int)juce::String(kSoundPayloadPrefix).length();
}

/** The file a sound payload names; an empty File for any other payload. */
inline juce::File soundFileFromPayload(const juce::var& description) {
    if (!isSoundPayload(description))
        return {};
    return juce::File(description.toString().substring((int)juce::String(kSoundPayloadPrefix).length()));
}

/** True for an .sfz instrument: it may drop on a Sampler card or the canvas, never on a track. */
inline bool isInstrumentSound(const juce::File& file) { return file.hasFileExtension("sfz"); }

/** True for a recording the Sampler and the audio tracks can load. */
inline bool isAudioSound(const juce::File& file) { return SamplerModule::isSupportedAudioFile(file); }

} // namespace synth::ui
