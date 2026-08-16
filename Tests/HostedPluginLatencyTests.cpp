// HostedPluginLatencyTests.cpp
//
// A hosted plugin's latency, detected when it MOVES and compensated once the graph is told.
//
// Two separate facts have to hold, and the file is organised around them:
//
//   1. THE MODULE FOLLOWS ITS INSTANCE. A plugin can change its reported latency at any moment (the
//      user flips a lookahead mode in its own editor) and reports it on whatever thread it happened
//      to be on. HostedPluginModule listens for that, hops to the message thread, re-mirrors the
//      value onto the node, and tells its owner exactly once per real change — and never on behalf
//      of an instance that has since been retired.
//
//   2. THE GRAPH ACTUALLY COMPENSATES. juce::AudioProcessorGraph bakes each node's
//      {bus layout, latencySamples} into its render sequence and re-derives the parallel-path delay
//      compensation only when that sequence is REBUILT. The acceptance test drives a real
//      AudioEngine graph with an impulse split down a dry path and a hosted-plugin path and asserts
//      both copies land on the SAME output sample — off-by-zero, at two different latencies, across
//      a rebuild.
//
// The stub plugin (Tests/StubPluginInstance.h) really delays its audio by the latency it reports,
// so the PDC test actually exercises the compensation rather than an unmoving "latent" path.
//
// Groups:
//   1. The module's own contract — publish, runtime change, thread hop, unload, retired-instance
//      guard.
//   2. PDC — parallel paths realigned by a rebuild, and the graph's reported latency following only
//      after one.
//   3. Flow — MainComponent's owner wiring: a completed async load rebuilds the graph, refreshes the
//      status bar, and reconciles the timeline.

#include "../Source/AudioEngine.h"
#include "../Source/Modules/AudioInputModule.h"
#include "../Source/Plugin/Hosting/HostedPluginModule.h"
#include "../Source/Plugin/PluginProcessor.h"
#include "FakeAudioIODevice.h"
#include "StubPluginInstance.h"
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#if SYNTH_ENABLE_TIMELINE
#include "../Source/AI/AIProvider.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "MainComponent.h"
#endif

using synth::HostedPluginModule;
using synth::test::FakeAudioIODevice;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubPluginInstance;

namespace {

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate; // 48000
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;      // 512

/** Pumps the JUCE message loop until `predicate` holds or the timeout expires — the idiom from
 *  HostedPluginTests.cpp, needed because the backend callback is posted, never fired re-entrantly,
 *  and (here) because the latency notification hops through a juce::AsyncUpdater. */
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

juce::PluginDescription stubDescription(const juce::String& name = "Stub Plugin", int uid = 0x5754424) {
    juce::PluginDescription description;
    description.name = name;
    description.pluginFormatName = "VST3";
    description.uniqueId = uid;
    description.deprecatedUid = uid;
    description.fileOrIdentifier = "/nonexistent/test/path/StubPlugin.vst3";
    return description;
}

/** A backend whose instances report `latency` from the moment they are created, and which hands the
 *  test a pointer to the live one so it can move that latency at runtime. */
struct StubFactory {
    StubPluginInstance* live = nullptr;

    StubBackend::Factory make(int latency, std::vector<StubParamSpec> params = {}) {
        return [this, latency, params] {
            auto instance = std::make_unique<StubPluginInstance>(2, 2, "Stub Plugin", 0x5754424, "VST3", params,
                                                                 /*reportsEditor=*/false, latency);
            live = instance.get();
            return instance;
        };
    }
};

} // namespace

// ============================================================================
// 1. The module's own contract
// ============================================================================

TEST(HostedPluginLatencyTest, PublishMirrorsLatencyAndUnloadDropsIt) {
    StubFactory factory;
    StubBackend backend(factory.make(256));

    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    int latencyFires = 0;
    int publishFires = 0;
    bool alwaysOnMessageThread = true;
    const auto onMessageThread = [&] {
        alwaysOnMessageThread = alwaysOnMessageThread && juce::MessageManager::getInstance()->isThisTheMessageThread();
    };
    module.onLatencyChanged = [&] {
        ++latencyFires;
        onMessageThread();
    };
    module.onInstancePublished = [&] {
        ++publishFires;
        onMessageThread();
    };

    EXPECT_EQ(module.getLatencySamples(), 0);
    module.loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); }));

    EXPECT_EQ(module.getLatencySamples(), 256) << "the node must report what the instance reports";
    EXPECT_EQ(publishFires, 1) << "a completed load fires the publish edge exactly once";
    EXPECT_EQ(latencyFires, 0) << "a publish is onInstancePublished's edge; firing both would rebuild twice";

    module.unloadPlugin();
    EXPECT_EQ(module.getLatencySamples(), 0);
    EXPECT_EQ(latencyFires, 1) << "an unload must tell the owner: the graph is still compensating for a "
                                  "plugin that is gone";
    EXPECT_EQ(publishFires, 1);
    EXPECT_TRUE(alwaysOnMessageThread);
}

