// PitchShifterModuleTests.cpp
// DSP-level tests for PitchShifterModule. The two engines are verified spectrally:
//   • Pitch mode must move the fundamental by a *ratio* (440 -> 880 for +12 semitones)
//   • Frequency mode must move it by a constant *offset* (1000 -> 1200 for +200 Hz),
//     with the opposite sideband suppressed — that is what distinguishes a real SSB
//     shifter from a ring modulator.
// Port labels live with the other FX modules in FXModuleTests.cpp.

#include "Modules/FX/PitchShifterModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 6; // 2 audio + 4 CV

void setFloatParam(juce::AudioProcessor& proc, const juce::String& id, float value) {
    for (auto* p : proc.getParameters()) {
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p)) {
            if (f->paramID == id) {
                f->setValueNotifyingHost(f->convertTo0to1(value));
                return;
            }
        }
    }
    ADD_FAILURE() << "No float parameter with id \"" << id << "\"";
}

void setChoiceParam(juce::AudioProcessor& proc, const juce::String& id, int index) {
    for (auto* p : proc.getParameters()) {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p)) {
            if (c->paramID == id) {
                c->setValueNotifyingHost(c->convertTo0to1((float)index));
                return;
            }
        }
    }
    ADD_FAILURE() << "No choice parameter with id \"" << id << "\"";
}

float getFloatParam(juce::AudioProcessor& proc, const juce::String& id) {
    for (auto* p : proc.getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(p))
            if (f->paramID == id)
                return f->get();
    ADD_FAILURE() << "No float parameter with id \"" << id << "\"";
    return 0.0f;
}

/** Magnitude of one frequency bin, via the Goertzel algorithm. */
double binMagnitude(const std::vector<float>& x, double freq) {
    if (x.empty())
        return 0.0;

    const double w = juce::MathConstants<double>::twoPi * freq / kSampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x) {
        const double s0 = (double)v + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    return std::sqrt(re * re + im * im) / (double)x.size();
}

/** Strongest integer frequency in [loHz, hiHz]. */
double peakFrequency(const std::vector<float>& x, int loHz, int hiHz) {
    double bestFreq = (double)loHz;
    double bestMag = -1.0;
    for (int f = loHz; f <= hiHz; ++f) {
        const double mag = binMagnitude(x, (double)f);
        if (mag > bestMag) {
            bestMag = mag;
            bestFreq = (double)f;
        }
    }
    return bestFreq;
}

struct RenderOptions {
    double inputFreq = 440.0;
    int totalSamples = 49152; // ~1.1 s: long enough to settle, cheap enough for CI
    int skipSamples = 16384;  // discard the fill-up transient of the delay line
    int numChannels = kNumChannels;
    int cvChannel = -1;
    float cvValue = 0.0f;
};

/** Renders a sine through the module and returns the left-channel output after the
    transient. `module` must already be prepared. */
std::vector<float> renderSine(PitchShifterModule& module, const RenderOptions& opts = {}) {
    std::vector<float> out;
    out.reserve((size_t)juce::jmax(0, opts.totalSamples - opts.skipSamples));

    juce::AudioBuffer<float> buffer(opts.numChannels, kBlockSize);
    juce::MidiBuffer midi;

    double phase = 0.0;
    const double inc = juce::MathConstants<double>::twoPi * opts.inputFreq / kSampleRate;

    for (int start = 0; start < opts.totalSamples; start += kBlockSize) {
        buffer.clear();
        for (int i = 0; i < kBlockSize; ++i) {
            const float s = (float)std::sin(phase);
            phase += inc;
            for (int ch = 0; ch < juce::jmin(2, opts.numChannels); ++ch)
                buffer.setSample(ch, i, s);
        }
        if (opts.cvChannel >= 0 && opts.cvChannel < opts.numChannels)
            for (int i = 0; i < kBlockSize; ++i)
                buffer.setSample(opts.cvChannel, i, opts.cvValue);

        module.processBlock(buffer, midi);

        for (int i = 0; i < kBlockSize; ++i) {
            const int absoluteIndex = start + i;
            if (absoluteIndex >= opts.skipSamples && absoluteIndex < opts.totalSamples)
                out.push_back(buffer.getSample(0, i));
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Identity / registration
// ---------------------------------------------------------------------------

TEST(PitchShifterModuleTest, ModuleTypeAndCategoryAreCorrect) {
    PitchShifterModule module;
    EXPECT_EQ(module.getModuleType(), ModuleType::PitchShifter);
    EXPECT_EQ(module.getModulationCategory(), ModulationCategory::FX);
    EXPECT_EQ(module.getName(), "Pitch Shifter");
}

TEST(PitchShifterModuleTest, ChannelLayoutMatchesPortLabels) {
    PitchShifterModule module;
    EXPECT_EQ(module.getTotalNumInputChannels(), 6);
    EXPECT_EQ(module.getTotalNumOutputChannels(), 2);
}

TEST(PitchShifterModuleTest, ModulationTargetsMatchCVChannels) {
    PitchShifterModule module;
    const auto targets = module.getModulationTargets();
    ASSERT_EQ(targets.size(), 4u);
    EXPECT_EQ(targets[0].name, "Pitch");
    EXPECT_EQ(targets[0].channelIndex, 2);
    EXPECT_EQ(targets[1].name, "Shift");
    EXPECT_EQ(targets[1].channelIndex, 3);
    EXPECT_EQ(targets[2].name, "Mix");
    EXPECT_EQ(targets[2].channelIndex, 4);
    EXPECT_EQ(targets[3].name, "Feedback");
    EXPECT_EQ(targets[3].channelIndex, 5);
}

// ---------------------------------------------------------------------------
// Pitch mode
// ---------------------------------------------------------------------------

TEST(PitchShifterModuleTest, DefaultsArePassThrough) {
    // Pitch = 0 with Mix = 1 must be transparent, not a two-tap comb filter.
    PitchShifterModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i) {
        const float s = (float)std::sin(juce::MathConstants<double>::twoPi * 440.0 * i / kSampleRate);
        buffer.setSample(0, i, s);
        buffer.setSample(1, i, s);
    }
    juce::AudioBuffer<float> expected(buffer);

    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), expected.getSample(ch, i)) << "Ch" << ch << " sample " << i;
}

