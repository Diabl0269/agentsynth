// FXModuleSidechainKeyTests.cpp — FRO317: the Key (sidechain) input on Compressor and Gate.
//
//   • jacks    — Key L/R are APPENDED on raw 7/8 (saved patches keep ch0-6), collapse to one "Key"
//                jack with Dual I/O off, carry PortRole::Sidechain, and every jack fronts exactly one
//                poly head (no phantom heads, no duplicated wires)
//   • DSP      — keyed, the detector hears only the key; unkeyed (connectivity flag off) the output is
//                bit-identical to the pre-key DSP even with a loud signal sitting on the key channels
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/GateModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kChannels = 9; // what the graph hands a 9-in / 2-out module

void setDualIO(ModuleBase& module, bool dual) {
    findParameterByID(&module, "dualIO")->setValueNotifyingHost(dual ? 1.0f : 0.0f);
}

// One block: own audio on ch0/1 (and a deterministic wobble so the detector has something to do),
// the key on ch7/8. The module clears every channel >= 2 after each block, so refill every time.
void fillBlock(juce::AudioBuffer<float>& buffer, int blockIndex, float ownLevel, float keyLevel) {
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const float phase = (float)(blockIndex * buffer.getNumSamples() + i) * 0.05f;
        buffer.setSample(0, i, ownLevel * std::sin(phase));
        buffer.setSample(1, i, ownLevel * std::cos(phase));
        if (buffer.getNumChannels() >= kChannels) {
            buffer.setSample(7, i, keyLevel);
            buffer.setSample(8, i, keyLevel);
        }
    }
}

float peakOf(const juce::AudioBuffer<float>& buffer, int channel) {
    return buffer.getMagnitude(channel, 0, buffer.getNumSamples());
}

// Renders `blocks` blocks and returns the ratio of output peak to input peak on the LAST block.
template <typename Module>
float lastBlockGain(Module& module, float ownLevel, float keyLevel, int blocks = 20) {
    juce::AudioBuffer<float> buffer(kChannels, kBlock);
    juce::MidiBuffer midi;
    float inPeak = 0.0f;
    for (int b = 0; b < blocks; ++b) {
        fillBlock(buffer, b, ownLevel, keyLevel);
        inPeak = peakOf(buffer, 0);
        module.processBlock(buffer, midi);
    }
    return peakOf(buffer, 0) / inPeak;
}

} // namespace

// ---------------------------------------------------------------------------
// Jacks
// ---------------------------------------------------------------------------

template <typename Module>
void expectKeyJacks(const juce::String& firstCv, const juce::String& lastCv) {
    Module module;
    ASSERT_EQ(module.getTotalNumInputChannels(), 9);
    ASSERT_EQ(module.getTotalNumOutputChannels(), 2) << "output shape stays a pair, so Dual I/O still inherits";
    ASSERT_TRUE(module.hasDualIOParameter());

    setDualIO(module, false);
    EXPECT_EQ(module.getVisibleInputPortCount(), 7);
    EXPECT_EQ(module.getInputPortLabel(0), "Audio");
    EXPECT_EQ(module.getInputPortLabel(1), firstCv) << "CV jacks keep their slots";
    EXPECT_EQ(module.getInputPortLabel(5), lastCv);
    EXPECT_EQ(module.getInputPortLabel(6), "Key");
    for (int raw = 2; raw <= 6; ++raw) {
        EXPECT_EQ(module.mapInputChannel(raw).visibleJackIndex, raw - 1) << "raw " << raw;
        EXPECT_EQ(module.mapInputChannel(raw).role, PortRole::ModCV) << "raw " << raw;
    }
    const auto keyTargets = module.getJackTargets(6, true);
    ASSERT_EQ(keyTargets.size(), 1u);
    EXPECT_EQ(keyTargets[0].rawHeadChannel, 7);
    EXPECT_EQ(keyTargets[0].voiceSpan, 2) << "the collapsed Key jack owns both key legs";
    EXPECT_EQ(keyTargets[0].role, PortRole::Sidechain);
    EXPECT_EQ(module.mapInputChannel(8).visibleJackIndex, 6);
    EXPECT_FALSE(module.mapInputChannel(8).isPolyGroupHead);

    setDualIO(module, true);
    EXPECT_EQ(module.getVisibleInputPortCount(), 9);
    EXPECT_EQ(module.getInputPortLabel(7), "Key L");
    EXPECT_EQ(module.getInputPortLabel(8), "Key R");
    for (int raw = 7; raw <= 8; ++raw) {
        const auto port = module.mapInputChannel(raw);
        EXPECT_EQ(port.visibleJackIndex, raw);
        EXPECT_EQ(port.role, PortRole::Sidechain);
        EXPECT_TRUE(port.isPolyGroupHead);
    }

    // No phantom heads: in both states every visible jack fronts exactly one fan.
    for (bool dual : {false, true}) {
        setDualIO(module, dual);
        int heads = 0;
        for (int raw = 0; raw < module.getTotalNumInputChannels(); ++raw)
            heads += module.mapInputChannel(raw).isPolyGroupHead ? 1 : 0;
        EXPECT_EQ(heads, module.getVisibleInputPortCount()) << (dual ? "dual" : "collapsed");
        for (int jack = 0; jack < module.getVisibleInputPortCount(); ++jack)
            EXPECT_EQ(module.getJackTargets(jack, true).size(), 1u) << "jack " << jack;
    }
}

