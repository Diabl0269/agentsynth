// FXModuleLimiterCeilingTests.cpp -- Limiter Ceiling and the gain-reduction meter source.
#include "Modules/FX/LimiterModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kChannels = 6; // 2 audio + Threshold, Release, Input Gain, Ceiling CV

juce::RangedAudioParameter* param(LimiterModule& module, const juce::String& id) {
    return dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
}

void setActual(LimiterModule& module, const juce::String& id, float value) {
    auto* p = param(module, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

float actual(LimiterModule& module, const juce::String& id) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&module, id));
    return p == nullptr ? -999.0f : p->get();
}

std::unique_ptr<LimiterModule> make() {
    auto module = std::make_unique<LimiterModule>();
    module->prepareToPlay(kRate, kBlock);
    return module;
}

// A stereo sine of `amplitude`, with a short much louder burst in the middle so the limiter has to
// react to a transient as well as to a steady level.
juce::AudioBuffer<float> sine(float amplitude, int numBlocks, float burst = 1.0f) {
    juce::AudioBuffer<float> in(2, numBlocks * kBlock);
    for (int i = 0; i < in.getNumSamples(); ++i) {
        const bool inBurst = i > in.getNumSamples() / 2 && i < in.getNumSamples() / 2 + 300;
        const float s = amplitude * (inBurst ? burst : 1.0f) * std::sin(0.0627f * (float)i);
        in.setSample(0, i, s);
        in.setSample(1, i, 0.9f * s);
    }
    return in;
}

juce::AudioBuffer<float> renderThrough(LimiterModule& module, const juce::AudioBuffer<float>& in, int cvChannel = -1,
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

float peakOf(const juce::AudioBuffer<float>& buffer, int fromSample = 0) {
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = std::max(peak, buffer.getMagnitude(ch, fromSample, buffer.getNumSamples() - fromSample));
    return peak;
}

// What the module did before Ceiling existed: juce::dsp::Limiter on the audio pair.
juce::AudioBuffer<float> referenceLimiter(const juce::AudioBuffer<float>& in, float thresholdDb, float releaseMs) {
    juce::dsp::Limiter<float> reference;
    juce::dsp::ProcessSpec spec{kRate, (juce::uint32)kBlock, 2};
    reference.prepare(spec);
    reference.reset();
    reference.setThreshold(thresholdDb);
    reference.setRelease(releaseMs);
    juce::AudioBuffer<float> out(in);
    juce::dsp::AudioBlock<float> block(out);
    reference.process(juce::dsp::ProcessContextReplacing<float>(block));
    return out;
}

} // namespace

TEST(LimiterCeiling, ParameterAndCvJackExistAndDefaultToTheUntouchedSetting) {
    auto module = make();
    ASSERT_NE(param(*module, "ceiling"), nullptr);
    EXPECT_FLOAT_EQ(actual(*module, "ceiling"), 0.0f);
    EXPECT_NEAR(param(*module, "ceiling")->convertFrom0to1(0.0f), -24.0f, 1.0e-4f);
    EXPECT_NEAR(param(*module, "ceiling")->convertFrom0to1(1.0f), 0.0f, 1.0e-4f);

    EXPECT_EQ(module->getTotalNumInputChannels(), kChannels);
    EXPECT_EQ(module->getInputPortLabel(module->mapInputChannel(LimiterModule::kCeilingCvChannel).visibleJackIndex),
              "Ceiling");
    const auto targets = module->getModulationTargets();
    ASSERT_EQ(targets.size(), 4u);
    EXPECT_EQ(targets.back().channelIndex, LimiterModule::kCeilingCvChannel);
    EXPECT_EQ(targets.back().paramId, "ceiling");
}

TEST(LimiterCeiling, DefaultRendersExactlyWhatJuceLimiterDidEvenAboveFullScale) {
    // Amplitude 8 drives the limiter into its own hard clip at 0 dBFS, which the default must leave alone.
    const auto in = sine(8.0f, 12, 4.0f);
    auto module = make();
    const auto out = renderThrough(*module, in);
    const auto expected = referenceLimiter(in, -1.0f, 100.0f);
    ASSERT_FLOAT_EQ(peakOf(expected), 1.0f) << "the reference must reach the clip for this to mean anything";
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            ASSERT_EQ(out.getSample(ch, i), expected.getSample(ch, i)) << "ch " << ch << " sample " << i;
}

