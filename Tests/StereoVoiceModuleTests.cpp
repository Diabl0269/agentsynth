// Stereo Audio L/R on the voice modules (issue #219).
//
// Oscillator and Filter grew a second audio leg on a dedicated kRightBase block rather than on
// ch1 the way an FX Dual I/O pair does — ch1 is Waveform / Cutoff CV. These tests pin the two
// things that can silently break: the raw channel numbers every saved patch depends on, and the
// mono-compatibility contract (at Pan 0 / with nothing patched into Audio R, the left leg carries
// exactly what it carried while the module was mono).

#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

float rmsOf(const juce::AudioBuffer<float>& buffer, int channel) {
    if (channel >= buffer.getNumChannels())
        return 0.0f;
    const float* data = buffer.getReadPointer(channel);
    const int n = buffer.getNumSamples();
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += data[i] * data[i];
    return std::sqrt(sum / (float)n);
}

float maxAbsDiff(const juce::AudioBuffer<float>& a, int chA, const juce::AudioBuffer<float>& b, int chB) {
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    float worst = 0.0f;
    for (int i = 0; i < n; ++i)
        worst = std::max(worst, std::abs(a.getReadPointer(chA)[i] - b.getReadPointer(chB)[i]));
    return worst;
}

void setFloatParam(juce::AudioProcessor& module, const juce::String& paramID, float value) {
    for (auto* param : module.getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (f->paramID == paramID) {
                *f = value;
                return;
            }
    FAIL() << "no float parameter \"" << paramID << "\"";
}

void setBoolParam(juce::AudioProcessor& module, const juce::String& paramID, bool value) {
    for (auto* param : module.getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(param))
            if (b->paramID == paramID) {
                *b = value;
                return;
            }
    FAIL() << "no bool parameter \"" << paramID << "\"";
}

/** Renders `blocks` blocks of an Oscillator driven by a MIDI note, returning the final block. */
juce::AudioBuffer<float> renderOscillator(OscillatorModule& osc, int blocks = 2, int panCVChannel = -1,
                                          float panCVValue = 0.0f) {
    juce::AudioBuffer<float> buffer(OscillatorModule::kNumOutputs, kBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

    for (int b = 0; b < blocks; ++b) {
        buffer.clear();
        if (panCVChannel >= 0)
            for (int i = 0; i < kBlockSize; ++i)
                buffer.setSample(panCVChannel, i, panCVValue);
        osc.processBlock(buffer, midi);
        midi.clear();
    }
    return buffer;
}

/** Fills `channel` with a deterministic mid-band tone so filtering has something to bite on. */
void fillTone(juce::AudioBuffer<float>& buffer, int channel, float frequency, float amplitude = 0.5f,
              float phase = 0.0f) {
    float* data = buffer.getWritePointer(channel);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        data[i] =
            amplitude * std::sin(phase + juce::MathConstants<float>::twoPi * frequency * (float)i / (float)kSampleRate);
}

} // namespace

// ---------------------------------------------------------------------------
// The shared pan law
// ---------------------------------------------------------------------------

TEST(StereoPanLaw, CentreLeavesBothLegsAtUnity) {
    // Not equal-power on purpose: an equal-power centre (1/sqrt2) would have quietened every
    // existing mono patch by 3 dB the moment these modules grew a second output jack.
    float gainL = 0.0f;
    float gainR = 0.0f;
    ModuleBase::panGains(0.0f, gainL, gainR);
    EXPECT_FLOAT_EQ(gainL, 1.0f);
    EXPECT_FLOAT_EQ(gainR, 1.0f);
}

TEST(StereoPanLaw, HardPanSilencesTheFarLegAndLeavesTheNearLegAtUnity) {
    float gainL = 0.0f;
    float gainR = 0.0f;

    ModuleBase::panGains(-1.0f, gainL, gainR);
    EXPECT_FLOAT_EQ(gainL, 1.0f);
    EXPECT_FLOAT_EQ(gainR, 0.0f);

    ModuleBase::panGains(1.0f, gainL, gainR);
    EXPECT_FLOAT_EQ(gainL, 0.0f);
    EXPECT_FLOAT_EQ(gainR, 1.0f);
}

TEST(StereoPanLaw, OutOfRangePanIsClamped) {
    float gainL = 0.0f;
    float gainR = 0.0f;
    ModuleBase::panGains(-4.0f, gainL, gainR);
    EXPECT_FLOAT_EQ(gainL, 1.0f);
    EXPECT_FLOAT_EQ(gainR, 0.0f);
}

// ---------------------------------------------------------------------------
// Oscillator — channel map
// ---------------------------------------------------------------------------

TEST(OscillatorStereo, AudioRLivesAboveTheCVBlockNotOnChannelOne) {
    EXPECT_EQ(OscillatorModule::kRightBase, 14);
    EXPECT_EQ(OscillatorModule::kNumOutputs, 22);

    OscillatorModule osc;
    EXPECT_EQ(osc.getTotalNumInputChannels(), 14) << "the input side must not grow — CV routings key off it";
    EXPECT_EQ(osc.getTotalNumOutputChannels(), OscillatorModule::kNumOutputs);
    EXPECT_EQ(osc.getVisibleOutputPortCount(), 2);
}

TEST(OscillatorStereo, ChannelOneStaysWaveformCVAndNeverAdvertisesItselfAsAudioR) {
    OscillatorModule osc;

    const auto in1 = osc.mapInputChannel(1);
    EXPECT_EQ(in1.role, PortRole::ModCV);
    EXPECT_EQ(in1.visibleJackIndex, 1) << "jack 1 is Waveform";

    // The dangerous case: ModuleBase's default output map clamps a raw channel onto a visible jack
    // index, which would report ch1 as the head of the Audio R jack and let a wire be drawn off the
    // Waveform CV channel.
    const auto out1 = osc.mapOutputChannel(1);
    EXPECT_FALSE(out1.isPolyGroupHead);
    EXPECT_EQ(out1.visibleJackIndex, 0);
}

TEST(OscillatorStereo, ExistingCVTargetChannelsAreUnchangedAndPanIsAppended) {
    OscillatorModule osc;

    // Mono: every pre-#219 target keeps its raw channel; Pan takes ch6, which was already declared
    // and unused.
    const std::vector<std::pair<juce::String, int>> expectedMono = {
        {"Pitch", 0}, {"Waveform", 1}, {"Octave", 2}, {"Coarse", 3}, {"Fine", 4}, {"Level", 5}, {"Pan", 6}};
    auto mono = osc.getModulationTargets();
    ASSERT_EQ(mono.size(), expectedMono.size());
    for (size_t i = 0; i < mono.size(); ++i) {
        EXPECT_EQ(mono[i].name, expectedMono[i].first);
        EXPECT_EQ(mono[i].channelIndex, expectedMono[i].second);
    }

    setBoolParam(osc, "poly", true);
    const std::vector<std::pair<juce::String, int>> expectedPoly = {{"Waveform", 8}, {"Octave", 9}, {"Coarse", 10},
                                                                    {"Fine", 11},    {"Level", 12}, {"Pan", 13}};
    auto poly = osc.getModulationTargets();
    ASSERT_EQ(poly.size(), expectedPoly.size());
    for (size_t i = 0; i < poly.size(); ++i) {
        EXPECT_EQ(poly[i].name, expectedPoly[i].first);
        EXPECT_EQ(poly[i].channelIndex, expectedPoly[i].second);
    }
}

TEST(OscillatorStereo, PolyFansBothLegsEightWideFromTheirOwnHeads) {
    OscillatorModule osc;
    setBoolParam(osc, "poly", true);

    const auto left = osc.mapOutputChannel(0);
    EXPECT_EQ(left.visibleJackIndex, 0);
    EXPECT_TRUE(left.isPolyGroupHead);
    EXPECT_EQ(left.polyVoiceSpan, 8);

    const auto right = osc.mapOutputChannel(OscillatorModule::kRightBase);
    EXPECT_EQ(right.visibleJackIndex, 1);
    EXPECT_TRUE(right.isPolyGroupHead);
    EXPECT_EQ(right.polyVoiceSpan, 8) << "Audio R is its own poly head, not voice 1 relabelled";

    // A follower channel inside either fan is not a head.
    EXPECT_FALSE(osc.mapOutputChannel(OscillatorModule::kRightBase + 3).isPolyGroupHead);
}

// ---------------------------------------------------------------------------
// Oscillator — DSP
// ---------------------------------------------------------------------------

TEST(OscillatorStereo, DefaultPanKeepsAudioRIdenticalToAudioL) {
    OscillatorModule osc;
    osc.prepareToPlay(kSampleRate, kBlockSize);

    const auto out = renderOscillator(osc);
    const float left = rmsOf(out, 0);
    ASSERT_GT(left, 0.05f) << "nothing was rendered, so the comparison below proves nothing";
    EXPECT_LT(maxAbsDiff(out, 0, out, OscillatorModule::kRightBase), 1.0e-7f)
        << "at Pan 0 a mono patch that only cables Audio L must sound exactly as it did before #219";
}

TEST(OscillatorStereo, CentrePanDoesNotAttenuateAudioL) {
    // Guards against someone "fixing" the balance law into an equal-power one, which would drop
    // every existing mono patch by 3 dB.
    OscillatorModule centred;
    centred.prepareToPlay(kSampleRate, kBlockSize);
    const float centredLeft = rmsOf(renderOscillator(centred), 0);

    OscillatorModule hardLeft;
    hardLeft.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(hardLeft, "pan", -1.0f);
    const float hardLeftLeft = rmsOf(renderOscillator(hardLeft), 0);

    ASSERT_GT(centredLeft, 0.05f);
    EXPECT_NEAR(centredLeft, hardLeftLeft, 1.0e-4f);
}

TEST(OscillatorStereo, HardPanSilencesTheOppositeLeg) {
    OscillatorModule osc;
    osc.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(osc, "pan", 1.0f);

    const auto out = renderOscillator(osc);
    EXPECT_LT(rmsOf(out, 0), 1.0e-6f);
    EXPECT_GT(rmsOf(out, OscillatorModule::kRightBase), 0.05f);

    setFloatParam(osc, "pan", -1.0f);
    const auto flipped = renderOscillator(osc);
    EXPECT_GT(rmsOf(flipped, 0), 0.05f);
    EXPECT_LT(rmsOf(flipped, OscillatorModule::kRightBase), 1.0e-6f);
}

TEST(OscillatorStereo, PanCVMovesTheImage) {
    OscillatorModule osc;
    osc.prepareToPlay(kSampleRate, kBlockSize);

    const int panCV = 6; // mono Pan jack
    const auto out = renderOscillator(osc, 2, panCV, 1.0f);
    EXPECT_LT(rmsOf(out, 0), 1.0e-6f) << "a full-scale Pan CV should push the image hard right";
    EXPECT_GT(rmsOf(out, OscillatorModule::kRightBase), 0.05f);
}

TEST(OscillatorStereo, PolyRendersBothLegsPerVoice) {
    OscillatorModule osc;
    setBoolParam(osc, "poly", true);
    osc.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(OscillatorModule::kNumOutputs, kBlockSize);
    juce::MidiBuffer midi;

    // Pitch CV in Hz on the per-voice fan.
    for (int block = 0; block < 2; ++block) {
        buffer.clear();
        for (int v = 0; v < 3; ++v)
            for (int i = 0; i < kBlockSize; ++i)
                buffer.setSample(v, i, 220.0f * (float)(v + 1));
        osc.processBlock(buffer, midi);
    }

    for (int v = 0; v < 3; ++v) {
        EXPECT_GT(rmsOf(buffer, v), 0.01f) << "voice " << v << " left leg";
        EXPECT_LT(maxAbsDiff(buffer, v, buffer, OscillatorModule::kRightBase + v), 1.0e-7f)
            << "voice " << v << " should be centred by default";
    }
}

TEST(OscillatorStereo, PolyLeavesAudioRIntactAndTheCVBlockSilent) {
    // Pins the end state of a poly block: the CV channels between the two audio blocks must not
    // leak downstream as audio, and the right leg must survive to the end of the block. (The clear
    // runs before the stereo pass, so this does not by itself catch an unbounded clear — the
    // equivalent Filter test does, because there the clear runs last.)
    OscillatorModule osc;
    setBoolParam(osc, "poly", true);
    osc.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(OscillatorModule::kNumOutputs, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 2; ++block) {
        buffer.clear();
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(0, i, 440.0f);
        osc.processBlock(buffer, midi);
    }

    EXPECT_GT(rmsOf(buffer, OscillatorModule::kRightBase), 0.01f);
    // The CV channels between the two audio blocks must still be silent.
    for (int ch = 8; ch < OscillatorModule::kRightBase; ++ch)
        EXPECT_LT(rmsOf(buffer, ch), 1.0e-6f) << "CV channel " << ch << " leaked";
}

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

// ---------------------------------------------------------------------------
// VCA — the last link in the default preset's stereo chain
// ---------------------------------------------------------------------------

TEST(VCAStereo, AudioRIsItsOwnBlockAndCVKeepsItsRawChannels) {
    EXPECT_EQ(VCAModule::kRightBase, 16);
    EXPECT_EQ(VCAModule::kNumChannels, 24);

    VCAModule vca;
    EXPECT_EQ(vca.getTotalNumInputChannels(), VCAModule::kNumChannels);
    EXPECT_EQ(vca.getTotalNumOutputChannels(), VCAModule::kNumChannels);
    EXPECT_EQ(vca.getVisibleInputPortCount(), 3);
    EXPECT_EQ(vca.getVisibleOutputPortCount(), 2);

    // The gain CV keeps raw ch1 (mono) / ch8 (poly) — only its visible slot moved.
    EXPECT_EQ(vca.getModulationTargets()[0].channelIndex, 1);
    EXPECT_EQ(vca.mapInputChannel(1).role, PortRole::ModCV);
    EXPECT_EQ(vca.mapInputChannel(1).visibleJackIndex, 2);

    // ch1 must never advertise itself as the Audio R output head.
    const auto out1 = vca.mapOutputChannel(1);
    EXPECT_FALSE(out1.isPolyGroupHead);
    EXPECT_EQ(out1.visibleJackIndex, 0);
    EXPECT_TRUE(vca.mapOutputChannel(VCAModule::kRightBase).isPolyGroupHead);
    EXPECT_EQ(vca.mapOutputChannel(VCAModule::kRightBase).visibleJackIndex, 1);
}

TEST(VCAStereo, MonoGatesBothLegsWithTheSameGainAndCV) {
    VCAModule vca;
    vca.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(vca, "gain", 1.0f);

    juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, kBlockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 4; ++block) {
        buffer.clear();
        fillTone(buffer, 0, 1000.0f);
        fillTone(buffer, VCAModule::kRightBase, 1000.0f);
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(1, i, 0.5f); // gain CV
        vca.processBlock(buffer, midi);
    }

    ASSERT_GT(rmsOf(buffer, 0), 1.0e-4f);
    EXPECT_LT(maxAbsDiff(buffer, 0, buffer, VCAModule::kRightBase), 1.0e-6f)
        << "both legs must ride one gain ramp, or the image drifts while Gain moves";
}

