// Tests for the opt-in output-level stage (ModuleBase::addOutputLevelParameter /
// prepareOutputLevel / applyOutputLevel) and for the modules that adopt it.
//
// See docs/fx_modules.md § Output Level and GitHub issue #122.

#include "Modules/AttenuverterModule.h"
#include "Modules/FX/BitcrusherModule.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/FX/PhaserModule.h"
#include "Modules/FX/PitchShifterModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FX/RingModulatorModule.h"
#include "Modules/FilterModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

juce::AudioParameterFloat* levelParamOf(ModuleBase& module) {
    return dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&module, "outputLevel"));
}

// Sets the level parameter to a raw (non-normalised) value in [0, 1].
void setLevel(ModuleBase& module, float level) {
    auto* param = levelParamOf(module);
    ASSERT_NE(param, nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(level));
}

void fillSine(juce::AudioBuffer<float>& buffer, int numAudioChannels, float amplitude = 0.5f) {
    buffer.clear();
    const float phaseStep = juce::MathConstants<float>::twoPi * 220.0f / static_cast<float>(kSampleRate);
    for (int ch = 0; ch < numAudioChannels; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, amplitude * std::sin(phaseStep * static_cast<float>(i)));
}

int bufferChannelsFor(const ModuleBase& module) {
    return juce::jmax(module.getTotalNumInputChannels(), module.getTotalNumOutputChannels());
}

float peakOf(const juce::AudioBuffer<float>& buffer, int numAudioChannels) {
    float peak = 0.0f;
    for (int ch = 0; ch < numAudioChannels; ++ch)
        peak = juce::jmax(peak, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
    return peak;
}

// A module factory plus the number of leading channels that carry audio, so one body of
// tests can cover every adopting module.
struct LevelModuleCase {
    const char* name;
    int numAudioChannels;
    std::function<std::unique_ptr<ModuleBase>()> make;
};

std::vector<LevelModuleCase> allCases() {
    return {
        {"Delay", 2, [] { return std::make_unique<DelayModule>(); }},
        {"Reverb", 2, [] { return std::make_unique<ReverbModule>(); }},
        {"Chorus", 2, [] { return std::make_unique<ChorusModule>(); }},
        {"Phaser", 2, [] { return std::make_unique<PhaserModule>(); }},
        {"Flanger", 2, [] { return std::make_unique<FlangerModule>(); }},
        {"Distortion", 2, [] { return std::make_unique<DistortionModule>(); }},
        {"Bitcrusher", 2, [] { return std::make_unique<BitcrusherModule>(); }},
        {"Pitch Shifter", 2, [] { return std::make_unique<PitchShifterModule>(); }},
        {"Ring Modulator", 2, [] { return std::make_unique<RingModulatorModule>(); }},
        {"Filter", 1, [] { return std::make_unique<FilterModule>(); }},
    };
}

// Minimal ModuleBase subclass used to exercise the helper contract directly, free of any
// module's DSP. Pass-through on the audio channels; CV channels are left alone so the
// tests can prove applyOutputLevel does not reach them.
class OutputLevelProbeModule : public ModuleBase {
public:
    OutputLevelProbeModule()
        : ModuleBase("Probe", 4, 4) {
        addOutputLevelParameter();
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int) override { prepareOutputLevel(sampleRate); }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        if (isBypassed())
            return;
        if (isMuted()) {
            buffer.clear();
            return;
        }
        applyOutputLevel(buffer, audioChannels);
    }

    ModuleType getModuleType() const override { return ModuleType::VCA; } // unused by these tests

    // Builds a pre-#122 state blob — one that has no "outputLevel" property at all.
    juce::MemoryBlock makeLegacyStateBlob() {
        juce::ValueTree state("ModuleState");
        state.setProperty("bypassed", 0.0f, nullptr);
        state.setProperty("muted", 0.0f, nullptr);
        juce::MemoryBlock block;
        copyXmlToBinary(*state.createXml(), block);
        return block;
    }

    int audioChannels = 2;
};

} // namespace

// ---------------------------------------------------------------------------
// Helper contract (ModuleBase)
// ---------------------------------------------------------------------------

TEST(OutputLevelHelper, DefaultsToUnityAndReportsAdoption) {
    OutputLevelProbeModule probe;
    EXPECT_TRUE(probe.hasOutputLevel());
    EXPECT_FLOAT_EQ(probe.getOutputLevel(), 1.0f);
}