TEST(LimiterCeiling, StateSavedBeforeCeilingLoadsTheDefaultAndRendersAsBefore) {
    juce::ValueTree state("ModuleState");
    state.setProperty("release", 0.2f, nullptr);
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), blob);

    auto loaded = make();
    loaded->setStateInformation(blob.getData(), (int)blob.getSize());
    ASSERT_NE(param(*loaded, "ceiling"), nullptr);
    EXPECT_FLOAT_EQ(actual(*loaded, "ceiling"), 0.0f);

    const auto in = sine(6.0f, 8, 3.0f);
    const auto out = renderThrough(*loaded, in);
    const auto expected = referenceLimiter(in, -1.0f, actual(*loaded, "release"));
    for (int i = 0; i < in.getNumSamples(); ++i)
        ASSERT_EQ(out.getSample(0, i), expected.getSample(0, i)) << "sample " << i;
}

TEST(LimiterCeiling, PeakNeverExceedsTheCeiling) {
    for (const float ceilingDb : {-1.0f, -6.0f, -12.0f, -24.0f}) {
        SCOPED_TRACE(ceilingDb);
        auto module = make();
        setActual(*module, "ceiling", ceilingDb);
        const float ceiling = juce::Decibels::decibelsToGain(ceilingDb);
        const auto out = renderThrough(*module, sine(8.0f, 16, 6.0f));
        EXPECT_LE(peakOf(out), ceiling + 1.0e-6f);
        // ...and it is not simply silent: a loud input still reaches near the ceiling.
        EXPECT_GT(peakOf(out), ceiling * 0.5f);
    }
}

TEST(LimiterCeiling, CeilingCvLowersTheCeiling) {
    auto module = make();
    // -1.0 normalised from the default 0 dBFS knob sweeps it to the -24 dB minimum.
    const auto out = renderThrough(*module, sine(4.0f, 8), LimiterModule::kCeilingCvChannel, -1.0f);
    EXPECT_LE(peakOf(out), juce::Decibels::decibelsToGain(-24.0f) + 1.0e-6f);
}

TEST(LimiterCeiling, RaisingTheCeilingBackToZeroLetsTheGainRecoverWithoutAJump) {
    auto module = make();
    setActual(*module, "ceiling", -12.0f);
    renderThrough(*module, sine(2.0f, 6));
    setActual(*module, "ceiling", 0.0f);
    const auto out = renderThrough(*module, sine(2.0f, 100));
    const auto reference = renderThrough(*make(), sine(2.0f, 100));
    // After ~1.1 s (many release times) the stage is out of the path and the two agree.
    EXPECT_NEAR(out.getSample(0, out.getNumSamples() - 1), reference.getSample(0, out.getNumSamples() - 1), 1.0e-3f);
    // And no sample step along the way is larger than the signal's own largest step.
    float worstStep = 0.0f, signalStep = 0.0f;
    for (int i = 1; i < out.getNumSamples(); ++i) {
        worstStep = std::max(worstStep, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
        signalStep = std::max(signalStep, std::abs(reference.getSample(0, i) - reference.getSample(0, i - 1)));
    }
    EXPECT_LE(worstStep, signalStep * 1.1f);
}

TEST(LimiterGainReduction, ModuleExposesTheMeterSourceAndReadsZeroForAQuietSignal) {
    auto module = make();
    auto* source = dynamic_cast<GainReductionMeterSource*>(module.get());
    ASSERT_NE(source, nullptr);
    EXPECT_FLOAT_EQ(source->getGainReductionDb(), 0.0f);
    renderThrough(*module, sine(0.03f, 8)); // about -30 dBFS: under every stage's threshold
    EXPECT_NEAR(source->getGainReductionDb(), 0.0f, 0.1f);
    juce::AudioBuffer<float> silence(2, 4 * kBlock);
    silence.clear();
    renderThrough(*module, silence);
    EXPECT_FLOAT_EQ(source->getGainReductionDb(), 0.0f);
}

TEST(LimiterGainReduction, SteadySineOverThresholdReportsTheReduction) {
    auto module = make();
    setActual(*module, "threshold", -6.0f);
    auto* source = dynamic_cast<GainReductionMeterSource*>(module.get());
    ASSERT_NE(source, nullptr);
    // A full-scale sine meets the first stage (-10 dB, 4:1) at about 7.5 dB of reduction; the
    // second stage (-6 dB) then has nothing left to do.
    renderThrough(*module, sine(1.0f, 40));
    EXPECT_NEAR(source->getGainReductionDb(), 7.5f, 1.0f);
}

TEST(LimiterGainReduction, CeilingReductionIsIncludedInTheReading) {
    auto open = make();
    auto capped = make();
    setActual(*capped, "ceiling", -24.0f);
    renderThrough(*open, sine(1.0f, 40));
    renderThrough(*capped, sine(1.0f, 40));
    auto* openSource = dynamic_cast<GainReductionMeterSource*>(open.get());
    auto* cappedSource = dynamic_cast<GainReductionMeterSource*>(capped.get());
    ASSERT_NE(openSource, nullptr);
    ASSERT_NE(cappedSource, nullptr);
    EXPECT_GT(cappedSource->getGainReductionDb(), openSource->getGainReductionDb() + 10.0f);
}
