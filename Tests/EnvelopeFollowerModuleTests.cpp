#include "../Source/Modules/EnvelopeFollowerModule.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

// Parameter indices — order matches the constructor (bypassed comes from ModuleBase).
enum ParamIndex { kBypassed = 0, kAttack, kRelease, kSensitivity, kDetection, kMuted };

class EnvelopeFollowerModuleTest : public ::testing::Test {
protected:
    std::unique_ptr<EnvelopeFollowerModule> module;
    juce::MidiBuffer midi;

    void SetUp() override {
        module = std::make_unique<EnvelopeFollowerModule>();
        module->prepareToPlay(kSampleRate, kBlockSize);
    }

    juce::AudioParameterFloat* floatParam(int index) {
        return dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[index]);
    }

    void setFloat(int index, float plainValue) {
        auto* p = floatParam(index);
        ASSERT_NE(p, nullptr);
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(plainValue));
    }

    // The module writes its Env CV into channel 0 — the same channel it reads audio from —
    // so every block has to be re-filled, exactly as the graph re-fills node inputs.
    static void fillDC(juce::AudioBuffer<float>& buffer, float value) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(0, i, value);
    }

    static void fillSine(juce::AudioBuffer<float>& buffer, float amplitude, float freq, int& phaseSample) {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float t = static_cast<float>(phaseSample++) / static_cast<float>(kSampleRate);
            buffer.setSample(0, i, amplitude * std::sin(juce::MathConstants<float>::twoPi * freq * t));
        }
    }

    /** Runs `blocks` blocks of DC and returns the final envelope sample. */
    float runDC(float value, int blocks, float cvOnChannel = 0.0f, int cvChannel = -1) {
        juce::AudioBuffer<float> buffer(4, kBlockSize);
        float last = 0.0f;
        for (int b = 0; b < blocks; ++b) {
            fillDC(buffer, value);
            if (cvChannel > 0)
                for (int i = 0; i < kBlockSize; ++i)
                    buffer.setSample(cvChannel, i, cvOnChannel);
            module->processBlock(buffer, midi);
            last = buffer.getSample(0, kBlockSize - 1);
        }
        return last;
    }
};

// ============================================================================
// Construction and parameters
// ============================================================================

TEST_F(EnvelopeFollowerModuleTest, FactoryInitialisation) {
    EXPECT_EQ(module->getModuleType(), ModuleType::EnvelopeFollower);
    EXPECT_EQ(module->getName(), "Envelope Follower");
    // Bypassed, Attack, Release, Sensitivity, Detection, Muted
    EXPECT_EQ(module->getParameters().size(), 6);
}

TEST_F(EnvelopeFollowerModuleTest, InitialParameterValues) {
    EXPECT_FLOAT_EQ(floatParam(kAttack)->get(), 10.0f);
    EXPECT_FLOAT_EQ(floatParam(kRelease)->get(), 150.0f);
    EXPECT_FLOAT_EQ(floatParam(kSensitivity)->get(), 0.5f);

    auto* detection = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[kDetection]);
    ASSERT_NE(detection, nullptr);
    EXPECT_EQ(detection->getIndex(), 0); // Peak
    EXPECT_EQ(detection->choices[0], "Peak");
    EXPECT_EQ(detection->choices[1], "RMS");
}

TEST_F(EnvelopeFollowerModuleTest, DefaultSensitivityIsUnityGain) {
    EXPECT_FLOAT_EQ(EnvelopeFollowerModule::sensitivityToGain(0.5f), 1.0f);
    EXPECT_FLOAT_EQ(EnvelopeFollowerModule::sensitivityToGain(0.0f), 0.25f);
    EXPECT_FLOAT_EQ(EnvelopeFollowerModule::sensitivityToGain(1.0f), 4.0f);
    // Out-of-range input is clamped, not extrapolated.
    EXPECT_FLOAT_EQ(EnvelopeFollowerModule::sensitivityToGain(-5.0f), 0.25f);
    EXPECT_FLOAT_EQ(EnvelopeFollowerModule::sensitivityToGain(5.0f), 4.0f);
}

TEST_F(EnvelopeFollowerModuleTest, VisualBufferIsEnabled) { EXPECT_NE(module->getVisualBuffer(), nullptr); }

