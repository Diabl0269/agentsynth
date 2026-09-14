// Filter channel-map and DSP tests for the L/R stereo split on voice modules (issue #219); see
// StereoVoiceModuleTestHelpers.h for the shared render/measurement helpers.

#include "Modules/FilterModule.h"
#include "StereoVoiceModuleTestHelpers.h"
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Filter — channel map
// ---------------------------------------------------------------------------

TEST(FilterStereo, AudioRIsItsOwnBlockInAndOut) {
    EXPECT_EQ(FilterModule::kRightBase, 11);
    EXPECT_EQ(FilterModule::kNumChannels, 19);

    FilterModule filter;
    // A processor needs the right leg as an input as well as an output, unlike a pure source.
    EXPECT_EQ(filter.getTotalNumInputChannels(), FilterModule::kNumChannels);
    EXPECT_EQ(filter.getTotalNumOutputChannels(), FilterModule::kNumChannels);
    EXPECT_EQ(filter.getVisibleInputPortCount(), 5);
    EXPECT_EQ(filter.getVisibleOutputPortCount(), 2);
}

TEST(FilterStereo, CutoffCVKeepsChannelOneAndIsNeverReportedAsAudio) {
    FilterModule filter;

    const auto in1 = filter.mapInputChannel(1);
    EXPECT_EQ(in1.role, PortRole::ModCV) << "ch1 is Cutoff CV — Dual I/O's contiguous 0/1 pair is not viable here";

    const auto out1 = filter.mapOutputChannel(1);
    EXPECT_FALSE(out1.isPolyGroupHead);
    EXPECT_EQ(out1.visibleJackIndex, 0);

    // Audio R, both directions.
    const auto rIn = filter.mapInputChannel(FilterModule::kRightBase);
    EXPECT_EQ(rIn.role, PortRole::Audio);
    EXPECT_EQ(rIn.visibleJackIndex, 1);
    const auto rOut = filter.mapOutputChannel(FilterModule::kRightBase);
    EXPECT_EQ(rOut.role, PortRole::Audio);
    EXPECT_EQ(rOut.visibleJackIndex, 1);
}

TEST(FilterStereo, UnclaimedChannelsAreNotPhantomJackHeads) {
    // ModuleBase's default input map reports isPolyGroupHead for any raw channel below the VISIBLE
    // jack count. Going from 4 jacks to 5 would therefore have made mono raw ch4 a second head on
    // the Drive jack, and getJackTargets would hand out two wires for one jack.
    FilterModule filter;
    for (int raw = 4; raw < FilterModule::kRightBase; ++raw)
        EXPECT_FALSE(filter.mapInputChannel(raw).isPolyGroupHead) << "mono raw channel " << raw << " is a phantom head";

    for (int jack = 0; jack < filter.getVisibleInputPortCount(); ++jack)
        EXPECT_EQ(filter.getJackTargets(jack, true).size(), 1u) << "visible input jack " << jack;
}

TEST(FilterStereo, CVTargetChannelsAreUnchangedInBothVoiceModes) {
    FilterModule filter;

    auto mono = filter.getModulationTargets();
    ASSERT_EQ(mono.size(), 3u);
    EXPECT_EQ(mono[0].channelIndex, 1);
    EXPECT_EQ(mono[1].channelIndex, 2);
    EXPECT_EQ(mono[2].channelIndex, 3);

    setBoolParam(filter, "poly", true);
    auto poly = filter.getModulationTargets();
    ASSERT_EQ(poly.size(), 3u);
    EXPECT_EQ(poly[0].channelIndex, 8);
    EXPECT_EQ(poly[1].channelIndex, 9);
    EXPECT_EQ(poly[2].channelIndex, 10);

    // And the helper agrees with the published targets.
    EXPECT_EQ(FilterModule::cvChannelFor(0, false), 1);
    EXPECT_EQ(FilterModule::cvChannelFor(2, false), 3);
    EXPECT_EQ(FilterModule::cvChannelFor(0, true), 8);
    EXPECT_EQ(FilterModule::cvChannelFor(2, true), 10);
}

