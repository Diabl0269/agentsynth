// CoreSounds.cpp -- the fixed table of core pack instruments the song planner can ask for.
#include "SoundPacks/CoreSounds.h"
#include "Modules/Sampler/SoundPackLibrary.h"

namespace synth::soundpacks {

namespace {

constexpr CoreSound kSounds[] = {
    {"core/upright-piano", "Upright piano", "Mallet", false},
    {"core/string-section", "String section", "Wavetable", false},
    {"core/trumpet", "Trumpet", "Wavetable", false},
    {"core/french-horn", "French horn", "Wavetable", false},
    {"core/flute", "Flute", "Wavetable", false},
    {"core/clarinet", "Clarinet", "Wavetable", false},
    {"core/double-bass-plucked", "Plucked double bass", "Pluck", false},
    {"core/electric-guitar", "Electric guitar", "Pluck", false},
    {"core/acoustic-kit", "Acoustic drum kit", "Drum Kit", true},
};

} // namespace

const CoreSound* findCoreSound(const juce::String& id) {
    for (const auto& sound : kSounds)
        if (id == sound.id)
            return &sound;
    return nullptr;
}

juce::String coreInstrumentId(const CoreSound& sound) {
    return juce::String(sound.id).fromFirstOccurrenceOf("/", false, false);
}

bool coreSoundMatchesInstrument(const juce::String& id, const juce::String& instrument) {
    const auto* sound = findCoreSound(id);
    return sound != nullptr && instrument == sound->standIn;
}

juce::String coreSoundNames(const std::vector<juce::String>& ids) {
    juce::StringArray names;
    for (const auto& id : ids)
        if (const auto* sound = findCoreSound(id))
            names.addIfNotAlreadyThere(sound->name);
    return names.joinIntoString(", ");
}

bool coreSoundInstalled(const CoreSound& sound) {
    return synth::sampler::SoundPackLibrary::find(kCorePackId, coreInstrumentId(sound)).has_value();
}

} // namespace synth::soundpacks
