// SampleHoldModuleTests.cpp
// Unit tests for SampleHoldModule (Sample & Hold / Randomizer, issue #148).
//   • Construction / port topology / modulation targets
//   • Sample mode: latches the source on a rising edge and holds between edges
//   • Track mode: follows the source while the gate is high, freezes when it falls
//   • Internal clock: free-running stepped output, rate affects step count
//   • Random source: bipolar, changes per step
//   • Level / Offset / Slew shaping and their CV inputs
//   • Bypass (dry pass-through) / Mute (silence) contract
//   • Edge cases: zero-length buffer, no trigger channel, prepare/reset

#include "Modules/SampleHoldModule.h"
#include "UI/TriggerMeterComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;
// One second of audio. The internal clock tops out at 50 Hz, so a 512-sample block is far
// shorter than a single step — rate-related tests need a buffer long enough to see steps.
constexpr int kOneSecond = 44100;

// Index of each input channel, mirroring the documented layout.
enum Ch { Signal = 0, Trigger = 1, RateCV = 2, SlewCV = 3, LevelCV = 4, OffsetCV = 5, ThresholdCV = 6 };

/** Sets a parameter by its paramID to a normalised 0..1 value. */
void setParam(SampleHoldModule& m, const juce::String& id, float normalised) {
    for (auto* p : m.getParameters()) {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p)) {
            if (withId->paramID == id) {
                withId->setValueNotifyingHost(normalised);
                return;
            }
        }
    }
    ADD_FAILURE() << "No parameter with id " << id;
}

/** Selects a choice parameter by index. */
void setChoice(SampleHoldModule& m, const juce::String& id, int index, int numChoices) {
    setParam(m, id, (float)index / (float)(numChoices - 1));
}

/** Selects "External" clock so tests drive the module from the Trigger channel. */
void useExternalClock(SampleHoldModule& m) { setChoice(m, "clock", 1, 2); }

/** Selects "Input" as the sampled source (default is "Random"). */
void useInputSource(SampleHoldModule& m) { setChoice(m, "source", 0, 2); }

/** Selects "Track" mode (default is "Sample"). */
void useTrackMode(SampleHoldModule& m) { setChoice(m, "holdMode", 1, 2); }

/** Fills a whole channel with a constant. */
void fill(juce::AudioBuffer<float>& b, int ch, float v) {
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample(ch, i, v);
}

/** Raises the trigger channel to 1.0 over [start, end). */
void gate(juce::AudioBuffer<float>& b, int start, int end) {
    for (int i = start; i < end; ++i)
        b.setSample(Ch::Trigger, i, 1.0f);
}

class SampleHoldModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<SampleHoldModule>();
        module->prepareToPlay(kSampleRate, kBlockSize);
    }

    juce::AudioBuffer<float> makeBuffer(int numSamples = kBlockSize) {
        juce::AudioBuffer<float> b(module->getTotalNumInputChannels(), numSamples);
        b.clear();
        return b;
    }

    void process(juce::AudioBuffer<float>& b) {
        juce::MidiBuffer midi;
        module->processBlock(b, midi);
    }

    std::unique_ptr<SampleHoldModule> module;
};

// ---------------------------------------------------------------------------
// Construction / topology
// ---------------------------------------------------------------------------

TEST_F(SampleHoldModuleTest, NameAndTypeAreCorrect) {
    EXPECT_EQ(module->getName(), "Sample & Hold");
    EXPECT_EQ(module->getModuleType(), ModuleType::SampleHold);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::LFO);
}

TEST_F(SampleHoldModuleTest, ChannelCountsMatchDocumentedLayout) {
    EXPECT_EQ(module->getTotalNumInputChannels(), 7);
    // Outputs must cover the highest CV input channel read (ch6) so the
    // AudioProcessorGraph cannot alias those buffers with another node's output.
    EXPECT_EQ(module->getTotalNumOutputChannels(), 7);
    EXPECT_EQ(module->getVisibleInputPortCount(), 7);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 1);
}

TEST_F(SampleHoldModuleTest, PortLabelsAreDescriptive) {
    EXPECT_EQ(module->getInputPortLabel(0), "Signal");
    EXPECT_EQ(module->getInputPortLabel(1), "Trigger");
    EXPECT_EQ(module->getInputPortLabel(2), "Rate");
    EXPECT_EQ(module->getInputPortLabel(3), "Slew");
    EXPECT_EQ(module->getInputPortLabel(4), "Level");
    EXPECT_EQ(module->getInputPortLabel(5), "Offset");
    EXPECT_EQ(module->getInputPortLabel(6), "Threshold");
    EXPECT_EQ(module->getOutputPortLabel(0), "CV");
}