TEST(VCAStereo, SilentAudioRStaysSilent) {
    VCAModule vca;
    vca.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, kBlockSize);
    buffer.clear();
    fillTone(buffer, 0, 1000.0f);
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(1, i, 1.0f);

    juce::MidiBuffer midi;
    vca.processBlock(buffer, midi);

    EXPECT_GT(rmsOf(buffer, 0), 1.0e-4f);
    EXPECT_LT(rmsOf(buffer, VCAModule::kRightBase), 1.0e-9f);
}

TEST(VCAStereo, ClearsDoNotEraseAudioR) {
    VCAModule vca;
    vca.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(vca, "gain", 1.0f);

    juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, kBlockSize);
    buffer.clear();
    fillTone(buffer, VCAModule::kRightBase, 1000.0f);
    for (int i = 0; i < kBlockSize; ++i)
        buffer.setSample(1, i, 1.0f);

    juce::MidiBuffer midi;
    vca.processBlock(buffer, midi);
    EXPECT_GT(rmsOf(buffer, VCAModule::kRightBase), 1.0e-4f);

    // And on bypass the right leg passes through dry rather than being cleared with the CV block.
    VCAModule bypassed;
    bypassed.prepareToPlay(kSampleRate, kBlockSize);
    bypassed.setBypassed(true);
    juce::AudioBuffer<float> dry(VCAModule::kNumChannels, kBlockSize);
    dry.clear();
    fillTone(dry, 0, 1000.0f);
    fillTone(dry, VCAModule::kRightBase, 1000.0f);
    juce::AudioBuffer<float> expected(dry);
    bypassed.processBlock(dry, midi);

    EXPECT_LT(maxAbsDiff(dry, 0, expected, 0), 1.0e-9f);
    EXPECT_LT(maxAbsDiff(dry, VCAModule::kRightBase, expected, VCAModule::kRightBase), 1.0e-9f);
}

