// FXModulePortLabelsTests.cpp — cross-module port-label conformance
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/FX/LimiterModule.h"
#include "Modules/FX/PhaserModule.h"
#include "Modules/FX/PitchShifterModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// ---------------------------------------------------------------------------
// Port Label tests
// ---------------------------------------------------------------------------

TEST(PortLabelTests, OscillatorPortLabels) {
    OscillatorModule osc;
    // Default mono mode: original channel layout
    EXPECT_EQ(osc.getInputPortLabel(0), "Pitch");
    EXPECT_EQ(osc.getInputPortLabel(1), "Waveform");
    EXPECT_EQ(osc.getInputPortLabel(2), "Octave");
    EXPECT_EQ(osc.getInputPortLabel(3), "Coarse");
    EXPECT_EQ(osc.getInputPortLabel(4), "Fine");
    EXPECT_EQ(osc.getInputPortLabel(5), "Level");
    EXPECT_EQ(osc.getInputPortLabel(6), "Pan");
    EXPECT_EQ(osc.getOutputPortLabel(0), "Audio L");
    EXPECT_EQ(osc.getOutputPortLabel(1), "Audio R");
}

TEST(PortLabelTests, FilterPortLabels) {
    FilterModule filter;
    // Audio L/R lead the visible jacks; the CV labels keep their order behind them.
    EXPECT_EQ(filter.getInputPortLabel(0), "Audio L");
    EXPECT_EQ(filter.getInputPortLabel(1), "Audio R");
    EXPECT_EQ(filter.getInputPortLabel(2), "Cutoff");
    EXPECT_EQ(filter.getInputPortLabel(3), "Resonance");
    EXPECT_EQ(filter.getInputPortLabel(4), "Drive");
    EXPECT_EQ(filter.getOutputPortLabel(0), "Audio L");
    EXPECT_EQ(filter.getOutputPortLabel(1), "Audio R");
}

TEST(PortLabelTests, VCAPortLabels) {
    VCAModule vca;
    // Audio L/R lead the visible jacks since #219; CV keeps its raw channel and only moves slot.
    EXPECT_EQ(vca.getInputPortLabel(0), "Audio L");
    EXPECT_EQ(vca.getInputPortLabel(1), "Audio R");
    EXPECT_EQ(vca.getInputPortLabel(2), "CV");
    EXPECT_EQ(vca.getOutputPortLabel(0), "Audio L");
    EXPECT_EQ(vca.getOutputPortLabel(1), "Audio R");
}

TEST(PortLabelTests, ADSRPortLabels) {
    ADSRModule adsr;
    EXPECT_EQ(adsr.getInputPortLabel(0), "Gate");
    EXPECT_EQ(adsr.getOutputPortLabel(0), "Env");
}

TEST(PortLabelTests, LFOPortLabels) {
    LFOModule lfo;
    EXPECT_EQ(lfo.getOutputPortLabel(0), "CV");
}

TEST(PortLabelTests, AttenuverterPortLabels) {
    AttenuverterModule att;
    EXPECT_EQ(att.getInputPortLabel(0), "Signal");
    EXPECT_EQ(att.getInputPortLabel(1), "Amount");
    EXPECT_EQ(att.getOutputPortLabel(0), "Out");
}

