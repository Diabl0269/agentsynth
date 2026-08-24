#include "../Source/Modules/FX/RingModulatorModule.h"
#include "../Source/Modules/ModuleBase.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

juce::RangedAudioParameter* paramByID(juce::AudioProcessor& module, const juce::String& id) {
    return findParameterByID(&module, id);
}

void setFloat(juce::AudioProcessor& module, const juce::String& id, float value) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(paramByID(module, id));
    ASSERT_NE(p, nullptr) << "no float parameter named " << id;
    *p = value;
}

void setBool(juce::AudioProcessor& module, const juce::String& id, bool value) {
    auto* p = dynamic_cast<juce::AudioParameterBool*>(paramByID(module, id));
    ASSERT_NE(p, nullptr) << "no bool parameter named " << id;
    p->setValueNotifyingHost(value ? 1.0f : 0.0f);
}

void setChoice(juce::AudioProcessor& module, const juce::String& id, int index) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(paramByID(module, id));
    ASSERT_NE(p, nullptr) << "no choice parameter named " << id;
    ASSERT_GT(p->choices.size(), 1);
    p->setValueNotifyingHost((float)index / (float)(p->choices.size() - 1));
}

void fillSines(juce::AudioBuffer<float>& buffer, float carrierHz, float modulatorHz, double sampleRate,
               float amplitude = 0.8f) {
    const int n = buffer.getNumSamples();
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        buffer.setSample(0, i, amplitude * std::sin(juce::MathConstants<float>::twoPi * carrierHz * t));
        buffer.setSample(1, i, amplitude * std::sin(juce::MathConstants<float>::twoPi * modulatorHz * t));
    }
}

float magnitudeAt(const juce::AudioBuffer<float>& buffer, int channel, float hz, double sampleRate, int start = 0) {
    const float* data = buffer.getReadPointer(channel);
    const int n = buffer.getNumSamples() - start;
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i) {
        const double w = 2.0 * juce::MathConstants<double>::pi * (double)hz * (double)i / sampleRate;
        re += data[start + i] * std::cos(w);
        im -= data[start + i] * std::sin(w);
    }
    return (float)(std::sqrt(re * re + im * im) * 2.0 / (double)n);
}

juce::AudioBuffer<float> render(RingModulatorModule& module, float carrierHz, float modulatorHz, int totalSamples) {
    juce::AudioBuffer<float> out(4, totalSamples);
    out.clear();
    juce::AudioBuffer<float> block(4, kBlockSize);
    juce::MidiBuffer midi;
    int written = 0;
    int global = 0;
    while (written < totalSamples) {
        const int n = std::min(kBlockSize, totalSamples - written);
        block.clear();
        for (int i = 0; i < n; ++i, ++global) {
            const float t = static_cast<float>(global) / static_cast<float>(kSampleRate);
            block.setSample(0, i, 0.8f * std::sin(juce::MathConstants<float>::twoPi * carrierHz * t));
            block.setSample(1, i, 0.8f * std::sin(juce::MathConstants<float>::twoPi * modulatorHz * t));
        }
        module.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, written, block, ch, 0, n);
        written += n;
    }
    return out;
}

} // namespace

class RingModulatorModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<RingModulatorModule>();
        module->prepareToPlay(kSampleRate, kBlockSize);
    }

    std::unique_ptr<RingModulatorModule> module;
};

TEST_F(RingModulatorModuleTest, ModuleTypeAndCategoryAreCorrect) {
    EXPECT_EQ(module->getModuleType(), ModuleType::RingModulator);
    EXPECT_EQ(module->getModulationCategory(), ModulationCategory::FX);
    EXPECT_EQ(module->getName(), "Ring Modulator");
}