TEST(SidechainKey, CompressorAppendsAStereoKeyPair) { expectKeyJacks<CompressorModule>("Threshold", "Makeup"); }

TEST(SidechainKey, GateAppendsAStereoKeyPair) { expectKeyJacks<GateModule>("Threshold", "Range"); }

TEST(SidechainKey, KeyIsNotAModulationTarget) {
    CompressorModule comp;
    GateModule gate;
    for (int raw : {7, 8}) {
        EXPECT_FALSE(comp.isAutoPromotableModTarget(raw));
        EXPECT_FALSE(gate.isAutoPromotableModTarget(raw));
    }
}

// ---------------------------------------------------------------------------
// Compressor DSP
// ---------------------------------------------------------------------------

TEST(SidechainKey, KeyedCompressorDucksItsOwnAudioOnALoudKey) {
    CompressorModule comp;
    comp.prepareToPlay(kSampleRate, kBlock);
    comp.setSidechainConnected(true);
    // Own audio at -20 dBFS sits below the -12 dB default threshold: alone it is never compressed.
    const float gain = lastBlockGain(comp, 0.1f, 1.0f);
    EXPECT_LT(gain, 0.5f) << "a 0 dBFS key must pull the quiet bass down";
}

TEST(SidechainKey, KeyedCompressorIgnoresItsOwnAudioWhenTheKeyIsSilent) {
    CompressorModule comp;
    comp.prepareToPlay(kSampleRate, kBlock);
    comp.setSidechainConnected(true);
    EXPECT_FLOAT_EQ(lastBlockGain(comp, 1.0f, 0.0f), 1.0f) << "a loud own signal with a silent key is untouched";
}

TEST(SidechainKey, UnpluggingTheKeyReturnsToSelfDetection) {
    CompressorModule comp;
    comp.prepareToPlay(kSampleRate, kBlock);
    comp.setSidechainConnected(true);
    ASSERT_FLOAT_EQ(lastBlockGain(comp, 1.0f, 0.0f), 1.0f);

    comp.setSidechainConnected(false); // what publishSidechainConnections does once the cable is gone
    EXPECT_LT(lastBlockGain(comp, 1.0f, 0.0f), 0.6f) << "a 0 dBFS own signal compresses itself again";
}

