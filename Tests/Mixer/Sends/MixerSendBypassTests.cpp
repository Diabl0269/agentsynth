// MixerSendBypassTests.cpp (docs/mixer/sends-and-buses.md#bypassing-a-send): a single send's bypass bit.
//
//   * audio      -- a bypassed send ramps to silence (no step) and back, leaves its siblings alone and never
//                   touches its level parameter
//   * state      -- the bit round-trips through the trusted extra state; an entry saved before bypass existed
//                   loads un-bypassed
//   * flows      -- synth::setSendBypassed, a freed slot forgetting the bit, and a reorder swap carrying it

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
constexpr int kRight = ChannelStripModule::kRightBase;

juce::AudioBuffer<float> stripInput(float value) {
    juce::AudioBuffer<float> buffer(ChannelStripModule::kNumOutputs, kBlockSize);
    buffer.clear();
    for (int i = 0; i < kBlockSize; ++i) {
        buffer.setSample(0, i, value);
        buffer.setSample(kRight, i, value);
    }
    return buffer;
}

// Runs `blocks` blocks of constant input through the strip; the last block's output is returned.
juce::AudioBuffer<float> run(ChannelStripModule& strip, int blocks) {
    juce::MidiBuffer midi;
    auto buffer = stripInput(1.0f);
    for (int i = 0; i < blocks; ++i) {
        buffer = stripInput(1.0f);
        strip.processBlock(buffer, midi);
    }
    return buffer;
}

// One strip with a pre-fader send at unity in slot 0 and a sibling in slot 1, already settled.
struct SendRig {
    ChannelStripModule strip;
    SendRig() {
        EXPECT_EQ(strip.addSend(), 0);
        EXPECT_EQ(strip.addSend(), 1);
        strip.setSendPreFader(0, true);
        strip.setSendPreFader(1, true);
        strip.prepareToPlay(kSampleRate, kBlockSize);
        run(strip, 40);
    }
};

float leg(const juce::AudioBuffer<float>& buffer, int slot, int sample) {
    return buffer.getSample(ChannelStripModule::sendLeftChannel(slot), sample);
}

NodeID addFactoryNode(juce::AudioProcessorGraph& graph, const juce::String& type) {
    auto node = graph.addNode(synth::AIStateMapper::createModule(type));
    return node != nullptr ? node->nodeID : NodeID{};
}

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

struct GraphRig {
    juce::AudioProcessorGraph graph;
    NodeID master, source, busA, busB;
    GraphRig() {
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
        master = addFactoryNode(graph, "Master");
        source = addFactoryNode(graph, "Channel Strip");
        busA = addFactoryNode(graph, "Channel Strip");
        busB = addFactoryNode(graph, "Channel Strip");
        for (auto id : {source, busA, busB}) {
            graph.addConnection({{id, 0}, {master, MasterModule::kMixLeft}});
            graph.addConnection({{id, kRight}, {master, MasterModule::kMixRight}});
        }
    }
};

} // namespace

TEST(MixerSendBypassTest, ABypassedSendRampsToSilenceWithoutAStepAndSiblingsKeepPlaying) {
    SendRig rig;
    ASSERT_NEAR(leg(run(rig.strip, 1), 0, 0), 1.0f, 1.0e-4f) << "sanity: the send passes signal before bypass";

    rig.strip.setSendBypassed(0, true);
    juce::MidiBuffer midi;
    auto first = stripInput(1.0f);
    rig.strip.processBlock(first, midi);

    // No click: the first sample of the block right after the toggle is still (almost) full level, and no
    // sample-to-sample step is larger than a linear ramp over the 20 ms smoothing time would make.
    EXPECT_GT(leg(first, 0, 0), 0.9f) << "the ramp starts from the level, it does not cut";
    float biggestStep = 0.0f;
    for (int i = 1; i < kBlockSize; ++i)
        biggestStep = std::max(biggestStep, std::abs(leg(first, 0, i) - leg(first, 0, i - 1)));
    EXPECT_LT(biggestStep, 0.01f) << "a 20 ms ramp at 48 kHz steps by about 0.001 per sample, never by a jump";
    EXPECT_LT(leg(first, 0, kBlockSize - 1), leg(first, 0, 0)) << "and it is heading down";

    const auto settled = run(rig.strip, 40);
    for (int i = 0; i < kBlockSize; ++i)
        ASSERT_EQ(leg(settled, 0, i), 0.0f) << "a bypassed send ends in exact silence (sample " << i << ")";
    EXPECT_NEAR(leg(settled, 1, 0), 1.0f, 1.0e-4f) << "the sibling send is untouched";
}

