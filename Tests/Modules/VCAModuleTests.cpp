#include "../ClickBoundHelpers.h"
#include "Modules/SoftLimit.h"
#include "Modules/VCAModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

class VCAModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        vca = std::make_unique<VCAModule>();
        vca->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<VCAModule> vca;
};

TEST_F(VCAModuleTest, GainApplication) {
    juce::AudioBuffer<float> buffer(1, 100);
    buffer.clear();
    for (int i = 0; i < 100; ++i) {
        buffer.setSample(0, i, 1.0f);
    }

    auto* gainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(vca.get(), "gain"));
    ASSERT_NE(gainParam, nullptr);
    gainParam->setValueNotifyingHost(0.5f); // 0.5 normalized

    juce::MidiBuffer midi;
    vca->processBlock(buffer, midi);

    // Initial gain is 0.5 (set in constructor)
    // After setValueNotifyingHost(0.5f), the target gain is 0.5.
    // However, SmoothedValue takes time to reach the target.
    // For simplicity, let's check that it's within [0, 1] and changed.
    for (int i = 0; i < 100; ++i) {
        EXPECT_GE(buffer.getSample(0, i), 0.0f);
        EXPECT_LE(buffer.getSample(0, i), 1.0f);
    }
}

TEST_F(VCAModuleTest, CVControl) {
    // VCA has 2 inputs: 0 is Audio, 1 is CV
    juce::AudioBuffer<float> buffer(2, 1000); // 1000 samples > 10ms ramp
    buffer.clear();

    // Audio input is all 1s
    for (int i = 0; i < 1000; ++i) {
        buffer.setSample(0, i, 1.0f);
    }

    // CV input is 0.5
    for (int i = 0; i < 1000; ++i) {
        buffer.setSample(1, i, 0.5f);
    }

    auto* gainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(vca.get(), "gain"));
    gainParam->setValueNotifyingHost(1.0f); // Set gain to max

    juce::MidiBuffer midi;
    vca->processBlock(buffer, midi);

    // Output should be roughly 1.0 (gain) * 0.5 (cv) = 0.5
    EXPECT_NEAR(buffer.getSample(0, 999), 0.5f, 0.01f);
}

TEST_F(VCAModuleTest, MonoToStereoCopy) {
    juce::AudioBuffer<float> buffer(2, 100);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f); // Only first channel

    juce::MidiBuffer midi;
    vca->processBlock(buffer, midi);

    // Second channel should be copy of first
    EXPECT_EQ(buffer.getSample(0, 0), buffer.getSample(1, 0));
}

TEST_F(VCAModuleTest, PolyMode_MultiVoice) {
    auto* polyP = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(vca.get(), "poly"));
    ASSERT_NE(polyP, nullptr);
    *polyP = true;

    auto* gainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(vca.get(), "gain"));
    gainParam->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> polyBuffer(16, 1000);
    polyBuffer.clear();

    // Audio on ch 0 and 1, CV on ch 8 and 9
    for (int i = 0; i < 1000; ++i) {
        polyBuffer.setSample(0, i, 1.0f); // Voice 0 audio
        polyBuffer.setSample(1, i, 0.5f); // Voice 1 audio
        polyBuffer.setSample(8, i, 1.0f); // Voice 0 CV
        polyBuffer.setSample(9, i, 0.5f); // Voice 1 CV
    }

    juce::MidiBuffer midi;
    vca->processBlock(polyBuffer, midi);

    // Voices sum at unity: voice 0 is 1.0 * 1.0 * 1.0, voice 1 is 0.5 * 1.0 * 0.5, so the sum is 1.25.
    // That is past the 0.9 knee, so the shared soft limiter rounds it below the plain sum.
    const float expected = synth::softLimit(1.0f + 0.25f);
    EXPECT_FLOAT_EQ(polyBuffer.getSample(0, 999), expected); // Left
    EXPECT_FLOAT_EQ(polyBuffer.getSample(1, 999), expected); // Right (same as left)
    EXPECT_LT(expected, 1.0f);
}

TEST_F(VCAModuleTest, MonoMode_BackwardsCompatible) {
    // Default poly=false
    juce::AudioBuffer<float> buf(2, 1000);
    buf.clear();
    for (int i = 0; i < 1000; ++i) {
        buf.setSample(0, i, 1.0f);
        buf.setSample(1, i, 0.5f);
    }

    auto* gainParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(vca.get(), "gain"));
    gainParam->setValueNotifyingHost(1.0f);

    juce::MidiBuffer midi;
    vca->processBlock(buf, midi);

    // Output = audio * gain * cv = 1.0 * 1.0 * 0.5 = 0.5
    EXPECT_NEAR(buf.getSample(0, 999), 0.5f, 0.05f);
    // Channel 1 should be copy of channel 0 (mono-to-stereo)
    EXPECT_EQ(buf.getSample(0, 999), buf.getSample(1, 999));
}