TEST(FilterStereo, PolyFansBothLegsEightWide) {
    FilterModule filter;
    setBoolParam(filter, "poly", true);

    for (bool isInput : {true, false}) {
        const auto left = isInput ? filter.mapInputChannel(0) : filter.mapOutputChannel(0);
        EXPECT_TRUE(left.isPolyGroupHead);
        EXPECT_EQ(left.polyVoiceSpan, 8);
        EXPECT_EQ(left.visibleJackIndex, 0);

        const int rBase = FilterModule::kRightBase;
        const auto right = isInput ? filter.mapInputChannel(rBase) : filter.mapOutputChannel(rBase);
        EXPECT_TRUE(right.isPolyGroupHead);
        EXPECT_EQ(right.polyVoiceSpan, 8);
        EXPECT_EQ(right.visibleJackIndex, 1);
    }
}

// ---------------------------------------------------------------------------
// Filter — DSP
// ---------------------------------------------------------------------------

TEST(FilterStereo, UnpatchedAudioRStaysSilentAndDoesNotDisturbAudioL) {
    FilterModule filter;
    filter.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(FilterModule::kNumChannels, kBlockSize);
    buffer.clear();
    fillTone(buffer, 0, 3000.0f);
    juce::MidiBuffer midi;
    filter.processBlock(buffer, midi);

    EXPECT_GT(rmsOf(buffer, 0), 1.0e-4f);
    EXPECT_LT(rmsOf(buffer, FilterModule::kRightBase), 1.0e-9f) << "nothing was patched into Audio R";
}

TEST(FilterStereo, AudioLIsUnaffectedByWhateverAudioRCarries) {
    // The two legs must not share filter state. Same left input, different right input, identical
    // left output.
    //
    // Must run several blocks: within one block the left leg is filtered before the right, so a
    // shared ladder would only show up as state bleeding into the NEXT block.
    auto renderLeft = [](bool feedRight) {
        FilterModule filter;
        filter.prepareToPlay(kSampleRate, kBlockSize);
        juce::AudioBuffer<float> buffer(FilterModule::kNumChannels, kBlockSize);
        juce::MidiBuffer midi;
        for (int block = 0; block < 4; ++block) {
            buffer.clear();
            fillTone(buffer, 0, 3000.0f);
            if (feedRight)
                fillTone(buffer, FilterModule::kRightBase, 180.0f, 0.9f);
            filter.processBlock(buffer, midi);
        }
        return buffer;
    };

    const auto withoutR = renderLeft(false);
    const auto withR = renderLeft(true);
    EXPECT_GT(rmsOf(withoutR, 0), 1.0e-4f);
    EXPECT_LT(maxAbsDiff(withoutR, 0, withR, 0), 1.0e-7f);
}

TEST(FilterStereo, BothLegsGetTheSameCoefficientsForTheSameInput) {
    FilterModule filter;
    filter.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(filter, "cutoff", 800.0f);

    juce::AudioBuffer<float> buffer(FilterModule::kNumChannels, kBlockSize);
    juce::MidiBuffer midi;

    // Several blocks so the cutoff smoother has settled and any per-leg drift would accumulate.
    for (int block = 0; block < 4; ++block) {
        buffer.clear();
        fillTone(buffer, 0, 3000.0f);
        fillTone(buffer, FilterModule::kRightBase, 3000.0f);
        filter.processBlock(buffer, midi);
    }

    EXPECT_GT(rmsOf(buffer, 0), 1.0e-5f);
    EXPECT_LT(maxAbsDiff(buffer, 0, buffer, FilterModule::kRightBase), 1.0e-6f)
        << "identical input through two linked ladders must come out identical";
}

TEST(FilterStereo, DifferentInputsStayDifferentThroughTheVCF) {
    FilterModule filter;
    filter.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(FilterModule::kNumChannels, kBlockSize);
    buffer.clear();
    fillTone(buffer, 0, 500.0f);
    fillTone(buffer, FilterModule::kRightBase, 500.0f, 0.5f, juce::MathConstants<float>::pi);
    juce::MidiBuffer midi;
    filter.processBlock(buffer, midi);

    EXPECT_GT(rmsOf(buffer, 0), 1.0e-4f);
    EXPECT_GT(rmsOf(buffer, FilterModule::kRightBase), 1.0e-4f);
    EXPECT_GT(maxAbsDiff(buffer, 0, buffer, FilterModule::kRightBase), 1.0e-3f)
        << "a stereo filter must keep L and R distinct, or the image collapses at the VCF";
}

