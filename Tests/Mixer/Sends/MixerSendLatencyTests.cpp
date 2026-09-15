// MixerSendLatencyTests.cpp -- FRO15 (P9-9, docs/mixer.md §5.15 D4): a send is a real parallel
// path, so it is subject to juce::AudioProcessorGraph's own delay compensation and nothing new is
// written for it. Same shape and the same StubPluginInstance as the reference test this is modelled
// on (Tests/Plugin/HostedPluginLatencyTests.cpp's ParallelPathsStayAlignedAcrossALatencyChange):
// an impulse fans out down a dry path and a latent path, and the two copies must land on ONE output
// sample.
//
// The test is only meaningful with something latent ON the bus, which is what the stub plugin is
// for -- it really delays its audio by the latency it reports.
//
//   * acceptance     -- source main -> Master vs source SEND -> latent bus -> Master: one hit
//   * negative       -- a latency that moves with the topology unchanged drifts until a rebuild,
//                        so the alignment above is really PDC and not an accident of the rig
//   * no manual hook -- adding a send is a TOPOLOGY change, so the graph schedules its own rebuild:
//                        pumping the message loop realigns with nobody calling rebuild() by hand
//   * removal        -- dropping the send takes the latency with it

#include "../../FakeAudioIODevice.h"
#include "../../StubPluginInstance.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using synth::HostedPluginModule;
using synth::test::FakeAudioIODevice;
using synth::test::StubBackend;
using synth::test::StubPluginInstance;

namespace {

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
using NodeID = juce::AudioProcessorGraph::NodeID;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate;
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;
constexpr int kRight = ChannelStripModule::kRightBase;
constexpr juce::int64 kImpulseAt = 1000;
constexpr int kBlocks = 8;
constexpr float kAmplitude = 0.5f;

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "Stub Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0x5754424;
    description.deprecatedUid = 0x5754424;
    description.fileOrIdentifier = "/nonexistent/test/path/StubPlugin.vst3";
    return description;
}

struct StubFactory {
    StubPluginInstance* live = nullptr;

    StubBackend::Factory make(int latency) {
        return [this, latency] {
            auto instance = std::make_unique<StubPluginInstance>(2, 2, "Stub Plugin", 0x5754424, "VST3",
                                                                 std::vector<synth::test::StubParamSpec>{},
                                                                 /*reportsEditor=*/false, latency);
            live = instance.get();
            return instance;
        };
    }
};

/** Audio In -> source strip -> Audio Out (dry), and source strip's SEND 0 -> hosted plugin -> bus
 *  strip -> Audio Out (the latent parallel path). Deliberately no Master node: this is about the
 *  graph's own delay compensation across a fan-out, and Master would only add a summing hop. */
struct SendPdcGraph {
    AudioEngine engine{AudioEngine::HostMode::Standalone};
    NodeID source, bus, plugin, output;
    HostedPluginModule* hosted = nullptr;

    SendPdcGraph() {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

        auto in = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode));
        auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
        auto sourceNode = graph.addNode(std::make_unique<ChannelStripModule>());
        auto busNode = graph.addNode(std::make_unique<ChannelStripModule>());
        auto pluginNode = graph.addNode(std::make_unique<HostedPluginModule>());
        output = out->nodeID;
        source = sourceNode->nodeID;
        bus = busNode->nodeID;
        plugin = pluginNode->nodeID;
        hosted = dynamic_cast<HostedPluginModule*>(pluginNode->getProcessor());
        dynamic_cast<ChannelStripModule*>(busNode->getProcessor())->setIsBus(true);

        graph.addConnection({{in->nodeID, 0}, {source, 0}});
        graph.addConnection({{in->nodeID, 1}, {source, kRight}});
        graph.addConnection({{source, 0}, {output, 0}}); // dry
        graph.addConnection({{source, kRight}, {output, 1}});
        graph.addConnection({{bus, 0}, {output, 0}}); // wet, once the send is wired
        graph.addConnection({{bus, kRight}, {output, 1}});
    }

    /** The send leg, wired the way the app's own flow wires it -- through synth::addSend, so the
     *  test exercises the shipped path rather than hand-built edges. Then the plugin, spliced onto
     *  the send's way into the bus. */
    void wireSendThroughThePlugin() {
        auto& graph = engine.getGraph();
        ASSERT_EQ(synth::addSend(graph, source, bus), 0);
        // Re-point the slot's cables through the latent plugin: source send -> plugin -> bus.
        graph.removeConnection({{source, ChannelStripModule::sendLeftChannel(0)}, {bus, 0}});
        graph.removeConnection({{source, ChannelStripModule::sendRightChannel(0)}, {bus, kRight}});
        graph.addConnection({{source, ChannelStripModule::sendLeftChannel(0)}, {plugin, 0}});
        graph.addConnection({{source, ChannelStripModule::sendRightChannel(0)}, {plugin, 1}});
        graph.addConnection({{plugin, 0}, {bus, 0}});
        graph.addConnection({{plugin, 1}, {bus, kRight}});
        // The slot still resolves to the bus: findSendTarget walks THROUGH the inserted module.
        EXPECT_EQ(synth::findSendTarget(graph, source, 0), bus);
    }
};

