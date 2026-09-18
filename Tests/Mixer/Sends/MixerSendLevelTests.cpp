// Module-level tests for a Channel Strip's send legs (FRO15 / P9-9, docs/mixer/sends-and-buses.md).
//
//   * pre vs post    -- a post-fader send carries exactly what the strip hands Master; a pre-fader
//                        one carries the signal before gain and pan
//   * level          -- a dB send level, applied to both legs of the pair
//   * mute / bypass  -- mute silences every send (the deliberate departure from DAW cue sends);
//                        bypass makes pre and post coincide
//   * hygiene        -- EVERY branch writes EVERY send channel, so a stale block can never leak
//                        into a bus
//   * sparse slots   -- removing a middle send leaves higher slots on their own raw channels and
//                        only renumbers the VISIBLE jacks
//   * state          -- the active slots and their pre/post round-trip through the trusted extra
//                        state
//
// The per-leg solo gate these legs also obey lives in MixerBusSoloTests.cpp.

#include "Modules/ChannelStripModule.h"
#include "Transport/TransportService.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
constexpr int kRight = ChannelStripModule::kRightBase;

void setParam(juce::AudioProcessor& processor, const juce::String& id, float value) {
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (ranged->getParameterID() == id) {
                ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
                return;
            }
    FAIL() << "no parameter " << id;
}

// A strip-sized buffer carrying the two inputs and JUNK on every other raw channel -- the reserved
// blocks and all eight send channels -- so every test also proves the hygiene pass overwrites them.
juce::AudioBuffer<float> stripInput(float left, float right) {
    juce::AudioBuffer<float> buffer(ChannelStripModule::kNumOutputs, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i) {
        for (int ch = 0; ch < ChannelStripModule::kNumOutputs; ++ch)
            buffer.setSample(ch, i, 9.0f);
        buffer.setSample(0, i, left);
        buffer.setSample(kRight, i, right);
    }
    return buffer;
}

// Processes enough blocks for every ramp (gain, pan, send level) to settle, then one more into
// `buffer` -- the same idiom ChannelStripTests.cpp uses.
void settleAndProcess(juce::AudioProcessor& processor, juce::AudioBuffer<float>& buffer) {
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i) { // 40 * 64 samples > the 20 ms ramp at 48 kHz
        auto scratch = buffer;
        processor.processBlock(scratch, midi);
    }
    processor.processBlock(buffer, midi);
}

void expectChannel(const juce::AudioBuffer<float>& buffer, int ch, float expected, const char* what) {
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        ASSERT_NEAR(buffer.getSample(ch, i), expected, 1.0e-5f) << what << " (ch " << ch << ", sample " << i << ")";
}

void expectChannelsEqual(const juce::AudioBuffer<float>& buffer, int a, int b, const char* what) {
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        ASSERT_NEAR(buffer.getSample(a, i), buffer.getSample(b, i), 1.0e-5f)
            << what << " (ch " << a << " vs " << b << ", sample " << i << ")";
}

int sendL(int slot) { return ChannelStripModule::sendLeftChannel(slot); }
int sendR(int slot) { return ChannelStripModule::sendRightChannel(slot); }

} // namespace

// ============================================================================
// Pre vs post fader
// ============================================================================

TEST(MixerSendLevelTest, PostFaderSendFollowsGainAndPan) {
    ChannelStripModule strip;
    ASSERT_EQ(strip.addSend(), 0) << "a new send takes the lowest free slot";
    EXPECT_FALSE(strip.isSendPreFader(0)) << "and is post-fader by default";
    setParam(strip, "gain", -6.0f);
    setParam(strip, "pan", 0.4f);
    strip.prepareToPlay(kSampleRate, kBlockSize);

    auto buffer = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, buffer);

    // Unity send level: the leg IS what the strip hands Master, gain and pan already baked in.
    expectChannelsEqual(buffer, sendL(0), 0, "post-fader send left == main left");
    expectChannelsEqual(buffer, sendR(0), kRight, "post-fader send right == main right");
    EXPECT_LT(buffer.getSample(0, 0), 0.5f) << "the fader really did attenuate";
    EXPECT_NE(buffer.getSample(0, 0), buffer.getSample(kRight, 0)) << "the pan really did split the legs";
}

