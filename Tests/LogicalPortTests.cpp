#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/NoiseModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/PolyMidiModule.h"
#include "../Source/Modules/VCAModule.h"
#include <gtest/gtest.h>

TEST(LogicalPortTests, DefaultMappingClampsPhantomChannels) {
    VCAModule vca;

    // VCAModule has getVisibleInputPortCount() == 2
    ASSERT_EQ(vca.getVisibleInputPortCount(), 2);

    // rawChannel 8 >= vis(2) in mono mode: falls through to base, clamped to vis-1 == 1, isPolyGroupHead == false
    auto port8 = vca.mapInputChannel(8);
    EXPECT_EQ(port8.visibleJackIndex, 1);
    EXPECT_EQ(port8.role, PortRole::Other);
    EXPECT_EQ(port8.polyVoiceSpan, 1);
    EXPECT_FALSE(port8.isPolyGroupHead);

    // rawChannel 0 < vis(2), so visibleJackIndex == 0, isPolyGroupHead == true
    auto port0 = vca.mapInputChannel(0);
    EXPECT_EQ(port0.visibleJackIndex, 0);
    EXPECT_TRUE(port0.isPolyGroupHead);

    // VCA target is {"CV", 1}, so channel 1 is auto-promotable, channel 8 is not
    EXPECT_TRUE(vca.isAutoPromotableModTarget(1));
    EXPECT_FALSE(vca.isAutoPromotableModTarget(8));
}

