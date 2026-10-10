// DrumKitModuleTests.cpp
// The Drum Kit's note map and sound: every mapped note sounds and decays to silence in time, an unmapped note
// is silent, velocity scales level, a closed hat chokes an open one, a full-velocity hit leaves headroom, and
// bypass and mute are silent. Hits are triggered with note-ons at exact sample positions.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

struct Hit {
    int note;
    float velocity = 1.0f;
    int atSample = 0;
};

// Renders `seconds` of left-channel audio with the given hits fired at their sample positions.
std::vector<float> render(DrumKitModule& module, const std::vector<Hit>& hits, double seconds) {
    const int total = static_cast<int>(seconds * kRate);
    std::vector<float> out(static_cast<std::size_t>(total), 0.0f);
    for (int start = 0; start < total; start += kBlock) {
        const int n = std::min(kBlock, total - start);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::MidiBuffer midi;
        for (const auto& hit : hits)
            if (hit.atSample >= start && hit.atSample < start + n)
                midi.addEvent(juce::MidiMessage::noteOn(10, hit.note, hit.velocity), hit.atSample - start);
        module.processBlock(block, midi);
        std::copy(block.getReadPointer(0), block.getReadPointer(0) + n, out.begin() + start);
    }
    return out;
}

std::unique_ptr<DrumKitModule> makeKit() {
    auto kit = std::make_unique<DrumKitModule>();
    kit->prepareToPlay(kRate, kBlock);
    return kit;
}

float peakOf(const std::vector<float>& s, double fromSec = 0.0, double toSec = 1.0e9) {
    const auto a = static_cast<std::size_t>(std::max(0.0, fromSec) * kRate);
    const auto b = std::min(s.size(), static_cast<std::size_t>(toSec * kRate));
    float peak = 0.0f;
    for (std::size_t i = a; i < b; ++i)
        peak = std::max(peak, std::fabs(s[i]));
    return peak;
}

float rmsOf(const std::vector<float>& s, double fromSec, double toSec) {
    const auto a = static_cast<std::size_t>(fromSec * kRate);
    const auto b = std::min(s.size(), static_cast<std::size_t>(toSec * kRate));
    double sum = 0.0;
    for (std::size_t i = a; i < b; ++i)
        sum += static_cast<double>(s[i]) * s[i];
    return b > a ? static_cast<float>(std::sqrt(sum / static_cast<double>(b - a))) : 0.0f;
}

float paramValue(DrumKitModule& kit, const char* id) {
    return static_cast<juce::RangedAudioParameter*>(findParameterByID(&kit, id))
        ->convertFrom0to1(static_cast<juce::RangedAudioParameter*>(findParameterByID(&kit, id))->getValue());
}

// Every mapped note with the decay setting (seconds) that governs it at the default parameters.
struct MappedNote {
    int note;
    const char* what;
    const char* decayParam; // the parameter that sets how long it rings; nullptr for a fixed-length drum
    float scale;            // multiplier the module applies to that parameter
    float fixedDecay;       // used when decayParam is nullptr
};

const std::vector<MappedNote>& mappedNotes() {
    static const std::vector<MappedNote> notes = {
        {35, "kick", "kickDecay", 1.0f, 0},         {36, "kick", "kickDecay", 1.0f, 0},
        {38, "snare", "snareDecay", 1.0f, 0},       {40, "snare", "snareDecay", 1.0f, 0},
        {37, "rim", nullptr, 1.0f, 0.03f},          {39, "clap", "clapDecay", 1.0f, 0},
        {42, "closed hat", "closedDecay", 1.0f, 0}, {44, "pedal hat", "closedDecay", 0.8f, 0},
        {46, "open hat", "openDecay", 1.0f, 0},     {41, "tom", "tomDecay", 1.0f, 0},
        {43, "tom", "tomDecay", 1.0f, 0},           {45, "tom", "tomDecay", 1.0f, 0},
        {47, "tom", "tomDecay", 1.0f, 0},           {48, "tom", "tomDecay", 1.0f, 0},
        {50, "tom", "tomDecay", 1.0f, 0},           {49, "crash", "cymbalDecay", 1.0f, 0},
        {57, "crash", "cymbalDecay", 1.0f, 0},      {51, "ride", "cymbalDecay", 0.8f, 0},
        {59, "ride", "cymbalDecay", 0.8f, 0},
    };
    return notes;
}

} // namespace