TEST(MixerSendLevelTest, PreFaderSendIgnoresGainAndPan) {
    ChannelStripModule strip;
    ASSERT_EQ(strip.addSend(), 0);
    strip.setSendPreFader(0, true);
    setParam(strip, "gain", -6.0f);
    setParam(strip, "pan", 0.4f);
    strip.prepareToPlay(kSampleRate, kBlockSize);

    auto buffer = stripInput(0.5f, 0.25f);
    settleAndProcess(strip, buffer);

    expectChannel(buffer, sendL(0), 0.5f, "pre-fader send left is the raw input");
    expectChannel(buffer, sendR(0), 0.25f, "pre-fader send right is the raw input");
    EXPECT_LT(buffer.getSample(0, 0), 0.5f) << "while the main leg is still faded";
}

TEST(MixerSendLevelTest, SendLevelScalesTheLegInDecibels) {
    ChannelStripModule strip;
    ASSERT_EQ(strip.addSend(), 0);
    strip.setSendPreFader(0, true);
    setParam(strip, "send1Level", -6.0f);
    strip.prepareToPlay(kSampleRate, kBlockSize);

    auto buffer = stripInput(1.0f, 1.0f);
    settleAndProcess(strip, buffer);

    const float expected = juce::Decibels::decibelsToGain(-6.0f, ChannelStripModule::kMinGainDb);
    expectChannel(buffer, sendL(0), expected, "send level is a dB gain on the leg");
    expectChannel(buffer, sendR(0), expected, "and on both legs of the pair");
    expectChannel(buffer, 0, 1.0f, "the main leg is untouched by the send level");
}

TEST(MixerSendLevelTest, MuteSilencesPreAndPostSends) {
    // Deliberate departure from DAWs that keep a pre-fader cue alive under mute: the mute branch
    // stays a whole-buffer clear (root CLAUDE.md's two-branch contract).
    ChannelStripModule strip;
    ASSERT_EQ(strip.addSend(), 0);
    strip.setSendPreFader(0, true);
    ASSERT_EQ(strip.addSend(), 1);
    strip.prepareToPlay(kSampleRate, kBlockSize);
    strip.setMuted(true);

    auto buffer = stripInput(0.5f, 0.5f);
    settleAndProcess(strip, buffer);

    for (int slot = 0; slot < ChannelStripModule::kMaxSends; ++slot) {
        expectChannel(buffer, sendL(slot), 0.0f, "mute silences every send leg");
        expectChannel(buffer, sendR(slot), 0.0f, "mute silences every send leg");
    }
    expectChannel(buffer, 0, 0.0f, "and the main leg");
}

TEST(MixerSendLevelTest, BypassMakesPreAndPostSendsIdentical) {
    // Bypass disables the strip's own gain and pan, so the two taps see the same signal.
    ChannelStripModule strip;
    ASSERT_EQ(strip.addSend(), 0);
    strip.setSendPreFader(0, true);
    ASSERT_EQ(strip.addSend(), 1); // post
    setParam(strip, "gain", -12.0f);
    setParam(strip, "pan", 0.7f);
    strip.prepareToPlay(kSampleRate, kBlockSize);
    strip.setBypassed(true);

    auto buffer = stripInput(0.5f, 0.25f);
    settleAndProcess(strip, buffer);

    expectChannelsEqual(buffer, sendL(0), sendL(1), "under bypass pre and post coincide");
    expectChannelsEqual(buffer, sendR(0), sendR(1), "under bypass pre and post coincide");
    expectChannel(buffer, sendL(0), 0.5f, "and both carry the dry signal");
    expectChannel(buffer, sendR(0), 0.25f, "and both carry the dry signal");
}

// ============================================================================
// Hygiene: every branch writes every send channel
// ============================================================================

TEST(MixerSendLevelTest, InactiveAndReservedSendChannelsAreClearedInEveryBranch) {
    // Risk 1 in the design: a branch that forgets a send channel leaks the previous callback's
    // block straight into a bus, silently.
    synth::TransportService transport;

    ChannelStripModule strip;
    ASSERT_EQ(strip.addSend(), 0); // only slot 0 is ever active here
    strip.setPlayHead(&transport);
    strip.prepareToPlay(kSampleRate, kBlockSize);

    struct Branch {
        const char* name;
        bool bypassed, muted, soloGated;
    };
    const Branch branches[] = {{"normal", false, false, false},
                               {"bypass", true, false, false},
                               {"mute", false, true, false},
                               {"solo-gated", false, false, true}};

    for (const auto& branch : branches) {
        strip.setBypassed(branch.bypassed);
        strip.setMuted(branch.muted);
        transport.setMixerSoloActiveForBlock(branch.soloGated);
        strip.setSoloAudibleMask(0); // gated shut: nothing of this strip is audible

        auto buffer = stripInput(0.5f, 0.5f);
        settleAndProcess(strip, buffer);

        for (int ch = kRight + 1; ch < ChannelStripModule::kSendBase; ++ch)
            expectChannel(buffer, ch, 0.0f, branch.name);
        for (int slot = 1; slot < ChannelStripModule::kMaxSends; ++slot) {
            expectChannel(buffer, sendL(slot), 0.0f, branch.name);
            expectChannel(buffer, sendR(slot), 0.0f, branch.name);
        }
        for (int ch = 1; ch < kRight; ++ch)
            expectChannel(buffer, ch, 0.0f, branch.name);
    }
}