TEST(MixerSendBypassTest, RestoringASendRampsBackUpToTheSameLevelItNeverLost) {
    SendRig rig;
    auto* level = rig.strip.getSendLevelParameter(0);
    level->setValueNotifyingHost(level->getNormalisableRange().convertTo0to1(-6.0f));
    run(rig.strip, 40);
    rig.strip.setSendBypassed(0, true);
    run(rig.strip, 40);

    rig.strip.setSendBypassed(0, false);
    juce::MidiBuffer midi;
    auto first = stripInput(1.0f);
    rig.strip.processBlock(first, midi);
    EXPECT_LT(leg(first, 0, 0), 0.05f) << "restoring starts from silence, it does not jump to the level";
    EXPECT_GT(leg(first, 0, kBlockSize - 1), leg(first, 0, 0)) << "and climbs";

    const float expected = juce::Decibels::decibelsToGain(-6.0f, ChannelStripModule::kMinGainDb);
    EXPECT_NEAR(leg(run(rig.strip, 40), 0, 0), expected, 1.0e-4f);
    EXPECT_NEAR(level->get(), -6.0f, 1.0e-3f) << "bypass never touched the level parameter";
}

TEST(MixerSendBypassTest, ASendRestoredAsBypassedStartsSilentInsteadOfRampingDown) {
    ChannelStripModule source;
    ASSERT_EQ(source.addSend(), 0);
    source.setSendBypassed(0, true);

    ChannelStripModule restored;
    restored.setExtraState(source.getExtraState());
    restored.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_EQ(leg(run(restored, 1), 0, 0), 0.0f);
}

TEST(MixerSendBypassTest, TheBitRoundTripsThroughExtraStateAndAnOldEntryLoadsUnbypassed) {
    ChannelStripModule source;
    ASSERT_EQ(source.addSend(), 0);
    ASSERT_EQ(source.addSend(), 1);
    source.setSendBypassed(1, true);

    ChannelStripModule restored;
    restored.setExtraState(source.getExtraState());
    EXPECT_TRUE(restored.isSendBypassed(1));
    EXPECT_FALSE(restored.isSendBypassed(0)) << "only the bypassed slot carries the bit";

    ChannelStripModule legacy;
    auto* obj = new juce::DynamicObject();
    juce::Array<juce::var> sends;
    auto* entry = new juce::DynamicObject();
    entry->setProperty("slot", 0);
    entry->setProperty("pre", false);
    sends.add(juce::var(entry));
    obj->setProperty("sends", sends);
    legacy.setExtraState(juce::var(obj));
    ASSERT_TRUE(legacy.isSendActive(0));
    EXPECT_FALSE(legacy.isSendBypassed(0)) << "no \"bypass\" key means not bypassed";
}

TEST(MixerSendBypassTest, SetSendBypassedTogglesTheBitAndRefusesANonStripOrInactiveSlot) {
    GraphRig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busA), 0);

    EXPECT_TRUE(synth::setSendBypassed(rig.graph, rig.source, 0, true));
    EXPECT_TRUE(stripAt(rig.graph, rig.source)->isSendBypassed(0));
    EXPECT_FALSE(stripAt(rig.graph, rig.source)->isSendMuted(0)) << "bypass is its own bit, not mute";
    EXPECT_TRUE(synth::setSendBypassed(rig.graph, rig.source, 0, false));
    EXPECT_FALSE(stripAt(rig.graph, rig.source)->isSendBypassed(0));

    EXPECT_FALSE(synth::setSendBypassed(rig.graph, rig.source, 1, true)) << "slot 1 is not active";
    EXPECT_FALSE(synth::setSendBypassed(rig.graph, rig.master, 0, true)) << "Master is not a strip";
}

TEST(MixerSendBypassTest, ARemovedSlotForgetsTheBitAndAReorderSwapCarriesIt) {
    GraphRig rig;
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busA), 0);
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busB), 1);
    ASSERT_TRUE(synth::setSendBypassed(rig.graph, rig.source, 0, true));

    ASSERT_TRUE(synth::swapSends(rig.graph, rig.source, 0, 1));
    EXPECT_FALSE(stripAt(rig.graph, rig.source)->isSendBypassed(0)) << "slot 0 now holds what was slot 1's";
    EXPECT_TRUE(stripAt(rig.graph, rig.source)->isSendBypassed(1)) << "the bit travelled with its send";

    ASSERT_TRUE(synth::removeSend(rig.graph, rig.source, 1));
    ASSERT_EQ(synth::addSend(rig.graph, rig.source, rig.busB), 1) << "the freed slot is reused";
    EXPECT_FALSE(stripAt(rig.graph, rig.source)->isSendBypassed(1)) << "a fresh send starts un-bypassed";
}