TEST(DrumKitModule, IsRegisteredAsAMidiInstrumentWithAStereoPairAndNoInputs) {
    auto module = synth::AIStateMapper::createModule("Drum Kit");
    ASSERT_NE(module, nullptr);
    auto* kit = dynamic_cast<DrumKitModule*>(module.get());
    ASSERT_NE(kit, nullptr);
    EXPECT_EQ(kit->getModuleType(), ModuleType::DrumKit);
    EXPECT_TRUE(isMidiInstrumentType(ModuleType::DrumKit));
    EXPECT_TRUE(kit->acceptsMidi());
    EXPECT_FALSE(kit->producesMidi());
    EXPECT_EQ(kit->getTotalNumInputChannels(), 0);
    EXPECT_EQ(kit->getTotalNumOutputChannels(), 2);
    EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(kit), "Drum Kit");
}

TEST(DrumKitModule, EveryMappedNoteSoundsAndDecaysToSilenceWithinItsDecay) {
    for (const auto& n : mappedNotes()) {
        SCOPED_TRACE(juce::String(n.what).toStdString() + " note " + std::to_string(n.note));
        auto kit = makeKit();
        const float decay = n.decayParam != nullptr ? paramValue(*kit, n.decayParam) * n.scale : n.fixedDecay;
        const auto out = render(*kit, {{n.note}}, decay * 1.6 + 0.3);
        EXPECT_GT(peakOf(out, 0.0, 0.2), 0.02f) << "the drum is audible";
        EXPECT_LT(peakOf(out, decay * 1.5 + 0.05), 0.003f) << "silent shortly after its decay time";
    }
}

TEST(DrumKitModule, AnUnmappedNoteIsSilent) {
    for (const int note : {0, 34, 52, 53, 55, 56, 60, 72, 127}) {
        auto kit = makeKit();
        const auto out = render(*kit, {{note}}, 0.3);
        EXPECT_EQ(peakOf(out), 0.0f) << "note " << note;
        EXPECT_EQ(DrumKitModule::drumForNote(note), DrumKitModule::Drum::None);
    }
}

TEST(DrumKitModule, NoteOffAndNoteOnWithZeroVelocityDoNothing) {
    auto kit = makeKit();
    juce::AudioBuffer<float> block(2, kBlock);
    block.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOff(10, 36), 0);
    midi.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(0)), 4);
    kit->processBlock(block, midi);
    EXPECT_EQ(block.getMagnitude(0, kBlock), 0.0f);
}

TEST(DrumKitModule, AHitStartsAtItsSamplePosition) {
    auto kit = makeKit();
    const auto out = render(*kit, {{38, 1.0f, 700}}, 0.1);
    EXPECT_EQ(peakOf(out, 0.0, 699.0 / kRate), 0.0f) << "nothing before the note-on";
    EXPECT_GT(peakOf(out, 700.0 / kRate, 1000.0 / kRate), 0.01f);
}

TEST(DrumKitModule, BothChannelsCarryTheSameSignal) {
    auto kit = makeKit();
    juce::AudioBuffer<float> block(2, kBlock);
    block.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(10, 38, 1.0f), 0);
    kit->processBlock(block, midi);
    for (int i = 0; i < kBlock; ++i)
        EXPECT_EQ(block.getSample(0, i), block.getSample(1, i));
}

TEST(DrumKitModule, VelocityScalesLevel) {
    for (const int note : {36, 38, 39, 42, 41, 49}) {
        SCOPED_TRACE(note);
        auto loud = makeKit();
        auto soft = makeKit();
        const float loudPeak = peakOf(render(*loud, {{note, 1.0f}}, 0.5));
        const float softPeak = peakOf(render(*soft, {{note, 0.5f}}, 0.5));
        EXPECT_GT(loudPeak, softPeak * 1.5f);
        EXPECT_GT(softPeak, 0.0f);
    }
}