TEST(FilterStereo, CVClearDoesNotEraseAudioR) {
    // Regression: the end-of-block CV clear used to run to getNumChannels(), which would wipe the
    // right leg now that it sits above the CV inputs.
    FilterModule filter;
    filter.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(FilterModule::kNumChannels, kBlockSize);
    buffer.clear();
    fillTone(buffer, FilterModule::kRightBase, 500.0f);
    // Non-zero CV on every shared CV channel, all of which must be cleared on the way out.
    for (int cv = 0; cv < FilterModule::kNumCVInputs; ++cv)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(FilterModule::cvChannelFor(cv, false), i, 0.4f);

    juce::MidiBuffer midi;
    filter.processBlock(buffer, midi);

    EXPECT_GT(rmsOf(buffer, FilterModule::kRightBase), 1.0e-4f);
    for (int cv = 0; cv < FilterModule::kNumCVInputs; ++cv)
        EXPECT_LT(rmsOf(buffer, FilterModule::cvChannelFor(cv, false)), 1.0e-9f)
            << "CV channel " << FilterModule::cvChannelFor(cv, false) << " leaked downstream as audio";
}

TEST(FilterStereo, OutputLevelScalesBothLegsFromOneRamp) {
    FilterModule filter;
    filter.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(filter, "outputLevel", 0.5f);

    juce::AudioBuffer<float> buffer(FilterModule::kNumChannels, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 4; ++block) {
        buffer.clear();
        fillTone(buffer, 0, 3000.0f);
        fillTone(buffer, FilterModule::kRightBase, 3000.0f);
        filter.processBlock(buffer, midi);
    }

    ASSERT_GT(rmsOf(buffer, 0), 1.0e-5f);
    // Two applyOutputLevel calls would advance the smoother twice and leave R behind L.
    EXPECT_LT(maxAbsDiff(buffer, 0, buffer, FilterModule::kRightBase), 1.0e-6f);
}

TEST(FilterStereo, NotchModeFiltersBothLegs) {
    FilterModule filter;
    filter.prepareToPlay(kSampleRate, kBlockSize);
    for (auto* param : filter.getParameters())
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param))
            if (choice->paramID == "filterType")
                *choice = 6; // Notch

    juce::AudioBuffer<float> buffer(FilterModule::kNumChannels, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 2; ++block) {
        buffer.clear();
        fillTone(buffer, 0, 3000.0f);
        fillTone(buffer, FilterModule::kRightBase, 3000.0f);
        filter.processBlock(buffer, midi);
    }

    EXPECT_GT(rmsOf(buffer, 0), 1.0e-6f);
    EXPECT_LT(maxAbsDiff(buffer, 0, buffer, FilterModule::kRightBase), 1.0e-6f)
        << "the notch path needs its own SVF per leg, wired the same way as the ladders";
}

TEST(FilterStereo, PolyModeFiltersBothEightWideBlocks) {
    FilterModule filter;
    setBoolParam(filter, "poly", true);
    filter.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(FilterModule::kNumChannels, kBlockSize);
    juce::MidiBuffer midi;
    buffer.clear();
    for (int v = 0; v < 4; ++v) {
        fillTone(buffer, v, 400.0f * (float)(v + 1));
        fillTone(buffer, FilterModule::kRightBase + v, 400.0f * (float)(v + 1));
    }
    filter.processBlock(buffer, midi);

    for (int v = 0; v < 4; ++v) {
        EXPECT_GT(rmsOf(buffer, v), 1.0e-5f) << "voice " << v << " left leg";
        EXPECT_GT(rmsOf(buffer, FilterModule::kRightBase + v), 1.0e-5f) << "voice " << v << " right leg";
    }
}

TEST(FilterStereo, BypassPassesBothLegsThroughUntouched) {
    FilterModule filter;
    filter.prepareToPlay(kSampleRate, kBlockSize);
    filter.setBypassed(true);

    juce::AudioBuffer<float> buffer(FilterModule::kNumChannels, kBlockSize);
    buffer.clear();
    fillTone(buffer, 0, 3000.0f);
    fillTone(buffer, FilterModule::kRightBase, 3000.0f);

    juce::AudioBuffer<float> expected(buffer);
    juce::MidiBuffer midi;
    filter.processBlock(buffer, midi);

    EXPECT_LT(maxAbsDiff(buffer, 0, expected, 0), 1.0e-9f);
    EXPECT_LT(maxAbsDiff(buffer, FilterModule::kRightBase, expected, FilterModule::kRightBase), 1.0e-9f)
        << "bypass is a dry pass-through for the right leg too";
}
