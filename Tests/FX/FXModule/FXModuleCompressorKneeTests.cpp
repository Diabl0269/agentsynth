// FXModuleCompressorKneeTests.cpp -- Compressor Knee and the gain-reduction meter source.
#include "Modules/FX/CompressorModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kChannels = 10; // 2 audio + 5 CV + Key L/R + Knee CV

juce::RangedAudioParameter* param(CompressorModule& module, const juce::String& id) {
    return dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
}

void setActual(CompressorModule& module, const juce::String& id, float value) {
    auto* p = param(module, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

float actual(CompressorModule& module, const juce::String& id) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&module, id));
    return p == nullptr ? -999.0f : p->get();
}

// A varied stereo signal: two sines of different pitch with a level that swells and falls so the
// detector sits above and below the default -12 dB threshold.
juce::AudioBuffer<float> programme(int numBlocks) {
    juce::AudioBuffer<float> in(2, numBlocks * kBlock);
    for (int i = 0; i < in.getNumSamples(); ++i) {
        const float swell = 0.05f + 0.9f * std::abs(std::sin(0.0002f * (float)i));
        in.setSample(0, i, swell * std::sin(0.05f * (float)i));
        in.setSample(1, i, 0.8f * swell * std::sin(0.031f * (float)i + 1.0f));
    }
    return in;
}

juce::AudioBuffer<float> renderThrough(CompressorModule& module, const juce::AudioBuffer<float>& in, int cvChannel = -1,
                                       float cv = 0.0f) {
    juce::AudioBuffer<float> out(2, in.getNumSamples());
    for (int start = 0; start < in.getNumSamples(); start += kBlock) {
        juce::AudioBuffer<float> block(kChannels, kBlock);
        block.clear();
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, in, ch, start, kBlock);
        if (cvChannel >= 0)
            for (int i = 0; i < kBlock; ++i)
                block.setSample(cvChannel, i, cv);
        juce::MidiBuffer midi;
        module.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, start, block, ch, 0, kBlock);
    }
    return out;
}

// What the module did before Knee existed: juce::dsp::Compressor on the audio pair, then makeup.
juce::AudioBuffer<float> referenceCompressor(const juce::AudioBuffer<float>& in, float thresholdDb, float ratio,
                                             float attackMs, float releaseMs) {
    juce::dsp::Compressor<float> reference;
    juce::dsp::ProcessSpec spec{kRate, (juce::uint32)kBlock, 2};
    reference.prepare(spec);
    reference.reset();
    reference.setThreshold(thresholdDb);
    reference.setRatio(ratio);
    reference.setAttack(attackMs);
    reference.setRelease(releaseMs);
    juce::AudioBuffer<float> out(in);
    juce::dsp::AudioBlock<float> block(out);
    reference.process(juce::dsp::ProcessContextReplacing<float>(block));
    return out;
}

// Steady gain the module settles on for a constant (DC) input of `levelDb`, as a linear ratio.
float settledGain(CompressorModule& module, float levelDb) {
    const float level = juce::Decibels::decibelsToGain(levelDb);
    float lastGain = 1.0f;
    for (int b = 0; b < 12; ++b) {
        juce::AudioBuffer<float> block(kChannels, kBlock);
        block.clear();
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                block.setSample(ch, i, level);
        juce::MidiBuffer midi;
        module.processBlock(block, midi);
        lastGain = block.getSample(0, kBlock - 1) / level;
    }
    return lastGain;
}

std::unique_ptr<CompressorModule> make() {
    auto module = std::make_unique<CompressorModule>();
    module->prepareToPlay(kRate, kBlock);
    return module;
}

juce::MemoryBlock stateBeforeKnee() {
    juce::ValueTree state("ModuleState"); // the parameters a saved patch had before Knee existed
    state.setProperty("ratio", 0.25f, nullptr);
    state.setProperty("makeupGain", 1.0f / 3.0f, nullptr);
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), block);
    return block;
}

} // namespace

TEST(CompressorKnee, ParameterAndCvJackExistWithAHardKneeDefault) {
    auto module = make();
    ASSERT_NE(param(*module, "knee"), nullptr);
    EXPECT_FLOAT_EQ(actual(*module, "knee"), 0.0f);
    EXPECT_FLOAT_EQ(param(*module, "knee")->convertFrom0to1(1.0f), 24.0f);

    EXPECT_EQ(module->getTotalNumInputChannels(), kChannels);
    EXPECT_EQ(module->getInputPortLabel(module->mapInputChannel(CompressorModule::kKneeCvChannel).visibleJackIndex),
              "Knee");
    EXPECT_EQ(module->mapInputChannel(CompressorModule::kKneeCvChannel).role, PortRole::ModCV);
    EXPECT_EQ(module->getVisibleInputPortCount(),
              module->mapInputChannel(CompressorModule::kKneeCvChannel).visibleJackIndex + 1);

    const auto targets = module->getModulationTargets();
    ASSERT_EQ(targets.size(), 6u);
    EXPECT_EQ(targets.back().channelIndex, CompressorModule::kKneeCvChannel);
    EXPECT_EQ(targets.back().paramId, "knee");
    // The key pair keeps the channels saved patches already route to.
    EXPECT_EQ(CompressorModule::kKeyBase, 7);
}

