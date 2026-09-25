// GateModuleTests.cpp — P9-11 (FRO17).
//
// Covers what is specific to GateModule beyond the generic factory/serialization/automation
// sweeps (AIStateMapperTests, ModuleAdoptionTests, AutomationZipperTests, StereoVoiceModuleTests
// all pick Gate up automatically once it is registered): the hysteresis comparator, Attack/Hold/
// Release timing, the Range floor, and the stereo-linked detector. Bypass/mute dry-pass/silence
// coverage lives in Tests/Modules/ModuleBypassTests.cpp alongside every other FX module.

#include "Modules/FX/GateModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace {

constexpr double kSampleRate = 48000.0;

void setDualIO(juce::AudioProcessor& proc, bool dual) {
    for (auto* param : proc.getParameters()) {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
            if (p->paramID == "dualIO") {
                p->setValueNotifyingHost(dual ? 1.0f : 0.0f);
                return;
            }
        }
    }
}

void setParam(juce::AudioProcessor& proc, const char* paramId, float plainValue) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&proc, paramId));
    ASSERT_NE(p, nullptr) << paramId;
    *p = plainValue;
}

/** Fills every sample of both channels with a constant level and runs one processBlock call. */
void processConstant(GateModule& module, juce::AudioBuffer<float>& buffer, float level) {
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, level);
    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);
}

} // namespace

class GateModuleTest : public ::testing::Test {
protected:
    void SetUp() override { module = std::make_unique<GateModule>(); }

    std::unique_ptr<GateModule> module;
};

TEST_F(GateModuleTest, ModuleTypeAndCategoryAreCorrect) {
    module->prepareToPlay(kSampleRate, 512);
    EXPECT_EQ(module->getModuleType(), ModuleType::Gate);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::FX);
}

TEST_F(GateModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    module->prepareToPlay(kSampleRate, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    processConstant(*module, buffer, 0.3f);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            EXPECT_TRUE(std::isfinite(buffer.getSample(ch, i)));
}

TEST_F(GateModuleTest, ProcessBlockProducesOutputWhenAboveThreshold) {
    module->prepareToPlay(kSampleRate, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    // Well above the default -40 dB threshold and past a couple of attack times.
    processConstant(*module, buffer, 0.5f);

    bool anyNonZero = false;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (buffer.getSample(0, i) != 0.0f)
            anyNonZero = true;
    EXPECT_TRUE(anyNonZero);
}

TEST_F(GateModuleTest, PortLabelsAndCounts) {
    // Audio pair first, then one parameter-CV jack per knob (these are NOT a sidechain — the
    // detector still listens to the audio pair only).
    const juce::String cv[] = {"Threshold", "Attack", "Hold", "Release", "Range"};
    EXPECT_EQ(module->getInputPortLabel(0), "Audio");
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(module->getInputPortLabel(1 + i), cv[i]);
    EXPECT_EQ(module->getOutputPortLabel(0), "Audio");
    EXPECT_EQ(module->getVisibleInputPortCount(), 6);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 1);

    setDualIO(*module, true);
    EXPECT_EQ(module->getInputPortLabel(0), "Left");
    EXPECT_EQ(module->getInputPortLabel(1), "Right");
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(module->getInputPortLabel(2 + i), cv[i]);
    EXPECT_EQ(module->getOutputPortLabel(0), "Left");
    EXPECT_EQ(module->getOutputPortLabel(1), "Right");
    EXPECT_EQ(module->getVisibleInputPortCount(), 7);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 2);
}

TEST_F(GateModuleTest, StartsClosedAtThePreparedRangeFloor) {
    setParam(*module, "range", -20.0f);
    module->prepareToPlay(kSampleRate, 512);

    // A level with no chance of opening the gate (default threshold is -40 dB): the fresh module
    // must already be sitting at the Range floor, not silence.
    juce::AudioBuffer<float> buffer(2, 256);
    processConstant(*module, buffer, 0.001f);

    const float expectedGain = juce::Decibels::decibelsToGain(-20.0f); // 0.1
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        EXPECT_NEAR(buffer.getSample(0, i), 0.001f * expectedGain, 1.0e-6f) << "sample " << i;
}

// A signal sitting strictly between Threshold and Threshold - hysteresis must keep whatever state
// the gate was already in — it must neither open a closed gate nor close an open one.
TEST_F(GateModuleTest, HysteresisGapKeepsAClosedGateClosed) {
    module->prepareToPlay(kSampleRate, 512);
    // Default threshold -40 dB (~0.01 linear), hysteresis 3 dB -> close level ~0.00708 linear.
    // 0.009 sits inside the gap: below open (0.01) but above close (0.00708).
    const float midLevel = 0.009f;
    ASSERT_LT(midLevel, juce::Decibels::decibelsToGain(-40.0f));
    ASSERT_GT(midLevel, juce::Decibels::decibelsToGain(-40.0f - GateModule::kGateHysteresisDb));

    juce::AudioBuffer<float> buffer(2, 4000); // far more than default attack/hold would need
    processConstant(*module, buffer, midLevel);

    const float rangeGain = juce::Decibels::decibelsToGain(-80.0f); // default Range
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        EXPECT_NEAR(buffer.getSample(0, i), midLevel * rangeGain, 1.0e-6f) << "sample " << i;
}