// ============================================================================
// Sparse slots
// ============================================================================

TEST(MixerSendLevelTest, RemovingAMiddleSendLeavesHigherSlotsOnTheirRawChannels) {
    ChannelStripModule strip;
    ASSERT_EQ(strip.addSend(), 0);
    ASSERT_EQ(strip.addSend(), 1);
    ASSERT_EQ(strip.addSend(), 2);
    EXPECT_EQ(strip.getVisibleOutputPortCount(), 2 + 2 * 3);

    strip.setSendActive(1, false);
    EXPECT_EQ(strip.getActiveSendCount(), 2);
    EXPECT_EQ(strip.getVisibleOutputPortCount(), 2 + 2 * 2);

    // The VISIBLE jacks renumber (slot 2's pair moves down to jacks 4/5)...
    EXPECT_EQ(strip.mapOutputChannel(sendL(2)).visibleJackIndex, 4);
    EXPECT_EQ(strip.mapOutputChannel(sendR(2)).visibleJackIndex, 5);
    // ...but the RAW channels do not, so no cable and no parameter value ever has to move.
    EXPECT_EQ(sendL(2), ChannelStripModule::kSendBase + 2);
    EXPECT_EQ(sendR(2), ChannelStripModule::kSendBase + ChannelStripModule::kMaxSends + 2);
    // ...and neither does the label, so the jack still agrees with send3Level.
    EXPECT_EQ(strip.getOutputPortLabel(4), "Send 3 L");
    EXPECT_EQ(strip.getOutputPortLabel(5), "Send 3 R");

    // A freed slot is what the next "Add send" takes.
    EXPECT_EQ(strip.addSend(), 1);
}

TEST(MixerSendLevelTest, EverySendLevelParameterExistsRegardlessOfActiveSlots) {
    // Adding a parameter later would renumber the host-visible layout and detach saved automation.
    ChannelStripModule strip;
    EXPECT_EQ(strip.getActiveSendCount(), 0);
    for (int slot = 0; slot < ChannelStripModule::kMaxSends; ++slot)
        EXPECT_NE(strip.getSendLevelParameter(slot), nullptr)
            << "send " << slot << " has its level parameter from construction";
    EXPECT_EQ(ChannelStripModule::getSendLevelParameterId(2), "send3Level");
    EXPECT_EQ(strip.getVisibleOutputPortCount(), 2) << "but no visible send jacks yet";
}

TEST(MixerSendLevelTest, SendStateRoundTripsThroughExtraState) {
    ChannelStripModule source;
    ASSERT_EQ(source.addSend(), 0);
    ASSERT_EQ(source.addSend(), 1);
    ASSERT_EQ(source.addSend(), 2);
    source.setSendActive(1, false);
    source.setSendPreFader(2, true);
    source.setIsBus(true);

    ChannelStripModule restored;
    restored.setExtraState(source.getExtraState());

    EXPECT_TRUE(restored.isSendActive(0));
    EXPECT_FALSE(restored.isSendActive(1)) << "the sparse hole survives";
    EXPECT_TRUE(restored.isSendActive(2));
    EXPECT_FALSE(restored.isSendPreFader(0));
    EXPECT_TRUE(restored.isSendPreFader(2));
    EXPECT_TRUE(restored.isBus());

    // And a patch saved before sends existed simply has no slots.
    ChannelStripModule legacy;
    auto* obj = new juce::DynamicObject();
    obj->setProperty("shape", "stereo");
    obj->setProperty("solo", false);
    legacy.setExtraState(juce::var(obj));
    EXPECT_EQ(legacy.getActiveSendCount(), 0);
    EXPECT_FALSE(legacy.isBus());
}
