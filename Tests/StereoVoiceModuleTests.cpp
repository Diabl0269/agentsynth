// Stereo Audio L/R on the voice modules (issue #219).
//
// Oscillator and Filter grew a second audio leg on a dedicated kRightBase block rather than on
// ch1 the way an FX Dual I/O pair does — ch1 is Waveform / Cutoff CV. These tests pin the two
// things that can silently break: the raw channel numbers every saved patch depends on, and the
// mono-compatibility contract (at Pan 0 / with nothing patched into Audio R, the left leg carries
// exactly what it carried while the module was mono).

#include "AI/AIStateMapper.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FX/RingModulatorModule.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

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
// The Dual I/O toggle on split-block modules
// ---------------------------------------------------------------------------

TEST(SplitBlockDualIO, EveryStereoCapableModuleHasTheToggle) {
    OscillatorModule osc;
    FilterModule filter;
    VCAModule vca;

    for (ModuleBase* m : {(ModuleBase*)&osc, (ModuleBase*)&filter, (ModuleBase*)&vca}) {
        EXPECT_TRUE(m->hasDualIOParameter()) << m->getName();
        EXPECT_TRUE(m->isDualIO()) << m->getName() << " should default to showing both legs";
        EXPECT_TRUE(m->hasSplitBlockStereo()) << m->getName() << " keeps its right leg off ch1";
        EXPECT_GT(m->rightAudioLegChannel(), 1) << m->getName();
    }

    EXPECT_EQ(osc.rightAudioLegChannel(), OscillatorModule::kRightBase);
    EXPECT_EQ(filter.rightAudioLegChannel(), FilterModule::kRightBase);
    EXPECT_EQ(vca.rightAudioLegChannel(), VCAModule::kRightBase);
}