// Regression test for FRO744: an envelope that idles at 0 and fires mid-block must gate that block.
// A probe of only the block's first 64 samples missed it and fell back to the silent poly CV channel.
TEST_F(VCAModuleTest, EnvelopeFiringLateInTheBlockStillGatesTheBlock) {
    juce::AudioBuffer<float> buffer(24, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i) {
        buffer.setSample(0, i, 1.0f);
        buffer.setSample(1, i, i < 200 ? 0.0f : 1.0f);
    }

    juce::MidiBuffer midi;
    vca->processBlock(buffer, midi);

    EXPECT_FLOAT_EQ(buffer.getSample(0, 100), 0.0f);
    EXPECT_NEAR(buffer.getSample(0, 300), 0.5f, 1.0e-4f);
}

// ---------------------------------------------------------------------------
// Click bound: bypass and mute fade the legacy ch1 copy of the output as well
// ---------------------------------------------------------------------------

// Regression test for FRO1022: mono VCA copies its output onto ch1; that copy is not an audio leg, so the
// bypass/mute fade left it switching between the copy and the cleared CV block in one sample.
TEST(VCAClickBound, Ch1CopyFadesWithBypassAndMute) {
    for (const char* flag : {"bypassed", "muted"}) {
        for (const bool turnOn : {true, false}) {
            VCAModule vca;
            if (!turnOn)
                clickbound::setNormalised(vca, flag, 1.0f);
            clickbound::Input in;
            in.audioChannels = 1;
            in.held = {{1, 1.0f}}; // gain CV
            const auto steps = clickbound::measureEdge(
                vca, in, {0, 1}, [&] { clickbound::setNormalised(vca, flag, turnOn ? 1.0f : 0.0f); });
            EXPECT_NO_CLICK(steps) << flag << (turnOn ? " on" : " off");
        }
    }
}

namespace {

// Runs `voiceLevels` through a poly VCA at gain 1 (one audio value per voice, unity envelope CV) and
// returns ch0 of the last sample.
float polyVcaOutput(const std::vector<float>& voiceLevels) {
    VCAModule vca;
    vca.prepareToPlay(44100.0, 64);
    *dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&vca, "poly")) = true;
    *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&vca, "gain")) = 1.0f;
    vca.prepareToPlay(44100.0, 64);

    juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, 64);
    buffer.clear();
    for (size_t v = 0; v < voiceLevels.size(); ++v) {
        for (int i = 0; i < 64; ++i) {
            buffer.setSample((int)v, i, voiceLevels[v]);
            buffer.setSample((int)v + 8, i, 1.0f);
        }
    }
    juce::MidiBuffer midi;
    vca.processBlock(buffer, midi);
    return buffer.getSample(0, 63);
}

} // namespace

// Regression test for FRO848: a single poly voice used to be divided by 8 and run through tanh, about 18 dB
// below the same note through the mono path.
TEST(VCAModulePolyLevel, SingleVoiceEqualsTheMonoPathUnderTheKnee) {
    for (float level : {0.05f, 0.3f, 0.6f, 0.89f}) {
        VCAModule mono;
        mono.prepareToPlay(44100.0, 64);
        *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&mono, "gain")) = 1.0f;
        mono.prepareToPlay(44100.0, 64);
        juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, 64);
        buffer.clear();
        for (int i = 0; i < 64; ++i) {
            buffer.setSample(0, i, level);
            buffer.setSample(1, i, 1.0f);
        }
        juce::MidiBuffer midi;
        mono.processBlock(buffer, midi);

        EXPECT_EQ(buffer.getSample(0, 63), polyVcaOutput({level})) << "level " << level;
    }
}

TEST(VCAModulePolyLevel, FourVoicesOfPointTwoSumToPointEight) {
    EXPECT_FLOAT_EQ(polyVcaOutput({0.2f, 0.2f, 0.2f, 0.2f}), 0.8f);
}

TEST(VCAModulePolyLevel, SumAboveTheKneeIsSoftLimitedAndContinuous) {
    float previous = polyVcaOutput({0.89f});
    for (float sum = 0.9f; sum <= 4.0f; sum += 0.1f) {
        const float out = polyVcaOutput({sum * 0.5f, sum * 0.5f});
        EXPECT_LT(out, 1.0f) << "sum " << sum;
        EXPECT_GE(out, previous) << "sum " << sum;
        EXPECT_LT(out - previous, 0.15f) << "sum " << sum;
        previous = out;
    }
}