TEST_F(SampleHoldModuleTest, ModulationTargetsCoverContinuousParams) {
    auto targets = module->getModulationTargets();
    ASSERT_EQ(targets.size(), 5u);
    EXPECT_EQ(targets[0].name, "Rate");
    EXPECT_EQ(targets[0].channelIndex, Ch::RateCV);
    EXPECT_EQ(targets[4].name, "Threshold");
    EXPECT_EQ(targets[4].channelIndex, Ch::ThresholdCV);
}

TEST_F(SampleHoldModuleTest, SignalAndTriggerAreNotAutoPromotableModTargets) {
    // Only the four CV jacks should get an attenuverter inserted; Signal/Trigger stay direct.
    EXPECT_FALSE(module->isAutoPromotableModTarget(Ch::Signal));
    EXPECT_FALSE(module->isAutoPromotableModTarget(Ch::Trigger));
    EXPECT_TRUE(module->isAutoPromotableModTarget(Ch::RateCV));
    EXPECT_TRUE(module->isAutoPromotableModTarget(Ch::OffsetCV));
    EXPECT_TRUE(module->isAutoPromotableModTarget(Ch::ThresholdCV));
}

TEST_F(SampleHoldModuleTest, LogicalPortRolesAreCorrect) {
    EXPECT_EQ(module->mapInputChannel(0).role, PortRole::Audio);
    EXPECT_EQ(module->mapInputChannel(1).role, PortRole::Gate);
    EXPECT_EQ(module->mapInputChannel(2).role, PortRole::ModCV);
    EXPECT_EQ(module->mapInputChannel(5).visibleJackIndex, 5);
    EXPECT_EQ(module->mapInputChannel(6).role, PortRole::ModCV);
    EXPECT_EQ(module->mapInputChannel(6).visibleJackIndex, 6);
    EXPECT_EQ(module->mapOutputChannel(0).role, PortRole::ModCV);
}

// ---------------------------------------------------------------------------
// Sample mode — external clock
// ---------------------------------------------------------------------------

TEST_F(SampleHoldModuleTest, SampleModeLatchesSourceOnRisingEdge) {
    useExternalClock(*module);
    useInputSource(*module);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.6f);
    gate(b, 100, 200); // single rising edge at sample 100

    process(b);

    // Before the edge nothing has been captured yet.
    EXPECT_FLOAT_EQ(b.getSample(0, 50), 0.0f);
    // From the edge onwards the captured value is held.
    EXPECT_NEAR(b.getSample(0, 100), 0.6f, 1e-5f);
    EXPECT_NEAR(b.getSample(0, 300), 0.6f, 1e-5f);
    EXPECT_NEAR(b.getSample(0, 511), 0.6f, 1e-5f);
}

TEST_F(SampleHoldModuleTest, SampleModeHoldsValueWhileSourceChanges) {
    useExternalClock(*module);
    useInputSource(*module);

    auto b = makeBuffer();
    // Source is 0.25 at the edge, then jumps to 0.9 afterwards.
    for (int i = 0; i < kBlockSize; ++i)
        b.setSample(Ch::Signal, i, i < 150 ? 0.25f : 0.9f);
    gate(b, 100, 120);

    process(b);

    // The held value must ignore the later jump — it was latched at sample 100.
    EXPECT_NEAR(b.getSample(0, 100), 0.25f, 1e-5f);
    EXPECT_NEAR(b.getSample(0, 200), 0.25f, 1e-5f);
    EXPECT_NEAR(b.getSample(0, 511), 0.25f, 1e-5f);
}

TEST_F(SampleHoldModuleTest, SampleModeIgnoresSustainedHighGate) {
    useExternalClock(*module);
    useInputSource(*module);

    auto b = makeBuffer();
    for (int i = 0; i < kBlockSize; ++i)
        b.setSample(Ch::Signal, i, i < 150 ? 0.3f : 0.8f);
    gate(b, 0, kBlockSize); // gate never falls — only one rising edge, at sample 0

    process(b);

    EXPECT_NEAR(b.getSample(0, 0), 0.3f, 1e-5f);
    EXPECT_NEAR(b.getSample(0, 400), 0.3f, 1e-5f) << "A sustained gate must not re-trigger";
}

