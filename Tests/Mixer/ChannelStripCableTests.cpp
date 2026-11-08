// ChannelStripCableTests.cpp
// The Channel Strip's Gain, Pan, send levels and send pans, and the Master's Gain, take cables on channels above every
// existing input. The strip's channel shape is frozen: the audio inputs, the reserved block and every output keep the
// channel they had, so a saved project's cables still land where they were drawn.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MasterModule.h"
#include <gtest/gtest.h>

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 64;
constexpr int kRight = ChannelStripModule::kRightBase;

void setParam(juce::AudioProcessor& processor, const juce::String& id, float value) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&processor, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

// Runs the strip over a constant (left, right) input with `cv` on `paramId`'s cable, long enough for the 20 ms ramps to
// settle, and returns the last block.
juce::AudioBuffer<float> runStrip(ChannelStripModule& strip, float left, float right, const char* paramId, float cv) {
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(ChannelStripModule::kNumOutputs, kBlock);
    for (int block = 0; block < 40; ++block) {
        buffer.clear();
        for (int i = 0; i < kBlock; ++i) {
            buffer.setSample(0, i, left);
            buffer.setSample(kRight, i, right);
        }
        if (paramId != nullptr)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(strip.modulationChannelForParam(paramId)), cv,
                                              kBlock);
        strip.processBlock(buffer, midi);
    }
    return buffer;
}

} // namespace