TEST(SplitBlockDualIO, CollapsingRestoresThePreStereoJackLayout) {
    // Dual I/O off must look exactly like the module did before #219 — that is the whole point of
    // calling it a jack-layout toggle rather than a mono/stereo switch.
    FilterModule filter;
    setBoolParam(filter, "dualIO", false);
    EXPECT_EQ(filter.getVisibleInputPortCount(), 4);
    EXPECT_EQ(filter.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(filter.getInputPortLabel(0), "Audio");
    EXPECT_EQ(filter.getInputPortLabel(1), "Cutoff");
    EXPECT_EQ(filter.getInputPortLabel(2), "Resonance");
    EXPECT_EQ(filter.getInputPortLabel(3), "Drive");
    EXPECT_EQ(filter.getOutputPortLabel(0), "Audio");

    VCAModule vca;
    setBoolParam(vca, "dualIO", false);
    EXPECT_EQ(vca.getVisibleInputPortCount(), 2);
    EXPECT_EQ(vca.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(vca.getInputPortLabel(0), "Audio");
    EXPECT_EQ(vca.getInputPortLabel(1), "CV");

    OscillatorModule osc;
    setBoolParam(osc, "dualIO", false);
    EXPECT_EQ(osc.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(osc.getOutputPortLabel(0), "Audio");
    EXPECT_EQ(osc.getVisibleInputPortCount(), OscillatorModule::kNumJacks) << "CV jacks are unaffected";
}

TEST(SplitBlockDualIO, CollapsedRightBlockIsNotAddressableAsAJack) {
    // The right block keeps rendering, but nothing may anchor a cable to it while it is hidden —
    // otherwise a wire exists that the user can hear but never reach.
    OscillatorModule osc;
    setBoolParam(osc, "dualIO", false);
    EXPECT_FALSE(osc.mapOutputChannel(OscillatorModule::kRightBase).isPolyGroupHead);

    FilterModule filter;
    setBoolParam(filter, "dualIO", false);
    EXPECT_FALSE(filter.mapInputChannel(FilterModule::kRightBase).isPolyGroupHead);
    EXPECT_FALSE(filter.mapOutputChannel(FilterModule::kRightBase).isPolyGroupHead);

    // Every visible jack still resolves to exactly one raw head in both states.
    for (bool dual : {true, false}) {
        FilterModule f;
        setBoolParam(f, "dualIO", dual);
        for (int jack = 0; jack < f.getVisibleInputPortCount(); ++jack)
            EXPECT_EQ(f.getJackTargets(jack, true).size(), 1u) << "dual=" << dual << " jack " << jack;
    }
}

TEST(SplitBlockDualIO, CollapsingDoesNotDisturbTheCVChannelMap) {
    // The CV jacks shift visible slot as the audio jacks appear and disappear, but their RAW
    // channels — the thing saved patches key off — must not move.
    for (bool dual : {true, false}) {
        FilterModule filter;
        setBoolParam(filter, "dualIO", dual);
        auto targets = filter.getModulationTargets();
        ASSERT_EQ(targets.size(), 3u);
        EXPECT_EQ(targets[0].channelIndex, 1) << "Cutoff, dual=" << dual;
        EXPECT_EQ(targets[1].channelIndex, 2);
        EXPECT_EQ(targets[2].channelIndex, 3);

        VCAModule vca;
        setBoolParam(vca, "dualIO", dual);
        EXPECT_EQ(vca.getModulationTargets()[0].channelIndex, 1) << "VCA gain CV, dual=" << dual;

        OscillatorModule osc;
        setBoolParam(osc, "dualIO", dual);
        auto oscTargets = osc.getModulationTargets();
        ASSERT_EQ(oscTargets.size(), 7u);
        EXPECT_EQ(oscTargets[1].channelIndex, 1) << "Waveform CV, dual=" << dual;
        EXPECT_EQ(oscTargets[6].channelIndex, 6) << "Pan CV, dual=" << dual;
    }
}

TEST(SplitBlockDualIO, FlippingTheParameterFlipsTheVisibleJacks) {
    // The header button and the Preferences default both work by setting this one parameter, so
    // this is what "pushing the toggle changes the module" reduces to.
    FilterModule filter;
    auto* dual = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&filter, "dualIO"));
    ASSERT_NE(dual, nullptr);

    ASSERT_TRUE(filter.isDualIO());
    ASSERT_EQ(filter.getVisibleInputPortCount(), 5);

    dual->setValueNotifyingHost(0.0f);
    EXPECT_FALSE(filter.isDualIO());
    EXPECT_EQ(filter.getVisibleInputPortCount(), 4) << "collapsing must actually drop a jack";
    EXPECT_EQ(filter.getVisibleOutputPortCount(), 1);

    dual->setValueNotifyingHost(1.0f);
    EXPECT_TRUE(filter.isDualIO());
    EXPECT_EQ(filter.getVisibleInputPortCount(), 5) << "and re-expanding must bring it back";
    EXPECT_EQ(filter.getVisibleOutputPortCount(), 2);
}

TEST(SplitBlockDualIO, CollapsingDoesNotChangeWhatTheModuleRenders) {
    // Collapsing hides a jack; it must not alter the DSP. Audio L is byte-identical either way.
    auto renderWith = [](bool dual) {
        OscillatorModule osc;
        setBoolParam(osc, "dualIO", dual);
        osc.prepareToPlay(kSampleRate, kBlockSize);
        return renderOscillator(osc);
    };

    const auto dualOut = renderWith(true);
    const auto singleOut = renderWith(false);
    ASSERT_GT(rmsOf(dualOut, 0), 0.05f);
    EXPECT_LT(maxAbsDiff(dualOut, 0, singleOut, 0), 1.0e-7f);
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

// ---------------------------------------------------------------------------
// Dual I/O is INHERITED, not registered (ModuleBase::StereoAudio)
//
// The toggle used to be a per-module `addDualIOParameter()` call, and the Ring Modulator shipped a
// stereo output pair without one — no header control, no Preferences row, nothing red. The base
// constructor now decides from the module's channel shape, so the only per-module decision left is
// the exception. These tests are what make an exception impossible to take silently: they sweep the
// whole factory and compare each module against the shape rule plus the two tables below.
// ---------------------------------------------------------------------------

namespace {

// Modules whose channel shape matches StereoAudio::Auto (>= 2 in, exactly 2 out) but which are NOT
// stereo — each passes StereoAudio::None, and each needs a reason recorded here.
const std::vector<std::pair<juce::String, const char*>> kDualIOOptOuts = {
    {"Comparator", "in ch0/ch1 are Signal + Threshold CV and out ch0/ch1 are Gate + inverted Gate — "
                   "two unrelated CV jacks per side, and no audio output at all"},
    {"Rec Tap", "a hidden recording tap: its two channels are the take's capture pair, wired by the "
                "record flow rather than patched, and it has no card to put a jack toggle on"},
};

// Modules that declare a second audio leg the shape rule cannot see (their own kRightBase block, or
// a ch0/ch1 pair alongside further outputs). Each passes StereoAudio::Declared and ships SPLIT.
const std::vector<juce::String> kDualIODeclared = {"Oscillator", "Wavetable", "Filter", "VCA", "Sampler"};

bool containsName(const std::vector<juce::String>& v, const juce::String& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

bool isDualIOOptOut(const juce::String& name) {
    for (const auto& entry : kDualIOOptOuts)
        if (entry.first == name)
            return true;
    return false;
}

// A brand-new module: the FX channel shape and NOTHING else. No Dual I/O registration, no jack
// maps, no port labels. Whatever this class can do, a module author gets for free.
class ShapeOnlyStereoModule : public ModuleBase {
public:
    ShapeOnlyStereoModule()
        : ModuleBase("Shape Only", 4, 2) {} // 2 audio + 2 CV in, stereo out — the Distortion shape

    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Distortion; }
};

} // namespace

TEST(StereoDeclaration, EveryFactoryModuleFollowsTheShapeRuleOrADocumentedException) {
    // The qualification rule, applied to every module the factory can build. A new stereo FX passes
    // this the moment it exists; a new module that needs an exception fails until it is listed above
    // with a reason.
    int autoQualified = 0;
    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames()) {
        SCOPED_TRACE(name.toStdString());
        auto probe = synth::AIStateMapper::createModule(name);
        ASSERT_NE(probe, nullptr);
        auto* mb = dynamic_cast<ModuleBase*>(probe.get());
        if (mb == nullptr)
            continue; // graph I/O nodes are not modules

        const bool shapeMatches =
            ModuleBase::hasStereoOutputPairShape(mb->getTotalNumInputChannels(), mb->getTotalNumOutputChannels());
        const bool declared = containsName(kDualIODeclared, name);

        if (declared) {
            EXPECT_TRUE(mb->hasDualIOParameter()) << "a Declared module must carry the toggle";
            EXPECT_TRUE(mb->isDualIO()) << "StereoAudio::Declared ships SPLIT — five saved-patch defaults "
                                           "depend on this";
        } else if (shapeMatches && !isDualIOOptOut(name)) {
            ++autoQualified;
            EXPECT_TRUE(mb->hasDualIOParameter())
                << "the channel shape says stereo, so the base must have added the toggle with no help "
                   "from the module";
            EXPECT_FALSE(mb->isDualIO()) << "StereoAudio::Auto ships COLLAPSED";
        } else {
            EXPECT_FALSE(mb->hasDualIOParameter())
                << (isDualIOOptOut(name) ? "this module opts out" : "this module's shape is not a stereo pair");
        }
    }
    EXPECT_GT(autoQualified, 10) << "expected the whole FX family to qualify by shape alone";
}

TEST(StereoDeclaration, TheExceptionTablesHaveNoStaleEntries) {
    // A renamed or deleted module must not leave a silent exception behind.
    const auto factory = synth::AIStateMapper::moduleFactoryTypeNames();
    for (const auto& entry : kDualIOOptOuts) {
        EXPECT_TRUE(factory.contains(entry.first)) << entry.first << " is no longer a factory module";
        EXPECT_GT(juce::String(entry.second).length(), 20) << entry.first << " needs a real reason recorded";
        auto probe = synth::AIStateMapper::createModule(entry.first);
        auto* mb = dynamic_cast<ModuleBase*>(probe.get());
        ASSERT_NE(mb, nullptr);
        EXPECT_TRUE(
            ModuleBase::hasStereoOutputPairShape(mb->getTotalNumInputChannels(), mb->getTotalNumOutputChannels()))
            << entry.first << " no longer matches the Auto shape, so it does not need an opt-out";
    }
    for (const auto& name : kDualIODeclared)
        EXPECT_TRUE(factory.contains(name)) << name << " is no longer a factory module";
}

TEST(StereoDeclaration, ANewModuleWithTheStereoShapeInheritsTheWholeToggle) {
    // The claim in one test: a module that writes no Dual I/O code at all still gets the parameter
    // AND the collapsing output jack.
    ShapeOnlyStereoModule fresh;
    ASSERT_TRUE(fresh.hasDualIOParameter()) << "the base constructor must add the toggle from the shape alone";
    EXPECT_FALSE(fresh.isDualIO());
    EXPECT_TRUE(fresh.hasCollapsibleOutputPair());

    EXPECT_EQ(fresh.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(fresh.getOutputPortLabel(0), "Audio");
    const auto collapsedHead = fresh.mapOutputChannel(0);
    EXPECT_TRUE(collapsedHead.isPolyGroupHead);
    EXPECT_EQ(collapsedHead.polyVoiceSpan, 2) << "the collapsed jack owns both raw legs";
    EXPECT_EQ(collapsedHead.role, PortRole::Audio);
    EXPECT_FALSE(fresh.mapOutputChannel(1).isPolyGroupHead);

    setBoolParam(fresh, "dualIO", true);
    EXPECT_EQ(fresh.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(fresh.getOutputPortLabel(0), "Left");
    EXPECT_EQ(fresh.getOutputPortLabel(1), "Right");
    EXPECT_TRUE(fresh.mapOutputChannel(1).isPolyGroupHead);
    EXPECT_EQ(fresh.rightAudioLegChannel(), 1);

    // The INPUT side is deliberately NOT inferred — ch0/ch1 being an input pair is not knowable from
    // the shape (Voice Mixer's are voice inputs, the Ring Modulator's are Carrier + Modulator), so
    // the inherited input map leaves every input jack alone.
    EXPECT_EQ(fresh.getVisibleInputPortCount(), 4);
    EXPECT_NE(fresh.mapInputChannel(1).role, PortRole::Audio);
}

TEST(StereoDeclaration, RingModulatorGetsTheTogglePurelyByInheritance) {
    // RingModulatorModule's constructor contains no Dual I/O registration and the class overrides
    // none of the three output-side hooks — this is the module the bug was reported on.
    RingModulatorModule ringMod;
    ASSERT_TRUE(ringMod.hasDualIOParameter());
    EXPECT_FALSE(ringMod.isDualIO()) << "collapsed by default, like every other FX";
    EXPECT_TRUE(ringMod.hasCollapsibleOutputPair());
    EXPECT_EQ(ringMod.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(ringMod.getOutputPortLabel(0), "Audio");
    ASSERT_EQ(ringMod.getJackTargets(0, /*isInput=*/false).size(), 1u);
    EXPECT_EQ(ringMod.getJackTargets(0, false)[0].voiceSpan, 2);

    setBoolParam(ringMod, "dualIO", true);
    EXPECT_EQ(ringMod.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(ringMod.getOutputPortLabel(0), "Left");
    EXPECT_EQ(ringMod.getOutputPortLabel(1), "Right");

    // ...and its five input jacks are still its own, in both states.
    for (bool dual : {false, true}) {
        setBoolParam(ringMod, "dualIO", dual);
        EXPECT_EQ(ringMod.getVisibleInputPortCount(), 5) << "dual=" << dual;
        EXPECT_EQ(ringMod.getInputPortLabel(0), "Carrier") << "dual=" << dual;
        EXPECT_EQ(ringMod.getInputPortLabel(1), "Modulator") << "dual=" << dual;
        EXPECT_NE(ringMod.mapInputChannel(1).role, PortRole::Audio) << "dual=" << dual;
    }
}

TEST(StereoDeclaration, TheRegistryReportsEveryInheritedToggle) {
    // The dynamic registry asks hasDualIOParameter(), so inherited toggles have to show up in it —
    // that is what carries them into the Preferences popup and the global default.
    const auto& registry = synth::AIStateMapper::dualIOCapableModuleTypes();
    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames()) {
        SCOPED_TRACE(name.toStdString());
        auto probe = synth::AIStateMapper::createModule(name);
        auto* mb = dynamic_cast<ModuleBase*>(probe.get());
        const bool expected = mb != nullptr && mb->hasDualIOParameter();
        EXPECT_EQ(registry.contains(name), expected);
    }
    EXPECT_TRUE(registry.contains("Ring Modulator"));
    EXPECT_FALSE(registry.contains("Comparator"));
    EXPECT_FALSE(registry.contains("Rec Tap"));
}

TEST(StereoDeclaration, MuteAndCVClearHoldForEveryDualIOCapableModuleInBothStates) {
    // The mute and CV-clear invariants, swept over the registry rather than spot-checked per module,
    // because the set now grows by inheritance.
    //
    // Mute is asserted for EVERY module in the registry: it clears the whole buffer, whatever the
    // channel layout. The CV-clear half is asserted only for the modules whose stereo pair the base
    // INFERRED (hasCollapsibleOutputPair() — audio on ch0/ch1, so every CV channel is >= 2, which is
    // what the bypass/mute contract in docs/architecture.md is written against). It is deliberately
    // NOT asserted for the split-block modules: on those, ch0 is a pitch CV *input* and the Audio L
    // *output* at the same time, and ch1 is a CV input below the audio pair — "the CV channels are
    // zero after a block" is not a statement about them. Their clears are pinned per module by
    // FilterStereo.CVClearDoesNotEraseAudioR / VCAStereo.ClearsDoNotEraseAudioR and the Oscillator's
    // own poly-clear tests.
    //
    // Bypass's AUDIO behaviour is also not asserted here — it splits by family (dry pass-through vs.
    // clear for a pure source) and each module pins its own.
    for (const auto& name : synth::AIStateMapper::dualIOCapableModuleTypes()) {
        for (bool dual : {false, true}) {
            SCOPED_TRACE(name.toStdString() + (dual ? " [dual]" : " [collapsed]"));
            auto probe = synth::AIStateMapper::createModule(name);
            auto* mb = dynamic_cast<ModuleBase*>(probe.get());
            ASSERT_NE(mb, nullptr);
            setBoolParam(*mb, "dualIO", dual);
            mb->prepareToPlay(kSampleRate, kBlockSize);

            const int channels = std::max(mb->getTotalNumInputChannels(), mb->getTotalNumOutputChannels());
            auto fill = [&](juce::AudioBuffer<float>& b) {
                b.clear();
                for (int ch = 0; ch < std::min(2, channels); ++ch)
                    fillTone(b, ch, 1000.0f);
                for (const auto& t : mb->getModulationTargets())
                    if (t.channelIndex < channels)
                        for (int i = 0; i < kBlockSize; ++i)
                            b.setSample(t.channelIndex, i, 0.5f);
            };

            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buffer(channels, kBlockSize);

            // Mute clears every channel — for the modules that HAVE a mute. Mute is opt-in like the
            // level stage (Voice Mixer is a summing utility and never took one), and setMuted() on a
            // module without the parameter dereferences a null, so ask first.
            if (findParameterByID(mb, "muted") != nullptr) {
                fill(buffer);
                mb->setMuted(true);
                mb->processBlock(buffer, midi);
                for (int ch = 0; ch < channels; ++ch)
                    EXPECT_LT(rmsOf(buffer, ch), 1.0e-9f) << "mute left channel " << ch << " audible";
                mb->setMuted(false);
            }

            // A normal block and a bypassed block both leave the CV inputs cleared — for the
            // inferred-pair family only (see the header comment).
            if (mb->hasCollapsibleOutputPair()) {
                for (bool bypassed : {false, true}) {
                    mb->setBypassed(bypassed);
                    fill(buffer);
                    mb->processBlock(buffer, midi);
                    for (const auto& t : mb->getModulationTargets()) {
                        ASSERT_GE(t.channelIndex, 2) << "an inferred stereo pair owns ch0/ch1, so a CV jack "
                                                        "cannot sit below ch2";
                        if (t.channelIndex < channels)
                            EXPECT_LT(rmsOf(buffer, t.channelIndex), 1.0e-6f)
                                << "CV channel " << t.channelIndex << " (" << t.name
                                << ") leaked, bypassed=" << bypassed;
                    }
                }
                mb->setBypassed(false);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// State compatibility: a patch saved BEFORE Dual I/O moved into the base
// ---------------------------------------------------------------------------

TEST(StereoDeclaration, APatchSavedBeforeTheMoveLoadsWithTheSameLayout) {
    // Authored by hand in the shape our own save path emitted before this change: parameters keyed
    // by ID (never by index), an explicit "dualIO" for the modules that had one, and NO "dualIO" for
    // the Ring Modulator, which had no such parameter then. The ids and defaults are unchanged, so
    // every module must come back with exactly the layout the patch describes — and the module that
    // gained the parameter must fall to its default rather than to something arbitrary.
    const juce::String legacyPatch = R"({
      "nodes": [
        {"id": 1, "type": "Oscillator", "params": {"waveform": "Saw", "dualIO": false}},
        {"id": 2, "type": "Filter", "params": {"cutoff": 800.0, "dualIO": true}},
        {"id": 3, "type": "Reverb", "params": {"roomSize": 0.7, "dualIO": true}},
        {"id": 4, "type": "Delay", "params": {"time": 250.0}},
        {"id": 5, "type": "Ring Modulator", "params": {"mix": 0.5}},
        {"id": 6, "type": "Audio Output"}
      ],
      "connections": [
        {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
        {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0},
        {"src": 3, "srcPort": 0, "dst": 6, "dstPort": 0}
      ]
    })";

    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(legacyPatch), graph,
                                                       /*clearExisting=*/true, /*trusted=*/true));

    auto moduleNamed = [&graph](const juce::String& name) -> ModuleBase* {
        for (auto* node : graph.getNodes())
            if (node->getProcessor()->getName() == name)
                return dynamic_cast<ModuleBase*>(node->getProcessor());
        return nullptr;
    };

    auto* osc = moduleNamed("Oscillator");
    auto* filter = moduleNamed("Filter");
    auto* reverb = moduleNamed("Reverb");
    auto* delay = moduleNamed("Delay");
    auto* ringMod = moduleNamed("Ring Modulator");
    ASSERT_NE(osc, nullptr);
    ASSERT_NE(filter, nullptr);
    ASSERT_NE(reverb, nullptr);
    ASSERT_NE(delay, nullptr);
    ASSERT_NE(ringMod, nullptr);

    EXPECT_FALSE(osc->isDualIO()) << "an explicit false must survive the move to the base";
    EXPECT_EQ(osc->getVisibleOutputPortCount(), 1);
    EXPECT_TRUE(filter->isDualIO()) << "an explicit true must survive";
    EXPECT_EQ(filter->getVisibleInputPortCount(), 5);
    EXPECT_TRUE(reverb->isDualIO());
    EXPECT_EQ(reverb->getVisibleOutputPortCount(), 2);
    EXPECT_FALSE(delay->isDualIO()) << "an omitted dualIO must fall to the module's default (collapsed)";
    EXPECT_FALSE(ringMod->isDualIO()) << "a patch older than this module's toggle must open collapsed, not "
                                         "split — its default is what every other FX has";

    // ...and re-saving now carries the values forward unchanged.
    const auto resaved = synth::AIStateMapper::graphToJSON(graph);
    juce::AudioProcessorGraph reloaded;
    reloaded.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(resaved, reloaded, /*clearExisting=*/true, /*trusted=*/true));

    auto dualOf = [](juce::AudioProcessorGraph& g, const juce::String& name) {
        for (auto* node : g.getNodes())
            if (node->getProcessor()->getName() == name)
                if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
                    return mb->isDualIO() ? 1 : 0;
        return -1;
    };
    for (const char* name : {"Oscillator", "Filter", "Reverb", "Delay", "Ring Modulator"})
        EXPECT_EQ(dualOf(reloaded, name), dualOf(graph, name)) << name << " changed layout across a save/load";
}

TEST(StereoDeclaration, ALegacyStateBlobWithNoDualIOPropertyLoadsAtTheModuleDefault) {
    // The other state path: ModuleBase::getStateInformation's XML, which the plugin's session state
    // and ProjectBundle use. Mirrors OutputLevelHelper's legacy-blob test — a blob written before the
    // parameter existed must leave the module at its own default, not at 0 or at whatever the
    // ValueTree happens to yield for a missing property.
    auto blobWithoutDualIO = [](ModuleBase& module) {
        juce::MemoryBlock full;
        module.getStateInformation(full);
        auto xml = juce::AudioProcessor::getXmlFromBinary(full.getData(), (int)full.getSize());
        // Make it look like a save from before the toggle existed.
        xml->removeAttribute("dualIO");
        juce::MemoryBlock legacy;
        juce::AudioProcessor::copyXmlToBinary(*xml, legacy);
        return legacy;
    };

    // `setStateInformation` only writes the properties the blob carries, so the value a legacy blob
    // lands on is whatever the instance already held — and every real caller (the plugin's session
    // restore, AIStateMapper) constructs the module first and applies state second. So the case that
    // matters is a FRESHLY constructed node: it must keep its own default.
    {
        // Auto family: default collapsed. The Ring Modulator is the real case — patches predating
        // its toggle exist in the wild.
        RingModulatorModule source;
        setBoolParam(source, "dualIO", true); // a value the legacy blob will NOT carry
        const auto legacy = blobWithoutDualIO(source);

        RingModulatorModule target; // fresh, exactly as a load would create it
        target.setStateInformation(legacy.getData(), (int)legacy.getSize());
        EXPECT_FALSE(target.isDualIO()) << "a blob with no dualIO must leave an Auto module collapsed";
        EXPECT_EQ(target.getVisibleOutputPortCount(), 1);
    }

    {
        // Declared family: default split, and a legacy blob must not silently collapse a saved patch.
        FilterModule source;
        setBoolParam(source, "dualIO", false);
        const auto legacy = blobWithoutDualIO(source);

        FilterModule target; // fresh
        target.setStateInformation(legacy.getData(), (int)legacy.getSize());
        EXPECT_TRUE(target.isDualIO()) << "a blob with no dualIO must leave a Declared module split";
        EXPECT_EQ(target.getVisibleInputPortCount(), 5);
    }

    {
        // And a blob that DOES carry the value round-trips it in both directions.
        for (bool dual : {false, true}) {
            ReverbModule source;
            setBoolParam(source, "dualIO", dual);
            juce::MemoryBlock blob;
            source.getStateInformation(blob);

            ReverbModule target;
            setBoolParam(target, "dualIO", !dual);
            target.setStateInformation(blob.getData(), (int)blob.getSize());
            EXPECT_EQ(target.isDualIO(), dual) << "dualIO did not survive a state round-trip, dual=" << dual;
        }
    }
}