TEST_F(SampleHoldModuleTest, SampleModeCapturesEachSeparateEdge) {
    useExternalClock(*module);
    useInputSource(*module);

    auto b = makeBuffer();
    for (int i = 0; i < kBlockSize; ++i)
        b.setSample(Ch::Signal, i, i < 250 ? 0.4f : -0.7f);
    gate(b, 100, 150); // first edge, source 0.4
    gate(b, 300, 350); // second edge, source -0.7

    process(b);

    EXPECT_NEAR(b.getSample(0, 200), 0.4f, 1e-5f);
    EXPECT_NEAR(b.getSample(0, 400), -0.7f, 1e-5f);
}

TEST_F(SampleHoldModuleTest, GateStateCarriesAcrossBlocks) {
    useExternalClock(*module);
    useInputSource(*module);

    // Block 1 ends with the gate still high.
    auto b1 = makeBuffer();
    fill(b1, Ch::Signal, 0.5f);
    gate(b1, 400, kBlockSize);
    process(b1);
    EXPECT_NEAR(b1.getSample(0, 500), 0.5f, 1e-5f);

    // Block 2 starts with the gate still high and a different source: no new edge,
    // so the value from block 1 must persist.
    auto b2 = makeBuffer();
    fill(b2, Ch::Signal, -0.9f);
    gate(b2, 0, 100);
    process(b2);
    EXPECT_NEAR(b2.getSample(0, 50), 0.5f, 1e-5f) << "Gate high across the block boundary is not a new edge";

    // The gate falls at sample 100 and never rises again in this block, so the value stands.
    EXPECT_NEAR(b2.getSample(0, 200), 0.5f, 1e-5f);
}

// ---------------------------------------------------------------------------
// Trigger threshold + hysteresis
// ---------------------------------------------------------------------------

// The threshold param is -1..+1, so a normalised value of n means (n * 2 - 1).
void setThreshold(SampleHoldModule& m, float actual) { setParam(m, "trigThreshold", (actual + 1.0f) * 0.5f); }

TEST_F(SampleHoldModuleTest, DefaultThresholdIsHalf) { EXPECT_NEAR(module->getEffectiveThreshold(), 0.5f, 1e-5f); }

TEST_F(SampleHoldModuleTest, LowAmplitudeGateFiresOnceThresholdIsLowered) {
    useExternalClock(*module);
    useInputSource(*module);

    // A gate that only reaches 0.3 — an attenuated LFO, a low-sustain envelope. Under the old
    // hard-coded 0.5 threshold this silently never fired.
    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.75f);
    for (int i = 100; i < 200; ++i)
        b.setSample(Ch::Trigger, i, 0.3f);

    process(b);
    EXPECT_FLOAT_EQ(b.getSample(0, 300), 0.0f) << "0.3 gate must not cross the default 0.5 threshold";

    module->prepareToPlay(kSampleRate, kBlockSize);
    setThreshold(*module, 0.2f);

    auto b2 = makeBuffer();
    fill(b2, Ch::Signal, 0.75f);
    for (int i = 100; i < 200; ++i)
        b2.setSample(Ch::Trigger, i, 0.3f);

    process(b2);
    EXPECT_NEAR(b2.getSample(0, 300), 0.75f, 1e-5f) << "Lowering the threshold below the gate must let it fire";
}

TEST_F(SampleHoldModuleTest, NegativeThresholdFiresOnBipolarSignal) {
    useExternalClock(*module);
    useInputSource(*module);
    setThreshold(*module, -0.5f);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.4f);
    // Trigger rises from -1.0 to -0.2, crossing -0.5 but never reaching 0.
    for (int i = 0; i < kBlockSize; ++i)
        b.setSample(Ch::Trigger, i, i < 100 ? -1.0f : -0.2f);

    process(b);
    EXPECT_NEAR(b.getSample(0, 300), 0.4f, 1e-5f);
}

TEST_F(SampleHoldModuleTest, HysteresisPreventsChatterAroundThreshold) {
    useExternalClock(*module);
    useInputSource(*module);
    setThreshold(*module, 0.0f);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.5f);
    // A signal dithering minutely around the threshold — the classic retrigger-storm case.
    // Every odd sample is above 0.0 and every even sample below, but the excursion is far
    // smaller than the hysteresis gap, so exactly one capture should occur.
    for (int i = 0; i < kBlockSize; ++i)
        b.setSample(Ch::Trigger, i, (i % 2 == 0) ? -0.001f : 0.001f);

    process(b);

    EXPECT_EQ(module->getTriggerCount(), 1) << "Dither around the threshold must not retrigger every sample";
}