TEST(HostedPluginLatencyTest, RuntimeLatencyChangeFollowsOnTheMessageThread) {
    StubFactory factory;
    StubBackend backend(factory.make(128));

    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    int latencyFires = 0;
    bool alwaysOnMessageThread = true;
    module.onLatencyChanged = [&] {
        ++latencyFires;
        alwaysOnMessageThread = alwaysOnMessageThread && juce::MessageManager::getInstance()->isThisTheMessageThread();
    };

    module.loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); }));
    ASSERT_NE(factory.live, nullptr);
    ASSERT_EQ(module.getLatencySamples(), 128);

    // "The user flipped the plugin's lookahead mode" — reported from a thread that is NOT the
    // message thread, which is the whole reason the notification hops through an AsyncUpdater.
    std::thread([&factory] { factory.live->setReportedLatency(512); }).join();

    EXPECT_EQ(module.getLatencySamples(), 128) << "nothing may change before the message thread runs";
    ASSERT_TRUE(pumpUntil([&] { return module.getLatencySamples() == 512; })) << "the change was never noticed";
    EXPECT_EQ(latencyFires, 1) << "exactly once per change";
    EXPECT_TRUE(alwaysOnMessageThread) << "the callback must never reach an owner off the message thread";

    // The same value again is not a change: juce::AudioProcessor::setLatencySamples does not even
    // notify, and the module must not manufacture an owner callback (i.e. a graph rebuild) for it.
    std::thread([&factory] { factory.live->setReportedLatency(512); }).join();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    EXPECT_EQ(latencyFires, 1) << "an unchanged latency must not rebuild anyone's graph";

    std::thread([&factory] { factory.live->setReportedLatency(64); }).join();
    ASSERT_TRUE(pumpUntil([&] { return module.getLatencySamples() == 64; }));
    EXPECT_EQ(latencyFires, 2);
}

TEST(HostedPluginLatencyTest, AnUnloadCancelsARetiredInstanceUpdate) {
    // The generation guard, in its sharpest form: a latency change that a plugin reported from its
    // own thread and that has NOT been delivered yet, followed by an unload before the message loop
    // next runs. The queued update belongs to an instance that is no longer live and must not
    // republish anything.
    StubFactory factory;
    StubBackend backend(factory.make(128));

    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);

    int latencyFires = 0;
    module.onLatencyChanged = [&] { ++latencyFires; };

    module.loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return module.hasInstance(); }));
    ASSERT_NE(factory.live, nullptr);

    std::thread([&factory] { factory.live->setReportedLatency(4096); }).join(); // queued, never pumped
    module.unloadPlugin();

    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_EQ(module.getLatencySamples(), 0) << "a retired instance's queued change must not resurrect its latency";
    EXPECT_EQ(latencyFires, 1) << "only the unload's own N -> 0 edge";
}

