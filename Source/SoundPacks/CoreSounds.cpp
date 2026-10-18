// CoreSounds.cpp -- the fixed table of sound pack instruments the song planner can ask for.
#include "SoundPacks/CoreSounds.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "Modules/SamplerModule.h"

namespace synth::soundpacks {

namespace {

// trimDb is measured offline (docs/modules/sound-pack-downloads.md#levels): the Sampler's RMS against its stand-in's.
constexpr CoreSound kSounds[] = {
    {"core/upright-piano", "Upright piano", "Mallet", false, 12.5f},
    {"core/string-section", "String section", "Wavetable", false, 7.0f},
    {"core/trumpet", "Trumpet", "Wavetable", false, 9.0f},
    {"core/french-horn", "French horn", "Wavetable", false, 6.0f},
    {"core/flute", "Flute", "Wavetable", false, 5.0f},
    {"core/clarinet", "Clarinet", "Wavetable", false, 4.5f},
    {"core/double-bass-plucked", "Plucked double bass", "Pluck", false, 0.0f},
    {"core/electric-guitar", "Electric guitar", "Pluck", false, 0.0f},
    {"core/acoustic-kit", "Acoustic drum kit", "Drum Kit", true, 0.0f},
    // One-shots that play at their recorded pitch; trims measured from the real pack
    // (docs/modules/sound-pack-downloads.md#levels).
    {"animals/whale", "Whale call", "Oscillator", false, 22.0f},
    {"animals/crow", "Crow caw", "Oscillator", false, 18.0f},
    {"animals/owl", "Owl hoot", "Oscillator", false, 23.0f},
    {"animals/frog", "Frog call", "Oscillator", false, 19.5f},
    {"animals/wolf", "Wolf howl", "Oscillator", false, 19.0f},
};

} // namespace

const CoreSound* findCoreSound(const juce::String& id) {
    for (const auto& sound : kSounds)
        if (id == sound.id)
            return &sound;
    return nullptr;
}

juce::String packIdOf(const CoreSound& sound) {
    return juce::String(sound.id).upToFirstOccurrenceOf("/", false, false);
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

bool loadCoreSoundIntoSampler(juce::AudioProcessor& sampler, const CoreSound& sound) {
    auto* module = dynamic_cast<SamplerModule*>(&sampler);
    return module != nullptr && module->loadInstrumentFromPack(packIdOf(sound), coreInstrumentId(sound), sound.trimDb);
}

bool coreSoundInstalled(const CoreSound& sound) {
    return synth::sampler::SoundPackLibrary::find(packIdOf(sound), coreInstrumentId(sound)).has_value();
}

} // namespace synth::soundpacks
