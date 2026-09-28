// MixerSendAutomationPlaybackTests.cpp (docs/mixer/sends-and-buses.md#the-send-and-bus-ui):
// a lane bound to a ChannelStripModule's own sendNLevel is driven by the SAME
// AutomationApplier/OfflineTransportDriver path AutomationApplierTests.cpp already pins for a
// built-in module's parameter -- proof that a send level needed no new evaluator, only the new
// lane-creation entry point MixerSendAutomationLaneTests.cpp covers. sendNLevel is an ordinary
// RangedAudioParameter (unlike a hosted plugin's always-normalised one), so the ramp is authored
// directly in dB and read back in dB, with no {0, 1} conversion anywhere in this file.
//
// Headless/deterministic house rules, same as AutomationApplierTests.cpp: HostMode::Hosted only,
// no audio device, no sleeps, everything driven through synth::OfflineTransportDriver.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/OfflineTransportDriver.h"
#include <gtest/gtest.h>
#include <memory>

using synth::AutomationLane;
using synth::TimelineDoc;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr const char* kStripUuid = "c0900000-0000-0000-0000-000000000005";

AutomationLane::RangeSnapshot range(float minValue, float maxValue, float defaultValue) {
    AutomationLane::RangeSnapshot snapshot;
    snapshot.minValue = minValue;
    snapshot.maxValue = maxValue;
    snapshot.defaultValue = defaultValue;
    return snapshot;
}

// Source strip (uuid'd, one active send) -> Bus -> Master -> Audio Output, exactly the topology
// docs/mixer/sends-and-buses.md describes and MixerSendFlowTests.cpp's Rig builds -- the graph a
// real render pass actually walks, not a bare unconnected node.
struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    TimelineDoc doc;
    synth::TrackId trackId;
    juce::AudioProcessorGraph::NodeID stripId;

    bool build() {
        engine.initialise();
        auto& graph = engine.getGraph();

        auto stripNode = graph.addNode(synth::AIStateMapper::createModule("Channel Strip"));
        auto busNode = graph.addNode(synth::AIStateMapper::createModule("Channel Strip"));
        auto masterNode = graph.addNode(synth::AIStateMapper::createModule("Master"));
        auto outputNode = graph.addNode(synth::AIStateMapper::createModule("Audio Output"));
        if (stripNode == nullptr || busNode == nullptr || masterNode == nullptr || outputNode == nullptr)
            return false;
        stripId = stripNode->nodeID;
        const auto busId = busNode->nodeID;
        const auto masterId = masterNode->nodeID;
        const auto outputId = outputNode->nodeID;

        stripNode->properties.set("uuid", kStripUuid);
        if (auto* module = dynamic_cast<ModuleBase*>(stripNode->getProcessor()))
            module->setNodeUuid(kStripUuid);

        auto* bus = dynamic_cast<ChannelStripModule*>(busNode->getProcessor());
        if (bus == nullptr)
            return false;
        bus->setIsBus(true);

        if (synth::addSend(graph, stripId, busId) != 0)
            return false;

        constexpr int kRight = ChannelStripModule::kRightBase;
        graph.addConnection({{busId, 0}, {masterId, MasterModule::kMixLeft}});
        graph.addConnection({{busId, kRight}, {masterId, MasterModule::kMixRight}});
        graph.addConnection({{masterId, 0}, {outputId, 0}});
        graph.addConnection({{masterId, 1}, {outputId, 1}});

        // Same ctor-time prepareForHost idiom as AutomationApplierTests::Fixture.
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, 2);

        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        return trackId.isValid();
    }

    void publish() { engine.publishTimeline(doc); }

    juce::RangedAudioParameter* sendLevelParam() {
        auto* node = engine.getGraph().getNodeForId(stripId);
        auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
        return strip != nullptr ? strip->getSendLevelParameter(0) : nullptr;
    }

    static double denormalised(const juce::RangedAudioParameter* parameter) {
        return static_cast<double>(parameter->convertFrom0to1(parameter->getValue()));
    }

    int bindingCount() { return static_cast<int>(engine.getAutomationBindings().beginAudioBlock().bindings.size()); }

    ~Fixture() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

} // namespace

TEST(MixerSendAutomationPlaybackTest, RampDrivesTheSendLevelParameterDuringPlayback) {
    Fixture f;
    ASSERT_TRUE(f.build());

    constexpr double kStartDb = -24.0;
    constexpr double kEndDb = 0.0;
    constexpr double kLengthBeats = 4.0;

    const auto laneId = f.doc.addLane(f.trackId, kStripUuid, "send1Level", range(-60.0f, 12.0f, 0.0f));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, kStartDb));
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, kLengthBeats, kEndDb));
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    auto* sendLevel = f.sendLevelParam();
    ASSERT_NE(sendLevel, nullptr);
    ASSERT_NEAR(Fixture::denormalised(sendLevel), 0.0, 0.01) << "ChannelStripModule's sendNLevel default (unity)";

    ASSERT_TRUE(f.driver->getTransport().play());

    int blocksChecked = 0;
    f.driver->renderToBeat(kLengthBeats, [&](const juce::AudioBuffer<float>&, const synth::BlockTimeInfo& info) {
        if (!info.playing)
            return;
        // Same "evaluated at block start, before the graph runs" contract AutomationApplierTests
        // pins for a Filter's cutoff -- nothing about the parameter being a mixer send changes it.
        const double beat = juce::jlimit(0.0, kLengthBeats, info.startPpq);
        const double expected = kStartDb + (kEndDb - kStartDb) * (beat / kLengthBeats);
        EXPECT_NEAR(Fixture::denormalised(sendLevel), expected, 0.1)
            << "at beat " << info.startPpq << " (block " << blocksChecked << ")";
        ++blocksChecked;
    });

    EXPECT_GT(blocksChecked, 20) << "the ramp must be sampled across many blocks, not just one";

    // Two more blocks put the start position past the last breakpoint, where the kernel holds the
    // final value rather than extrapolating -- same tail idiom AutomationApplierTests uses.
    f.driver->renderBlocks(2);
    EXPECT_NEAR(Fixture::denormalised(sendLevel), kEndDb, 0.1);
}

TEST(MixerSendAutomationPlaybackTest, StoppedTransportLeavesSendLevelAlone) {
    Fixture f;
    ASSERT_TRUE(f.build());

    const auto laneId = f.doc.addLane(f.trackId, kStripUuid, "send1Level", range(-60.0f, 12.0f, 0.0f));
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 0.0, -24.0));
    ASSERT_TRUE(f.doc.addBreakpoint(laneId, 4.0, 0.0));
    f.publish();
    ASSERT_EQ(f.bindingCount(), 1);

    auto* sendLevel = f.sendLevelParam();
    ASSERT_NE(sendLevel, nullptr);
    const double before = Fixture::denormalised(sendLevel);
    ASSERT_NEAR(before, 0.0, 0.01);

    ASSERT_FALSE(f.driver->getTransport().getPositionSnapshot().playing);
    f.driver->renderBlocks(64);

    EXPECT_NEAR(Fixture::denormalised(sendLevel), before, 1e-6)
        << "automation must not fight the user for the send knob while the transport is stopped";
}