TEST(HostedPluginLatencyTest, ReloadRepointsTheListenerAtTheNewInstance) {
    // A swap retires one instance and publishes another in the same call stack. The listener has to
    // travel with it, or a runtime change on the SECOND plugin is silently missed.
    StubFactory factory;
    StubBackend backend(factory.make(0));

    HostedPluginModule module;
    module.prepareToPlay(kSampleRate, kBlockSize);
    int latencyFires = 0;
    int publishes = 0;
    module.onLatencyChanged = [&] { ++latencyFires; };
    // Counted, not inferred from hasInstance(): a reload leaves the OLD instance live until its
    // replacement is published, so "has an instance" is already true the moment the second load
    // starts and would let this test run against a plugin that was never published.
    module.onInstancePublished = [&] { ++publishes; };

    module.loadPlugin(stubDescription("First"), backend);
    ASSERT_TRUE(pumpUntil([&] { return publishes == 1; }));
    auto* first = dynamic_cast<StubPluginInstance*>(module.getActiveInstanceForEditor());
    ASSERT_NE(first, nullptr);

    backend.setFactory(factory.make(0));
    module.loadPlugin(stubDescription("Second"), backend);
    ASSERT_TRUE(pumpUntil([&] { return publishes == 2; }));
    ASSERT_EQ(module.getActiveInstanceForEditor(), factory.live);
    ASSERT_NE(factory.live, first) << "the reload must have produced a different instance";

    factory.live->setReportedLatency(192);
    ASSERT_TRUE(pumpUntil([&] { return module.getLatencySamples() == 192; })) << "the listener never moved";
    EXPECT_EQ(latencyFires, 1);

    // ...and the RETIRED instance is no longer listened to. (It is still alive — reaping needs the
    // audio thread to move on — which is exactly what makes this assertable.)
    first->setReportedLatency(9999);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    EXPECT_EQ(module.getLatencySamples(), 192) << "a retired instance must not drive the live node's latency";
    EXPECT_EQ(latencyFires, 1);
}

// ============================================================================
// 2. PDC — the acceptance test
// ============================================================================

namespace {

/** The device's input fanned out down TWO paths into Audio Output: one dry, one through a hosted
 *  plugin. That fan-out is the whole point — parallel-path compensation has nothing to compensate in
 *  a single chain.
 *
 *  The source is the bare `audioInputNode` IO processor rather than `AudioInputModule`,
 *  deliberately: the module reads the device input off the transport playhead, which the engine only
 *  installs in a `SYNTH_ENABLE_TIMELINE` build, and none of this has anything to do with the
 *  timeline. The IO node reads the render buffer's own input channels, so these tests run in both
 *  builds. */
struct PdcGraph {
    AudioEngine engine{AudioEngine::HostMode::Standalone};
    HostedPluginModule* hosted = nullptr;

    PdcGraph() {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

        auto in = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode));
        auto plugin = graph.addNode(std::make_unique<HostedPluginModule>());
        auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
        hosted = dynamic_cast<HostedPluginModule*>(plugin->getProcessor());

        for (int channel = 0; channel < 2; ++channel) {
            graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});    // dry
            graph.addConnection({{in->nodeID, channel}, {plugin->nodeID, channel}}); // ...and wet
            graph.addConnection({{plugin->nodeID, channel}, {out->nodeID, channel}});
        }
    }
};

/** One device callback, with `impulseAmplitude` at absolute input sample `impulseSample` (negative:
 *  none), appending this block's LEFT output channel to `capturedOutput`. Zero-latency fake device,
 *  so the only latency anywhere in the run is the plugin's. */
void driveBlock(AudioEngine& engine, juce::int64 firstSample, juce::int64 impulseSample, float impulseAmplitude,
                std::vector<float>& capturedOutput) {
    std::vector<float> left((std::size_t)kBlockSize, 0.0f), right((std::size_t)kBlockSize, 0.0f);
    if (impulseSample >= firstSample && impulseSample < firstSample + kBlockSize)
        left[(std::size_t)(impulseSample - firstSample)] = right[(std::size_t)(impulseSample - firstSample)] =
            impulseAmplitude;

    std::vector<float> outLeft((std::size_t)kBlockSize, 0.0f), outRight((std::size_t)kBlockSize, 0.0f);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});

    capturedOutput.insert(capturedOutput.end(), outLeft.begin(), outLeft.end());
}

/** Every output sample above `threshold`, as absolute sample indices. One entry means the two copies
 *  of the impulse landed together; two means they did not. */
std::vector<juce::int64> nonSilentSamples(const std::vector<float>& output, float threshold) {
    std::vector<juce::int64> found;
    for (std::size_t i = 0; i < output.size(); ++i)
        if (std::abs(output[i]) > threshold)
            found.push_back((juce::int64)i);
    return found;
}

/** Runs `blocks` callbacks with a single impulse and returns the captured left output. */
std::vector<float> renderImpulse(AudioEngine& engine, juce::int64 impulseSample, int blocks) {
    std::vector<float> output;
    output.reserve((std::size_t)(blocks * kBlockSize));
    for (int block = 0; block < blocks; ++block)
        driveBlock(engine, (juce::int64)block * kBlockSize, impulseSample, 0.5f, output);
    return output;
}

} // namespace