// ============================================================================
// Port topology
// ============================================================================

TEST_F(EnvelopeFollowerModuleTest, PortLabelsAndCounts) {
    EXPECT_EQ(module->getVisibleInputPortCount(), 4);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 1);
    EXPECT_EQ(module->getInputPortLabel(0), "Audio");
    EXPECT_EQ(module->getInputPortLabel(1), "Attack");
    EXPECT_EQ(module->getInputPortLabel(2), "Release");
    EXPECT_EQ(module->getInputPortLabel(3), "Sensitivity");
    EXPECT_EQ(module->getOutputPortLabel(0), "Env");
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::Envelope);
}

TEST_F(EnvelopeFollowerModuleTest, AudioInputIsAudioRoleAndCVInputsAreModCV) {
    EXPECT_EQ(module->mapInputChannel(0).role, PortRole::Audio);
    for (int raw = 1; raw <= 3; ++raw) {
        EXPECT_EQ(module->mapInputChannel(raw).role, PortRole::ModCV) << "raw channel " << raw;
        EXPECT_EQ(module->mapInputChannel(raw).visibleJackIndex, raw);
    }
    // The Env output is CV, not audio — this is what keeps it out of audio-chain handling.
    EXPECT_EQ(module->mapOutputChannel(0).role, PortRole::ModCV);
}

TEST_F(EnvelopeFollowerModuleTest, ModulationTargetsMatchCVChannels) {
    auto targets = module->getModulationTargets();
    ASSERT_EQ(targets.size(), 3u);
    EXPECT_EQ(targets[0].name, "Attack");
    EXPECT_EQ(targets[0].channelIndex, 1);
    EXPECT_EQ(targets[1].name, "Release");
    EXPECT_EQ(targets[1].channelIndex, 2);
    EXPECT_EQ(targets[2].name, "Sensitivity");
    EXPECT_EQ(targets[2].channelIndex, 3);

    // Declaring >= 4 outputs is what stops JUCE aliasing CV input ch3 onto another
    // node's output buffer.
    EXPECT_GE(module->getTotalNumOutputChannels(), 4);
}

// ============================================================================
// Detection behaviour
// ============================================================================

TEST_F(EnvelopeFollowerModuleTest, SilenceProducesZeroEnvelope) {
    EXPECT_NEAR(runDC(0.0f, 5), 0.0f, 1e-6f);
    EXPECT_NEAR(module->getCurrentEnvelope(), 0.0f, 1e-6f);
}

TEST_F(EnvelopeFollowerModuleTest, EnvelopeConvergesToConstantAmplitude) {
    // Default sensitivity is unity gain, so a DC of 0.8 must settle at 0.8.
    EXPECT_NEAR(runDC(0.8f, 30), 0.8f, 0.01f);
    EXPECT_NEAR(module->getCurrentEnvelope(), 0.8f, 0.01f);
}

TEST_F(EnvelopeFollowerModuleTest, EnvelopeRisesMonotonicallyWithinFirstBlock) {
    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillDC(buffer, 1.0f);
    module->processBlock(buffer, midi);

    const float first = buffer.getSample(0, 0);
    const float mid = buffer.getSample(0, kBlockSize / 2);
    const float last = buffer.getSample(0, kBlockSize - 1);
    EXPECT_LT(first, mid);
    EXPECT_LT(mid, last);
    EXPECT_GT(first, 0.0f);
}

TEST_F(EnvelopeFollowerModuleTest, EnvelopeDecaysAfterInputStops) {
    const float peak = runDC(1.0f, 30);
    ASSERT_GT(peak, 0.9f);

    const float afterSilence = runDC(0.0f, 2);
    EXPECT_LT(afterSilence, peak);
    EXPECT_GT(afterSilence, 0.0f) << "a 150 ms release must not collapse to zero instantly";

    // Long silence eventually drains it.
    EXPECT_NEAR(runDC(0.0f, 60), 0.0f, 0.01f);
}