TEST(CompressorKnee, ZeroKneeRendersExactlyWhatJuceCompressorDid) {
    const auto in = programme(12);
    auto module = make();
    const auto actualOut = renderThrough(*module, in);
    const auto expected = referenceCompressor(in, -12.0f, 4.0f, 10.0f, 100.0f);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            ASSERT_EQ(actualOut.getSample(ch, i), expected.getSample(ch, i)) << "ch " << ch << " sample " << i;
}

TEST(CompressorKnee, StateSavedBeforeKneeLoadsHardKneeAndRendersAsBefore) {
    const auto in = programme(8);
    auto loaded = make();
    const auto blob = stateBeforeKnee();
    loaded->setStateInformation(blob.getData(), (int)blob.getSize());
    loaded->prepareToPlay(kRate, kBlock); // a loaded patch starts from its own makeup, not a ramp to it
    ASSERT_NE(param(*loaded, "knee"), nullptr);
    EXPECT_FLOAT_EQ(actual(*loaded, "knee"), 0.0f);

    const float ratio = actual(*loaded, "ratio");
    const float makeup = actual(*loaded, "makeupGain");
    const auto expected = referenceCompressor(in, -12.0f, ratio, 10.0f, 100.0f);
    const auto out = renderThrough(*loaded, in);
    const float makeupGain = juce::Decibels::decibelsToGain(makeup);
    for (int i = 0; i < in.getNumSamples(); ++i)
        ASSERT_EQ(out.getSample(0, i), expected.getSample(0, i) * makeupGain) << "sample " << i;
}

TEST(CompressorKnee, SoftKneeStartsEasingInBelowTheThreshold) {
    // Threshold -12 dB, ratio 4, knee 12 dB: the knee spans -18..-6 dB.
    auto hard = make();
    auto soft = make();
    setActual(*soft, "knee", 12.0f);

    // 3 dB under the threshold the hard knee leaves the signal alone; the soft knee has begun to
    // bend: (1/4 - 1) * (-3 + 6)^2 / (2 * 12) = -0.281 dB.
    EXPECT_FLOAT_EQ(settledGain(*hard, -15.0f), 1.0f);
    const float softBelow = settledGain(*soft, -15.0f);
    EXPECT_LT(softBelow, 1.0f);
    EXPECT_NEAR(juce::Decibels::gainToDecibels(softBelow), -0.281f, 0.02f);
}

TEST(CompressorKnee, SoftKneeJoinsTheHardCurveAtTheKneeEdgeAndBeyond) {
    auto hard = make();
    auto soft = make();
    setActual(*soft, "knee", 12.0f);
    // 6 dB over the threshold is the top of a 12 dB knee; 10 dB over is well past it.
    for (const float levelDb : {-6.0f, -2.0f}) {
        EXPECT_NEAR(juce::Decibels::gainToDecibels(settledGain(*soft, levelDb)),
                    juce::Decibels::gainToDecibels(settledGain(*hard, levelDb)), 0.01f)
            << levelDb << " dB in";
    }
}

TEST(CompressorKnee, SoftKneeAlreadyReducesAtTheThreshold) {
    // At the threshold itself the hard knee has just started (0 dB of reduction) while the soft knee
    // already takes (1/4 - 1) * 6^2 / 24 = -1.125 dB. The soft curve is never above the hard one.
    auto hard = make();
    auto soft = make();
    setActual(*soft, "knee", 12.0f);
    const float hardAt = juce::Decibels::gainToDecibels(settledGain(*hard, -12.0f));
    const float softAt = juce::Decibels::gainToDecibels(settledGain(*soft, -12.0f));
    EXPECT_NEAR(softAt, -1.125f, 0.05f);
    EXPECT_LE(softAt, hardAt + 1.0e-4f);
}