// THE ACCEPTANCE TEST.
//
// An impulse split down two parallel paths — one dry, one through a plugin that reports N samples of
// latency and really delays its audio by N — must come out of Audio Output as ONE sample, not two N
// apart. JUCE achieves that by delaying the dry path by N when it builds the render sequence, which
// it only does on a rebuild; the point of this test is that something calls that rebuild.
TEST(HostedPluginLatencyTest, ParallelPathsStayAlignedAcrossALatencyChange) {
    constexpr int kFirstLatency = 128;
    constexpr int kSecondLatency = 320;
    constexpr juce::int64 kImpulseAt = 1000;
    constexpr int kBlocks = 8; // 4096 samples: comfortably past kImpulseAt + kSecondLatency
    constexpr float kAmplitude = 0.5f;
    // Dry copy + the stub's marked copy, on the same sample.
    constexpr float kExpectedSum = kAmplitude * (1.0f + StubPluginInstance::kGainMarker);

    PdcGraph fixture;
    ASSERT_NE(fixture.hosted, nullptr);

    StubFactory factory;
    StubBackend backend(factory.make(kFirstLatency));

    FakeAudioIODevice fake(2, 2, /*inputLatency=*/0, /*outputLatency=*/0);
    fixture.engine.audioDeviceAboutToStart(&fake);

    fixture.hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->hasInstance(); }));
    ASSERT_EQ(fixture.hosted->getLatencySamples(), kFirstLatency);

    // THE REBUILD — what MainComponent's onInstancePublished callback does in the app, and the one
    // thing that makes any of the above audible. (Flow-level coverage of that wiring is below.)
    fixture.engine.getGraph().rebuild();
    ASSERT_EQ(fixture.engine.getGraphLatencySamples(), kFirstLatency);

    {
        const auto output = renderImpulse(fixture.engine, kImpulseAt, kBlocks);
        const auto hits = nonSilentSamples(output, 0.05f);
        ASSERT_EQ(hits.size(), 1u) << "the two copies of the impulse must land on ONE sample, not " << hits.size();
        EXPECT_EQ(hits[0], kImpulseAt + kFirstLatency)
            << "both paths arrive at the graph's own latency — off-by-zero, not near";
        EXPECT_NEAR(output[(std::size_t)hits[0]], kExpectedSum, 1e-5f)
            << "the dry copy and the plugin's marked copy must both be in that one sample";
    }

    // Now the runtime change: the plugin decides it needs more lookahead, mid-session.
    ASSERT_NE(factory.live, nullptr);
    factory.live->setReportedLatency(kSecondLatency);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->getLatencySamples() == kSecondLatency; }));

    fixture.engine.getGraph().rebuild();
    ASSERT_EQ(fixture.engine.getGraphLatencySamples(), kSecondLatency);

    {
        const auto output = renderImpulse(fixture.engine, kImpulseAt, kBlocks);
        const auto hits = nonSilentSamples(output, 0.05f);
        ASSERT_EQ(hits.size(), 1u) << "still exactly one sample at the new latency, not " << hits.size();
        EXPECT_EQ(hits[0], kImpulseAt + kSecondLatency);
        EXPECT_NEAR(output[(std::size_t)hits[0]], kExpectedSum, 1e-5f);
    }

    fixture.engine.audioDeviceStopped();
}

// The negative control for the test above: WITHOUT the rebuild, the graph is still compensating for
// the old latency, so the two copies separate. Pinned so a future "rebuild() looks redundant"
// cleanup fails loudly.
TEST(HostedPluginLatencyTest, WithoutARebuildTheParallelPathsDrift) {
    constexpr int kFirstLatency = 128;
    constexpr int kSecondLatency = 320;
    constexpr juce::int64 kImpulseAt = 1000;

    PdcGraph fixture;
    ASSERT_NE(fixture.hosted, nullptr);

    StubFactory factory;
    StubBackend backend(factory.make(kFirstLatency));

    FakeAudioIODevice fake(2, 2, 0, 0);
    fixture.engine.audioDeviceAboutToStart(&fake);

    fixture.hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->hasInstance(); }));
    fixture.engine.getGraph().rebuild();

    ASSERT_NE(factory.live, nullptr);
    factory.live->setReportedLatency(kSecondLatency);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->getLatencySamples() == kSecondLatency; }));
    // ...and deliberately NO rebuild here.
    EXPECT_EQ(fixture.engine.getGraphLatencySamples(), kFirstLatency)
        << "the graph reports what its baked sequence says, not what the nodes now claim";

    const auto output = renderImpulse(fixture.engine, kImpulseAt, 8);
    const auto hits = nonSilentSamples(output, 0.05f);
    ASSERT_EQ(hits.size(), 2u) << "uncompensated, the impulse must arrive TWICE — that is the whole bug";
    EXPECT_EQ(hits[1] - hits[0], kSecondLatency - kFirstLatency) << "separated by exactly the uncompensated delta";

    fixture.engine.audioDeviceStopped();
}