TEST_F(RingModulatorModuleTest, PortLabelsAndModulationTargets) {
    EXPECT_EQ(module->getInputPortLabel(0), "Carrier");
    EXPECT_EQ(module->getInputPortLabel(1), "Modulator");
    EXPECT_EQ(module->getInputPortLabel(2), "Mix");
    EXPECT_EQ(module->getInputPortLabel(3), "Drive");
    EXPECT_EQ(module->getInputPortLabel(4), "Character");
    // Dual I/O defaults OFF, like every other FX: one "Audio" output jack owning both raw legs.
    EXPECT_EQ(module->getOutputPortLabel(0), "Audio");

    auto targets = module->getModulationTargets();
    ASSERT_EQ(targets.size(), 3u);
    EXPECT_EQ(targets[0].name, "Mix");
    EXPECT_EQ(targets[0].channelIndex, 2);
    EXPECT_EQ(targets[1].name, "Drive");
    EXPECT_EQ(targets[1].channelIndex, 3);
    EXPECT_EQ(targets[2].name, "Character");
    EXPECT_EQ(targets[2].channelIndex, 4);
}

// ---------------------------------------------------------------------------
// Dual I/O (the module was missing the toggle entirely, and therefore missing from the global
// "Split Left/Right jacks" preference and from the per-module defaults popup)
// ---------------------------------------------------------------------------

TEST_F(RingModulatorModuleTest, DualIOSplitsTheOutputPairAndNothingElse) {
    ASSERT_TRUE(module->hasDualIOParameter()) << "the Ring Modulator is a stereo-out FX; it carries the toggle";
    EXPECT_FALSE(module->isDualIO()) << "collapsed by default, like every other FX";
    EXPECT_FALSE(module->hasSplitBlockStereo()) << "the right leg is the contiguous ch1, not a kRightBase block";

    // Collapsed: one Audio jack that owns raw ch0 AND ch1.
    EXPECT_EQ(module->getVisibleOutputPortCount(), 1);
    EXPECT_EQ(module->getOutputPortLabel(0), "Audio");
    const auto collapsedHead = module->mapOutputChannel(0);
    EXPECT_TRUE(collapsedHead.isPolyGroupHead);
    EXPECT_EQ(collapsedHead.polyVoiceSpan, 2);
    EXPECT_EQ(collapsedHead.role, PortRole::Audio);
    EXPECT_FALSE(module->mapOutputChannel(1).isPolyGroupHead);
    ASSERT_EQ(module->getJackTargets(0, /*isInput=*/false).size(), 1u);
    EXPECT_EQ(module->getJackTargets(0, false)[0].voiceSpan, 2);

    // The five INPUT jacks are untouched in both states — Carrier and Modulator are two unrelated
    // mono inputs, not a stereo pair, so splitting the output must not relabel or remap them.
    for (bool dual : {false, true}) {
        setBool(*module, "dualIO", dual);
        EXPECT_EQ(module->getVisibleInputPortCount(), 5) << "dual=" << dual;
        EXPECT_EQ(module->getInputPortLabel(0), "Carrier") << "dual=" << dual;
        EXPECT_EQ(module->getInputPortLabel(1), "Modulator") << "dual=" << dual;
        EXPECT_NE(module->mapInputChannel(1).role, PortRole::Audio)
            << "the Modulator input must never look like an audio right leg, or the Dual I/O toggle "
               "would wire a neighbour's Audio R into it";
        auto targets = module->getModulationTargets();
        ASSERT_EQ(targets.size(), 3u) << "dual=" << dual;
        EXPECT_EQ(targets[0].channelIndex, 2) << "CV raw channels must not move, dual=" << dual;
    }

    setBool(*module, "dualIO", true);
    EXPECT_EQ(module->getVisibleOutputPortCount(), 2);
    EXPECT_EQ(module->getOutputPortLabel(0), "Left");
    EXPECT_EQ(module->getOutputPortLabel(1), "Right");
    EXPECT_TRUE(module->mapOutputChannel(1).isPolyGroupHead);
    EXPECT_EQ(module->mapOutputChannel(1).visibleJackIndex, 1);
    EXPECT_EQ(module->rightAudioLegChannel(), 1);
}