void driveBlock(AudioEngine& engine, juce::int64 firstSample, std::vector<float>& capturedOutput) {
    std::vector<float> left((std::size_t)kBlockSize, 0.0f), right((std::size_t)kBlockSize, 0.0f);
    if (kImpulseAt >= firstSample && kImpulseAt < firstSample + kBlockSize)
        left[(std::size_t)(kImpulseAt - firstSample)] = right[(std::size_t)(kImpulseAt - firstSample)] = kAmplitude;

    std::vector<float> outLeft((std::size_t)kBlockSize, 0.0f), outRight((std::size_t)kBlockSize, 0.0f);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
    capturedOutput.insert(capturedOutput.end(), outLeft.begin(), outLeft.end());
}

std::vector<juce::int64> nonSilentSamples(const std::vector<float>& output, float threshold) {
    std::vector<juce::int64> found;
    for (std::size_t i = 0; i < output.size(); ++i)
        if (std::abs(output[i]) > threshold)
            found.push_back((juce::int64)i);
    return found;
}

std::vector<float> renderImpulse(AudioEngine& engine) {
    std::vector<float> output;
    output.reserve((std::size_t)(kBlocks * kBlockSize));
    for (int block = 0; block < kBlocks; ++block)
        driveBlock(engine, (juce::int64)block * kBlockSize, output);
    return output;
}

} // namespace

// THE ACCEPTANCE TEST. The dry copy (source strip straight out) and the send copy (source strip's
// send leg through a plugin that really delays by N, into the bus strip) must arrive on ONE sample.
TEST(MixerSendLatencyTest, SendPathStaysAlignedWithTheDirectPath) {
    constexpr int kLatency = 128;
    // Dry copy + the stub's marked copy, both at unity send level, on the same sample.
    constexpr float kExpectedSum = kAmplitude * (1.0f + StubPluginInstance::kGainMarker);

    SendPdcGraph fixture;
    ASSERT_NE(fixture.hosted, nullptr);
    StubFactory factory;
    StubBackend backend(factory.make(kLatency));

    FakeAudioIODevice fake(2, 2, 0, 0);
    fixture.engine.audioDeviceAboutToStart(&fake);
    fixture.wireSendThroughThePlugin();

    fixture.hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->hasInstance(); }));
    ASSERT_EQ(fixture.hosted->getLatencySamples(), kLatency);

    // Mirrors the reference test: rebuild explicitly so nothing depends on async timing.
    fixture.engine.getGraph().rebuild();
    ASSERT_EQ(fixture.engine.getGraphLatencySamples(), kLatency);

    const auto output = renderImpulse(fixture.engine);
    const auto hits = nonSilentSamples(output, 0.05f);
    ASSERT_EQ(hits.size(), 1u) << "the dry copy and the send copy must land on ONE sample, not " << hits.size();
    EXPECT_EQ(hits[0], kImpulseAt + kLatency) << "both paths arrive at the graph's own latency -- off-by-zero";
    EXPECT_NEAR(output[(std::size_t)hits[0]], kExpectedSum, 1e-5f)
        << "and that one sample holds both the dry signal and the bus return";

    fixture.engine.audioDeviceStopped();
}

