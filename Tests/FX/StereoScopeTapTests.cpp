// StereoScopeTapTests.cpp
//
// A stereo module's scope and analyzer show the mid of both legs. With Dual I/O split and a signal on one leg only
// (what a hard balance pan leaves), the view must still draw: a left-only tap goes blank when the sound is on the
// right. Bug kind: leg-asymmetry.

#include "Modules/FX/BitcrusherModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FX/PitchShifterModule.h"
#include "Modules/FX/RingModulatorModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/VCAModule.h"
#include "Modules/VisualBuffer.h"
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kBlocks = 24;

void setPlain(juce::AudioProcessor& proc, const juce::String& id, float plain) {
    for (auto* p : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id) {
            r->setValueNotifyingHost(r->convertTo0to1(plain));
            return;
        }
    FAIL() << "no parameter " << id;
}

// Runs a 440 Hz sine on `feedChannels` (all other channels silent) and returns the scope's peak.
float scopePeak(ModuleBase& module, const std::vector<int>& feedChannels, int unityChannel = -1) {
    module.prepareToPlay(kSampleRate, kBlock);
    const int channels = std::max(module.getTotalNumInputChannels(), module.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer(channels, kBlock);
    juce::MidiBuffer midi;
    double phase = 0.0;
    for (int b = 0; b < kBlocks; ++b) {
        buffer.clear();
        for (int i = 0; i < kBlock; ++i, phase += 440.0 / kSampleRate)
            for (int ch : feedChannels)
                buffer.setSample(ch, i, 0.8f * (float)std::sin(juce::MathConstants<double>::twoPi * phase));
        if (unityChannel >= 0)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(unityChannel), 1.0f, kBlock);
        module.processBlock(buffer, midi);
    }
    float peak = 0.0f;
    std::vector<float> view((size_t)module.getVisualBuffer()->getSize());
    module.getVisualBuffer()->copyTo(view);
    for (float s : view)
        peak = std::max(peak, std::abs(s));
    return peak;
}

struct Case {
    const char* name;
    std::function<std::unique_ptr<ModuleBase>()> make;
    int gainCvChannel = -1; // a VCA's gain is its CV: held at unity
};

std::vector<Case> cases() {
    return {{"ParametricEQ", [] { return std::make_unique<ParametricEQModule>(); }},
            {"Distortion", [] { return std::make_unique<DistortionModule>(); }},
            {"Bitcrusher", [] { return std::make_unique<BitcrusherModule>(); }},
            {"PitchShifter", [] { return std::make_unique<PitchShifterModule>(); }},
            {"Filter", [] { return std::make_unique<FilterModule>(); }},
            {"VCA", [] { return std::make_unique<VCAModule>(); }, 1}};
}
} // namespace

// Regression test for AGE281: the scope tapped the left leg only, so a signal on the right drew nothing.
TEST(StereoScopeTap, SplitModuleScopeShowsASignalOnEitherLegAlone) {
    for (const auto& c : cases()) {
        auto module = c.make();
        setPlain(*module, "dualIO", 1.0f);
        const int right = module->rightAudioLegChannel();
        ASSERT_GT(right, 0) << c.name;

        EXPECT_GT(scopePeak(*module, {right}, c.gainCvChannel), 0.1f) << c.name << ": signal on the right leg only";

        auto left = c.make();
        setPlain(*left, "dualIO", 1.0f);
        EXPECT_GT(scopePeak(*left, {0}, c.gainCvChannel), 0.1f) << c.name << ": signal on the left leg only";
    }
}

// Regression test for AGE281: the Ring Modulator's legs are one wet signal under a pan, so a hard pan empties one leg.
TEST(StereoScopeTap, RingModulatorScopeShowsAHardPannedOutput) {
    for (float pan : {-1.0f, 1.0f}) {
        RingModulatorModule module;
        setPlain(module, "dualIO", 1.0f);
        setPlain(module, "pan", pan);
        EXPECT_GT(scopePeak(module, {0, 1}), 0.05f) << "pan " << pan;
    }
}

// Regression test for AGE281: a stereo Macro Out port shows both legs.
TEST(StereoScopeTap, StereoMacroOutletScopeShowsASignalOnTheRightLegAlone) {
    MacroOutletModule module;
    module.setPortShape(MacroPortShape::Stereo);
    EXPECT_GT(scopePeak(module, {MacroOutletModule::kRightBase}), 0.1f);
    MacroOutletModule left;
    left.setPortShape(MacroPortShape::Stereo);
    EXPECT_GT(scopePeak(left, {0}), 0.1f);
}