TEST(OutputLevelHelper, UnityIsBitExactPassThrough) {
    OutputLevelProbeModule probe;
    probe.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillSine(buffer, 2);
    juce::AudioBuffer<float> expected(buffer);

    juce::MidiBuffer midi;
    probe.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), expected.getSample(ch, i)) << "ch " << ch << " sample " << i;
}

TEST(OutputLevelHelper, ScalesOnlyTheDeclaredAudioChannels) {
    OutputLevelProbeModule probe;
    setLevel(probe, 0.25f);
    probe.prepareToPlay(kSampleRate, kBlockSize); // snaps the smoother to 0.25

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillSine(buffer, 4); // audio on 0-1, CV-shaped content on 2-3
    juce::AudioBuffer<float> input(buffer);

    juce::MidiBuffer midi;
    probe.processBlock(buffer, midi);

    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_NEAR(buffer.getSample(0, i), input.getSample(0, i) * 0.25f, 1.0e-6f);
        EXPECT_NEAR(buffer.getSample(1, i), input.getSample(1, i) * 0.25f, 1.0e-6f);
        // Channels at/above audioChannels are CV — they must come through untouched.
        EXPECT_FLOAT_EQ(buffer.getSample(2, i), input.getSample(2, i));
        EXPECT_FLOAT_EQ(buffer.getSample(3, i), input.getSample(3, i));
    }
}

TEST(OutputLevelHelper, ZeroLevelSilencesAudioChannels) {
    OutputLevelProbeModule probe;
    setLevel(probe, 0.0f);
    probe.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillSine(buffer, 2);

    juce::MidiBuffer midi;
    probe.processBlock(buffer, midi);

    EXPECT_FLOAT_EQ(peakOf(buffer, 2), 0.0f);
}

TEST(OutputLevelHelper, StepChangeRampsInsteadOfClicking) {
    OutputLevelProbeModule probe;
    probe.prepareToPlay(kSampleRate, kBlockSize);

    juce::MidiBuffer midi;

    // Block 1 at unity on DC input, so any discontinuity is purely the level ramp.
    juce::AudioBuffer<float> first(4, kBlockSize);
    first.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            first.setSample(ch, i, 1.0f);
    probe.processBlock(first, midi);
    ASSERT_FLOAT_EQ(first.getSample(0, kBlockSize - 1), 1.0f);

    // Slam the level to zero; the 10 ms ramp must spread it over ~441 samples.
    setLevel(probe, 0.0f);

    juce::AudioBuffer<float> second(4, kBlockSize);
    second.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            second.setSample(ch, i, 1.0f);
    probe.processBlock(second, midi);

    float previous = first.getSample(0, kBlockSize - 1);
    float maxStep = 0.0f;
    for (int i = 0; i < kBlockSize; ++i) {
        const float current = second.getSample(0, i);
        maxStep = juce::jmax(maxStep, std::abs(current - previous));
        previous = current;
    }
    // A hard switch would show a step of 1.0; a 10 ms ramp steps by ~1/441.
    EXPECT_LT(maxStep, 0.01f);
    // And it must actually reach zero by the end of the block (441 < 512 samples).
    EXPECT_NEAR(second.getSample(0, kBlockSize - 1), 0.0f, 1.0e-6f);
}

TEST(OutputLevelHelper, BypassPassesDrySignalAtAnyLevel) {
    OutputLevelProbeModule probe;
    setLevel(probe, 0.0f);
    probe.prepareToPlay(kSampleRate, kBlockSize);
    probe.setBypassed(true);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillSine(buffer, 2);
    juce::AudioBuffer<float> input(buffer);

    juce::MidiBuffer midi;
    probe.processBlock(buffer, midi);

    // Bypass is a dry pass-through — Level must not be able to silence it.
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), input.getSample(ch, i));
}

TEST(OutputLevelHelper, MuteClearsEvenAtUnityLevel) {
    OutputLevelProbeModule probe;
    probe.prepareToPlay(kSampleRate, kBlockSize);
    probe.setMuted(true);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillSine(buffer, 2);

    juce::MidiBuffer midi;
    probe.processBlock(buffer, midi);

    EXPECT_FLOAT_EQ(peakOf(buffer, 4), 0.0f);
}

TEST(OutputLevelHelper, LegacyStateWithoutOutputLevelLoadsAtUnity) {
    OutputLevelProbeModule probe;
    auto legacy = probe.makeLegacyStateBlob();

    // A preset saved before this parameter existed must leave it at unity, so existing
    // patches sound identical after the upgrade.
    probe.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
    EXPECT_FLOAT_EQ(probe.getOutputLevel(), 1.0f);
}