TEST_F(GateModuleTest, HysteresisGapKeepsAnOpenGateOpen) {
    setParam(*module, "attack", 1.0f);
    module->prepareToPlay(kSampleRate, 512);

    // Open it fully first.
    juce::AudioBuffer<float> openBuf(2, 1000);
    processConstant(*module, openBuf, 0.5f);
    ASSERT_NEAR(openBuf.getSample(0, openBuf.getNumSamples() - 1), 0.5f, 1.0e-3f) << "did not fully open";

    // Now hold it in the hysteresis gap for a long time — long enough that Hold + Release would
    // have fully closed it if the gap were being treated as "below Threshold, start closing".
    const float midLevel = 0.009f;
    juce::AudioBuffer<float> midBuf(2, 20000);
    processConstant(*module, midBuf, midLevel);
    for (int i = 0; i < midBuf.getNumSamples(); ++i)
        EXPECT_NEAR(midBuf.getSample(0, i), midLevel, 1.0e-4f) << "sample " << i << " — gate closed inside the gap";
}

TEST_F(GateModuleTest, ClosesOnlyOnceBelowThresholdMinusHysteresis) {
    setParam(*module, "attack", 1.0f);
    setParam(*module, "hold", 0.0f);
    module->prepareToPlay(kSampleRate, 512);

    juce::AudioBuffer<float> openBuf(2, 1000);
    processConstant(*module, openBuf, 0.5f);
    ASSERT_NEAR(openBuf.getSample(0, openBuf.getNumSamples() - 1), 0.5f, 1.0e-3f) << "did not fully open";

    // Below the close level now (default threshold -40 dB - 3 dB hysteresis): with Hold at 0 this
    // must start releasing immediately.
    const float lowLevel = 0.005f;
    ASSERT_LT(lowLevel, juce::Decibels::decibelsToGain(-40.0f - GateModule::kGateHysteresisDb));
    juce::AudioBuffer<float> lowBuf(2, 1);
    processConstant(*module, lowBuf, lowLevel);
    const float gainAfterOneSample = lowBuf.getSample(0, 0) / lowLevel;
    EXPECT_LT(gainAfterOneSample, 1.0f) << "gate did not begin releasing the very next sample";
}

TEST_F(GateModuleTest, AttackReachesFullyOpenWithinExpectedSamples) {
    setParam(*module, "attack", 5.0f); // 5 ms @ 48 kHz = 240 samples, exactly
    module->prepareToPlay(kSampleRate, 512);

    const int expectedSamples = 240;
    juce::AudioBuffer<float> buffer(2, expectedSamples + 50);
    processConstant(*module, buffer, 0.5f); // well above threshold

    int firstFullyOpenSample = -1;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        if (buffer.getSample(0, i) / 0.5f >= 0.999f) {
            firstFullyOpenSample = i;
            break;
        }
    }
    ASSERT_GE(firstFullyOpenSample, 0) << "gate never fully opened";
    EXPECT_NEAR(firstFullyOpenSample, expectedSamples - 1, 2)
        << "attack ramp took " << (firstFullyOpenSample + 1) << " samples, expected ~" << expectedSamples;
}

TEST_F(GateModuleTest, HoldKeepsTheGateOpenBeforeReleaseBegins) {
    setParam(*module, "attack", 1.0f);   // negligible next to Hold
    setParam(*module, "hold", 10.0f);    // 10 ms @ 48 kHz = 480 samples
    setParam(*module, "release", 50.0f); // slow enough that Hold's boundary is unambiguous
    module->prepareToPlay(kSampleRate, 512);

    juce::AudioBuffer<float> openBuf(2, 200);
    processConstant(*module, openBuf, 0.5f);
    ASSERT_NEAR(openBuf.getSample(0, openBuf.getNumSamples() - 1), 0.5f, 1.0e-3f) << "did not fully open";

    // A quiet-but-nonzero probe tone (below the close level, so the hold timer starts) rather
    // than literal silence, so the gain itself stays observable via output/input.
    const int holdSamples = 480;
    const float probe = 0.0005f; // below the -43 dB close level
    juce::AudioBuffer<float> probeBuf(2, holdSamples + 400);
    processConstant(*module, probeBuf, probe);

    int firstBelowFullyOpen = -1;
    for (int i = 0; i < probeBuf.getNumSamples(); ++i) {
        if (probeBuf.getSample(0, i) / probe < 0.999f) {
            firstBelowFullyOpen = i;
            break;
        }
    }
    ASSERT_GE(firstBelowFullyOpen, 0) << "gate never started releasing";
    EXPECT_NEAR(firstBelowFullyOpen, holdSamples, 2)
        << "release began after " << firstBelowFullyOpen << " samples, expected ~" << holdSamples << " (Hold)";
}

