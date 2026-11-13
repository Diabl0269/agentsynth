// CoreSoundLevelTests.cpp -- the level trim of each core and animal sound against its stand-in synth, rendered offline.
// Needs the real packs, so the core part runs only when AGENTSYNTH_CORE_PACK_DIR names a folder that holds the
// unpacked `core` folder, and the animals part only when AGENTSYNTH_ANIMALS_PACK_DIR names the one holding `animals`.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "Modules/SamplerModule.h"
#include "SoundPacks/CoreSounds.h"
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kSeconds = 3;
// The plucked pair has no trim: the recordings already sit 2 dB above the stand-ins' attack.
constexpr double kToleranceDb = 2.5;
constexpr int kSustainedBlocks = (int)kRate / kBlock; // RMS window of a held sound: its first second
constexpr int kStruckBlocks = kSustainedBlocks / 4;   // RMS window of a struck or plucked sound: its attack
constexpr int kHoldBlocks = 2 * (int)kRate / kBlock;

struct Level {
    double rmsDb = -200.0;
    double peakDb = -200.0;
};

bool isBass(const synth::soundpacks::CoreSound& sound) {
    const juce::String id(sound.id);
    return id.contains("bass") || id.contains("guitar");
}

// Struck and plucked sounds decay at once; their loudness is the attack, not the tail.
bool isStruck(const synth::soundpacks::CoreSound& sound) {
    return isBass(sound) || juce::String(sound.id).contains("piano");
}

// What is played and how much of it is measured.
struct Phrase {
    std::vector<int> notes;
    int rmsBlocks;
};

// A mid-velocity phrase: a C3-C4-E4-G4 chord, or a single E2 for the bass and the guitar.
Phrase phraseOf(const synth::soundpacks::CoreSound& sound) {
    const std::vector<int> notes = isBass(sound) ? std::vector<int>{40} : std::vector<int>{48, 60, 64, 67};
    return {notes, isStruck(sound) ? kStruckBlocks : kSustainedBlocks};
}

Level render(juce::AudioProcessor& processor, const Phrase& phrase) {
    const int rmsBlocks = phrase.rmsBlocks;
    processor.setPlayConfigDetails(processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels(), kRate,
                                   kBlock);
    processor.prepareToPlay(kRate, kBlock);
    const std::vector<int>& notes = phrase.notes;
    const int channels = std::max(processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
    double sumSquares = 0.0, peak = 0.0;
    long count = 0;
    for (int b = 0; b < kSeconds * (int)kRate / kBlock; ++b) {
        juce::AudioBuffer<float> buffer(channels, kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        for (int note : notes) {
            if (b == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)90), 0);
            if (b == kHoldBlocks)
                midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        }
        processor.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i) {
                const double v = buffer.getSample(ch, i);
                peak = std::max(peak, std::fabs(v));
                if (b < rmsBlocks) {
                    sumSquares += v * v;
                    ++count;
                }
            }
    }
    Level level;
    level.rmsDb = 20.0 * std::log10(std::max(1e-10, std::sqrt(sumSquares / (double)count)));
    level.peakDb = 20.0 * std::log10(std::max(1e-10, peak));
    return level;
}

// The stand-in params the song planner typically sends for each sound (recorded answers); Level goes to the Sampler
// too.
struct Typical {
    const char* id;
    const char* params;
};

constexpr Typical kTypical[] = {
    {"core/upright-piano",
     R"({"material":"Marimba","decay":2.5,"damping":0.6,"brightness":0.3,"hardness":0.25,"resonator":0.35,"release":1.2,"velBright":0.4,"level":0.3})"},
    {"core/string-section", R"({"table":"Basic Shapes","level":0.9,"unison":4,"detune":18,"spread":0.6,"width":0.7})"},
    {"core/trumpet", R"({"table":"Formant","position":0.35,"level":0.48,"unison":2,"detune":8,"width":0.4})"},
    {"core/french-horn", R"({"table":"Formant","position":0.35,"level":0.48,"unison":2,"detune":8,"width":0.4})"},
    {"core/flute", R"({"table":"Formant","position":0.35,"level":0.48,"unison":2,"detune":8,"spread":0.3})"},
    {"core/clarinet", R"({"table":"Formant","position":0.35,"level":0.48,"unison":2,"detune":8,"spread":0.3})"},
    {"core/double-bass-plucked",
     R"({"decay":1.2,"decayTrack":0.5,"brightness":0.3,"pickHardness":0.4,"body":0.7,"release":0.4,"velBright":0.35,"level":0.3})"},
    {"core/electric-guitar",
     R"({"decay":1.2,"decayTrack":0.3,"brightness":0.45,"pickHardness":0.8,"body":0.6,"release":0.25,"velBright":0.6,"level":0.3})"},
};

