// Dual I/O jack-layout toggle tests for the split-block stereo voice modules (issue #219); see
// StereoVoiceModuleTestHelpers.h for the shared render/measurement helpers.

#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "StereoVoiceModuleTestHelpers.h"
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// The Dual I/O toggle on split-block modules
// ---------------------------------------------------------------------------

TEST(SplitBlockDualIO, EveryStereoCapableModuleHasTheToggle) {
    OscillatorModule osc;
    FilterModule filter;
    VCAModule vca;

    for (ModuleBase* m : {(ModuleBase*)&osc, (ModuleBase*)&filter, (ModuleBase*)&vca}) {
        EXPECT_TRUE(m->hasDualIOParameter()) << m->getName();
        EXPECT_TRUE(m->isDualIO()) << m->getName() << " should default to showing both legs";
        EXPECT_TRUE(m->hasSplitBlockStereo()) << m->getName() << " keeps its right leg off ch1";
        EXPECT_GT(m->rightAudioLegChannel(), 1) << m->getName();
    }

    EXPECT_EQ(osc.rightAudioLegChannel(), OscillatorModule::kRightBase);
    EXPECT_EQ(filter.rightAudioLegChannel(), FilterModule::kRightBase);
    EXPECT_EQ(vca.rightAudioLegChannel(), VCAModule::kRightBase);
}

TEST(SplitBlockDualIO, CollapsingRestoresThePreStereoJackLayout) {
    // Dual I/O off must look exactly like the module did before #219 — that is the whole point of
    // calling it a jack-layout toggle rather than a mono/stereo switch.
    FilterModule filter;
    setBoolParam(filter, "dualIO", false);
    EXPECT_EQ(filter.getVisibleInputPortCount(), 4);
    EXPECT_EQ(filter.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(filter.getInputPortLabel(0), "Audio");
    EXPECT_EQ(filter.getInputPortLabel(1), "Cutoff");
    EXPECT_EQ(filter.getInputPortLabel(2), "Resonance");
    EXPECT_EQ(filter.getInputPortLabel(3), "Drive");
    EXPECT_EQ(filter.getOutputPortLabel(0), "Audio");

    VCAModule vca;
    setBoolParam(vca, "dualIO", false);
    EXPECT_EQ(vca.getVisibleInputPortCount(), 2);
    EXPECT_EQ(vca.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(vca.getInputPortLabel(0), "Audio");
    EXPECT_EQ(vca.getInputPortLabel(1), "CV");

    OscillatorModule osc;
    setBoolParam(osc, "dualIO", false);
    EXPECT_EQ(osc.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(osc.getOutputPortLabel(0), "Audio");
    EXPECT_EQ(osc.getVisibleInputPortCount(), OscillatorModule::kNumJacks) << "CV jacks are unaffected";
}

TEST(SplitBlockDualIO, CollapsedRightBlockIsNotAddressableAsAJack) {
    // The right block keeps rendering, but nothing may anchor a cable to it while it is hidden —
    // otherwise a wire exists that the user can hear but never reach.
    OscillatorModule osc;
    setBoolParam(osc, "dualIO", false);
    EXPECT_FALSE(osc.mapOutputChannel(OscillatorModule::kRightBase).isPolyGroupHead);

    FilterModule filter;
    setBoolParam(filter, "dualIO", false);
    EXPECT_FALSE(filter.mapInputChannel(FilterModule::kRightBase).isPolyGroupHead);
    EXPECT_FALSE(filter.mapOutputChannel(FilterModule::kRightBase).isPolyGroupHead);

    // Every visible jack still resolves to exactly one raw head in both states.
    for (bool dual : {true, false}) {
        FilterModule f;
        setBoolParam(f, "dualIO", dual);
        for (int jack = 0; jack < f.getVisibleInputPortCount(); ++jack)
            EXPECT_EQ(f.getJackTargets(jack, true).size(), 1u) << "dual=" << dual << " jack " << jack;
    }
}

TEST(SplitBlockDualIO, CollapsingDoesNotDisturbTheCVChannelMap) {
    // The CV jacks shift visible slot as the audio jacks appear and disappear, but their RAW
    // channels — the thing saved patches key off — must not move.
    for (bool dual : {true, false}) {
        FilterModule filter;
        setBoolParam(filter, "dualIO", dual);
        auto targets = filter.getModulationTargets();
        ASSERT_EQ(targets.size(), 3u);
        EXPECT_EQ(targets[0].channelIndex, 1) << "Cutoff, dual=" << dual;
        EXPECT_EQ(targets[1].channelIndex, 2);
        EXPECT_EQ(targets[2].channelIndex, 3);

        VCAModule vca;
        setBoolParam(vca, "dualIO", dual);
        EXPECT_EQ(vca.getModulationTargets()[0].channelIndex, 1) << "VCA gain CV, dual=" << dual;

        OscillatorModule osc;
        setBoolParam(osc, "dualIO", dual);
        auto oscTargets = osc.getModulationTargets();
        ASSERT_EQ(oscTargets.size(), 9u); // FRO314 appended Unison/Detune
        EXPECT_EQ(oscTargets[1].channelIndex, 1) << "Waveform CV, dual=" << dual;
        EXPECT_EQ(oscTargets[6].channelIndex, 6) << "Pan CV, dual=" << dual;
        EXPECT_EQ(oscTargets[7].channelIndex, 14) << "Unison CV, dual=" << dual;
        EXPECT_EQ(oscTargets[8].channelIndex, 15) << "Detune CV, dual=" << dual;
    }
}

TEST(SplitBlockDualIO, FlippingTheParameterFlipsTheVisibleJacks) {
    // The header button and the Preferences default both work by setting this one parameter, so
    // this is what "pushing the toggle changes the module" reduces to.
    FilterModule filter;
    auto* dual = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&filter, "dualIO"));
    ASSERT_NE(dual, nullptr);

    ASSERT_TRUE(filter.isDualIO());
    ASSERT_EQ(filter.getVisibleInputPortCount(), 5);

    dual->setValueNotifyingHost(0.0f);
    EXPECT_FALSE(filter.isDualIO());
    EXPECT_EQ(filter.getVisibleInputPortCount(), 4) << "collapsing must actually drop a jack";
    EXPECT_EQ(filter.getVisibleOutputPortCount(), 1);

    dual->setValueNotifyingHost(1.0f);
    EXPECT_TRUE(filter.isDualIO());
    EXPECT_EQ(filter.getVisibleInputPortCount(), 5) << "and re-expanding must bring it back";
    EXPECT_EQ(filter.getVisibleOutputPortCount(), 2);
}

TEST(SplitBlockDualIO, CollapsingDoesNotChangeWhatTheModuleRenders) {
    // Collapsing hides a jack; it must not alter the DSP. Audio L is byte-identical either way.
    auto renderWith = [](bool dual) {
        OscillatorModule osc;
        setBoolParam(osc, "dualIO", dual);
        osc.prepareToPlay(kSampleRate, kBlockSize);
        return renderOscillator(osc);
    };

    const auto dualOut = renderWith(true);
    const auto singleOut = renderWith(false);
    ASSERT_GT(rmsOf(dualOut, 0), 0.05f);
    EXPECT_LT(maxAbsDiff(dualOut, 0, singleOut, 0), 1.0e-7f);
}