// The negative control. With the topology unchanged and only the reported latency moving, the graph
// is still compensating for the old value until something rebuilds it -- so the two copies separate
// by exactly the delta. Pinned so the acceptance test above can never pass by accident.
TEST(MixerSendLatencyTest, WithoutARebuildTheSendPathDrifts) {
    constexpr int kFirstLatency = 128;
    constexpr int kSecondLatency = 320;

    SendPdcGraph fixture;
    ASSERT_NE(fixture.hosted, nullptr);
    StubFactory factory;
    StubBackend backend(factory.make(kFirstLatency));

    FakeAudioIODevice fake(2, 2, 0, 0);
    fixture.engine.audioDeviceAboutToStart(&fake);
    fixture.wireSendThroughThePlugin();

    fixture.hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->hasInstance(); }));
    fixture.engine.getGraph().rebuild();

    ASSERT_NE(factory.live, nullptr);
    factory.live->setReportedLatency(kSecondLatency);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->getLatencySamples() == kSecondLatency; }));
    // NO rebuild here.

    const auto output = renderImpulse(fixture.engine);
    const auto hits = nonSilentSamples(output, 0.05f);
    ASSERT_EQ(hits.size(), 2u) << "uncompensated, the two copies must separate";
    EXPECT_EQ(hits[1] - hits[0], kSecondLatency - kFirstLatency) << "by exactly the uncompensated delta";

    fixture.engine.audioDeviceStopped();
}

// D4's open question, answered empirically: adding a send is a TOPOLOGY change, and
// juce::AudioProcessorGraph schedules its own rebuild for those. Nothing in the send flow has to
// call MainComponent::rebuildGraphForLatencyChange() -- pumping the message loop is enough, which
// the app does continuously.
TEST(MixerSendLatencyTest, AddingASendSchedulesItsOwnRebuild) {
    constexpr int kLatency = 256;
    constexpr float kExpectedSum = kAmplitude * (1.0f + StubPluginInstance::kGainMarker);

    SendPdcGraph fixture;
    ASSERT_NE(fixture.hosted, nullptr);
    StubFactory factory;
    StubBackend backend(factory.make(kLatency));

    FakeAudioIODevice fake(2, 2, 0, 0);
    fixture.engine.audioDeviceAboutToStart(&fake);
    fixture.hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->hasInstance(); }));

    // The send is wired LAST, with the graph already live -- and nobody calls rebuild().
    fixture.wireSendThroughThePlugin();
    ASSERT_TRUE(pumpUntil([&] { return fixture.engine.getGraphLatencySamples() == kLatency; }))
        << "the graph never rebuilt itself after the topology change";

    const auto output = renderImpulse(fixture.engine);
    const auto hits = nonSilentSamples(output, 0.05f);
    ASSERT_EQ(hits.size(), 1u) << "aligned with no manual rebuild call anywhere";
    EXPECT_NEAR(output[(std::size_t)hits[0]], kExpectedSum, 1e-5f);

    fixture.engine.audioDeviceStopped();
}

TEST(MixerSendLatencyTest, RemovingTheSendRestoresAlignment) {
    constexpr int kLatency = 128;

    SendPdcGraph fixture;
    ASSERT_NE(fixture.hosted, nullptr);
    StubFactory factory;
    StubBackend backend(factory.make(kLatency));

    FakeAudioIODevice fake(2, 2, 0, 0);
    fixture.engine.audioDeviceAboutToStart(&fake);
    fixture.wireSendThroughThePlugin();
    fixture.hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->hasInstance(); }));
    fixture.engine.getGraph().rebuild();
    ASSERT_EQ(fixture.engine.getGraphLatencySamples(), kLatency);

    ASSERT_TRUE(synth::removeSend(fixture.engine.getGraph(), fixture.source, 0));
    EXPECT_EQ(synth::findSendTarget(fixture.engine.getGraph(), fixture.source, 0), NodeID{});
    fixture.engine.getGraph().rebuild();

    const auto output = renderImpulse(fixture.engine);
    const auto hits = nonSilentSamples(output, 0.05f);
    // The bus is still wired to the output, it just has nothing feeding it any more -- so exactly
    // one copy of the impulse survives, and it is the dry one at full amplitude.
    ASSERT_EQ(hits.size(), 1u) << "the send's copy is gone, not merely moved";
    EXPECT_NEAR(output[(std::size_t)hits[0]], kAmplitude, 1e-5f) << "the dry path alone, with no bus return";

    fixture.engine.audioDeviceStopped();
}
