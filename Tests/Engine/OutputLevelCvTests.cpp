// Level, Pan and the shared Mix of ModuleBase's output stage take a modulation cable: a real LFO at full amount moves
// the output, the CV lands where the knob would, and an unpatched module sounds exactly as it did with its old
// channels. See docs/modules/fx-modules.md#output-level-shared-stage.

#include "Modules/FX/BitcrusherModule.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/FX/GateModule.h"
#include "Modules/FX/LimiterModule.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FX/PhaserModule.h"
#include "Modules/FX/PitchShifterModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FX/RingModulatorModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VoiceMixerModule.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <optional>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

struct Case {
    const char* name;
    int audioChannels;
    int widthBeforeCables; // how many input channels the module had before Level, Mix and Pan took cables
    std::function<std::unique_ptr<ModuleBase>()> make;
};

template <typename T>
std::function<std::unique_ptr<ModuleBase>()> maker() {
    return [] { return std::make_unique<T>(); };
}

const std::vector<Case>& cases() {
    static const std::vector<Case> all = {
        {"Delay", 2, 5, maker<DelayModule>()},
        {"Reverb", 2, 8, maker<ReverbModule>()},
        {"Chorus", 2, 7, maker<ChorusModule>()},
        {"Phaser", 2, 7, maker<PhaserModule>()},
        {"Flanger", 2, 7, maker<FlangerModule>()},
        {"Distortion", 2, 4, maker<DistortionModule>()},
        {"Bitcrusher", 2, 6, maker<BitcrusherModule>()},
        {"Pitch Shifter", 2, 8, maker<PitchShifterModule>()},
        {"Ring Modulator", 2, 5, maker<RingModulatorModule>()},
        {"Gate", 2, 9, maker<GateModule>()},
        {"Filter", 1, 32, maker<FilterModule>()},
        {"Compressor", 2, 10, maker<CompressorModule>()},
        {"Limiter", 2, 6, maker<LimiterModule>()},
        {"Parametric EQ", 2, 15, maker<ParametricEQModule>()},
    };
    return all;
}

// Fills the audio channels with a running sine, and every other channel of a module's buffer with silence.
struct Sine {
    double phase = 0.0;
    void fill(juce::AudioBuffer<float>& buffer, int audioChannels) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const auto value = 0.5f * (float)std::sin(phase);
            phase += juce::MathConstants<double>::twoPi * 220.0 / kRate;
            for (int ch = 0; ch < audioChannels; ++ch)
                buffer.setSample(ch, i, value);
        }
    }
};

void prepare(ModuleBase& module) {
    module.setPlayConfigDetails(module.getTotalNumInputChannels(), module.getTotalNumOutputChannels(), kRate, kBlock);
    module.prepareToPlay(kRate, kBlock);
}

int widthOf(const ModuleBase& module) {
    return juce::jmax(module.getTotalNumInputChannels(), module.getTotalNumOutputChannels());
}

void setActual(ModuleBase& module, const juce::String& paramId, float value) {
    auto* param = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, paramId));
    ASSERT_NE(param, nullptr) << paramId;
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

void setDualIO(ModuleBase& module, bool dual) {
    if (auto* param = findParameterByID(&module, "dualIO"))
        param->setValueNotifyingHost(dual ? 1.0f : 0.0f);
}

// The block peak of the audio channels at the top and bottom of a real LFO's swing on `paramId`, full amount.
std::pair<float, float> peakRangeUnderLfo(ModuleBase& module, const juce::String& paramId, int audioChannels) {
    LFOModule lfo;
    lfo.setPlayConfigDetails(LFOModule::kNumInputs, LFOModule::kNumOutputs, kRate, kBlock);
    lfo.prepareToPlay(kRate, kBlock);
    setActual(lfo, "rateHz", 2.0f);
    if (auto* mode = findParameterByID(&lfo, "mode"))
        mode->setValueNotifyingHost(0.0f); // Free, so no transport is needed
    prepare(module);
    const int channel = module.modulationChannelForParam(paramId);
    EXPECT_GE(channel, 0) << paramId << " takes no cable";
    juce::AudioBuffer<float> buffer(juce::jmax(widthOf(module), channel + 1), kBlock);
    juce::AudioBuffer<float> lfoBuffer(LFOModule::kNumOutputs, kBlock);
    juce::MidiBuffer midi;
    Sine sine;
    float top = 0.0f, bottom = 10.0f;
    for (int b = 0; b < 200; ++b) { // over two LFO periods, after the module has settled
        lfoBuffer.clear();
        lfo.processBlock(lfoBuffer, midi);
        sine.fill(buffer, audioChannels);
        if (channel >= 0)
            buffer.copyFrom(channel, 0, lfoBuffer, 0, 0, kBlock);
        module.processBlock(buffer, midi);
        if (b < 80)
            continue;
        float peak = 0.0f;
        for (int ch = 0; ch < audioChannels; ++ch)
            peak = juce::jmax(peak, buffer.getMagnitude(ch, 0, kBlock));
        top = juce::jmax(top, peak);
        bottom = juce::jmin(bottom, peak);
    }
    return {top, bottom};
}

