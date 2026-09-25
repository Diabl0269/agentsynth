// FXModuleCVJackTests.cpp — the CV jacks the FX modules gained so that every continuous parameter
// is a modulation target (Flanger/Chorus/Phaser Centre/Feedback/Mix, Compressor, Gate, Limiter,
// Pitch Shifter Fine/Window). Each test drives ONE new jack with a constant CV and checks the audible
// consequence against the same module run without it, so the plumbing (channel index, normalised
// convention, per-block read) is pinned end to end rather than by a channel count.

#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/FlangerModule.h"
#include "Modules/FX/GateModule.h"
#include "Modules/FX/LimiterModule.h"
#include "Modules/FX/PhaserModule.h"
#include "Modules/FX/PitchShifterModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/ModuleBase.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

struct CvRun {
    int cvChannel = -1;
    float cvValue = 0.0f;
    float amplitude = 0.5f;
    // 233 Hz, not a round 220: the Delay's default 250 ms is exactly 55 cycles of 220 Hz, which
    // puts the delayed copy in phase with the input and makes the effect vanish on a steady tone.
    double freqHz = 233.0;
    int blocks = 40; // ~0.46 s: past every smoothing ramp in these modules
};

/** Renders `run.blocks` blocks of a sine through a fresh, prepared module and returns the LAST
    block's left channel, so every comparison is made after the ramps have settled. */
template <typename Module>
juce::AudioBuffer<float> render(const CvRun& run, void (*setup)(Module&) = nullptr) {
    Module module;
    if (setup != nullptr)
        setup(module);
    module.prepareToPlay(kSampleRate, kBlockSize);

    const int channels = module.getTotalNumInputChannels();
    juce::AudioBuffer<float> buffer(channels, kBlockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;
    const double inc = juce::MathConstants<double>::twoPi * run.freqHz / kSampleRate;
    for (int b = 0; b < run.blocks; ++b) {
        buffer.clear();
        for (int i = 0; i < kBlockSize; ++i) {
            const float s = run.amplitude * (float)std::sin(phase);
            phase += inc;
            buffer.setSample(0, i, s);
            buffer.setSample(1, i, s);
        }
        if (run.cvChannel >= 0)
            for (int i = 0; i < kBlockSize; ++i)
                buffer.setSample(run.cvChannel, i, run.cvValue);
        module.processBlock(buffer, midi);
        for (int ch = 2; ch < channels; ++ch)
            EXPECT_LT(buffer.getRMSLevel(ch, 0, kBlockSize), 1.0e-6f) << "CV channel " << ch << " leaked";
    }
    return buffer;
}

float rms(const juce::AudioBuffer<float>& b) { return b.getRMSLevel(0, 0, b.getNumSamples()); }

/** RMS of (a - b) on the left channel. */
float rmsDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    double acc = 0.0;
    for (int i = 0; i < a.getNumSamples(); ++i) {
        const double d = a.getSample(0, i) - b.getSample(0, i);
        acc += d * d;
    }
    return (float)std::sqrt(acc / a.getNumSamples());
}

void setFloat(juce::AudioProcessor& proc, const char* id, float value) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&proc, id));
    ASSERT_NE(p, nullptr) << id;
    *p = value;
}