TEST_F(SampleHoldModuleTest, SignalMustFallBelowHysteresisGapToReArm) {
    useExternalClock(*module);
    useInputSource(*module);
    setThreshold(*module, 0.5f);

    const float gap = SampleHoldModule::getTriggerHysteresis();

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.5f);
    // Rise above threshold, dip to just inside the hysteresis gap, rise again.
    // The dip is not a release, so the second rise is not a new edge.
    for (int i = 0; i < kBlockSize; ++i) {
        float v = 0.9f;
        if (i >= 100 && i < 200)
            v = 0.5f - (gap * 0.5f); // below threshold but inside the gap
        b.setSample(Ch::Trigger, i, v);
    }

    process(b);
    EXPECT_EQ(module->getTriggerCount(), 1) << "A dip inside the hysteresis gap must not re-arm the trigger";
}

TEST_F(SampleHoldModuleTest, FullReleaseReArmsTheTrigger) {
    useExternalClock(*module);
    useInputSource(*module);
    setThreshold(*module, 0.5f);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.5f);
    for (int i = 0; i < kBlockSize; ++i) {
        float v = 0.9f;
        if (i >= 100 && i < 200)
            v = 0.0f; // a genuine release, well below threshold - hysteresis
        b.setSample(Ch::Trigger, i, v);
    }

    process(b);
    EXPECT_EQ(module->getTriggerCount(), 2) << "A full release then rise is a second edge";
}

TEST_F(SampleHoldModuleTest, ThresholdCVShiftsTheTriggerPoint) {
    useExternalClock(*module);
    useInputSource(*module);

    // Gate reaches only 0.3, below the 0.5 default — but negative Threshold CV pulls the
    // threshold down far enough for it to fire.
    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.6f);
    fill(b, Ch::ThresholdCV, -0.4f); // effective threshold 0.1
    for (int i = 100; i < 200; ++i)
        b.setSample(Ch::Trigger, i, 0.3f);

    process(b);

    EXPECT_NEAR(b.getSample(0, 300), 0.6f, 1e-5f);
    EXPECT_NEAR(module->getEffectiveThreshold(), 0.1f, 1e-5f);
}

// ---------------------------------------------------------------------------
// Trigger meter telemetry
// ---------------------------------------------------------------------------

TEST_F(SampleHoldModuleTest, TriggerLevelReportsSignedPeakOfBlock) {
    useExternalClock(*module);

    auto b = makeBuffer();
    for (int i = 0; i < kBlockSize; ++i)
        b.setSample(Ch::Trigger, i, i == 42 ? -0.8f : 0.2f);

    process(b);

    EXPECT_NEAR(module->getTriggerLevel(), -0.8f, 1e-5f) << "Meter should report the largest-magnitude sample, signed";
}

TEST_F(SampleHoldModuleTest, TriggerLevelIsLiveOnInternalClockToo) {
    // Clock defaults to Internal. The meter must still track the jack so the threshold can be
    // dialled in before switching over.
    auto b = makeBuffer();
    fill(b, Ch::Trigger, 0.7f);

    process(b);

    EXPECT_NEAR(module->getTriggerLevel(), 0.7f, 1e-5f);
}

TEST_F(SampleHoldModuleTest, TriggerHighReflectsSchmittState) {
    useExternalClock(*module);

    auto b = makeBuffer();
    fill(b, Ch::Trigger, 0.9f);
    process(b);
    EXPECT_TRUE(module->isTriggerHigh());

    auto b2 = makeBuffer(); // trigger returns to 0
    process(b2);
    EXPECT_FALSE(module->isTriggerHigh());
}

TEST_F(SampleHoldModuleTest, TriggerCountAdvancesOnInternalClockSteps) {
    setParam(*module, "rate", 1.0f); // 50 Hz

    juce::AudioBuffer<float> b(module->getTotalNumInputChannels(), kOneSecond);
    b.clear();
    process(b);

    EXPECT_GT(module->getTriggerCount(), 20) << "Internal clock steps should advance the capture count";
}

TEST_F(SampleHoldModuleTest, MetersResetOnBypassAndMute) {
    useExternalClock(*module);

    auto b = makeBuffer();
    fill(b, Ch::Trigger, 0.9f);
    process(b);
    ASSERT_TRUE(module->isTriggerHigh());

    module->setBypassed(true);
    auto b2 = makeBuffer();
    fill(b2, Ch::Trigger, 0.9f);
    process(b2);
    EXPECT_FLOAT_EQ(module->getTriggerLevel(), 0.0f);
    EXPECT_FALSE(module->isTriggerHigh());
}

// ---------------------------------------------------------------------------
// Track mode
// ---------------------------------------------------------------------------