TEST(OutputLevelHelper, LevelSurvivesStateRoundTrip) {
    OutputLevelProbeModule saver;
    setLevel(saver, 0.25f);

    juce::MemoryBlock state;
    saver.getStateInformation(state);

    OutputLevelProbeModule loader;
    loader.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    EXPECT_NEAR(loader.getOutputLevel(), 0.25f, 1.0e-4f);
}

// ---------------------------------------------------------------------------
// Adopting modules
// ---------------------------------------------------------------------------

TEST(OutputLevelModules, EveryAdoptingModuleExposesUnityByDefault) {
    for (const auto& testCase : allCases()) {
        auto module = testCase.make();
        EXPECT_TRUE(module->hasOutputLevel()) << testCase.name << " should opt into the output-level stage";
        EXPECT_FLOAT_EQ(module->getOutputLevel(), 1.0f) << testCase.name << " must default to unity";
    }
}

TEST(OutputLevelModules, LevelParameterIsAddedLast) {
    // The parameter must land at the end of each module's list; positional
    // getParameters()[n] lookups elsewhere in the codebase depend on it.
    for (const auto& testCase : allCases()) {
        auto module = testCase.make();
        const auto& params = module->getParameters();
        ASSERT_GT(params.size(), 1) << testCase.name;

        int levelIndex = -1;
        for (int i = 0; i < params.size(); ++i)
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(params[i]))
                if (ranged->paramID == "outputLevel")
                    levelIndex = i;

        ASSERT_GE(levelIndex, 0) << testCase.name << " has no outputLevel parameter";
        // "muted" is appended by addMuteParameter() after it, so allow that one trailing slot.
        EXPECT_GE(levelIndex, params.size() - 2) << testCase.name << " added outputLevel too early";
    }
}

TEST(OutputLevelModules, HalfLevelHalvesTheOutputExactly) {
    // The stage sits after each module's DSP, so scaling is linear in the output even for
    // the non-linear modules (Distortion) — two identically-seeded instances differ by
    // exactly the level ratio.
    for (const auto& testCase : allCases()) {
        auto unity = testCase.make();
        auto halved = testCase.make();
        setLevel(*halved, 0.5f);

        unity->prepareToPlay(kSampleRate, kBlockSize);
        halved->prepareToPlay(kSampleRate, kBlockSize);

        juce::AudioBuffer<float> unityBuffer(bufferChannelsFor(*unity), kBlockSize);
        juce::AudioBuffer<float> halvedBuffer(bufferChannelsFor(*halved), kBlockSize);
        fillSine(unityBuffer, testCase.numAudioChannels);
        fillSine(halvedBuffer, testCase.numAudioChannels);

        juce::MidiBuffer midi;
        unity->processBlock(unityBuffer, midi);
        halved->processBlock(halvedBuffer, midi);

        const float unityPeak = peakOf(unityBuffer, testCase.numAudioChannels);
        ASSERT_GT(unityPeak, 1.0e-3f) << testCase.name << " produced no signal to scale";

        for (int ch = 0; ch < testCase.numAudioChannels; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                EXPECT_NEAR(halvedBuffer.getSample(ch, i), unityBuffer.getSample(ch, i) * 0.5f, 1.0e-5f)
                    << testCase.name << " ch " << ch << " sample " << i;
    }
}

TEST(OutputLevelModules, ZeroLevelSilencesEveryAdoptingModule) {
    for (const auto& testCase : allCases()) {
        auto module = testCase.make();
        setLevel(*module, 0.0f);
        module->prepareToPlay(kSampleRate, kBlockSize);

        juce::AudioBuffer<float> buffer(bufferChannelsFor(*module), kBlockSize);
        fillSine(buffer, testCase.numAudioChannels);

        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);

        EXPECT_NEAR(peakOf(buffer, testCase.numAudioChannels), 0.0f, 1.0e-6f)
            << testCase.name << " still passed audio at level 0";
    }
}

TEST(OutputLevelModules, BypassStillPassesDryAudioAtZeroLevel) {
    for (const auto& testCase : allCases()) {
        auto module = testCase.make();
        setLevel(*module, 0.0f);
        module->prepareToPlay(kSampleRate, kBlockSize);
        module->setBypassed(true);

        juce::AudioBuffer<float> buffer(bufferChannelsFor(*module), kBlockSize);
        fillSine(buffer, testCase.numAudioChannels);
        juce::AudioBuffer<float> input(buffer);

        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);

        for (int ch = 0; ch < testCase.numAudioChannels; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                EXPECT_FLOAT_EQ(buffer.getSample(ch, i), input.getSample(ch, i))
                    << testCase.name << " bypass was attenuated at ch " << ch << " sample " << i;
    }
}