TEST(SidechainKey, UnkeyedCompressorIsBitIdenticalToThePreKeyDSP) {
    CompressorModule comp;
    comp.prepareToPlay(kSampleRate, kBlock);
    ASSERT_FALSE(comp.isSidechainConnected());

    // The pre-change processBlock at default knobs: juce::dsp::Compressor over ch0/1, then unity makeup.
    juce::dsp::Compressor<float> reference;
    reference.prepare({kSampleRate, (juce::uint32)kBlock, 2});
    const auto knob = [&comp](const char* id) {
        return dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&comp, id))->get();
    };
    reference.setThreshold(knob("threshold"));
    reference.setRatio(knob("ratio"));
    reference.setAttack(knob("attack"));
    reference.setRelease(knob("release"));
    reference.reset();

    juce::AudioBuffer<float> buffer(kChannels, kBlock);
    juce::AudioBuffer<float> expected(2, kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < 8; ++b) {
        fillBlock(buffer, b, 0.9f, 1.0f); // a loud key sits on ch7/8 but no cable is published
        for (int ch = 0; ch < 2; ++ch)
            expected.copyFrom(ch, 0, buffer, ch, 0, kBlock);
        juce::dsp::AudioBlock<float> block(expected);
        juce::dsp::ProcessContextReplacing<float> context(block);
        reference.process(context);
        expected.applyGain(juce::Decibels::decibelsToGain(knob("makeupGain")));

        comp.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                ASSERT_EQ(buffer.getSample(ch, i), expected.getSample(ch, i)) << "block " << b << " ch " << ch;
    }
}

TEST(SidechainKey, AKeyFlagWithoutKeyChannelsFallsBackToSelfDetection) {
    // A 2-channel buffer (a headless render with no key channels) can never be keyed.
    CompressorModule keyed;
    CompressorModule plain;
    keyed.prepareToPlay(kSampleRate, kBlock);
    plain.prepareToPlay(kSampleRate, kBlock);
    keyed.setSidechainConnected(true);
    juce::AudioBuffer<float> a(2, kBlock), b(2, kBlock);
    juce::MidiBuffer midi;
    for (int block = 0; block < 4; ++block) {
        fillBlock(a, block, 0.9f, 0.0f);
        fillBlock(b, block, 0.9f, 0.0f);
        keyed.processBlock(a, midi);
        plain.processBlock(b, midi);
        for (int i = 0; i < kBlock; ++i)
            ASSERT_EQ(a.getSample(0, i), b.getSample(0, i));
    }
}

// ---------------------------------------------------------------------------
// Gate DSP
// ---------------------------------------------------------------------------

TEST(SidechainKey, KeyedGateOpensOnTheKey) {
    GateModule gate;
    gate.prepareToPlay(kSampleRate, kBlock);
    gate.setSidechainConnected(true);
    // Own audio at -60 dBFS is under the -40 dB default threshold: alone the gate stays shut.
    EXPECT_NEAR(lastBlockGain(gate, 0.001f, 0.5f), 1.0f, 1.0e-3f) << "a loud key opens the gate";
}

TEST(SidechainKey, KeyedGateStaysShutOnASilentKeyEvenWithLoudAudio) {
    GateModule gate;
    gate.prepareToPlay(kSampleRate, kBlock);
    gate.setSidechainConnected(true);
    EXPECT_LT(lastBlockGain(gate, 0.5f, 0.0f), 0.01f) << "the gate listens to the key, not its own audio";

    gate.setSidechainConnected(false);
    EXPECT_NEAR(lastBlockGain(gate, 0.5f, 0.0f), 1.0f, 1.0e-3f) << "unplugged, its own audio opens it again";
}

TEST(SidechainKey, UnkeyedGateIgnoresTheKeyChannelsBitForBit) {
    // Unkeyed, a loud signal on ch7/8 must change nothing: the 9-channel render equals the 2-channel
    // one, whose buffer has no key channels to read at all.
    GateModule withKeyChannels;
    GateModule withoutKeyChannels;
    withKeyChannels.prepareToPlay(kSampleRate, kBlock);
    withoutKeyChannels.prepareToPlay(kSampleRate, kBlock);
    juce::AudioBuffer<float> wide(kChannels, kBlock), narrow(2, kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < 8; ++b) {
        fillBlock(wide, b, b % 2 == 0 ? 0.3f : 0.002f, 1.0f);
        fillBlock(narrow, b, b % 2 == 0 ? 0.3f : 0.002f, 0.0f);
        withKeyChannels.processBlock(wide, midi);
        withoutKeyChannels.processBlock(narrow, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                ASSERT_EQ(wide.getSample(ch, i), narrow.getSample(ch, i)) << "block " << b;
    }
}