TEST_F(SampleHoldModuleTest, TrackModeFollowsSourceThenFreezes) {
    useExternalClock(*module);
    useInputSource(*module);
    useTrackMode(*module);

    auto b = makeBuffer();
    // Ramp the source so "following" is distinguishable from "holding".
    for (int i = 0; i < kBlockSize; ++i)
        b.setSample(Ch::Signal, i, (float)i / (float)kBlockSize);
    gate(b, 0, 256); // track for the first half, hold for the second

    process(b);

    // While tracking, output equals the source.
    EXPECT_NEAR(b.getSample(0, 100), 100.0f / kBlockSize, 1e-5f);
    EXPECT_NEAR(b.getSample(0, 255), 255.0f / kBlockSize, 1e-5f);
    // After the gate falls, the last tracked value is frozen.
    const float frozen = 255.0f / kBlockSize;
    EXPECT_NEAR(b.getSample(0, 300), frozen, 1e-5f);
    EXPECT_NEAR(b.getSample(0, 511), frozen, 1e-5f);
}

// ---------------------------------------------------------------------------
// Internal clock
// ---------------------------------------------------------------------------

TEST_F(SampleHoldModuleTest, InternalClockProducesSteppedOutputWithoutAnyInput) {
    // Defaults: internal clock, random source. The module must be useful stand-alone.
    auto b = makeBuffer();
    process(b);

    std::set<float> distinct;
    for (int i = 0; i < kBlockSize; ++i)
        distinct.insert(b.getSample(0, i));

    // 8 Hz over 512 samples @ 44.1kHz is well under one full step, but the module fires on
    // the first sample, so there is at least one non-zero held value and few distinct values.
    EXPECT_GE(distinct.size(), 1u);
    EXPECT_LE(distinct.size(), 4u) << "Output should be stepped, not continuously varying";
    EXPECT_NE(b.getSample(0, 10), 0.0f) << "Internal clock should latch a value immediately";
}

// Counts how many times the held value changes across a block.
int countSteps(const juce::AudioBuffer<float>& b) {
    int changes = 0;
    for (int i = 1; i < b.getNumSamples(); ++i)
        if (b.getSample(0, i) != b.getSample(0, i - 1))
            ++changes;
    return changes;
}

TEST_F(SampleHoldModuleTest, HigherInternalRateProducesMoreSteps) {
    auto runOneSecond = [](float normalisedRate) {
        SampleHoldModule m;
        m.prepareToPlay(kSampleRate, kOneSecond);
        setParam(m, "rate", normalisedRate);
        juce::AudioBuffer<float> b(m.getTotalNumInputChannels(), kOneSecond);
        b.clear();
        juce::MidiBuffer midi;
        m.processBlock(b, midi);
        return countSteps(b);
    };

    const int slowSteps = runOneSecond(0.0f); // minimum rate (0.1 Hz)
    const int fastSteps = runOneSecond(1.0f); // maximum rate (50 Hz)

    EXPECT_EQ(slowSteps, 0) << "0.1 Hz latches once at t=0 and holds for the rest of the second";
    EXPECT_GT(fastSteps, 20) << "50 Hz should latch dozens of values per second";
    EXPECT_GT(fastSteps, slowSteps) << "A higher Rate must produce more steps per second";
}

TEST_F(SampleHoldModuleTest, RandomSourceStaysInBipolarRangeAndVaries) {
    setParam(*module, "rate", 1.0f); // max rate → many steps within one second

    juce::AudioBuffer<float> b(module->getTotalNumInputChannels(), kOneSecond);
    b.clear();
    process(b);

    std::set<float> distinct;
    for (int i = 0; i < kOneSecond; ++i) {
        const float v = b.getSample(0, i);
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
        distinct.insert(v);
    }
    EXPECT_GT(distinct.size(), 1u) << "Random source should produce differing values across steps";
}

TEST_F(SampleHoldModuleTest, RateCVIncreasesStepCount) {
    auto runOneSecond = [](float rateCVValue) {
        SampleHoldModule m;
        m.prepareToPlay(kSampleRate, kOneSecond);
        juce::AudioBuffer<float> b(m.getTotalNumInputChannels(), kOneSecond);
        b.clear();
        fill(b, Ch::RateCV, rateCVValue);
        juce::MidiBuffer midi;
        m.processBlock(b, midi);
        return countSteps(b);
    };

    const int plainSteps = runOneSecond(0.0f);     // 8 Hz default
    const int modulatedSteps = runOneSecond(1.0f); // +4 octaves, clamped to the 50 Hz ceiling

    EXPECT_GT(modulatedSteps, plainSteps) << "Positive Rate CV must speed the internal clock up";
}