TEST(OutputLevelModules, MuteClearsAtUnityLevel) {
    for (const auto& testCase : allCases()) {
        auto module = testCase.make();
        module->prepareToPlay(kSampleRate, kBlockSize);
        module->setMuted(true);

        juce::AudioBuffer<float> buffer(bufferChannelsFor(*module), kBlockSize);
        fillSine(buffer, testCase.numAudioChannels);

        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);

        EXPECT_FLOAT_EQ(peakOf(buffer, testCase.numAudioChannels), 0.0f) << testCase.name << " was not muted";
    }
}

TEST(OutputLevelModules, DelayLevelDoesNotStarveTheFeedbackPath) {
    // Level is an output stage, deliberately outside the feedback loop: dropping it and
    // restoring it must leave the repeats intact rather than decaying the delay line.
    DelayModule attenuated;
    // Short time + high feedback so the repeats are still circulating a few blocks later.
    auto* timeParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&attenuated, "time"));
    auto* feedbackParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&attenuated, "feedback"));
    ASSERT_NE(timeParam, nullptr);
    ASSERT_NE(feedbackParam, nullptr);
    timeParam->setValueNotifyingHost(timeParam->convertTo0to1(10.0f));
    feedbackParam->setValueNotifyingHost(feedbackParam->convertTo0to1(0.9f));

    setLevel(attenuated, 0.0f);
    attenuated.prepareToPlay(kSampleRate, kBlockSize);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, kBlockSize);

    // Feed an impulse-rich block, then several silent blocks at level 0.
    fillSine(buffer, 2, 1.0f);
    attenuated.processBlock(buffer, midi);
    for (int block = 0; block < 4; ++block) {
        buffer.clear();
        attenuated.processBlock(buffer, midi);
        ASSERT_FLOAT_EQ(peakOf(buffer, 2), 0.0f);
    }

    // Restore unity: the stored repeats should still be there.
    setLevel(attenuated, 1.0f);
    buffer.clear();
    for (int block = 0; block < 3; ++block) {
        buffer.clear();
        attenuated.processBlock(buffer, midi);
    }
    EXPECT_GT(peakOf(buffer, 2), 1.0e-3f) << "delay line was starved while Level was down";
}

TEST(OutputLevelModules, FilterScalesAllEightVoicesInPolyMode) {
    auto makePolyFilter = [](float level) {
        auto filter = std::make_unique<FilterModule>();
        auto* poly = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(filter.get(), "poly"));
        EXPECT_NE(poly, nullptr);
        if (poly != nullptr)
            poly->setValueNotifyingHost(1.0f);
        setLevel(*filter, level);
        filter->prepareToPlay(kSampleRate, kBlockSize);
        return filter;
    };

    auto unity = makePolyFilter(1.0f);
    auto halved = makePolyFilter(0.5f);

    juce::AudioBuffer<float> unityBuffer(11, kBlockSize);
    juce::AudioBuffer<float> halvedBuffer(11, kBlockSize);
    fillSine(unityBuffer, 8); // all eight voice channels carry audio in poly mode
    fillSine(halvedBuffer, 8);

    juce::MidiBuffer midi;
    unity->processBlock(unityBuffer, midi);
    halved->processBlock(halvedBuffer, midi);

    ASSERT_GT(peakOf(unityBuffer, 8), 1.0e-3f);
    for (int voice = 0; voice < 8; ++voice)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_NEAR(halvedBuffer.getSample(voice, i), unityBuffer.getSample(voice, i) * 0.5f, 1.0e-5f)
                << "voice " << voice << " sample " << i;
}

// ---------------------------------------------------------------------------
// Regression guard for the positional-parameter landmine
// ---------------------------------------------------------------------------

TEST(OutputLevelModules, AttenuverterKeepsAmountAtParameterIndexOne) {
    // GraphEditor / AIStateMapper now look "amount" up by paramID, but FXModuleTests and
    // any external tooling still index it positionally. Adding an output level to the
    // Attenuverter would repoint that index — this pins it shut.
    AttenuverterModule attenuverter;
    EXPECT_FALSE(attenuverter.hasOutputLevel()) << "Attenuverter is itself a gain stage; it must not adopt one";

    auto* atIndexOne = dynamic_cast<juce::RangedAudioParameter*>(attenuverter.getParameters()[1]);
    ASSERT_NE(atIndexOne, nullptr);
    EXPECT_EQ(atIndexOne->paramID, "amount");
}