// Helper to find and set a bool parameter by ID
static void setPolyParam(juce::AudioProcessor& proc, bool value) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "poly") {
                p->setValueNotifyingHost(value ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

TEST(LogicalPortTests, PolyModulesDescribeTheirFans) {
    // ---- VCA (poly) ----
    VCAModule vca;
    setPolyParam(vca, true);

    // Raw 0: Audio jack0, head of 8-voice fan
    auto vca_in0 = vca.mapInputChannel(0);
    EXPECT_EQ(vca_in0.visibleJackIndex, 0);
    EXPECT_EQ(vca_in0.role, PortRole::Audio);
    EXPECT_TRUE(vca_in0.isPolyGroupHead);
    EXPECT_EQ(vca_in0.polyVoiceSpan, 8);

    // Raw 8: ModCV jack1, head of 8-voice fan
    auto vca_in8 = vca.mapInputChannel(8);
    EXPECT_EQ(vca_in8.visibleJackIndex, 1);
    EXPECT_EQ(vca_in8.role, PortRole::ModCV);
    EXPECT_TRUE(vca_in8.isPolyGroupHead);
    EXPECT_EQ(vca_in8.polyVoiceSpan, 8);

    // Raw 9: ModCV jack1, NOT head (not a group head)
    auto vca_in9 = vca.mapInputChannel(9);
    EXPECT_FALSE(vca_in9.isPolyGroupHead);

    // ---- ADSR (poly) ----
    ADSRModule adsr;
    setPolyParam(adsr, true);

    // Output raw 0: ModCV, head of 8-voice fan
    auto adsr_out0 = adsr.mapOutputChannel(0);
    EXPECT_EQ(adsr_out0.visibleJackIndex, 0);
    EXPECT_EQ(adsr_out0.role, PortRole::ModCV);
    EXPECT_TRUE(adsr_out0.isPolyGroupHead);
    EXPECT_EQ(adsr_out0.polyVoiceSpan, 8);

    // Output raw 3: ModCV, NOT head
    auto adsr_out3 = adsr.mapOutputChannel(3);
    EXPECT_FALSE(adsr_out3.isPolyGroupHead);

    // ---- Oscillator (poly) ----
    OscillatorModule osc;
    setPolyParam(osc, true);

    // Input raw 0: Pitch, head of 8-voice fan
    auto osc_in0 = osc.mapInputChannel(0);
    EXPECT_EQ(osc_in0.role, PortRole::Pitch);
    EXPECT_TRUE(osc_in0.isPolyGroupHead);
    EXPECT_EQ(osc_in0.polyVoiceSpan, 8);

    // Input raw 12: ModCV at jack 5 (Level), head span 1
    auto osc_in12 = osc.mapInputChannel(12);
    EXPECT_EQ(osc_in12.visibleJackIndex, 5);
    EXPECT_EQ(osc_in12.role, PortRole::ModCV);
    EXPECT_TRUE(osc_in12.isPolyGroupHead);
    EXPECT_EQ(osc_in12.polyVoiceSpan, 1);

    // ---- PolyMidi (always poly) ----
    PolyMidiModule polyMidi;

    // Output raw 0: Pitch, head of 8-voice fan
    auto pm_out0 = polyMidi.mapOutputChannel(0);
    EXPECT_EQ(pm_out0.role, PortRole::Pitch);
    EXPECT_TRUE(pm_out0.isPolyGroupHead);
    EXPECT_EQ(pm_out0.polyVoiceSpan, 8);

    // Output raw 8: Gate, head of 8-voice fan
    auto pm_out8 = polyMidi.mapOutputChannel(8);
    EXPECT_EQ(pm_out8.role, PortRole::Gate);
    EXPECT_TRUE(pm_out8.isPolyGroupHead);
    EXPECT_EQ(pm_out8.polyVoiceSpan, 8);

    // ---- Auto-promotion guard ----
    // Poly osc: isAutoPromotableModTarget returns false
    EXPECT_FALSE(osc.isAutoPromotableModTarget(12));
    // Poly VCA: isAutoPromotableModTarget returns false for any channel
    EXPECT_FALSE(vca.isAutoPromotableModTarget(1));

    // Mono osc: channel 0 (Pitch) is in getModulationTargets(), so it should be auto-promotable
    OscillatorModule monoOsc;
    // polyParam defaults to false
    EXPECT_TRUE(monoOsc.isAutoPromotableModTarget(0));
}

// getJackTargets is the inverse of mapInput/OutputChannel: it resolves a *visible* jack index
// back to the raw channel(s) a wire anchored there should wire. This is the crux of issue #163 —
// the editor used to wire raw channel == visible jack index, which is wrong once a module goes
// poly (a poly VCA's CV jack is visible jack 1, but raw channel 8).
TEST(LogicalPortTests, JackTargetsResolveVisibleJacksToRawHeads) {
    VCAModule vca;
    setPolyParam(vca, true);

    // Poly VCA: visible jack 0 (Audio) fronts raw head 0, an 8-voice fan.
    auto audioTargets = vca.getJackTargets(0, true);
    ASSERT_EQ(audioTargets.size(), 1u);
    EXPECT_EQ(audioTargets[0].rawHeadChannel, 0);
    EXPECT_EQ(audioTargets[0].role, PortRole::Audio);
    EXPECT_EQ(audioTargets[0].voiceSpan, 8);

    // Poly VCA: visible jack 1 (CV) fronts raw head 8, NOT raw channel 1.
    auto cvTargets = vca.getJackTargets(1, true);
    ASSERT_EQ(cvTargets.size(), 1u);
    EXPECT_EQ(cvTargets[0].rawHeadChannel, 8);
    EXPECT_EQ(cvTargets[0].role, PortRole::ModCV);
    EXPECT_EQ(cvTargets[0].voiceSpan, 8);

    // Mono VCA: visible jack 1 (CV) fronts raw channel 1, a mono (span-1) wire.
    VCAModule monoVca;
    auto monoCvTargets = monoVca.getJackTargets(1, true);
    ASSERT_EQ(monoCvTargets.size(), 1u);
    EXPECT_EQ(monoCvTargets[0].rawHeadChannel, 1);
    EXPECT_EQ(monoCvTargets[0].role, PortRole::ModCV);
    EXPECT_EQ(monoCvTargets[0].voiceSpan, 1);
}

// Poly MIDI's single "Poly Out" jack fronts two independent fans (Pitch at raw 0, Gate at raw 8);
// getJackTargets must surface both so the caller can disambiguate by role.
TEST(LogicalPortTests, PolyMidiOutJackFrontsBothPitchAndGateFans) {
    PolyMidiModule polyMidi;

    auto targets = polyMidi.getJackTargets(0, false);
    ASSERT_EQ(targets.size(), 2u);

    bool foundPitch = false;
    bool foundGate = false;
    for (const auto& t : targets) {
        if (t.role == PortRole::Pitch) {
            foundPitch = true;
            EXPECT_EQ(t.rawHeadChannel, 0);
            EXPECT_EQ(t.voiceSpan, 8);
        } else if (t.role == PortRole::Gate) {
            foundGate = true;
            EXPECT_EQ(t.rawHeadChannel, 8);
            EXPECT_EQ(t.voiceSpan, 8);
        }
    }
    EXPECT_TRUE(foundPitch);
    EXPECT_TRUE(foundGate);
}

// Oscillator, Filter and Noise all declare their per-voice audio output fan the same way: raw 0 is
// the poly-group head of an 8-voice fan on the single visible Audio jack; raw 1 is not a head; and
// in mono mode the span collapses back to 1.
TEST(LogicalPortTests, PolyAudioOutputsDeclareTheirVoiceFan) {
    // ---- Oscillator ----
    OscillatorModule osc;
    setPolyParam(osc, true);

    auto oscOut0 = osc.mapOutputChannel(0);
    EXPECT_EQ(oscOut0.visibleJackIndex, 0);
    EXPECT_EQ(oscOut0.role, PortRole::Audio);
    EXPECT_TRUE(oscOut0.isPolyGroupHead);
    EXPECT_EQ(oscOut0.polyVoiceSpan, 8);

    auto oscOut1 = osc.mapOutputChannel(1);
    EXPECT_FALSE(oscOut1.isPolyGroupHead);

    OscillatorModule monoOsc;
    EXPECT_EQ(monoOsc.mapOutputChannel(0).polyVoiceSpan, 1);

    // ---- Filter ----
    FilterModule filter;
    setPolyParam(filter, true);

    auto filterOut0 = filter.mapOutputChannel(0);
    EXPECT_EQ(filterOut0.visibleJackIndex, 0);
    EXPECT_EQ(filterOut0.role, PortRole::Audio);
    EXPECT_TRUE(filterOut0.isPolyGroupHead);
    EXPECT_EQ(filterOut0.polyVoiceSpan, 8);

    auto filterOut1 = filter.mapOutputChannel(1);
    EXPECT_FALSE(filterOut1.isPolyGroupHead);

    FilterModule monoFilter;
    EXPECT_EQ(monoFilter.mapOutputChannel(0).polyVoiceSpan, 1);

    // ---- Noise ----
    NoiseModule noise;
    setPolyParam(noise, true);

    auto noiseOut0 = noise.mapOutputChannel(0);
    EXPECT_EQ(noiseOut0.visibleJackIndex, 0);
    EXPECT_EQ(noiseOut0.role, PortRole::Audio);
    EXPECT_TRUE(noiseOut0.isPolyGroupHead);
    EXPECT_EQ(noiseOut0.polyVoiceSpan, 8);

    auto noiseOut1 = noise.mapOutputChannel(1);
    EXPECT_FALSE(noiseOut1.isPolyGroupHead);

    NoiseModule monoNoise;
    EXPECT_EQ(monoNoise.mapOutputChannel(0).polyVoiceSpan, 1);
}

// Regression guard: unlike Oscillator/Filter/Noise, VCA deliberately does NOT override
// mapOutputChannel. Its poly output is 8 voices summed to a stereo pair on raw ch0/1, not a fan —
// so its output must never be reported as a poly-voice-spanning fan.
TEST(LogicalPortTests, VCAPolyOutputIsSummedNotFanned) {
    VCAModule vca;
    setPolyParam(vca, true);

    EXPECT_EQ(vca.mapOutputChannel(0).polyVoiceSpan, 1);
}

#include "../Source/Modules/FX/DelayModule.h"
#include "../Source/Modules/FX/DistortionModule.h"

static void setDualIOParam(juce::AudioProcessor& proc, bool dual) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "dualIO") {
                p->setValueNotifyingHost(dual ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

TEST(LogicalPortTests, CollapsedStereoPairFansBothRawChannelsOntoOneJack) {
    DelayModule delay;
    ASSERT_FALSE(delay.isDualIO());

    auto in0 = delay.mapInputChannel(0);
    EXPECT_EQ(in0.visibleJackIndex, 0);
    EXPECT_EQ(in0.role, PortRole::Audio);
    EXPECT_TRUE(in0.isPolyGroupHead);
    EXPECT_EQ(in0.polyVoiceSpan, 2);

    auto in1 = delay.mapInputChannel(1);
    EXPECT_EQ(in1.visibleJackIndex, 0);
    EXPECT_EQ(in1.role, PortRole::Audio);
    EXPECT_FALSE(in1.isPolyGroupHead);

    const auto targets = delay.getJackTargets(0, true);
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0].rawHeadChannel, 0);
    EXPECT_EQ(targets[0].voiceSpan, 2);
    EXPECT_EQ(targets[0].role, PortRole::Audio);
}

TEST(LogicalPortTests, DualIOExposesSeparateLeftRightJacks) {
    DelayModule delay;
    setDualIOParam(delay, true);

    EXPECT_EQ(delay.getVisibleInputPortCount(), 2);
    auto left = delay.mapInputChannel(0);
    auto right = delay.mapInputChannel(1);
    EXPECT_EQ(left.visibleJackIndex, 0);
    EXPECT_EQ(right.visibleJackIndex, 1);
    EXPECT_EQ(left.polyVoiceSpan, 1);
    EXPECT_EQ(right.polyVoiceSpan, 1);
    EXPECT_TRUE(left.isPolyGroupHead);
    EXPECT_TRUE(right.isPolyGroupHead);
}

TEST(LogicalPortTests, DistortionCvJacksShiftWhenStereoCollapses) {
    DistortionModule dist;
    // Single: Audio @0, Drive @1, Mix @2 — raw Drive stays ch2
    auto drive = dist.mapInputChannel(2);
    EXPECT_EQ(drive.visibleJackIndex, 1);
    EXPECT_EQ(drive.role, PortRole::ModCV);

    setDualIOParam(dist, true);
    drive = dist.mapInputChannel(2);
    EXPECT_EQ(drive.visibleJackIndex, 2);
}