TEST_F(EnvelopeFollowerModuleTest, FasterAttackRisesQuickerThanSlowerAttack) {
    setFloat(kAttack, 1.0f);
    const float fast = runDC(1.0f, 1);

    auto slow = std::make_unique<EnvelopeFollowerModule>();
    slow->prepareToPlay(kSampleRate, kBlockSize);
    auto* slowAttack = dynamic_cast<juce::AudioParameterFloat*>(slow->getParameters()[kAttack]);
    slowAttack->setValueNotifyingHost(slowAttack->getNormalisableRange().convertTo0to1(200.0f));

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillDC(buffer, 1.0f);
    slow->processBlock(buffer, midi);
    const float slowValue = buffer.getSample(0, kBlockSize - 1);

    EXPECT_GT(fast, slowValue);
}

TEST_F(EnvelopeFollowerModuleTest, PeakModeTracksSinePeak) {
    setFloat(kAttack, 1.0f);
    setFloat(kRelease, 500.0f);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    int phase = 0;
    for (int b = 0; b < 40; ++b) {
        fillSine(buffer, 1.0f, 1000.0f, phase);
        module->processBlock(buffer, midi);
    }
    // A fast-attack / slow-release peak follower sits just under the true peak.
    EXPECT_GT(buffer.getSample(0, kBlockSize - 1), 0.85f);
    EXPECT_LE(buffer.getSample(0, kBlockSize - 1), 1.0f);
}

TEST_F(EnvelopeFollowerModuleTest, RMSModeOfUnitSineIsAboutMinus3dB) {
    auto* detection = dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[kDetection]);
    detection->setValueNotifyingHost(1.0f); // RMS
    ASSERT_EQ(detection->getIndex(), 1);

    // Symmetric, slow time constants so the squared signal is averaged rather than tracked.
    setFloat(kAttack, 200.0f);
    setFloat(kRelease, 200.0f);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    int phase = 0;
    for (int b = 0; b < 200; ++b) {
        fillSine(buffer, 1.0f, 1000.0f, phase);
        module->processBlock(buffer, midi);
    }
    // RMS of a unit sine is 1/sqrt(2).
    EXPECT_NEAR(buffer.getSample(0, kBlockSize - 1), 0.7071f, 0.03f);
}

TEST_F(EnvelopeFollowerModuleTest, OutputIsUnipolarForBipolarInput) {
    juce::AudioBuffer<float> buffer(4, kBlockSize);
    int phase = 0;
    for (int b = 0; b < 10; ++b) {
        fillSine(buffer, 1.0f, 220.0f, phase);
        module->processBlock(buffer, midi);
        for (int i = 0; i < kBlockSize; ++i) {
            const float v = buffer.getSample(0, i);
            EXPECT_GE(v, 0.0f);
            EXPECT_LE(v, 1.0f);
        }
    }
}

TEST_F(EnvelopeFollowerModuleTest, OutputIsClampedToUnityAtHighSensitivity) {
    setFloat(kSensitivity, 1.0f); // 4x gain
    EXPECT_FLOAT_EQ(runDC(0.5f, 30), 1.0f);
}

// ============================================================================
// CV modulation
// ============================================================================

TEST_F(EnvelopeFollowerModuleTest, AttackCVSlowsTheRise) {
    const float noCV = runDC(1.0f, 1);

    auto modulated = std::make_unique<EnvelopeFollowerModule>();
    modulated->prepareToPlay(kSampleRate, kBlockSize);
    juce::AudioBuffer<float> buffer(4, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, 1.0f); // Attack CV at full scale -> slowest attack
    }
    modulated->processBlock(buffer, midi);

    EXPECT_LT(buffer.getSample(0, kBlockSize - 1), noCV);
}

TEST_F(EnvelopeFollowerModuleTest, SensitivityCVScalesTheOutput) {
    // +0.25 CV on top of the 0.5 default lands on 0.75 -> 2x gain.
    const float withCV = runDC(0.3f, 30, 0.25f, 3);
    EXPECT_NEAR(withCV, 0.6f, 0.02f);
}

TEST_F(EnvelopeFollowerModuleTest, ReleaseCVLengthensTheDecay) {
    setFloat(kRelease, 1.0f); // fastest release
    runDC(1.0f, 30);
    const float fastDecay = runDC(0.0f, 1);

    auto slow = std::make_unique<EnvelopeFollowerModule>();
    slow->prepareToPlay(kSampleRate, kBlockSize);
    auto* slowRelease = dynamic_cast<juce::AudioParameterFloat*>(slow->getParameters()[kRelease]);
    slowRelease->setValueNotifyingHost(slowRelease->getNormalisableRange().convertTo0to1(1.0f));

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    for (int b = 0; b < 30; ++b) {
        fillDC(buffer, 1.0f);
        slow->processBlock(buffer, midi);
    }
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(2, i, 1.0f); // Release CV at full scale -> longest release
    slow->processBlock(buffer, midi);

    EXPECT_GT(buffer.getSample(0, kBlockSize - 1), fastDecay);
}