TEST_F(RingModulatorModuleTest, DualIODoesNotChangeWhatItRenders) {
    // A layout toggle, not a mono/stereo switch: both legs already carry the same wet signal, so
    // collapsing must be sample-identical on both channels.
    auto renderWith = [](bool dual) {
        RingModulatorModule m;
        setBool(m, "dualIO", dual);
        setChoice(m, "oversampling", 0);
        m.prepareToPlay(kSampleRate, kBlockSize);
        return render(m, 440.0f, 220.0f, kBlockSize * 2);
    };

    const auto split = renderWith(true);
    const auto collapsed = renderWith(false);

    float maxDiff = 0.0f;
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < split.getNumSamples(); ++i) {
            maxDiff = std::max(maxDiff, std::abs(split.getSample(ch, i) - collapsed.getSample(ch, i)));
            peak = std::max(peak, std::abs(split.getSample(ch, i)));
        }
    ASSERT_GT(peak, 0.01f) << "nothing was rendered, so nothing is proven";
    EXPECT_LT(maxDiff, 1.0e-7f) << "the Dual I/O toggle must not touch the DSP";

    // ...and the two legs are equal, which is what makes the collapsed single jack honest.
    for (int i = 0; i < collapsed.getNumSamples(); ++i)
        ASSERT_NEAR(collapsed.getSample(0, i), collapsed.getSample(1, i), 1.0e-7f) << "sample " << i;
}

TEST_F(RingModulatorModuleTest, BypassAndMuteAreUnaffectedByDualIO) {
    // The bypass/mute contract, in both jack layouts: bypass passes the two audio channels dry and
    // clears ONLY the CV block; mute clears everything.
    for (bool dual : {false, true}) {
        SCOPED_TRACE(dual ? "dual" : "collapsed");

        RingModulatorModule bypassed;
        setBool(bypassed, "dualIO", dual);
        bypassed.prepareToPlay(kSampleRate, kBlockSize);
        bypassed.setBypassed(true);

        juce::AudioBuffer<float> buffer(5, kBlockSize);
        buffer.clear();
        fillSines(buffer, 440.0f, 220.0f, kSampleRate);
        for (int ch = 2; ch < 5; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                buffer.setSample(ch, i, 0.5f);
        const juce::AudioBuffer<float> dry(buffer);

        juce::MidiBuffer midi;
        bypassed.processBlock(buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                ASSERT_FLOAT_EQ(buffer.getSample(ch, i), dry.getSample(ch, i)) << "ch " << ch << " sample " << i;
        for (int ch = 2; ch < 5; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                ASSERT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "CV ch " << ch << " must be cleared on bypass";

        RingModulatorModule muted;
        setBool(muted, "dualIO", dual);
        muted.prepareToPlay(kSampleRate, kBlockSize);
        muted.setMuted(true);

        juce::AudioBuffer<float> mutedBuffer(5, kBlockSize);
        mutedBuffer.clear();
        fillSines(mutedBuffer, 440.0f, 220.0f, kSampleRate);
        muted.processBlock(mutedBuffer, midi);
        for (int ch = 0; ch < 5; ++ch)
            EXPECT_LT(mutedBuffer.getRMSLevel(ch, 0, kBlockSize), 1.0e-9f) << "ch " << ch << " must be silent on mute";
    }
}

TEST_F(RingModulatorModuleTest, OversamplingNotInModulationTargets) {
    for (const auto& t : module->getModulationTargets())
        EXPECT_NE(t.name, "Oversampling");
}

TEST_F(RingModulatorModuleTest, OversamplingParameterProperties) {
    auto* os = dynamic_cast<juce::AudioParameterChoice*>(paramByID(*module, "oversampling"));
    ASSERT_NE(os, nullptr);
    EXPECT_EQ(os->choices.size(), 3);
    EXPECT_EQ(os->choices[0], "Off");
    EXPECT_EQ(os->choices[1], "2x");
    EXPECT_EQ(os->choices[2], "4x");
    EXPECT_EQ(os->getIndex(), 1); // default 2x
}

TEST_F(RingModulatorModuleTest, MixDefaultIsFullyWet) {
    auto* mix = dynamic_cast<juce::AudioParameterFloat*>(paramByID(*module, "mix"));
    ASSERT_NE(mix, nullptr);
    EXPECT_NEAR(mix->get(), 1.0f, 1.0e-5f);
}

TEST_F(RingModulatorModuleTest, ProcessBlockProducesOutputAndClearsCV) {
    juce::AudioBuffer<float> buffer(5, kBlockSize);
    buffer.clear();
    fillSines(buffer, 440.0f, 220.0f, kSampleRate);
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(2, i, 0.4f);
        buffer.setSample(3, i, 0.4f);
        buffer.setSample(4, i, 0.4f);
    }

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    bool anyNonZero = false;
    for (int i = 0; i < kBlockSize; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);

    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(2, i), 0.0f);
        EXPECT_FLOAT_EQ(buffer.getSample(3, i), 0.0f);
        EXPECT_FLOAT_EQ(buffer.getSample(4, i), 0.0f);
    }
}