TEST(CompressorKnee, KneeCvJackWidensTheKnee) {
    auto plain = make();
    EXPECT_FLOAT_EQ(settledGain(*plain, -15.0f), 1.0f); // hard knee: nothing happens 3 dB under

    // +0.5 normalised on a 0..24 dB knee is 12 dB, the same knee as the knob test above.
    auto modulated = make();
    const float level = juce::Decibels::decibelsToGain(-15.0f);
    float gain = 1.0f;
    for (int b = 0; b < 12; ++b) {
        juce::AudioBuffer<float> block(kChannels, kBlock);
        block.clear();
        for (int i = 0; i < kBlock; ++i) {
            block.setSample(0, i, level);
            block.setSample(1, i, level);
            block.setSample(CompressorModule::kKneeCvChannel, i, 0.5f);
        }
        juce::MidiBuffer midi;
        modulated->processBlock(block, midi);
        gain = block.getSample(0, kBlock - 1) / level;
    }
    EXPECT_NEAR(juce::Decibels::gainToDecibels(gain), -0.281f, 0.02f);
}

TEST(CompressorGainReduction, ModuleExposesTheMeterSourceAndReadsZeroAtRest) {
    auto module = make();
    auto* source = dynamic_cast<GainReductionMeterSource*>(module.get());
    ASSERT_NE(source, nullptr);
    EXPECT_FLOAT_EQ(source->getGainReductionDb(), 0.0f);

    // Silence, and a signal well under the -12 dB threshold, both leave it at 0 dB.
    juce::AudioBuffer<float> quiet(kChannels, kBlock);
    for (int b = 0; b < 4; ++b) {
        quiet.clear();
        for (int i = 0; i < kBlock; ++i)
            quiet.setSample(0, i, 0.05f * std::sin(0.06f * (float)i));
        juce::MidiBuffer midi;
        module->processBlock(quiet, midi);
    }
    EXPECT_FLOAT_EQ(source->getGainReductionDb(), 0.0f);
}

TEST(CompressorGainReduction, SteadySineOverThresholdReportsTheStaticReduction) {
    auto module = make();
    auto* source = dynamic_cast<GainReductionMeterSource*>(module.get());
    ASSERT_NE(source, nullptr);
    // The fastest attack and slowest release, so the detector sits on the sine's peak.
    setActual(*module, "attack", 0.1f);
    setActual(*module, "release", 1000.0f);

    // Peak 0.5 is -6.02 dB, 5.98 dB over the -12 dB threshold: (1 - 1/4) * 5.98 = 4.48 dB.
    for (int b = 0; b < 40; ++b) {
        juce::AudioBuffer<float> block(kChannels, kBlock);
        block.clear();
        for (int i = 0; i < kBlock; ++i) {
            const float s = 0.5f * std::sin(0.0627f * (float)(b * kBlock + i));
            block.setSample(0, i, s);
            block.setSample(1, i, s);
        }
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
    }
    EXPECT_NEAR(source->getGainReductionDb(), 4.48f, 0.1f);
}

TEST(CompressorGainReduction, MeterHangsOnATransientThenFallsBackToZero) {
    auto module = make();
    auto* source = dynamic_cast<GainReductionMeterSource*>(module.get());
    ASSERT_NE(source, nullptr);

    auto blockOf = [&](float level) {
        juce::AudioBuffer<float> block(kChannels, kBlock);
        block.clear();
        for (int i = 0; i < kBlock; ++i) {
            block.setSample(0, i, level);
            block.setSample(1, i, level);
        }
        juce::MidiBuffer midi;
        module->processBlock(block, midi);
    };
    for (int b = 0; b < 8; ++b)
        blockOf(0.8f);
    const float loud = source->getGainReductionDb();
    EXPECT_GT(loud, 6.0f);

    blockOf(0.0f);
    const float next = source->getGainReductionDb();
    EXPECT_GT(next, 0.0f) << "a poll between blocks must not lose the reduction at once";
    EXPECT_LE(next, loud);

    for (int b = 0; b < 200; ++b)
        blockOf(0.0f);
    EXPECT_FLOAT_EQ(source->getGainReductionDb(), 0.0f);
}

TEST(CompressorGainReduction, MuteReadsZero) {
    auto module = make();
    auto* source = dynamic_cast<GainReductionMeterSource*>(module.get());
    ASSERT_NE(source, nullptr);
    juce::AudioBuffer<float> block(kChannels, kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < 8; ++b) {
        block.clear();
        for (int i = 0; i < kBlock; ++i)
            block.setSample(0, i, 0.8f);
        module->processBlock(block, midi);
    }
    ASSERT_GT(source->getGainReductionDb(), 1.0f);
    setActual(*module, "muted", 1.0f);
    module->processBlock(block, midi);
    EXPECT_FLOAT_EQ(source->getGainReductionDb(), 0.0f);
}