TEST(VCAStereo, PolySumsTheRightBlockToItsOwnHead) {
    VCAModule vca;
    setBoolParam(vca, "poly", true);
    vca.prepareToPlay(kSampleRate, kBlockSize);
    setFloatParam(vca, "gain", 1.0f);

    juce::AudioBuffer<float> buffer(VCAModule::kNumChannels, kBlockSize);
    buffer.clear();
    for (int v = 0; v < 3; ++v) {
        fillTone(buffer, v, 300.0f * (float)(v + 1));
        fillTone(buffer, VCAModule::kRightBase + v, 300.0f * (float)(v + 1));
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample(8 + v, i, 1.0f); // per-voice gain CV
    }

    juce::MidiBuffer midi;
    vca.processBlock(buffer, midi);

    EXPECT_GT(rmsOf(buffer, 0), 1.0e-4f);
    EXPECT_GT(rmsOf(buffer, VCAModule::kRightBase), 1.0e-4f);
    // Follower voices of the right block are zeroed, exactly like the left block's.
    for (int v = 1; v < 8; ++v)
        EXPECT_LT(rmsOf(buffer, VCAModule::kRightBase + v), 1.0e-9f) << "right follower voice " << v;
}

// ---------------------------------------------------------------------------
// The default preset carries stereo all the way to the FX chain
// ---------------------------------------------------------------------------