TEST_F(RingModulatorModuleTest, MixZeroPassesCarrierOnBothOutputs) {
    setChoice(*module, "oversampling", 0);
    setFloat(*module, "mix", 0.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    buffer.clear();
    fillSines(buffer, 440.0f, 1300.0f, kSampleRate);
    juce::AudioBuffer<float> original = buffer;

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int i = 64; i < kBlockSize; ++i) {
        EXPECT_NEAR(buffer.getSample(0, i), original.getSample(0, i), 1.0e-4f) << "L sample " << i;
        EXPECT_NEAR(buffer.getSample(1, i), original.getSample(0, i), 1.0e-4f) << "R sample " << i;
    }
}

TEST_F(RingModulatorModuleTest, MixOneDiffersFromCarrier) {
    setChoice(*module, "oversampling", 0);
    setFloat(*module, "mix", 1.0f);
    setFloat(*module, "character", 0.5f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    buffer.clear();
    fillSines(buffer, 440.0f, 1300.0f, kSampleRate);
    juce::AudioBuffer<float> original = buffer;

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    bool changed = false;
    for (int i = 64; i < kBlockSize; ++i) {
        if (std::abs(buffer.getSample(0, i) - original.getSample(0, i)) > 1.0e-3f) {
            changed = true;
            break;
        }
    }
    EXPECT_TRUE(changed);
}

TEST_F(RingModulatorModuleTest, CharacterChangesTheOutput) {
    setChoice(*module, "oversampling", 0);
    setFloat(*module, "mix", 1.0f);

    auto renderWithCharacter = [&](float character) {
        setFloat(*module, "character", character);
        module->prepareToPlay(kSampleRate, kBlockSize);
        juce::AudioBuffer<float> buffer(4, kBlockSize);
        buffer.clear();
        fillSines(buffer, 440.0f, 1300.0f, kSampleRate);
        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);
        return buffer;
    };

    auto clean = renderWithCharacter(0.0f);
    auto gated = renderWithCharacter(1.0f);

    bool different = false;
    for (int i = 64; i < kBlockSize; ++i) {
        if (std::abs(clean.getSample(0, i) - gated.getSample(0, i)) > 1.0e-3f) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
}

TEST_F(RingModulatorModuleTest, LatencyZeroWhenOff) {
    setChoice(*module, "oversampling", 0);
    EXPECT_NEAR(module->getLatencyInSamples(), 0.0, 1.0e-6);
}

TEST_F(RingModulatorModuleTest, LatencyNonZeroWhenEnabled) {
    setChoice(*module, "oversampling", 1);
    module->prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_GT(module->getLatencyInSamples(), 0.0);
}

TEST_F(RingModulatorModuleTest, OversamplingOffAnd4xProduceOutput) {
    juce::MidiBuffer midi;
    for (int mode : {0, 2}) {
        setChoice(*module, "oversampling", mode);
        module->prepareToPlay(kSampleRate, kBlockSize);

        juce::AudioBuffer<float> buffer(4, kBlockSize);
        buffer.clear();
        fillSines(buffer, 440.0f, 220.0f, kSampleRate);
        module->processBlock(buffer, midi);

        bool anyNonZero = false;
        for (int i = 0; i < kBlockSize; ++i) {
            if (std::abs(buffer.getSample(0, i)) > 1.0e-6f) {
                anyNonZero = true;
                break;
            }
        }
        EXPECT_TRUE(anyNonZero) << "oversampling index " << mode;
    }
}

TEST_F(RingModulatorModuleTest, SwitchModesDuringPlayback) {
    juce::AudioBuffer<float> buffer(4, kBlockSize);
    juce::MidiBuffer midi;
    fillSines(buffer, 440.0f, 220.0f, kSampleRate);
    EXPECT_NO_THROW(module->processBlock(buffer, midi));

    setChoice(*module, "oversampling", 2);
    fillSines(buffer, 440.0f, 220.0f, kSampleRate);
    EXPECT_NO_THROW(module->processBlock(buffer, midi));

    setChoice(*module, "oversampling", 0);
    fillSines(buffer, 440.0f, 220.0f, kSampleRate);
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(RingModulatorModuleTest, ZeroLengthBufferIsSafe) {
    juce::AudioBuffer<float> buffer(4, 0);
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(RingModulatorModuleTest, MixCVSweepsTowardDry) {
    setChoice(*module, "oversampling", 0);
    setFloat(*module, "mix", 1.0f);
    module->prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(4, kBlockSize);
    buffer.clear();
    fillSines(buffer, 440.0f, 1300.0f, kSampleRate);
    juce::AudioBuffer<float> original = buffer;
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(2, i, -1.0f); // mix + (-1) → 0

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    for (int i = 64; i < kBlockSize; ++i)
        EXPECT_NEAR(buffer.getSample(0, i), original.getSample(0, i), 1.0e-3f) << "sample " << i;
}

TEST_F(RingModulatorModuleTest, CharacterCVMatchesFullKnob) {
    setChoice(*module, "oversampling", 0);
    setFloat(*module, "mix", 1.0f);

    auto renderBlock = [&](float characterKnob, float characterCv) {
        setFloat(*module, "character", characterKnob);
        module->prepareToPlay(kSampleRate, kBlockSize);
        juce::AudioBuffer<float> buffer(5, kBlockSize);
        buffer.clear();
        fillSines(buffer, 440.0f, 1300.0f, kSampleRate);
        if (characterCv != 0.0f)
            for (int i = 0; i < kBlockSize; ++i)
                buffer.setSample(4, i, characterCv);
        juce::MidiBuffer midi;
        module->processBlock(buffer, midi);
        return buffer;
    };

    auto viaKnob = renderBlock(1.0f, 0.0f);
    auto viaCv = renderBlock(0.0f, 1.0f);

    for (int i = 64; i < kBlockSize; ++i)
        EXPECT_NEAR(viaCv.getSample(0, i), viaKnob.getSample(0, i), 1.0e-3f) << "sample " << i;
}

// High-frequency carrier × modulator produces a sum tone above Nyquist. At 1x that
// energy folds back; 4x oversampling should leave measurably less energy in the
// aliased bin. Character is held near 0 so the spectrum stays close to a multiply.
TEST_F(RingModulatorModuleTest, Oversampling4xReducesFoldedAliasEnergy) {
    constexpr float kCarrierHz = 15000.0f;
    constexpr float kModulatorHz = 12000.0f;
    constexpr float kAliasHz = 17100.0f; // 44100 - (15000+12000)
    constexpr int kTotal = 16384;
    constexpr int kSkip = 4096;

    auto run = [&](int osIndex) {
        RingModulatorModule m;
        setChoice(m, "oversampling", osIndex);
        setFloat(m, "mix", 1.0f);
        setFloat(m, "drive", 1.0f);
        setFloat(m, "character", 0.0f);
        m.prepareToPlay(kSampleRate, kBlockSize);
        return render(m, kCarrierHz, kModulatorHz, kTotal);
    };

    auto off = run(0);
    auto x4 = run(2);

    const float aliasOff = magnitudeAt(off, 0, kAliasHz, kSampleRate, kSkip);
    const float alias4x = magnitudeAt(x4, 0, kAliasHz, kSampleRate, kSkip);
    float rmsOff = 0.0f, rms4x = 0.0f;
    const int n = off.getNumSamples() - kSkip;
    for (int i = kSkip; i < off.getNumSamples(); ++i) {
        rmsOff += off.getSample(0, i) * off.getSample(0, i);
        rms4x += x4.getSample(0, i) * x4.getSample(0, i);
    }
    rmsOff = std::sqrt(rmsOff / (float)n);
    rms4x = std::sqrt(rms4x / (float)n);

    EXPECT_GT(rms4x, 0.1f) << "4x path should still produce a full-scale ring-mod result";
    EXPECT_NEAR(rmsOff, rms4x, 0.05f) << "oversampling must not just attenuate the whole signal";
    EXPECT_GT(aliasOff, alias4x * 4.0f) << "folded-back energy at " << kAliasHz << " Hz: 1x=" << aliasOff
                                        << " 4x=" << alias4x;
}