// ---------------------------------------------------------------------------
// Output shaping — Level / Offset / Slew
// ---------------------------------------------------------------------------

TEST_F(SampleHoldModuleTest, LevelScalesHeldValue) {
    useExternalClock(*module);
    useInputSource(*module);
    setParam(*module, "level", 0.5f); // Level range is 0..1, so normalised 0.5 == 0.5

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.8f);
    gate(b, 10, 50);
    process(b);

    EXPECT_NEAR(b.getSample(0, 200), 0.4f, 1e-4f);
}

TEST_F(SampleHoldModuleTest, OffsetShiftsHeldValue) {
    useExternalClock(*module);
    useInputSource(*module);
    setParam(*module, "offset", 0.75f); // -1..1 range → normalised 0.75 == +0.5

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.2f);
    gate(b, 10, 50);
    process(b);

    EXPECT_NEAR(b.getSample(0, 200), 0.7f, 1e-4f);
}

TEST_F(SampleHoldModuleTest, OutputIsClampedToBipolarRange) {
    useExternalClock(*module);
    useInputSource(*module);
    setParam(*module, "offset", 1.0f); // +1.0 offset on top of a +1.0 sample

    auto b = makeBuffer();
    fill(b, Ch::Signal, 1.0f);
    gate(b, 10, 50);
    process(b);

    EXPECT_FLOAT_EQ(b.getSample(0, 200), 1.0f);
}

TEST_F(SampleHoldModuleTest, LevelCVAttenuatesOutput) {
    useExternalClock(*module);
    useInputSource(*module);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.8f);
    fill(b, Ch::LevelCV, -0.5f); // 1.0 + (-0.5) = 0.5
    gate(b, 10, 50);
    process(b);

    EXPECT_NEAR(b.getSample(0, 200), 0.4f, 1e-4f);
}

TEST_F(SampleHoldModuleTest, SlewRampsTowardTheHeldValueInsteadOfJumping) {
    useExternalClock(*module);
    useInputSource(*module);
    setParam(*module, "slew", 0.2f); // ~100 ms lag

    auto b = makeBuffer();
    fill(b, Ch::Signal, 1.0f);
    gate(b, 0, 50);
    process(b);

    // With slew engaged the output must climb gradually rather than snap to 1.0.
    EXPECT_LT(b.getSample(0, 0), 0.05f);
    EXPECT_GT(b.getSample(0, 511), b.getSample(0, 0));
    EXPECT_LT(b.getSample(0, 511), 1.0f) << "100ms lag should not complete within one 512-sample block";
}

TEST_F(SampleHoldModuleTest, ZeroSlewSnapsImmediately) {
    useExternalClock(*module);
    useInputSource(*module);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 1.0f);
    gate(b, 0, 50);
    process(b);

    EXPECT_FLOAT_EQ(b.getSample(0, 0), 1.0f);
}

// ---------------------------------------------------------------------------
// Channel hygiene
// ---------------------------------------------------------------------------

TEST_F(SampleHoldModuleTest, TriggerAndCVChannelsAreClearedOnOutput) {
    useExternalClock(*module);
    useInputSource(*module);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.5f);
    fill(b, Ch::RateCV, 0.3f);
    fill(b, Ch::SlewCV, 0.3f);
    fill(b, Ch::LevelCV, 0.0f);
    fill(b, Ch::OffsetCV, 0.0f);
    gate(b, 10, 50);

    process(b);

    for (int ch = 1; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(b.getSample(ch, i), 0.0f) << "Channel " << ch << " sample " << i << " must not leak";
}

TEST_F(SampleHoldModuleTest, LastValueTracksOutput) {
    useExternalClock(*module);
    useInputSource(*module);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.65f);
    gate(b, 10, 50);
    process(b);

    EXPECT_NEAR(module->getLastValue(), 0.65f, 1e-4f);
}

// ---------------------------------------------------------------------------
// Bypass / Mute contract
// ---------------------------------------------------------------------------

TEST_F(SampleHoldModuleTest, BypassPassesDrySignalThrough) {
    module->setBypassed(true);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.7f);
    process(b);

    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_FLOAT_EQ(b.getSample(0, i), 0.7f) << "Ch0 sample " << i << " must pass through untouched";
}