// Status-bar honesty (the readout is a sum whose graph term is this): the number the user sees
// only becomes true after the rebuild, which is why the owner refreshes it there.
TEST(HostedPluginLatencyTest, ReportedGraphLatencyFollowsOnlyAfterARebuild) {
    constexpr int kLatency = 4800; // 100 ms at 48 kHz — a real lookahead limiter's order of magnitude

    PdcGraph fixture;
    ASSERT_NE(fixture.hosted, nullptr);

    StubFactory factory;
    StubBackend backend(factory.make(kLatency));

    FakeAudioIODevice fake(2, 2); // the shared 64 in / 128 out
    fixture.engine.audioDeviceAboutToStart(&fake);
    ASSERT_EQ(fixture.engine.getGraphLatencySamples(), 0);
    const int deviceRoundTrip = fixture.engine.getRecordingLatencySamples();
    ASSERT_EQ(deviceRoundTrip, synth::test::kFakeDeviceInputLatency + synth::test::kFakeDeviceOutputLatency);

    fixture.hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return fixture.hosted->hasInstance(); }));

    EXPECT_EQ(fixture.hosted->getLatencySamples(), kLatency);
    EXPECT_EQ(fixture.engine.getGraphLatencySamples(), 0) << "the node knows; the baked sequence does not, yet";
    EXPECT_EQ(fixture.engine.getRecordingLatencySamples(), deviceRoundTrip);

    fixture.engine.getGraph().rebuild();
    EXPECT_EQ(fixture.engine.getGraphLatencySamples(), kLatency);
    EXPECT_EQ(fixture.engine.getRecordingLatencySamples(), deviceRoundTrip + kLatency)
        << "input + graph + output: a recorded take is shifted back by the plugin's latency too";

    fixture.engine.audioDeviceStopped();
}

// The wrapper's half of PDC: a DAW hosting AgentSynth can only compensate for a plugin nested
// INSIDE AgentSynth if AgentSynthAudioProcessor reports the inner graph's latency as its own. The
// inner rebuild (above) fixes alignment BETWEEN the graph's parallel paths; this mirror is what
// keeps AgentSynth's whole track aligned with every other track in the host.
TEST(HostedPluginLatencyTest, InnerGraphLatencyIsMirroredToTheHost) {
    constexpr int kLatencyA = 512;
    constexpr int kLatencyB = 4800;

    synth::AgentSynthAudioProcessor processor;
    auto& graph = processor.getAudioEngine().getGraph();

    // Replace the default patch with the minimal latent one: Hosted Plugin -> Audio Output.
    graph.clear();
    auto plugin = graph.addNode(std::make_unique<HostedPluginModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    for (int channel = 0; channel < 2; ++channel)
        graph.addConnection({{plugin->nodeID, channel}, {out->nodeID, channel}});
    auto* hosted = dynamic_cast<HostedPluginModule*>(plugin->getProcessor());
    ASSERT_NE(hosted, nullptr);

    StubFactory factory;
    StubBackend backend(factory.make(kLatencyA));
    hosted->prepareToPlay(kSampleRate, kBlockSize);
    hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return hosted->hasInstance(); }));

    // prepareToPlay is the DAW's own prepare call: the graph is (re)prepared inside it, so the
    // mirror must be current the moment it returns.
    processor.prepareToPlay(kSampleRate, kBlockSize);
    EXPECT_EQ(processor.getLatencySamples(), kLatencyA);

    // Runtime change: the plugin flips its lookahead, the module follows on the message thread,
    // and the owner rebuilds (MainComponent's reaction, simulated here). The new figure reaches
    // the host on the next processBlock, not the next prepareToPlay.
    ASSERT_NE(factory.live, nullptr);
    factory.live->setReportedLatency(kLatencyB);
    ASSERT_TRUE(pumpUntil([&] { return hosted->getLatencySamples() == kLatencyB; }));
    graph.rebuild();

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    EXPECT_EQ(processor.getLatencySamples(), kLatencyB);

    processor.releaseResources();
}