// The animals are one-shots at their recorded pitch: a single C4 held, measured over the first second (the crow's one
// short caw over the first 0.3 s). The stand-in is the Oscillator the Nature card's flavour sends, at the middle of its
// role's Level range (lead 0.24-0.32, fx 0.2-0.3, arp 0.2-0.28).
struct Animal {
    const char* id;
    const char* params;
    int rmsBlocks;
};

constexpr int kCawBlocks = (int)(0.3 * kRate) / kBlock;

constexpr Animal kAnimals[] = {
    {"animals/whale", R"({"waveform":"Sine","octave":0,"level":0.28})", kSustainedBlocks},
    {"animals/wolf", R"({"waveform":"Saw","octave":0,"level":0.28})", kSustainedBlocks},
    {"animals/frog", R"({"waveform":"Saw","octave":2,"level":0.24})", kSustainedBlocks},
    {"animals/owl", R"({"waveform":"Sine","octave":0,"level":0.25})", kSustainedBlocks},
    {"animals/crow", R"({"waveform":"Saw","octave":0,"level":0.25})", kCawBlocks},
};

void applyParams(juce::AudioProcessor& processor, const char* json) {
    const auto parsed = juce::JSON::parse(json);
    synth::AIStateMapper::applyUntrustedParams(&processor, parsed.getDynamicObject());
}

double levelOf(const char* json) { return (double)juce::JSON::parse(json).getProperty("level", 0.8); }

void setLevel(juce::AudioProcessor& processor, double level) {
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter); ranged && ranged->paramID == "level")
            ranged->setValueNotifyingHost(ranged->convertTo0to1((float)level));
}

// Renders the stand-in, the bare Sampler and the trimmed Sampler for one sound and checks the trimmed one.
void checkSound(const char* id, const char* params, const Phrase& phrase) {
    const auto* sound = synth::soundpacks::findCoreSound(id);
    ASSERT_NE(sound, nullptr);
    auto standIn = synth::AIStateMapper::createModule(sound->standIn);
    applyParams(*standIn, params);
    const auto reference = render(*standIn, phrase);

    auto bare = synth::AIStateMapper::createModule("Sampler");
    auto* bareSampler = dynamic_cast<SamplerModule*>(bare.get());
    ASSERT_TRUE(bareSampler->loadInstrumentFromPack(synth::soundpacks::packIdOf(*sound),
                                                    synth::soundpacks::coreInstrumentId(*sound)));
    setLevel(*bare, levelOf(params));
    const auto before = render(*bare, phrase);

    auto trimmed = synth::AIStateMapper::createModule("Sampler");
    ASSERT_TRUE(synth::soundpacks::loadCoreSoundIntoSampler(*trimmed, *sound));
    setLevel(*trimmed, levelOf(params));
    const auto after = render(*trimmed, phrase);

    std::printf("%-26s %9.1f %9.1f %9.1f %8.1f %9.1f %9.1f\n", id, reference.rmsDb, reference.peakDb, before.rmsDb,
                sound->trimDb, after.rmsDb, after.peakDb);
    EXPECT_NEAR(after.rmsDb, reference.rmsDb, kToleranceDb) << id;
    EXPECT_LT(after.peakDb, -1.0) << id;
}

void printHeader() {
    std::printf("%-26s %9s %9s %9s %8s %9s %9s\n", "sound", "standIn", "standPeak", "sampler0", "trim", "after",
                "afterPeak");
}

} // namespace

TEST(CoreSoundLevels, EachSamplerSitsWithinTwoAndAHalfDecibelsOfItsStandInAndPeaksBelowMinusOne) {
    const char* dir = std::getenv("AGENTSYNTH_CORE_PACK_DIR");
    if (dir == nullptr)
        GTEST_SKIP() << "set AGENTSYNTH_CORE_PACK_DIR to the folder holding the unpacked core pack";
    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{juce::File(dir)});
    printHeader();
    for (const auto& typical : kTypical)
        checkSound(typical.id, typical.params, phraseOf(*synth::soundpacks::findCoreSound(typical.id)));
    synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
}

TEST(CoreSoundLevels, EachAnimalSitsWithinTwoAndAHalfDecibelsOfItsStandInAndPeaksBelowMinusOne) {
    const char* dir = std::getenv("AGENTSYNTH_ANIMALS_PACK_DIR");
    if (dir == nullptr)
        GTEST_SKIP() << "set AGENTSYNTH_ANIMALS_PACK_DIR to the folder holding the unpacked animals pack";
    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{juce::File(dir)});
    printHeader();
    for (const auto& animal : kAnimals)
        checkSound(animal.id, animal.params, Phrase{{60}, animal.rmsBlocks});
    synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
}