TEST_F(GateModuleTest, ReleaseReachesTheRangeFloorWithinExpectedSamples) {
    setParam(*module, "attack", 1.0f);
    setParam(*module, "hold", 0.0f);
    setParam(*module, "release", 100.0f); // 100 ms @ 48 kHz = 4800 samples
    setParam(*module, "range", -20.0f);
    module->prepareToPlay(kSampleRate, 512);

    juce::AudioBuffer<float> openBuf(2, 200);
    processConstant(*module, openBuf, 0.5f);
    ASSERT_NEAR(openBuf.getSample(0, openBuf.getNumSamples() - 1), 0.5f, 1.0e-3f) << "did not fully open";

    const float probe = 0.0005f; // below the close level, Hold is 0 so release starts immediately
    const int releaseSamples = 4800;
    juce::AudioBuffer<float> buffer(2, releaseSamples + 50);
    processConstant(*module, buffer, probe);

    // The release ramp is linear and fully deterministic: gain(i) = max(Range, 1 - (i+1)*step),
    // step = (1 - Range) / releaseSamples. Compare the actual trajectory against that analytic
    // model at a few sample indices, rather than scanning for a threshold crossing — the
    // per-sample step here is itself smaller than any workable scan-detection epsilon, which is
    // exactly what made a crossing-scan cross a few samples early.
    const float rangeGain = juce::Decibels::decibelsToGain(-20.0f); // 0.1
    const float step = (1.0f - rangeGain) / (float)releaseSamples;
    auto expectedGainAt = [&](int i) { return std::max(rangeGain, 1.0f - (float)(i + 1) * step); };

    for (int i : {0, releaseSamples / 4, releaseSamples / 2, releaseSamples - 1, releaseSamples + 10}) {
        const float actualGain = buffer.getSample(0, i) / probe;
        EXPECT_NEAR(actualGain, expectedGainAt(i), 1.0e-3f) << "sample " << i;
    }

    // And the floor is genuinely Range, not silence.
    EXPECT_NEAR(buffer.getSample(0, buffer.getNumSamples() - 1), probe * rangeGain, 1.0e-6f);
    EXPECT_GT(std::abs(buffer.getSample(0, buffer.getNumSamples() - 1)), 0.0f);
}

TEST_F(GateModuleTest, BothStereoLegsAreGatedIdenticallyByTheLinkedDetector) {
    setParam(*module, "attack", 1.0f);
    module->prepareToPlay(kSampleRate, 512);

    // Left is loud enough to open the gate on its own; Right alone would not be (it sits below
    // the open threshold). A linked detector opens BOTH legs together, driven by max(|L|,|R|).
    juce::AudioBuffer<float> buffer(2, 300);
    const float loudLeft = 0.5f;
    const float quietRight = 0.001f;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, loudLeft);
        buffer.setSample(1, i, quietRight);
    }
    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    const int last = buffer.getNumSamples() - 1;
    const float leftGain = buffer.getSample(0, last) / loudLeft;
    const float rightGain = buffer.getSample(1, last) / quietRight;
    EXPECT_NEAR(leftGain, 1.0f, 1.0e-3f);
    // If the detector were per-channel (unlinked), the right leg's own quiet level would never
    // have opened it, and rightGain would still be near the Range floor.
    EXPECT_NEAR(rightGain, leftGain, 1.0e-3f) << "right leg was not gated by the linked (louder) left signal";
}

TEST_F(GateModuleTest, RangeFloorIsNotSilence) {
    setParam(*module, "attack", 1.0f);
    setParam(*module, "hold", 0.0f);
    setParam(*module, "release", 20.0f);
    setParam(*module, "range", -20.0f); // -20 dB -> 0.1 linear, not mute
    module->prepareToPlay(kSampleRate, 512);

    juce::AudioBuffer<float> openBuf(2, 200);
    processConstant(*module, openBuf, 0.5f);

    const float probe = 0.0005f;
    juce::AudioBuffer<float> closeBuf(2, 5000); // comfortably past release
    processConstant(*module, closeBuf, probe);

    const float expectedGain = juce::Decibels::decibelsToGain(-20.0f);
    const float finalSample = closeBuf.getSample(0, closeBuf.getNumSamples() - 1);
    EXPECT_NEAR(finalSample, probe * expectedGain, 1.0e-6f);
    EXPECT_GT(std::abs(finalSample), 0.0f) << "Range floor collapsed to silence instead of the parameterised gain";
}