TEST_F(SampleHoldModuleTest, BypassClearsTriggerAndCVChannels) {
    module->setBypassed(true);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.7f);
    for (int ch = 1; ch < b.getNumChannels(); ++ch)
        fill(b, ch, 0.5f);

    process(b);

    for (int ch = 1; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(b.getSample(ch, i), 0.0f) << "Bypassed channel " << ch << " must be cleared";
}

TEST_F(SampleHoldModuleTest, MuteSilencesEveryChannel) {
    module->setMuted(true);

    auto b = makeBuffer();
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        fill(b, ch, 0.7f);

    process(b);

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(b.getSample(ch, i), 0.0f) << "Muted channel " << ch << " must be silent";
}

TEST_F(SampleHoldModuleTest, DefaultsAreNotBypassedOrMuted) {
    EXPECT_FALSE(module->isBypassed());
    EXPECT_FALSE(module->isMuted());
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_F(SampleHoldModuleTest, ZeroLengthBufferDoesNotCrash) {
    juce::AudioBuffer<float> b(module->getTotalNumInputChannels(), 0);
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(b, midi));
}

TEST_F(SampleHoldModuleTest, ZeroChannelBufferDoesNotCrash) {
    juce::AudioBuffer<float> b(0, kBlockSize);
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(b, midi));
}

TEST_F(SampleHoldModuleTest, SingleSampleBufferDoesNotCrash) {
    auto b = makeBuffer(1);
    EXPECT_NO_THROW(process(b));
}

TEST_F(SampleHoldModuleTest, ExternalClockWithNoTriggerChannelHoldsSilently) {
    useExternalClock(*module);
    useInputSource(*module);

    // A 1-channel buffer has no trigger channel — the module must not read out of bounds.
    juce::AudioBuffer<float> b(1, kBlockSize);
    for (int i = 0; i < kBlockSize; ++i)
        b.setSample(0, i, 0.9f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(b, midi));

    // Nothing ever triggered, so the output holds at the initial 0.
    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_FLOAT_EQ(b.getSample(0, i), 0.0f);
}

TEST_F(SampleHoldModuleTest, PrepareToPlayResetsHeldState) {
    useExternalClock(*module);
    useInputSource(*module);

    auto b = makeBuffer();
    fill(b, Ch::Signal, 0.8f);
    gate(b, 10, 50);
    process(b);
    EXPECT_NEAR(module->getLastValue(), 0.8f, 1e-4f);

    module->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_FLOAT_EQ(module->getLastValue(), 0.0f);

    auto b2 = makeBuffer();
    fill(b2, Ch::Signal, 0.8f);
    process(b2); // no trigger this time
    EXPECT_FLOAT_EQ(b2.getSample(0, 100), 0.0f) << "Held value must be cleared by prepareToPlay";
}

TEST_F(SampleHoldModuleTest, HandlesZeroSampleRateWithoutDividingByZero) {
    SampleHoldModule m;
    m.prepareToPlay(0.0, kBlockSize);

    juce::AudioBuffer<float> b(m.getTotalNumInputChannels(), kBlockSize);
    b.clear();
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(m.processBlock(b, midi));

    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_TRUE(std::isfinite(b.getSample(0, i)));
}

// ---------------------------------------------------------------------------
// TriggerMeterComponent — static helpers and paint smoke test
// ---------------------------------------------------------------------------

TEST(TriggerMeterTest, ValueToXMapsBipolarRangeAcrossWidth) {
    EXPECT_FLOAT_EQ(TriggerMeterComponent::valueToX(-1.0f, 0.0f, 100.0f), 0.0f);
    EXPECT_FLOAT_EQ(TriggerMeterComponent::valueToX(0.0f, 0.0f, 100.0f), 50.0f);
    EXPECT_FLOAT_EQ(TriggerMeterComponent::valueToX(1.0f, 0.0f, 100.0f), 100.0f);
    // Honours the x origin.
    EXPECT_FLOAT_EQ(TriggerMeterComponent::valueToX(0.0f, 20.0f, 100.0f), 70.0f);
}

TEST(TriggerMeterTest, ValueToXClampsOutOfRangeInput) {
    EXPECT_FLOAT_EQ(TriggerMeterComponent::valueToX(-5.0f, 0.0f, 100.0f), 0.0f);
    EXPECT_FLOAT_EQ(TriggerMeterComponent::valueToX(5.0f, 0.0f, 100.0f), 100.0f);
}