TEST(DrumKitModule, AFullVelocityHitLeavesHeadroomAtTheDefaultSettings) {
    for (const auto& n : mappedNotes()) {
        SCOPED_TRACE(juce::String(n.what).toStdString() + " note " + std::to_string(n.note));
        auto kit = makeKit();
        const float peak = peakOf(render(*kit, {{n.note}}, 1.0));
        EXPECT_LT(peak, 0.5f) << "no louder than -6 dBFS";
        EXPECT_GT(peak, 0.1f) << "no quieter than -20 dBFS";
    }
}

TEST(DrumKitModule, EveryDrumAtOnceStaysBelowFullScale) {
    auto kit = makeKit();
    std::vector<Hit> hits;
    for (const int note : {36, 38, 37, 39, 42, 46, 41, 43, 45, 47, 48, 50, 49, 51})
        hits.push_back({note});
    EXPECT_LT(peakOf(render(*kit, hits, 0.5)), 1.0f);
}

TEST(DrumKitModule, AClosedHatChokesTheOpenHat) {
    // Open hat at 0, closed hat at 0.1 s. Without the closed hat the open hat still rings at 0.3 s.
    auto choked = makeKit();
    auto open = makeKit();
    const int closedAt = static_cast<int>(0.1 * kRate);
    const auto withChoke = render(*choked, {{46}, {42, 1.0f, closedAt}}, 0.6);
    const auto withoutChoke = render(*open, {{46}}, 0.6);
    EXPECT_GT(rmsOf(withoutChoke, 0.2, 0.3), 0.001f) << "the open hat rings on its own";
    EXPECT_LT(rmsOf(withChoke, 0.2, 0.3), rmsOf(withoutChoke, 0.2, 0.3) * 0.1f) << "the closed hat cut it off";
    // The pedal hat chokes too.
    auto pedal = makeKit();
    EXPECT_LT(rmsOf(render(*pedal, {{46}, {44, 1.0f, closedAt}}, 0.6), 0.2, 0.3), rmsOf(withoutChoke, 0.2, 0.3) * 0.1f);
}

// Share of a hit's energy below `splitHz`, from a Goertzel-style DFT sum over a coarse frequency grid.
float energyShareBelow(const std::vector<float>& s, double splitHz) {
    double below = 0.0, total = 0.0;
    for (double hz = 200.0; hz < 20000.0; hz += 100.0) {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            const double ph = 2.0 * 3.14159265358979 * hz * static_cast<double>(i) / kRate;
            re += s[i] * std::cos(ph);
            im += s[i] * std::sin(ph);
        }
        const double power = re * re + im * im;
        total += power;
        if (hz < splitHz)
            below += power;
    }
    return total > 0.0 ? static_cast<float>(below / total) : 1.0f;
}

TEST(DrumKitModule, HatsAreBrightHissWithNoLowToneToRingLikeACowbell) {
    for (const int note : {42, 46}) {
        SCOPED_TRACE("note " + std::to_string(note));
        auto kit = makeKit();
        auto hit = render(*kit, {{note}}, 0.2);
        EXPECT_GT(peakOf(hit), 0.01f);
        EXPECT_LT(energyShareBelow(hit, 5000.0), 0.02f) << "the hat's body is above 5 kHz";
    }
}

TEST(DrumKitModule, AnOpenHatDoesNotChokeAClosedHat) {
    auto kit = makeKit();
    const int openAt = static_cast<int>(0.02 * kRate);
    const auto out = render(*kit, {{42}, {46, 1.0f, openAt}}, 0.3);
    EXPECT_GT(rmsOf(out, 0.1, 0.2), 0.003f);
}

TEST(DrumKitModule, RetriggeringADrumRestartsIt) {
    auto kit = makeKit();
    const auto out = render(*kit, {{36}, {36, 1.0f, static_cast<int>(0.2 * kRate)}}, 0.4);
    EXPECT_GT(peakOf(out, 0.2, 0.22), peakOf(out, 0.15, 0.17)) << "the second hit is a fresh attack";
}