TEST(StereoDefaultPreset, RightLegIsWiredFromOscillatorThroughToTheFX) {
    juce::AudioProcessorGraph graph;
    ASSERT_TRUE(synth::PresetManager::loadDefaultPreset(graph));

    auto nodeNamed = [&graph](const juce::String& name) -> juce::AudioProcessorGraph::NodeID {
        for (auto* node : graph.getNodes())
            if (node->getProcessor()->getName() == name)
                return node->nodeID;
        return {};
    };

    const auto osc = nodeNamed("Oscillator");
    const auto filter = nodeNamed("Filter");
    const auto vca = nodeNamed("VCA");
    const auto dist = nodeNamed("Distortion");
    ASSERT_NE(osc.uid, 0u);
    ASSERT_NE(filter.uid, 0u);
    ASSERT_NE(vca.uid, 0u);
    ASSERT_NE(dist.uid, 0u);

    // Left leg: unchanged from before #219.
    EXPECT_TRUE(graph.isConnected({{osc, 0}, {filter, 0}}));
    EXPECT_TRUE(graph.isConnected({{filter, 0}, {vca, 0}}));
    EXPECT_TRUE(graph.isConnected({{vca, 0}, {dist, 0}}));

    // Right leg: each module's own kRightBase block, never ch1 (which is CV on all three).
    EXPECT_TRUE(graph.isConnected({{osc, OscillatorModule::kRightBase}, {filter, FilterModule::kRightBase}}));
    EXPECT_TRUE(graph.isConnected({{filter, FilterModule::kRightBase}, {vca, VCAModule::kRightBase}}));
    EXPECT_TRUE(graph.isConnected({{vca, VCAModule::kRightBase}, {dist, 1}}))
        << "the FX chain takes a contiguous ch0/ch1 pair, so the right leg lands on Distortion ch1";

    // The gain/cutoff CV wires must not have been displaced onto an audio leg.
    EXPECT_FALSE(graph.isConnected({{osc, 0}, {filter, 1}}));
    EXPECT_FALSE(graph.isConnected({{filter, 0}, {vca, 1}}));
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