// ============================================================================
// 3. Flow — MainComponent's owner wiring
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

namespace {

// Same minimal provider shape as every other MainComponent-level test (RecordTapTests.cpp,
// LatencyAlignmentTests.cpp).
class MinimalProviderHPL : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "HostedPluginLatencyMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"mock-model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{true, "Mock response.", {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }

private:
    juce::String model = "mock-model";
};

constexpr const char* kPluginUuid = "b0770000-0000-0000-0000-000000000001";

} // namespace

class HostedPluginLatencyFlowTest : public ::testing::Test {
protected:
    // The delegating MainComponent ctor reads the shared on-disk "Agent Synth" settings — same
    // hygiene as LatencyAlignmentTests.cpp's LatencyFlowTest.
    void writeKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("timelinePanelVisible", "0");
            s->setValue("timelineCountInBars", 0);
            s->saveIfNeeded();
        }
    }

    void SetUp() override { writeKeys(); }
    void TearDown() override { writeKeys(); }

    static void quiesceEngine(MainComponent& mc) { mc.getAudioEngine().suspendDeviceCallback(); }

    /** Audio Input fanned out dry + through a hosted plugin into Audio Output, on MainComponent's
     *  own graph. Returns the hosted module, with `kPluginUuid` on its node. */
    static HostedPluginModule* buildPluginPatch(MainComponent& mc) {
        auto& graph = mc.getAudioEngine().getGraph();
        graph.clear();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);

        auto in = graph.addNode(std::make_unique<AudioInputModule>());
        auto plugin = graph.addNode(std::make_unique<HostedPluginModule>());
        auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));

        // The uuid pairing every writer keeps: the message-thread property AND the processor mirror.
        plugin->properties.set("uuid", juce::String(kPluginUuid));
        if (auto* module = dynamic_cast<ModuleBase*>(plugin->getProcessor()))
            module->setNodeUuid(kPluginUuid);

        for (int channel = 0; channel < 2; ++channel) {
            graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});
            graph.addConnection({{in->nodeID, channel}, {plugin->nodeID, channel}});
            graph.addConnection({{plugin->nodeID, channel}, {out->nodeID, channel}});
        }

        return dynamic_cast<HostedPluginModule*>(plugin->getProcessor());
    }
};