TEST(DrumKitModule, TuneAndDecayParametersChangeTheSound) {
    auto base = makeKit();
    auto longKick = makeKit();
    static_cast<juce::AudioParameterFloat*>(findParameterByID(longKick.get(), "kickDecay"))
        ->setValueNotifyingHost(1.0f);
    const auto a = render(*base, {{36}}, 1.0);
    const auto b = render(*longKick, {{36}}, 1.0);
    EXPECT_GT(rmsOf(b, 0.6, 0.8), rmsOf(a, 0.6, 0.8) * 5.0f) << "a longer Kick decay rings longer";

    auto quiet = makeKit();
    static_cast<juce::AudioParameterFloat*>(findParameterByID(quiet.get(), "kickLevel"))->setValueNotifyingHost(0.45f);
    EXPECT_LT(peakOf(render(*quiet, {{36}}, 0.3)), peakOf(a, 0.0, 0.3) * 0.7f);

    // Tune: a higher tom has more zero crossings in the same window.
    auto lowTune = makeKit();
    auto highTune = makeKit();
    static_cast<juce::AudioParameterFloat*>(findParameterByID(highTune.get(), "tomTune"))->setValueNotifyingHost(1.0f);
    const auto crossings = [](const std::vector<float>& s) {
        int count = 0;
        for (std::size_t i = 1; i < s.size() && i < 4410; ++i)
            count += (s[i - 1] < 0.0f) != (s[i] < 0.0f);
        return count;
    };
    EXPECT_GT(crossings(render(*highTune, {{45}}, 0.2)), crossings(render(*lowTune, {{45}}, 0.2)));
}

TEST(DrumKitModule, TomsAreTunedLowToHighByNote) {
    const auto crossings = [](int note) {
        auto kit = makeKit();
        const auto s = render(*kit, {{note}}, 0.3);
        int count = 0;
        for (std::size_t i = 4411; i < 4411 + 8820; ++i) // 0.1 s .. 0.3 s, after the pitch drop
            count += (s[i - 1] < 0.0f) != (s[i] < 0.0f);
        return count;
    };
    int previous = 0;
    for (const int note : {41, 43, 45, 47, 48, 50}) {
        const int c = crossings(note);
        EXPECT_GT(c, previous) << "note " << note;
        previous = c;
    }
}

TEST(DrumKitModule, BypassAndMuteAreSilentAndStopRingingDrums) {
    for (const bool bypass : {true, false}) {
        auto kit = makeKit();
        render(*kit, {{49}}, 0.05); // a crash is ringing
        if (bypass)
            kit->setBypassed(true);
        else
            kit->setMuted(true);
        EXPECT_EQ(peakOf(render(*kit, {{36}, {38}}, 0.2)), 0.0f) << (bypass ? "bypassed" : "muted");
        if (bypass)
            kit->setBypassed(false);
        else
            kit->setMuted(false);
        const auto after = render(*kit, {}, 0.1);
        EXPECT_EQ(peakOf(after), 0.0f) << "the crash did not resume";
        EXPECT_GT(peakOf(render(*kit, {{36}}, 0.1)), 0.05f) << "and the kit plays again";
    }
}

TEST(DrumKitModule, MasterLevelScalesEverything) {
    auto full = makeKit();
    auto half = makeKit();
    static_cast<juce::AudioParameterFloat*>(findParameterByID(half.get(), "level"))->setValueNotifyingHost(0.35f);
    half->prepareToPlay(kRate, kBlock); // the master level glides over 10 ms; prepare snaps it
    const float ratio = peakOf(render(*half, {{38}}, 0.3)) / peakOf(render(*full, {{38}}, 0.3));
    EXPECT_NEAR(ratio, 0.5f, 0.05f);
}

TEST(DrumKitModule, OutputIsAlwaysFinite) {
    auto kit = makeKit();
    std::vector<Hit> hits;
    for (int i = 0; i < 120; ++i)
        hits.push_back({35 + (i * 7) % 25, 1.0f, i * 400});
    for (const float s : render(*kit, hits, 1.5))
        ASSERT_TRUE(std::isfinite(s));
}

TEST(DrumKitModule, ChangingTheMasterLevelWhileADrumRingsDoesNotStepTheOutput) {
    auto kit = makeKit();
    render(*kit, {{41}}, 0.02); // a tom is ringing
    static_cast<juce::AudioParameterFloat*>(findParameterByID(kit.get(), "level"))->setValueNotifyingHost(0.05f);
    const auto after = render(*kit, {}, 0.05);
    float worst = 0.0f;
    for (std::size_t i = 1; i < after.size(); ++i)
        worst = std::max(worst, std::fabs(after[i] - after[i - 1]));
    EXPECT_LT(worst, 0.02f) << "the level glides instead of jumping";
}