TEST(PitchShifterModuleTest, PitchUpOneOctaveDoublesFundamental) {
    PitchShifterModule module;
    setFloatParam(module, "pitch", 12.0f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderSine(module);
    const double peak = peakFrequency(out, 600, 1100);

    EXPECT_NEAR(peak, 880.0, 6.0) << "+12 semitones should transpose 440 Hz to 880 Hz";
    EXPECT_GT(binMagnitude(out, 880.0), 3.0 * binMagnitude(out, 440.0))
        << "The original fundamental should be largely gone at Mix = 1";
}

TEST(PitchShifterModuleTest, PitchDownOneOctaveHalvesFundamental) {
    PitchShifterModule module;
    setFloatParam(module, "pitch", -12.0f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderSine(module);
    const double peak = peakFrequency(out, 120, 600);

    EXPECT_NEAR(peak, 220.0, 6.0) << "-12 semitones should transpose 440 Hz to 220 Hz";
    EXPECT_GT(binMagnitude(out, 220.0), 3.0 * binMagnitude(out, 440.0));
}

TEST(PitchShifterModuleTest, FineTuneDetunesInCents) {
    // +100 cents == +1 semitone: 1000 Hz -> 1000 * 2^(1/12) ~= 1059.5 Hz.
    PitchShifterModule module;
    setFloatParam(module, "fine", 100.0f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    RenderOptions opts;
    opts.inputFreq = 1000.0;
    const auto out = renderSine(module, opts);

    EXPECT_NEAR(peakFrequency(out, 950, 1150), 1059.5, 6.0) << "Fine tune must apply as cents on top of Pitch";
}

TEST(PitchShifterModuleTest, MixZeroPassesDryOnly) {
    PitchShifterModule module;
    setFloatParam(module, "pitch", 12.0f);
    setFloatParam(module, "mix", 0.0f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(0, i, 0.6f);
        buffer.setSample(1, i, -0.4f);
    }

    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(buffer.getSample(0, i), 0.6f) << "Ch0 sample " << i;
        EXPECT_FLOAT_EQ(buffer.getSample(1, i), -0.4f) << "Ch1 sample " << i;
    }
}

// ---------------------------------------------------------------------------
// Frequency (single-sideband) mode
// ---------------------------------------------------------------------------

TEST(PitchShifterModuleTest, FrequencyModeShiftsByConstantOffset) {
    PitchShifterModule module;
    setChoiceParam(module, "shiftMode", 1); // Frequency
    setFloatParam(module, "shiftHz", 200.0f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    RenderOptions opts;
    opts.inputFreq = 1000.0;
    const auto out = renderSine(module, opts);

    EXPECT_NEAR(peakFrequency(out, 1050, 1350), 1200.0, 3.0) << "+200 Hz must land at 1200 Hz, not at a pitch ratio";
    EXPECT_LT(binMagnitude(out, 800.0), 0.25 * binMagnitude(out, 1200.0))
        << "The lower sideband must be suppressed — otherwise this is ring modulation, not a frequency shift";
}

TEST(PitchShifterModuleTest, FrequencyModeShiftsDownwards) {
    PitchShifterModule module;
    setChoiceParam(module, "shiftMode", 1);
    setFloatParam(module, "shiftHz", -200.0f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    RenderOptions opts;
    opts.inputFreq = 1000.0;
    const auto out = renderSine(module, opts);

    EXPECT_NEAR(peakFrequency(out, 650, 950), 800.0, 3.0);
    EXPECT_LT(binMagnitude(out, 1200.0), 0.25 * binMagnitude(out, 800.0));
}

TEST(PitchShifterModuleTest, FrequencyModeIsInharmonic) {
    // The signature of a frequency shifter: a harmonic pair does NOT stay harmonic.
    // 1000 Hz shifted by +150 lands at 1150, while its 2nd harmonic lands at 2150 —
    // a pitch shifter would have put it at 2300.
    PitchShifterModule module;
    setChoiceParam(module, "shiftMode", 1);
    setFloatParam(module, "shiftHz", 150.0f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    RenderOptions opts;
    opts.inputFreq = 1000.0;
    const auto out = renderSine(module, opts);

    EXPECT_NEAR(peakFrequency(out, 1000, 1400), 1150.0, 3.0);
    EXPECT_LT(binMagnitude(out, 2300.0), 0.25 * binMagnitude(out, 1150.0));
}

// ---------------------------------------------------------------------------
// CV modulation
// ---------------------------------------------------------------------------

TEST(PitchShifterModuleTest, PitchCVTransposesWithoutTouchingTheParameter) {
    PitchShifterModule module;
    module.prepareToPlay(kSampleRate, kBlockSize); // Pitch parameter stays at 0

    RenderOptions opts;
    opts.cvChannel = 2;
    opts.cvValue = 0.5f; // 0.5 * 24 semitones = +12
    const auto out = renderSine(module, opts);

    EXPECT_NEAR(peakFrequency(out, 600, 1100), 880.0, 6.0)
        << "Full-scale CV must map onto the full +/-24 semitone range";
    EXPECT_FLOAT_EQ(getFloatParam(module, "pitch"), 0.0f) << "CV must not write back into the parameter";
}

TEST(PitchShifterModuleTest, ShiftCVMovesTheFrequencyOffset) {
    PitchShifterModule module;
    setChoiceParam(module, "shiftMode", 1);
    module.prepareToPlay(kSampleRate, kBlockSize); // Shift parameter stays at 0 Hz

    RenderOptions opts;
    opts.inputFreq = 1000.0;
    opts.cvChannel = 3;
    opts.cvValue = 0.2f; // 0.2 * 1000 Hz = +200 Hz
    const auto out = renderSine(module, opts);

    EXPECT_NEAR(peakFrequency(out, 1050, 1350), 1200.0, 3.0);
}

TEST(PitchShifterModuleTest, MixCVBlendsTowardsDry) {
    // Mix parameter at 0 with full negative-to-positive CV should reach the wet signal.
    PitchShifterModule module;
    setFloatParam(module, "pitch", 12.0f);
    setFloatParam(module, "mix", 0.0f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    RenderOptions opts;
    opts.cvChannel = 4;
    opts.cvValue = 1.0f;
    const auto out = renderSine(module, opts);

    EXPECT_NEAR(peakFrequency(out, 600, 1100), 880.0, 6.0) << "Mix CV must be able to open the wet path fully";
}

TEST(PitchShifterModuleTest, CVChannelsAreClearedAfterProcessing) {
    PitchShifterModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);
    buffer.clear();
    for (int ch = 2; ch < kNumChannels; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module.processBlock(buffer, midi);

    for (int ch = 2; ch < kNumChannels; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(ch, i), 0.0f) << "CV ch" << ch << " sample " << i;
}

// ---------------------------------------------------------------------------
// Feedback / robustness
// ---------------------------------------------------------------------------

TEST(PitchShifterModuleTest, MaximumFeedbackStaysBounded) {
    // Shepard-tone patches run the shifter into itself; the loop must not blow up.
    PitchShifterModule module;
    setFloatParam(module, "pitch", 7.0f);
    setFloatParam(module, "feedback", 0.95f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    RenderOptions opts;
    opts.totalSamples = (int)(kSampleRate * 3.0);
    opts.skipSamples = 0;
    const auto out = renderSine(module, opts);

    for (size_t i = 0; i < out.size(); ++i) {
        ASSERT_TRUE(std::isfinite(out[i])) << "Non-finite output at sample " << i;
        ASSERT_LT(std::abs(out[i]), 8.0f) << "Feedback loop diverged at sample " << i;
    }
}

TEST(PitchShifterModuleTest, FrequencyModeMaximumFeedbackStaysBounded) {
    PitchShifterModule module;
    setChoiceParam(module, "shiftMode", 1);
    setFloatParam(module, "shiftHz", 120.0f);
    setFloatParam(module, "feedback", 0.95f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    RenderOptions opts;
    opts.totalSamples = (int)(kSampleRate * 3.0);
    opts.skipSamples = 0;
    const auto out = renderSine(module, opts);

    for (size_t i = 0; i < out.size(); ++i) {
        ASSERT_TRUE(std::isfinite(out[i])) << "Non-finite output at sample " << i;
        ASSERT_LT(std::abs(out[i]), 8.0f) << "Feedback loop diverged at sample " << i;
    }
}

TEST(PitchShifterModuleTest, ExtremeParametersProduceFiniteOutput) {
    for (float semitones : {-24.0f, 24.0f}) {
        for (float windowMs : {10.0f, 100.0f}) {
            PitchShifterModule module;
            setFloatParam(module, "pitch", semitones);
            setFloatParam(module, "fine", semitones < 0.0f ? -100.0f : 100.0f);
            setFloatParam(module, "window", windowMs);
            module.prepareToPlay(kSampleRate, kBlockSize);

            RenderOptions opts;
            opts.totalSamples = 8192;
            opts.skipSamples = 0;
            const auto out = renderSine(module, opts);

            for (float v : out)
                ASSERT_TRUE(std::isfinite(v)) << "pitch=" << semitones << " window=" << windowMs;
        }
    }
}

TEST(PitchShifterModuleTest, ZeroLengthAndSingleSampleBuffersAreSafe) {
    PitchShifterModule module;
    setFloatParam(module, "pitch", 5.0f);
    module.prepareToPlay(kSampleRate, kBlockSize);

    juce::MidiBuffer midi;

    juce::AudioBuffer<float> empty(kNumChannels, 0);
    EXPECT_NO_THROW(module.processBlock(empty, midi));

    juce::AudioBuffer<float> single(kNumChannels, 1);
    single.clear();
    single.setSample(0, 0, 0.5f);
    single.setSample(1, 0, 0.5f);
    EXPECT_NO_THROW(module.processBlock(single, midi));
    EXPECT_TRUE(std::isfinite(single.getSample(0, 0)));
}

TEST(PitchShifterModuleTest, MonoBufferIsSafe) {
    // The graph can hand a module fewer channels than it declares; that must not read
    // past the buffer.
    PitchShifterModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> mono(1, kBlockSize);
    mono.clear();
    for (int i = 0; i < kBlockSize; ++i)
        mono.setSample(0, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module.processBlock(mono, midi));
    for (int i = 0; i < kBlockSize; ++i)
        EXPECT_FLOAT_EQ(mono.getSample(0, i), 0.3f) << "Mono input should pass through untouched";
}

TEST(PitchShifterModuleTest, ProcessBeforePrepareDoesNotCrash) {
    PitchShifterModule module;
    juce::AudioBuffer<float> buffer(kNumChannels, kBlockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module.processBlock(buffer, midi));
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

TEST(PitchShifterModuleTest, StateRoundTripPreservesParameters) {
    PitchShifterModule source;
    setChoiceParam(source, "shiftMode", 1);
    setFloatParam(source, "pitch", -7.0f);
    setFloatParam(source, "fine", 25.0f);
    setFloatParam(source, "shiftHz", 333.0f);
    setFloatParam(source, "window", 80.0f);
    setFloatParam(source, "feedback", 0.5f);
    setFloatParam(source, "mix", 0.35f);

    juce::MemoryBlock state;
    source.getStateInformation(state);

    PitchShifterModule restored;
    restored.setStateInformation(state.getData(), (int)state.getSize());

    EXPECT_NEAR(getFloatParam(restored, "pitch"), -7.0f, 0.01f);
    EXPECT_NEAR(getFloatParam(restored, "fine"), 25.0f, 0.05f);
    EXPECT_NEAR(getFloatParam(restored, "shiftHz"), 333.0f, 0.5f);
    EXPECT_NEAR(getFloatParam(restored, "window"), 80.0f, 0.05f);
    EXPECT_NEAR(getFloatParam(restored, "feedback"), 0.5f, 0.01f);
    EXPECT_NEAR(getFloatParam(restored, "mix"), 0.35f, 0.01f);

    for (auto* p : restored.getParameters())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
            if (c->paramID == "shiftMode")
                EXPECT_EQ(c->getIndex(), 1);
}