// A project opens with a lane bound to a hosted plugin's parameter; the plugin load is async, so at
// reconcile time there is no instance and the lane orphans — correctly. The load completing must
// re-run the reconcile, or the lane stays orphaned (and unplayable) until some unrelated graph edit
// happens to trigger the next pass.
TEST_F(HostedPluginLatencyFlowTest, CompletedAsyncLoadRebindsLanesWithNoOtherGraphChange) {
    MainComponent mc(std::make_unique<MinimalProviderHPL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto* hosted = buildPluginPatch(mc);
    ASSERT_NE(hosted, nullptr);

    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    synth::AutomationLane::RangeSnapshot rangeSnapshot;
    rangeSnapshot.minValue = 0.0f;
    rangeSnapshot.maxValue = 1.0f;
    rangeSnapshot.defaultValue = 0.25f;
    const auto laneId = doc.addLane(trackId, kPluginUuid, "cutoff", rangeSnapshot, /*paramIndexHint=*/0);
    ASSERT_TRUE(laneId.isValid());

    // The one hook every node-adding path runs through — it installs MainComponent's observers and
    // reconciles. With nothing loaded, the lane cannot resolve.
    mc.getGraphEditor().updateComponents();
    ASSERT_NE(doc.getLane(laneId), nullptr);
    ASSERT_TRUE(doc.getLane(laneId)->orphaned) << "a node with no instance cannot vouch for any parameter";

    // Now the load completes — and NOTHING else touches the graph from here on. (Pumping runs
    // MainComponent's 10 Hz poll and GraphEditor's animation timer; neither reconciles or calls
    // updateComponents, so a rebind can only have come from the publish hook.)
    StubFactory factory;
    StubBackend backend(factory.make(0, {{"cutoff", "Cutoff", 0.25f}}));
    hosted->loadPlugin(stubDescription(), backend);

    ASSERT_TRUE(pumpUntil([&] { return hosted->hasInstance(); }));
    ASSERT_TRUE(pumpUntil([&] { return !doc.getLane(laneId)->orphaned; }))
        << "a completed async load must reconcile the timeline";
}

// The other half of the owner wiring: the publish rebuilds the graph (so PDC is live immediately,
// not at the next unrelated edit) and refreshes the status bar's round-trip readout.
TEST_F(HostedPluginLatencyFlowTest, CompletedAsyncLoadRebuildsPdcAndRefreshesTheStatusBar) {
    constexpr int kLatency = 4800; // 100.0 ms at 48 kHz

    MainComponent mc(std::make_unique<MinimalProviderHPL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto* hosted = buildPluginPatch(mc);
    ASSERT_NE(hosted, nullptr);
    mc.getGraphEditor().updateComponents(); // installs MainComponent's observers on the new node

    auto& engine = mc.getAudioEngine();
    FakeAudioIODevice fake(2, 2); // 64 in / 128 out = 4.0 ms of device round trip
    engine.audioDeviceAboutToStart(&fake);
    ASSERT_EQ(engine.getGraphLatencySamples(), 0);

    // Two ticks to reach the first 5 Hz status-bar poll, exactly as StatusBarShowsRoundTrip does.
    mc.timerCallback();
    mc.timerCallback();
    ASSERT_EQ(mc.getStatusBar().getRoundTripTextForTest(), "RT 4.0 ms");

    StubFactory factory;
    StubBackend backend(factory.make(kLatency));
    hosted->loadPlugin(stubDescription(), backend);
    ASSERT_TRUE(pumpUntil([&] { return hosted->hasInstance(); }));

    EXPECT_EQ(engine.getGraphLatencySamples(), kLatency)
        << "the publish hook must have rebuilt the graph: nothing else in the app calls rebuild()";
    EXPECT_EQ(mc.getStatusBar().getRoundTripTextForTest(), "RT 104.0 ms")
        << "the readout is fed from the same path the 5 Hz poll uses, refreshed when the sum moves";

    // ...and a RUNTIME change goes down the same wire, with no graph edit anywhere.
    ASSERT_NE(factory.live, nullptr);
    factory.live->setReportedLatency(2400); // 50 ms
    ASSERT_TRUE(pumpUntil([&] { return engine.getGraphLatencySamples() == 2400; }))
        << "the latency hook must rebuild the graph too";
    EXPECT_EQ(mc.getStatusBar().getRoundTripTextForTest(), "RT 54.0 ms");

    engine.audioDeviceStopped();
}

// The observers MainComponent installs capture `this` — and on the plugin path the engine, and
// every hosted module in its graph, OUTLIVES the editor-owned MainComponent (hosts close and
// reopen editors freely). The destructor must uninstall them, or the next latency change/publish
// after an editor close calls through freed memory inside the host.
TEST_F(HostedPluginLatencyFlowTest, ClosingTheEditorUninstallsTheObservers) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;

    // Mirrors AgentSynthAudioProcessor: Hosted mode, initialise() by the owner, never MainComponent.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    HostedPluginModule* hosted = nullptr;
    {
        MainComponent mc(tm, lf, engine, std::make_unique<MinimalProviderHPL>());
        mc.setSize(1600, 900);

        hosted = buildPluginPatch(mc);
        ASSERT_NE(hosted, nullptr);
        mc.getGraphEditor().updateComponents(); // the hook that installs the observers
        ASSERT_TRUE(static_cast<bool>(hosted->onLatencyChanged));
        ASSERT_TRUE(static_cast<bool>(hosted->onInstancePublished));
    } // the editor closes; the engine — and `hosted` — live on

    EXPECT_FALSE(static_cast<bool>(hosted->onLatencyChanged))
        << "a destroyed MainComponent must not stay reachable from a hosted module";
    EXPECT_FALSE(static_cast<bool>(hosted->onInstancePublished));

    engine.shutdown();
}

#endif // SYNTH_ENABLE_TIMELINE