TEST(ThresholdControlTest, UnipolarMapsZeroToLeftAndOneToRight) {
    EXPECT_FLOAT_EQ(ThresholdControlComponent::valueToNormalized(0.0f, ThresholdScale::Unipolar), 0.0f);
    EXPECT_FLOAT_EQ(ThresholdControlComponent::valueToNormalized(1.0f, ThresholdScale::Unipolar), 1.0f);
    EXPECT_FLOAT_EQ(ThresholdControlComponent::valueToX(0.5f, 0.0f, 100.0f, ThresholdScale::Unipolar), 50.0f);
}

TEST(ThresholdControlTest, DecibelsMapsFloorToLeftAndZeroToRight) {
    EXPECT_FLOAT_EQ(ThresholdControlComponent::valueToNormalized(-60.0f, ThresholdScale::Decibels, -60.0f), 0.0f);
    EXPECT_FLOAT_EQ(ThresholdControlComponent::valueToNormalized(0.0f, ThresholdScale::Decibels, -60.0f), 1.0f);
    EXPECT_FLOAT_EQ(ThresholdControlComponent::valueToNormalized(-30.0f, ThresholdScale::Decibels, -60.0f), 0.5f);
}

TEST(ThresholdControlTest, PreferredHeightDependsOnSliderMode) {
    EXPECT_EQ(ThresholdControlComponent::getMeterOnlyHeight(), 18);
    EXPECT_GT(ThresholdControlComponent::getSliderModeHeight(), ThresholdControlComponent::getMeterOnlyHeight());
}

TEST(TriggerMeterTest, NeedsRepaintIsFalseWhenNothingChanged) {
    EXPECT_FALSE(TriggerMeterComponent::needsRepaint(0.5f, 0.5f, 0.5f, 0.5f, false, false, 3, 3, 0))
        << "An idle meter must not repaint — that would invalidate the parent's cached image";
}

TEST(TriggerMeterTest, NeedsRepaintIgnoresSubVisualLevelJitter) {
    // A steady signal jitters slightly block to block; that must not cause a repaint.
    EXPECT_FALSE(TriggerMeterComponent::needsRepaint(0.500f, 0.502f, 0.5f, 0.5f, false, false, 1, 1, 0));
}

TEST(TriggerMeterTest, NeedsRepaintOnMeaningfulChanges) {
    // Level moved visibly.
    EXPECT_TRUE(TriggerMeterComponent::needsRepaint(0.1f, 0.9f, 0.5f, 0.5f, false, false, 1, 1, 0));
    // Threshold moved.
    EXPECT_TRUE(TriggerMeterComponent::needsRepaint(0.5f, 0.5f, 0.2f, 0.8f, false, false, 1, 1, 0));
    // Armed state flipped.
    EXPECT_TRUE(TriggerMeterComponent::needsRepaint(0.5f, 0.5f, 0.5f, 0.5f, false, true, 1, 1, 0));
    // A capture happened.
    EXPECT_TRUE(TriggerMeterComponent::needsRepaint(0.5f, 0.5f, 0.5f, 0.5f, false, false, 1, 2, 0));
    // Flash still decaying.
    EXPECT_TRUE(TriggerMeterComponent::needsRepaint(0.5f, 0.5f, 0.5f, 0.5f, false, false, 1, 1, 2));
}

TEST(TriggerMeterTest, PaintDoesNotCrashAndDrawsSomething) {
    SampleHoldModule m;
    m.prepareToPlay(kSampleRate, kBlockSize);
    TriggerMeterComponent meter(m);
    meter.setSize(260, 18);

    juce::Image img(juce::Image::ARGB, 260, 18, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(meter.paint(g));

    bool hasPixel = false;
    for (int y = 0; y < img.getHeight() && !hasPixel; ++y)
        for (int x = 0; x < img.getWidth() && !hasPixel; ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                hasPixel = true;
    EXPECT_TRUE(hasPixel) << "paint() should produce visible pixels";
}

TEST_F(SampleHoldModuleTest, StateSerializationRoundTrips) {
    setParam(*module, "level", 0.25f);
    setParam(*module, "offset", 0.8f);
    useTrackMode(*module);

    juce::MemoryBlock state;
    module->getStateInformation(state);

    SampleHoldModule restored;
    restored.setStateInformation(state.getData(), (int)state.getSize());

    auto valueOf = [](SampleHoldModule& m, const juce::String& id) {
        for (auto* p : m.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                if (withId->paramID == id)
                    return withId->getValue();
        return -1.0f;
    };

    EXPECT_NEAR(valueOf(restored, "level"), 0.25f, 1e-4f);
    EXPECT_NEAR(valueOf(restored, "offset"), 0.8f, 1e-4f);
    EXPECT_NEAR(valueOf(restored, "holdMode"), 1.0f, 1e-4f);
}

} // namespace