// The last block of a render, with either a constant CV on `paramId`'s channel or the knob set instead.
juce::AudioBuffer<float> settledBlock(const Case& testCase, const juce::String& paramId, std::optional<float> cv,
                                      std::optional<float> knob, bool dual, bool bypassed = false) {
    auto module = testCase.make();
    setDualIO(*module, dual);
    if (knob.has_value())
        setActual(*module, paramId, *knob);
    module->setBypassed(bypassed);
    prepare(*module);
    const int channel = module->modulationChannelForParam(paramId);
    juce::AudioBuffer<float> buffer(widthOf(*module), kBlock);
    juce::MidiBuffer midi;
    Sine sine;
    for (int b = 0; b < 120; ++b) {
        sine.fill(buffer, testCase.audioChannels);
        if (cv.has_value() && channel >= 0)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), *cv, kBlock);
        module->processBlock(buffer, midi);
    }
    return buffer;
}

float differenceOn(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channel) {
    float largest = 0.0f;
    for (int i = 0; i < kBlock; ++i)
        largest = juce::jmax(largest, std::abs(a.getSample(channel, i) - b.getSample(channel, i)));
    return largest;
}

} // namespace

// Regression test for FRO1156: Level refused a cable on the effects and the Filter.
TEST(OutputLevelCv, AFullAmountLfoOnLevelChangesTheOutputOfEveryModule) {
    for (const auto& testCase : cases()) {
        auto module = testCase.make();
        if (findParameterByID(module.get(), "outputLevel") == nullptr)
            continue; // Compressor, Limiter and Parametric EQ have their own output gain, which already takes a cable
        SCOPED_TRACE(testCase.name);
        const auto [top, bottom] = peakRangeUnderLfo(*module, "outputLevel", testCase.audioChannels);
        printf("[Level under LFO] %-15s peak top %.4f bottom %.4f\n", testCase.name, top, bottom);
        EXPECT_GT(top, 0.05f);
        EXPECT_LT(bottom, top * 0.1f);
    }
}

// Regression test for FRO1156: the CV must land where the knob would, with Dual I/O either way, on Pan and shared Mix
// too.
TEST(OutputLevelCv, FullCvOnLevelPanAndMixLandsWhereTheKnobWould) {
    struct Move {
        const char* paramId;
        float cv;
        float knob;
    };
    for (const auto& testCase : cases()) {
        for (const auto& move : {Move{"outputLevel", -1.0f, 0.0f}, Move{"pan", 1.0f, 1.0f}, Move{"mix", -1.0f, 0.0f}}) {
            auto probe = testCase.make();
            if (findParameterByID(probe.get(), move.paramId) == nullptr)
                continue;
            for (const bool dual : {false, true}) {
                SCOPED_TRACE(juce::String(testCase.name) + " / " + move.paramId + (dual ? " / dual" : " / summed"));
                ASSERT_GE(probe->modulationChannelForParam(move.paramId), 0);
                const auto viaCv = settledBlock(testCase, move.paramId, move.cv, std::nullopt, dual);
                const auto viaKnob = settledBlock(testCase, move.paramId, std::nullopt, move.knob, dual);
                for (int ch = 0; ch < testCase.audioChannels; ++ch)
                    EXPECT_LT(differenceOn(viaCv, viaKnob, ch), 1.0e-3f) << "channel " << ch;
            }
        }
    }
}

// The whole point of appending the channels: with nothing on them the module must not know they exist.
TEST(OutputLevelCv, AModuleWithNothingOnTheNewChannelsRendersBitIdenticallyToItsOldWidth) {
    for (const auto& testCase : cases()) {
        SCOPED_TRACE(testCase.name);
        auto newWidth = testCase.make();
        auto oldWidth = testCase.make();
        prepare(*newWidth);
        prepare(*oldWidth);
        const int outputs = newWidth->getTotalNumOutputChannels();
        juce::AudioBuffer<float> wide(widthOf(*newWidth), kBlock);
        juce::AudioBuffer<float> narrow(juce::jmax(testCase.widthBeforeCables, outputs), kBlock);
        juce::MidiBuffer midi;
        Sine sineA, sineB;
        for (int b = 0; b < 40; ++b) {
            sineA.fill(wide, testCase.audioChannels);
            sineB.fill(narrow, testCase.audioChannels);
            newWidth->processBlock(wide, midi);
            oldWidth->processBlock(narrow, midi);
            for (int ch = 0; ch < testCase.audioChannels; ++ch)
                ASSERT_EQ(differenceOn(wide, narrow, ch), 0.0f) << "block " << b << " channel " << ch;
        }
    }
}