int channelOf(const ModuleBase& mb, const juce::String& jack) {
    for (const auto& t : mb.getModulationTargets())
        if (t.name == jack)
            return t.channelIndex;
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------
// Flanger / Chorus / Phaser: a Mix CV of -1.0 sweeps Mix to 0, which is the dry signal.
// ---------------------------------------------------------------------------

template <typename Module>
void expectMixCVAtMinusOneIsDry() {
    Module probe;
    const int mixCh = channelOf(probe, "Mix");
    ASSERT_GE(mixCh, 2);

    CvRun dryRun;
    dryRun.cvChannel = mixCh;
    dryRun.cvValue = -1.0f;
    const auto modulated = render<Module>(dryRun);
    const auto wet = render<Module>(CvRun{});

    // The dry signal is the input sine itself: compare against a freshly rendered reference sine.
    juce::AudioBuffer<float> reference(1, kBlockSize);
    double phase = 0.0;
    const double inc = juce::MathConstants<double>::twoPi * CvRun{}.freqHz / kSampleRate;
    for (int b = 0; b < dryRun.blocks; ++b)
        for (int i = 0; i < kBlockSize; ++i) {
            if (b == dryRun.blocks - 1)
                reference.setSample(0, i, 0.5f * (float)std::sin(phase));
            phase += inc;
        }
    EXPECT_LT(rmsDifference(modulated, reference), 0.01f) << "Mix CV -1 must leave only the dry signal";
    EXPECT_GT(rmsDifference(wet, reference), 0.01f) << "the effect must audibly differ from dry with no CV";
}

TEST(FXModuleCVJack, FlangerMixCVAtMinusOneIsDry) { expectMixCVAtMinusOneIsDry<FlangerModule>(); }
TEST(FXModuleCVJack, ChorusMixCVAtMinusOneIsDry) { expectMixCVAtMinusOneIsDry<ChorusModule>(); }
TEST(FXModuleCVJack, PhaserMixCVAtMinusOneIsDry) { expectMixCVAtMinusOneIsDry<PhaserModule>(); }

TEST(FXModuleCVJack, FlangerFeedbackCVChangesTheOutput) {
    FlangerModule probe;
    CvRun run;
    run.cvChannel = channelOf(probe, "Feedback");
    run.cvValue = 0.8f;
    EXPECT_GT(rmsDifference(render<FlangerModule>(run), render<FlangerModule>(CvRun{})), 1.0e-3f);
}

// With the LFO parked (Depth 0) and no feedback, a Flanger is a plain delay of Centre Delay ms, so
// a CV that pushes Centre Delay from 2 ms to 5 ms shifts the tone by 3 ms.
TEST(FXModuleCVJack, FlangerCentreDelayCVShiftsTheDelay) {
    FlangerModule probe;
    auto plainDelay = [](FlangerModule& m) {
        setFloat(m, "depth", 0.0f);
        setFloat(m, "feedback", 0.0f);
        setFloat(m, "mix", 1.0f);
    };
    CvRun run;
    run.cvChannel = channelOf(probe, "Centre Delay");
    run.cvValue = 0.8f; // 2 ms + 0.8 of the 1..5 ms range, clamped: 5 ms
    const auto a = render<FlangerModule>(run, plainDelay);
    const auto b = render<FlangerModule>(CvRun{}, plainDelay);
    EXPECT_GT(rmsDifference(a, b), 1.0e-2f);
}

// ---------------------------------------------------------------------------
// Delay / Reverb: targets that were declared on channels the modules never had.
// ---------------------------------------------------------------------------

TEST(FXModuleCVJack, DelayMixCVAtMinusOneIsDry) { expectMixCVAtMinusOneIsDry<DelayModule>(); }

TEST(FXModuleCVJack, DelayTimeCVChangesTheOutput) {
    DelayModule probe;
    CvRun run;
    run.cvChannel = channelOf(probe, "Time");
    run.cvValue = -0.2f; // 250 ms minus a fifth of the 1..1000 ms range: ~50 ms
    EXPECT_GT(rmsDifference(render<DelayModule>(run), render<DelayModule>(CvRun{})), 1.0e-2f);
}

TEST(FXModuleCVJack, ReverbWetAndDryCVChangeTheOutput) {
    ReverbModule probe;
    // Wet CV -1 (no tail) on top of Dry CV +1 (dry at full) is the same as no reverb at all: the
    // Dry pass is scaled by juce::Reverb's own dryScaleFactor, so compare wet-off runs instead.
    CvRun wetOff;
    wetOff.cvChannel = channelOf(probe, "Wet");
    wetOff.cvValue = -1.0f;
    const auto a = render<ReverbModule>(wetOff);
    const auto b = render<ReverbModule>(CvRun{});
    EXPECT_GT(rmsDifference(a, b), 1.0e-2f) << "removing the tail must be audible";
    CvRun dryOff;
    dryOff.cvChannel = channelOf(probe, "Dry");
    dryOff.cvValue = -1.0f;
    EXPECT_LT(rms(render<ReverbModule>(dryOff)), rms(b)) << "Dry CV -1 removes the dry signal";
}

// ---------------------------------------------------------------------------
// Compressor: Makeup CV lifts the output; Threshold CV pulls it down.
// ---------------------------------------------------------------------------

TEST(FXModuleCVJack, CompressorMakeupCVRaisesTheOutput) {
    CompressorModule probe;
    CvRun quiet;
    quiet.amplitude = 0.05f; // -26 dBFS: under the -12 dB default threshold, so no compression
    CvRun boosted = quiet;
    boosted.cvChannel = channelOf(probe, "Makeup");
    boosted.cvValue = 0.5f; // half of -20..+40 dB = +30 dB
    const float gain = rms(render<CompressorModule>(boosted)) / rms(render<CompressorModule>(quiet));
    EXPECT_NEAR(juce::Decibels::gainToDecibels(gain), 30.0f, 1.0f);
}

TEST(FXModuleCVJack, CompressorThresholdCVLowersTheOutput) {
    CompressorModule probe;
    CvRun loud;
    loud.amplitude = 0.5f; // -6 dBFS: above the default threshold
    CvRun squashed = loud;
    squashed.cvChannel = channelOf(probe, "Threshold");
    squashed.cvValue = -0.5f; // -12 dB minus half of the 60 dB range = -42 dB
    EXPECT_LT(rms(render<CompressorModule>(squashed)), rms(render<CompressorModule>(loud)) * 0.5f);
}

// ---------------------------------------------------------------------------
// Gate: Threshold CV closes a gate the signal would otherwise hold open.
// ---------------------------------------------------------------------------

TEST(FXModuleCVJack, GateThresholdCVClosesTheGate) {
    GateModule probe;
    CvRun open;
    open.amplitude = 0.1f; // -20 dBFS: well above the -40 dB default threshold
    CvRun closed = open;
    closed.cvChannel = channelOf(probe, "Threshold");
    closed.cvValue = 0.5f; // -40 dB plus half of the 80 dB range = 0 dB: nothing gets through
    const float openRms = rms(render<GateModule>(open));
    const float closedRms = rms(render<GateModule>(closed));
    EXPECT_NEAR(openRms, 0.1f / std::sqrt(2.0f), 0.005f) << "an open gate passes the signal";
    EXPECT_LT(closedRms, openRms * 0.01f) << "the gate must sit at its Range floor";
}

// ---------------------------------------------------------------------------
// Limiter: Input Gain CV drives the pre-limiter gain.
// ---------------------------------------------------------------------------

TEST(FXModuleCVJack, LimiterInputGainCVScalesTheInput) {
    LimiterModule probe;
    CvRun quiet;
    quiet.amplitude = 0.01f; // far under the ceiling, so the limiter never engages
    CvRun driven = quiet;
    driven.cvChannel = channelOf(probe, "Input Gain");
    driven.cvValue = 0.5f; // half of -20..+20 dB = +20 dB
    const float gain = rms(render<LimiterModule>(driven)) / rms(render<LimiterModule>(quiet));
    EXPECT_NEAR(juce::Decibels::gainToDecibels(gain), 20.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Pitch Shifter: Fine CV leaves unity, so the pass-through becomes a transposition.
// ---------------------------------------------------------------------------

TEST(FXModuleCVJack, PitchShifterFineAndWindowCVChangeTheOutput) {
    PitchShifterModule probe;
    CvRun fine;
    fine.cvChannel = channelOf(probe, "Fine");
    fine.cvValue = 0.5f; // +100 cents on a -100..+100 range: one semitone up
    const auto unity = render<PitchShifterModule>(CvRun{});
    EXPECT_GT(rmsDifference(render<PitchShifterModule>(fine), unity), 0.05f) << "Fine CV must transpose";

    // Window only matters while transposing: pin it with a base Pitch of +7 semitones.
    auto transposed = [](PitchShifterModule& m) { setFloat(m, "pitch", 7.0f); };
    CvRun window;
    window.cvChannel = channelOf(probe, "Window");
    window.cvValue = -0.9f; // 50 ms towards the 10 ms minimum
    EXPECT_GT(
        rmsDifference(render<PitchShifterModule>(window, transposed), render<PitchShifterModule>(CvRun{}, transposed)),
        1.0e-3f)
        << "Window CV must change the grain length";
}