static void setDualIO(juce::AudioProcessor& proc, bool dual) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "dualIO") {
                p->setValueNotifyingHost(dual ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

// The FX modules whose every continuous parameter has a CV jack: the visible jacks are the
// collapsed Audio (or Left/Right) pair followed by one jack per parameter, in parameter order,
// and every jack is a modulation target on the matching raw channel (audio pair + jack index).
static void expectStereoCvJacks(ModuleBase& module, const std::vector<juce::String>& cvLabels) {
    ASSERT_EQ(module.getVisibleInputPortCount(), 1 + (int)cvLabels.size());
    EXPECT_EQ(module.getInputPortLabel(0), "Audio");
    for (size_t i = 0; i < cvLabels.size(); ++i)
        EXPECT_EQ(module.getInputPortLabel(1 + (int)i), cvLabels[i]);

    const auto targets = module.getModulationTargets();
    ASSERT_EQ(targets.size(), cvLabels.size());
    for (size_t i = 0; i < cvLabels.size(); ++i) {
        EXPECT_EQ(targets[i].name, cvLabels[i]);
        EXPECT_EQ(targets[i].channelIndex, 2 + (int)i);
        EXPECT_NE(module.parameterForModTarget(targets[i]), nullptr) << cvLabels[i] << " binds to no knob";
    }
    EXPECT_EQ(module.getTotalNumInputChannels(), 2 + (int)cvLabels.size());

    setDualIO(module, true);
    ASSERT_EQ(module.getVisibleInputPortCount(), 2 + (int)cvLabels.size());
    EXPECT_EQ(module.getInputPortLabel(0), "Left");
    EXPECT_EQ(module.getInputPortLabel(1), "Right");
    for (size_t i = 0; i < cvLabels.size(); ++i)
        EXPECT_EQ(module.getInputPortLabel(2 + (int)i), cvLabels[i]);
}

TEST(PortLabelTests, DelayPortLabelsDefaultToSingleAudioJack) {
    DelayModule delay;
    EXPECT_FALSE(delay.isDualIO());
    EXPECT_EQ(delay.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(delay.getOutputPortLabel(0), "Audio");
    // Time/Feedback/Mix were declared as targets on ch2-4 while the module declared only two
    // inputs, so those jacks never existed; expectStereoCvJacks pins that they do now.
    expectStereoCvJacks(delay, {"Time", "Feedback", "Mix"});
    EXPECT_EQ(delay.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(delay.getOutputPortLabel(0), "Left");
    EXPECT_EQ(delay.getOutputPortLabel(1), "Right");
}

TEST(PortLabelTests, DistortionPortLabelsCollapseCvJacksInSingleMode) {
    DistortionModule dist;
    EXPECT_EQ(dist.getVisibleInputPortCount(), 3);
    EXPECT_EQ(dist.getInputPortLabel(0), "Audio");
    EXPECT_EQ(dist.getInputPortLabel(1), "Drive");
    EXPECT_EQ(dist.getInputPortLabel(2), "Mix");
    EXPECT_EQ(dist.getOutputPortLabel(0), "Audio");

    setDualIO(dist, true);
    EXPECT_EQ(dist.getVisibleInputPortCount(), 4);
    EXPECT_EQ(dist.getInputPortLabel(0), "Left");
    EXPECT_EQ(dist.getInputPortLabel(1), "Right");
    EXPECT_EQ(dist.getInputPortLabel(2), "Drive");
    EXPECT_EQ(dist.getInputPortLabel(3), "Mix");
    EXPECT_EQ(dist.getOutputPortLabel(0), "Left");
    EXPECT_EQ(dist.getOutputPortLabel(1), "Right");
}

TEST(PortLabelTests, ReverbPortLabels) {
    ReverbModule reverb;
    EXPECT_EQ(reverb.getOutputPortLabel(0), "Audio");
    // Same history as Delay: five targets on ch2-6 and only two declared inputs, until now.
    expectStereoCvJacks(reverb, {"Size", "Damping", "Wet", "Dry", "Width"});
    EXPECT_EQ(reverb.getOutputPortLabel(0), "Left");
    EXPECT_EQ(reverb.getOutputPortLabel(1), "Right");
}

TEST(PortLabelTests, ChorusPortLabels) {
    ChorusModule chorus;
    expectStereoCvJacks(chorus, {"Rate", "Depth", "Centre Delay", "Feedback", "Mix"});
}

TEST(PortLabelTests, PhaserPortLabels) {
    PhaserModule phaser;
    expectStereoCvJacks(phaser, {"Rate", "Depth", "Centre Freq", "Feedback", "Mix"});
}

TEST(PortLabelTests, CompressorPortLabels) {
    CompressorModule comp;
    expectStereoCvJacks(comp, {"Threshold", "Ratio", "Attack", "Release", "Makeup"});
}

TEST(PortLabelTests, FlangerPortLabels) {
    FlangerModule flanger;
    expectStereoCvJacks(flanger, {"Rate", "Depth", "Centre Delay", "Feedback", "Mix"});
}

TEST(PortLabelTests, LimiterPortLabels) {
    LimiterModule limiter;
    expectStereoCvJacks(limiter, {"Threshold", "Release", "Input Gain"});
}

TEST(PortLabelTests, PitchShifterPortLabels) {
    PitchShifterModule shifter;
    // Fine/Window came after the original four, so they sit LAST — saved patches keep ch2-5.
    expectStereoCvJacks(shifter, {"Pitch", "Shift", "Mix", "Feedback", "Fine", "Window"});
}
