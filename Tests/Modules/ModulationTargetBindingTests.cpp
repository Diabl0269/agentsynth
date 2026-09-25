// ModulationTargetBindingTests.cpp — every ModulationTarget a module declares binds to the knob it
// drives (ModuleBase::parameterForModTarget), swept over the whole factory, plus the two helpers
// the normalised CV convention rests on (blockCV / modulateNormalised). Before paramId existed the
// card matched a jack LABEL ("Rate") against a knob NAME ("Rate (Hz)") and silently found nothing
// on every module whose labels carry no unit: no modulation ring, no drag-to-knob drop. The sweep
// is what stops that coming back one module at a time.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include <string>

namespace {

// Jacks that deliberately drive no knob: a bare CV input with no parameter behind it. Everything
// else a module lists in getModulationTargets() must resolve to a RangedAudioParameter.
const std::set<std::string>& knoblessTargets() {
    static const std::set<std::string> knobless = {
        "Oscillator/Pitch", // ch0 pitch CV; the oscillator has no pitch knob
        "Wavetable/Pitch",  // same, mono mode only
        "Wavetable/Sync",   // audio-rate sync input; its mode lives on the Sync In combo
    };
    return knobless;
}

} // namespace

TEST(ModulationTargetBinding, EveryFactoryTargetBindsToAParameterOrIsKnownKnobless) {
    for (const auto& type : synth::AIStateMapper::authorableModuleTypes()) {
        auto probe = synth::AIStateMapper::createModule(type);
        auto* mb = dynamic_cast<ModuleBase*>(probe.get());
        if (mb == nullptr)
            continue;
        std::set<juce::String> seenChannels;
        for (const auto& t : mb->getModulationTargets()) {
            SCOPED_TRACE(type.toStdString() + "/" + t.name.toStdString());
            EXPECT_TRUE(seenChannels.insert(juce::String(t.channelIndex)).second)
                << "two targets claim raw channel " << t.channelIndex;
            const auto key = (type + "/" + t.name).toStdString();
            const auto* param = mb->parameterForModTarget(t);
            if (knoblessTargets().count(key) != 0) {
                EXPECT_EQ(param, nullptr) << "listed as knobless but now binds to " << param->paramID;
                continue;
            }
            ASSERT_NE(param, nullptr) << "no parameter binds to this target: set ModulationTarget::paramId "
                                         "(or add the jack to knoblessTargets() with a reason)";
            EXPECT_LT(t.channelIndex, mb->getTotalNumInputChannels())
                << "the target's channel is beyond the module's declared inputs";
            if (t.paramId.isNotEmpty())
                EXPECT_EQ(param->paramID, t.paramId);
        }
    }
}

TEST(ModulationTargetBinding, ParamIdWinsOverAMatchingDisplayName) {
    FlangerModule flanger;
    // "Rate" is the jack label; the knob is "Rate (Hz)" (paramID "rate"). Neither would have matched
    // by display name alone.
    const auto* rate = flanger.parameterForModTarget({"Rate", 2, "rate"});
    ASSERT_NE(rate, nullptr);
    EXPECT_EQ(rate->getName(100), "Rate (Hz)");

    // Without a paramId the lookup falls back to the display name — the pre-paramId behaviour that
    // every module with unit-free labels still relies on.
    const auto* depth = flanger.parameterForModTarget({"Depth", 3});
    ASSERT_NE(depth, nullptr);
    EXPECT_EQ(depth->paramID, "depth");
    EXPECT_EQ(flanger.parameterForModTarget({"Rate", 2}), nullptr) << "no knob is called plain \"Rate\"";
    EXPECT_EQ(flanger.parameterForModTarget({"Depth", 3, "noSuchParam"}), nullptr)
        << "a paramId that names nothing must not fall back to the label and bind the wrong knob";
}

TEST(ModulationTargetBinding, VCACVJackDrivesTheGainKnob) {
    VCAModule vca;
    const auto targets = vca.getModulationTargets();
    ASSERT_EQ(targets.size(), 1u);
    const auto* gain = vca.parameterForModTarget(targets[0]);
    ASSERT_NE(gain, nullptr);
    EXPECT_EQ(gain->paramID, "gain");
}

TEST(ModulationTargetBinding, BlockCVReadsTheFirstSampleAndSilenceOffTheEnd) {
    juce::AudioBuffer<float> buffer(3, 16);
    buffer.clear();
    buffer.setSample(2, 0, 0.25f);
    buffer.setSample(2, 8, 0.9f);
    EXPECT_FLOAT_EQ(ModuleBase::blockCV(buffer, 2), 0.25f);
    EXPECT_FLOAT_EQ(ModuleBase::blockCV(buffer, 0), 0.0f);
    EXPECT_FLOAT_EQ(ModuleBase::blockCV(buffer, 3), 0.0f) << "a channel the graph never handed over reads as silence";
    EXPECT_FLOAT_EQ(ModuleBase::blockCV(buffer, -1), 0.0f);
    juce::AudioBuffer<float> empty(3, 0);
    EXPECT_FLOAT_EQ(ModuleBase::blockCV(empty, 2), 0.0f);
}

TEST(ModulationTargetBinding, ModulateNormalisedSweepsTheParametersOwnRangeAndClamps) {
    juce::AudioParameterFloat threshold("threshold", "Threshold (dB)", -60.0f, 0.0f, -12.0f);
    // +1.0 from anywhere lands on the maximum, -1.0 on the minimum.
    EXPECT_FLOAT_EQ(ModuleBase::modulateNormalised(threshold, -12.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(ModuleBase::modulateNormalised(threshold, -12.0f, -1.0f), -60.0f);
    // +0.5 is half the range: -12 dB + 30 dB, clamped to the top.
    EXPECT_FLOAT_EQ(ModuleBase::modulateNormalised(threshold, -30.0f, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(ModuleBase::modulateNormalised(threshold, -40.0f, 0.5f), -10.0f);
    // Zero CV is a bit-exact no-op, even for a base outside the range (a smoothed value mid-ramp).
    EXPECT_FLOAT_EQ(ModuleBase::modulateNormalised(threshold, -12.0f, 0.0f), -12.0f);
    EXPECT_FLOAT_EQ(ModuleBase::modulateNormalised(threshold, -70.0f, 0.0f), -70.0f);

    // A skewed range moves in the knob's own (skewed) units, not linearly in the plain value.
    juce::AudioParameterFloat rate("rate", "Rate (Hz)", juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.5f),
                                   1.0f);
    const float halfway = rate.getNormalisableRange().convertFrom0to1(0.5f);
    EXPECT_NEAR(ModuleBase::modulateNormalised(rate, 0.01f, 0.5f), halfway, 1.0e-4f);
}
