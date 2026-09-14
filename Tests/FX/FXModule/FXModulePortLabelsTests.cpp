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

TEST(PortLabelTests, DelayPortLabelsDefaultToSingleAudioJack) {
    DelayModule delay;
    EXPECT_FALSE(delay.isDualIO());
    EXPECT_EQ(delay.getVisibleInputPortCount(), 1);
    EXPECT_EQ(delay.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(delay.getInputPortLabel(0), "Audio");
    EXPECT_EQ(delay.getOutputPortLabel(0), "Audio");

    setDualIO(delay, true);
    EXPECT_EQ(delay.getVisibleInputPortCount(), 2);
    EXPECT_EQ(delay.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(delay.getInputPortLabel(0), "Left");
    EXPECT_EQ(delay.getInputPortLabel(1), "Right");
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
    EXPECT_EQ(reverb.getInputPortLabel(0), "Audio");
    EXPECT_EQ(reverb.getOutputPortLabel(0), "Audio");
    setDualIO(reverb, true);
    EXPECT_EQ(reverb.getInputPortLabel(0), "Left");
    EXPECT_EQ(reverb.getInputPortLabel(1), "Right");
    EXPECT_EQ(reverb.getOutputPortLabel(0), "Left");
    EXPECT_EQ(reverb.getOutputPortLabel(1), "Right");
}

TEST(PortLabelTests, ChorusPortLabels) {
    ChorusModule chorus;
    EXPECT_EQ(chorus.getInputPortLabel(0), "Audio");
    EXPECT_EQ(chorus.getInputPortLabel(1), "Rate");
    EXPECT_EQ(chorus.getInputPortLabel(2), "Depth");
    setDualIO(chorus, true);
    EXPECT_EQ(chorus.getInputPortLabel(0), "Left");
    EXPECT_EQ(chorus.getInputPortLabel(1), "Right");
    EXPECT_EQ(chorus.getInputPortLabel(2), "Rate");
    EXPECT_EQ(chorus.getInputPortLabel(3), "Depth");
}

TEST(PortLabelTests, PhaserPortLabels) {
    PhaserModule phaser;
    EXPECT_EQ(phaser.getInputPortLabel(0), "Audio");
    setDualIO(phaser, true);
    EXPECT_EQ(phaser.getInputPortLabel(0), "Left");
    EXPECT_EQ(phaser.getInputPortLabel(1), "Right");
    EXPECT_EQ(phaser.getInputPortLabel(2), "Rate");
    EXPECT_EQ(phaser.getInputPortLabel(3), "Depth");
}

TEST(PortLabelTests, CompressorPortLabels) {
    CompressorModule comp;
    EXPECT_EQ(comp.getInputPortLabel(0), "Audio");
    setDualIO(comp, true);
    EXPECT_EQ(comp.getInputPortLabel(0), "Left");
    EXPECT_EQ(comp.getInputPortLabel(1), "Right");
}

TEST(PortLabelTests, FlangerPortLabels) {
    FlangerModule flanger;
    EXPECT_EQ(flanger.getInputPortLabel(0), "Audio");
    setDualIO(flanger, true);
    EXPECT_EQ(flanger.getInputPortLabel(0), "Left");
    EXPECT_EQ(flanger.getInputPortLabel(1), "Right");
    EXPECT_EQ(flanger.getInputPortLabel(2), "Rate");
    EXPECT_EQ(flanger.getInputPortLabel(3), "Depth");
}

TEST(PortLabelTests, LimiterPortLabels) {
    LimiterModule limiter;
    EXPECT_EQ(limiter.getInputPortLabel(0), "Audio");
    setDualIO(limiter, true);
    EXPECT_EQ(limiter.getInputPortLabel(0), "Left");
    EXPECT_EQ(limiter.getInputPortLabel(1), "Right");
}

TEST(PortLabelTests, PitchShifterPortLabels) {
    PitchShifterModule shifter;
    EXPECT_EQ(shifter.getInputPortLabel(0), "Audio");
    EXPECT_EQ(shifter.getInputPortLabel(1), "Pitch");
    EXPECT_EQ(shifter.getInputPortLabel(2), "Shift");
    EXPECT_EQ(shifter.getInputPortLabel(3), "Mix");
    EXPECT_EQ(shifter.getInputPortLabel(4), "Feedback");
    setDualIO(shifter, true);
    EXPECT_EQ(shifter.getInputPortLabel(0), "Left");
    EXPECT_EQ(shifter.getInputPortLabel(1), "Right");
    EXPECT_EQ(shifter.getInputPortLabel(2), "Pitch");
    EXPECT_EQ(shifter.getInputPortLabel(3), "Shift");
    EXPECT_EQ(shifter.getInputPortLabel(4), "Mix");
    EXPECT_EQ(shifter.getInputPortLabel(5), "Feedback");
}