TEST(OutputLevelCv, ABypassedModulePassesItsInputWhateverTheCables) {
    for (const auto& testCase : cases())
        for (const juce::String paramId : {"outputLevel", "pan", "mix"}) {
            auto probe = testCase.make();
            if (findParameterByID(probe.get(), paramId) == nullptr)
                continue;
            SCOPED_TRACE(juce::String(testCase.name) + " / " + paramId);
            const auto cabled = settledBlock(testCase, paramId, -1.0f, std::nullopt, true, true);
            const auto clean = settledBlock(testCase, paramId, std::nullopt, std::nullopt, true, true);
            for (int ch = 0; ch < testCase.audioChannels; ++ch)
                EXPECT_EQ(differenceOn(cabled, clean, ch), 0.0f) << "channel " << ch;
        }
}

// The appended targets are knob-only: no new gutter row on any card, and every old target keeps its channel.
TEST(OutputLevelCv, TheNewTargetsAreAppendedLastAndHaveNoGutterJack) {
    for (const auto& testCase : cases()) {
        SCOPED_TRACE(testCase.name);
        auto module = testCase.make();
        for (const auto& target : module->getModulationTargets())
            if (target.channelIndex >= testCase.widthBeforeCables) {
                EXPECT_TRUE(target.knobOnly) << target.name;
                EXPECT_TRUE(target.paramId == "outputLevel" || target.paramId == "pan" || target.paramId == "mix")
                    << target.name;
            }
    }
}

TEST(OutputLevelCv, VoiceMixerLevelTakesACableAboveItsVoices) {
    VoiceMixerModule mixer;
    prepare(mixer);
    const int channel = mixer.modulationChannelForParam("level");
    ASSERT_EQ(channel, VoiceMixerModule::kLevelCvChannel);
    juce::AudioBuffer<float> buffer(widthOf(mixer), kBlock);
    juce::MidiBuffer midi;
    Sine sine;
    float withoutCv = 0.0f, withCv = 0.0f;
    for (int b = 0; b < 40; ++b) {
        sine.fill(buffer, 1);
        mixer.processBlock(buffer, midi);
        withoutCv = buffer.getMagnitude(0, 0, kBlock);
    }
    for (int b = 0; b < 40; ++b) {
        sine.fill(buffer, 1);
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), -1.0f, kBlock);
        mixer.processBlock(buffer, midi);
        withCv = buffer.getMagnitude(0, 0, kBlock);
    }
    EXPECT_GT(withoutCv, 0.1f);
    EXPECT_LT(withCv, 1.0e-3f) << "the CV must not be summed in as a voice either";
    EXPECT_EQ(mixer.getInputPortLabel(channel), "Level");
    EXPECT_EQ(mixer.mapInputChannel(channel).role, PortRole::ModCV);
}

TEST(OutputLevelCv, FixedRateTakesACableInBothVoiceModes) {
    for (const bool poly : {false, true}) {
        SCOPED_TRACE(poly ? "poly" : "mono");
        OscillatorModule osc;
        setActual(osc, "rateMode", 1.0f);
        setActual(osc, "fixedRate", 100.0f);
        if (auto* param = findParameterByID(&osc, "poly"))
            param->setValueNotifyingHost(poly ? 1.0f : 0.0f);
        prepare(osc);
        const int channel = osc.modulationChannelForParam("fixedRate");
        ASSERT_EQ(channel, OscillatorModule::kFixedRateCVChannel);
        juce::AudioBuffer<float> buffer(widthOf(osc), kBlock);
        juce::MidiBuffer midi;
        const auto crossingsWith = [&](float cv) {
            int crossings = 0;
            float last = 0.0f;
            for (int b = 0; b < 40; ++b) {
                buffer.clear();
                juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), cv, kBlock);
                midi.clear();
                if (b == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
                osc.processBlock(buffer, midi);
                if (b < 10)
                    continue;
                for (int i = 0; i < kBlock; ++i) {
                    const float sample = buffer.getSample(0, i);
                    crossings += (last <= 0.0f && sample > 0.0f) ? 1 : 0;
                    last = sample;
                }
            }
            return crossings;
        };
        const int unpatched = crossingsWith(0.0f);
        const int raised = crossingsWith(0.5f);
        EXPECT_GT(unpatched, 0);
        EXPECT_GT(raised, unpatched + 5);
    }
}
