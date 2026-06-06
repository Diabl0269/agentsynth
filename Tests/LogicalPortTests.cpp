#include "../Source/Modules/ADSRModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/ModuleBase.h"
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