TEST_F(EnvelopeFollowerModuleTest, CVInputChannelsAreClearedOnOutput) {
    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillDC(buffer, 0.5f);
    for (int ch = 1; ch <= 3; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(ch, i, 0.4f);

    module->processBlock(buffer, midi);

    for (int ch = 1; ch <= 3; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "channel " << ch << " sample " << i;
}

// ============================================================================
// Bypass / mute
// ============================================================================

// This module has an audio input but no audio output, so — like the pure-source modules —
// bypass emits no CV rather than passing the dry signal into a CV destination.
TEST_F(EnvelopeFollowerModuleTest, BypassEmitsNoEnvelopeInsteadOfPassingAudioThrough) {
    module->setBypassed(true);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillDC(buffer, 0.9f);
    module->processBlock(buffer, midi);

    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.0f) << "sample " << i;
    EXPECT_FLOAT_EQ(module->getCurrentEnvelope(), 0.0f);
}

TEST_F(EnvelopeFollowerModuleTest, MuteEmitsNoEnvelope) {
    module->setMuted(true);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillDC(buffer, 0.9f);
    module->processBlock(buffer, midi);

    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.0f) << "sample " << i;
}

TEST_F(EnvelopeFollowerModuleTest, BypassResetsStateSoReenablingStartsFromZero) {
    runDC(1.0f, 30);
    module->setBypassed(true);
    runDC(1.0f, 1);
    module->setBypassed(false);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillDC(buffer, 1.0f);
    module->processBlock(buffer, midi);
    EXPECT_LT(buffer.getSample(0, 0), 0.2f) << "detector should restart from silence, not resume at the old level";
}

// ============================================================================
// Robustness
// ============================================================================

TEST_F(EnvelopeFollowerModuleTest, ZeroChannelsDoesNotCrash) {
    juce::AudioBuffer<float> empty(0, kBlockSize);
    EXPECT_NO_THROW(module->processBlock(empty, midi));
}

TEST_F(EnvelopeFollowerModuleTest, ZeroSamplesDoesNotCrash) {
    juce::AudioBuffer<float> empty(4, 0);
    EXPECT_NO_THROW(module->processBlock(empty, midi));
}

TEST_F(EnvelopeFollowerModuleTest, FewerChannelsThanDeclaredDoesNotCrash) {
    juce::AudioBuffer<float> mono(1, kBlockSize);
    fillDC(mono, 0.7f);
    EXPECT_NO_THROW(module->processBlock(mono, midi));
    EXPECT_GT(mono.getSample(0, kBlockSize - 1), 0.0f);
}

TEST_F(EnvelopeFollowerModuleTest, ZeroSampleRateFallsBackInsteadOfDividingByZero) {
    module->prepareToPlay(0.0, kBlockSize);
    juce::AudioBuffer<float> buffer(4, kBlockSize);
    fillDC(buffer, 0.5f);
    module->processBlock(buffer, midi);
    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_TRUE(std::isfinite(buffer.getSample(0, i)));
}

TEST_F(EnvelopeFollowerModuleTest, StateRoundTripsThroughGetSetStateInformation) {
    setFloat(kAttack, 42.0f);
    setFloat(kSensitivity, 0.8f);

    juce::MemoryBlock state;
    module->getStateInformation(state);

    auto restored = std::make_unique<EnvelopeFollowerModule>();
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    auto* attack = dynamic_cast<juce::AudioParameterFloat*>(restored->getParameters()[kAttack]);
    auto* sensitivity = dynamic_cast<juce::AudioParameterFloat*>(restored->getParameters()[kSensitivity]);
    EXPECT_NEAR(attack->get(), 42.0f, 0.1f);
    EXPECT_NEAR(sensitivity->get(), 0.8f, 0.01f);
}

} // namespace