TEST(ChannelStripCable, EveryExistingChannelIsWhereItWas) {
    ChannelStripModule strip;
    EXPECT_EQ(ChannelStripModule::kRightBase, 4);
    EXPECT_EQ(ChannelStripModule::kSendBase, 8);
    EXPECT_EQ(ChannelStripModule::kNumOutputs, 16);
    for (int slot = 0; slot < ChannelStripModule::kMaxSends; ++slot) {
        EXPECT_EQ(ChannelStripModule::sendLeftChannel(slot), 8 + slot);
        EXPECT_EQ(ChannelStripModule::sendRightChannel(slot), 12 + slot);
    }
    EXPECT_EQ(strip.getTotalNumOutputChannels(), 16);
    // Inputs: the audio pair and the reserved channels are where they were; the cables are all above them.
    EXPECT_EQ(strip.mapInputChannel(0).role, PortRole::Audio);
    EXPECT_EQ(strip.mapInputChannel(kRight).role, PortRole::Audio);
    for (int reserved = 1; reserved < kRight; ++reserved)
        EXPECT_EQ(strip.mapInputChannel(reserved).role, PortRole::Other) << reserved;
    EXPECT_EQ(strip.getTotalNumInputChannels(), 15);
    EXPECT_EQ(strip.getVisibleInputPortCount(), 2);
    EXPECT_EQ(strip.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(strip.mapOutputChannel(kRight).visibleJackIndex, 1);
}

TEST(ChannelStripCable, EveryKnobIsAKnobOnlyTargetOnAChannelAboveTheAudioInputs) {
    ChannelStripModule strip;
    const char* ids[] = {"gain",       "pan",      "send1Level", "send2Level", "send3Level",
                         "send4Level", "send1Pan", "send2Pan",   "send3Pan",   "send4Pan"};
    const auto targets = strip.getModulationTargets();
    ASSERT_EQ(targets.size(), 10u);
    for (int k = 0; k < 10; ++k) {
        SCOPED_TRACE(ids[k]);
        EXPECT_EQ(targets[(size_t)k].paramId, ids[k]);
        EXPECT_EQ(targets[(size_t)k].channelIndex, 5 + k);
        EXPECT_TRUE(targets[(size_t)k].knobOnly);
        EXPECT_EQ(strip.mapInputChannel(5 + k).role, PortRole::ModCV);
        // A drop maps raw -> jack -> raw: each cable-only knob must come back to its own channel.
        const auto port = strip.mapInputChannel(5 + k);
        ASSERT_TRUE(port.isPolyGroupHead);
        const auto jack = strip.getJackTargets(port.visibleJackIndex, /*isInput=*/true);
        ASSERT_EQ(jack.size(), 1u);
        EXPECT_EQ(jack.front().rawHeadChannel, 5 + k);
    }
}

// Regression test for FRO1556: the Channel Strip's gain and pan took no cable.
TEST(ChannelStripCable, AGainCableMovesTheFaderAndNeverTheKnob) {
    ChannelStripModule plain;
    plain.prepareToPlay(kRate, kBlock);
    const auto open = runStrip(plain, 0.5f, 0.5f, nullptr, 0.0f);
    EXPECT_NEAR(open.getSample(0, kBlock - 1), 0.5f, 1e-4f);

    ChannelStripModule cabled;
    cabled.prepareToPlay(kRate, kBlock);
    // 0 dB sits at 0.833 of the -60..+12 fader; -0.5 lands at 0.333, which is -36 dB.
    const auto lowered = runStrip(cabled, 0.5f, 0.5f, "gain", -0.5f);
    EXPECT_NEAR(lowered.getSample(0, kBlock - 1), 0.5f * juce::Decibels::decibelsToGain(-36.0f), 2e-3f);
    EXPECT_NEAR(lowered.getSample(kRight, kBlock - 1), 0.5f * juce::Decibels::decibelsToGain(-36.0f), 2e-3f);
    float knob = 0.0f;
    for (auto* p : cabled.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged && ranged->paramID == "gain")
            knob = ranged->convertFrom0to1(ranged->getValue());
    EXPECT_NEAR(knob, 0.0f, 1e-3f);
}

TEST(ChannelStripCable, APanCableBalancesAStereoStrip) {
    ChannelStripModule strip;
    strip.prepareToPlay(kRate, kBlock);
    const auto hardRight = runStrip(strip, 0.5f, 0.5f, "pan", 1.0f);
    EXPECT_NEAR(hardRight.getSample(0, kBlock - 1), 0.0f, 1e-4f);
    EXPECT_NEAR(hardRight.getSample(kRight, kBlock - 1), 0.5f, 1e-4f);
}

TEST(ChannelStripCable, ASendLevelAndPanCableReachTheSendLegs) {
    ChannelStripModule strip;
    strip.prepareToPlay(kRate, kBlock);
    ASSERT_GE(strip.addSend(), 0);
    const int left = ChannelStripModule::sendLeftChannel(0), right = ChannelStripModule::sendRightChannel(0);
    const auto open = runStrip(strip, 0.5f, 0.5f, nullptr, 0.0f);
    EXPECT_NEAR(open.getSample(left, kBlock - 1), 0.5f, 1e-4f);

    ChannelStripModule quiet;
    quiet.prepareToPlay(kRate, kBlock);
    ASSERT_GE(quiet.addSend(), 0);
    const auto lowered = runStrip(quiet, 0.5f, 0.5f, "send1Level", -0.5f);
    EXPECT_NEAR(lowered.getSample(left, kBlock - 1), 0.5f * juce::Decibels::decibelsToGain(-36.0f), 2e-3f);

    ChannelStripModule panned;
    panned.prepareToPlay(kRate, kBlock);
    ASSERT_GE(panned.addSend(), 0);
    const auto aside = runStrip(panned, 0.5f, 0.5f, "send1Pan", 0.5f); // -1..1: +0.5 of the travel is hard right
    EXPECT_NEAR(aside.getSample(left, kBlock - 1), 0.0f, 1e-4f);
    EXPECT_NEAR(aside.getSample(right, kBlock - 1), 0.5f, 1e-4f);
}

TEST(MasterCable, GainIsAKnobOnlyTargetOnTheChannelAfterTheFourAudioInputs) {
    MasterModule master;
    EXPECT_EQ(MasterModule::kNumInputs, 4);
    EXPECT_EQ(master.getTotalNumInputChannels(), 5);
    const auto targets = master.getModulationTargets();
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0].paramId, "gain");
    EXPECT_EQ(targets[0].channelIndex, 4);
    EXPECT_TRUE(targets[0].knobOnly);
    EXPECT_EQ(master.mapInputChannel(4).role, PortRole::ModCV);
    EXPECT_EQ(master.mapInputChannel(3).role, PortRole::Audio);
    EXPECT_EQ(master.getVisibleInputPortCount(), 4);
}

TEST(MasterCable, AGainCableMovesTheMasterFader) {
    const auto run = [](float cv) {
        MasterModule master;
        master.prepareToPlay(kRate, kBlock);
        setParam(master, "safetyLimiter", 0.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer(5, kBlock);
        for (int block = 0; block < 40; ++block) {
            buffer.clear();
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(MasterModule::kMixLeft, i, 0.5f);
            juce::FloatVectorOperations::fill(buffer.getWritePointer(MasterModule::kGainCvChannel), cv, kBlock);
            master.processBlock(buffer, midi);
        }
        return buffer.getSample(MasterModule::kMixLeft, kBlock - 1);
    };
    EXPECT_NEAR(run(0.0f), 0.5f, 1e-4f);
    EXPECT_NEAR(run(-0.5f), 0.5f * juce::Decibels::decibelsToGain(-36.0f), 2e-3f);
}
