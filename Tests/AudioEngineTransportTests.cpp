// Tests for the transport wiring inside AudioEngine: the once-per-block tick in renderNextBlock,
// the TransportService installed as the graph's juce::AudioPlayHead, and report-only latency.
//
// Headless/deterministic constraints (see docs/testing.md and house style in
// PluginProcessorTests.cpp / MainComponentTests.cpp): no real audio device, no network, no sleeps.
// Every engine here is HostMode::Hosted — hosted engines never touch the device manager or open
// MIDI inputs, so initialise() is safe. A HostMode::Standalone engine must never have initialise()
// called on it in tests, since that opens real hardware. The graph is clocked exclusively through
// prepareForHost() / processHostBlock(), which funnel into the same private renderNextBlock() the
// standalone device callback uses — so what is asserted here holds for both modes.

#include "../Source/AppUndoManager.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Transport/TransportService.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 44100.0;
constexpr double kDefaultBpm = 120.0;

void processHostBlocks(AudioEngine& engine, int numBlocks, int blockSize = kBlockSize) {
    for (int i = 0; i < numBlocks; ++i) {
        juce::AudioBuffer<float> buffer(2, blockSize);
        buffer.clear(); // mirrors AgentSynthAudioProcessor::processBlock zeroing before the graph runs
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
    }
}

std::int64_t samplePositionOf(const AudioEngine& engine) {
    return engine.getTransport().getPositionSnapshot().samplePosition;
}

/** Every node in the graph must be reading the engine's transport through the standard playhead
 *  API. Returns the number of nodes that are NOT, so a failure message can be specific. */
int nodesMissingPlayHead(AudioEngine& engine) {
    const juce::AudioPlayHead* expected = &engine.getTransport();
    int missing = 0;
    for (auto* node : engine.getGraph().getNodes())
        if (node->getProcessor()->getPlayHead() != expected)
            ++missing;
    return missing;
}

} // namespace

// ============================================================================
// The one clock: tick() happens exactly once per rendered block
// ============================================================================

// Asserts the timeline integration: tick advances the transport.
TEST(AudioEngineTransportTest, HostedEngineTicksTransportOncePerBlock) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(kSampleRate, kBlockSize, 0, 2);

    ASSERT_TRUE(engine.getTransport().play());

    // The snapshot published by tick() describes the START of the block being rendered, so after N
    // blocks it reads (N-1) * blockSize. play() itself is a queued command drained by the first
    // tick, which is why block 1 still starts at sample 0.
    constexpr int kBlocks = 5;
    processHostBlocks(engine, kBlocks);
    EXPECT_EQ(samplePositionOf(engine), (std::int64_t)(kBlocks - 1) * kBlockSize);

    processHostBlocks(engine, 1);
    EXPECT_EQ(samplePositionOf(engine), (std::int64_t)kBlocks * kBlockSize)
        << "one rendered block must advance the transport by exactly one block of samples";

    // Stopped: the position freezes. The stop() command lands on the next tick, so take the
    // reference reading after that block, then prove further blocks don't move it.
    ASSERT_TRUE(engine.getTransport().stop());
    processHostBlocks(engine, 1);
    const auto stoppedAt = samplePositionOf(engine);
    processHostBlocks(engine, 4);
    EXPECT_EQ(samplePositionOf(engine), stoppedAt) << "a stopped transport must not advance while blocks render";
    EXPECT_FALSE(engine.getTransport().getPositionSnapshot().playing);

    engine.releaseFromHost();
    engine.shutdown();
}

// prepare() (unlike tick()) always runs.
TEST(AudioEngineTransportTest, PrepareForHostPreparesTransport) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(96000.0, 256, 0, 2);

    EXPECT_EQ(engine.getTransport().getPositionSnapshot().sampleRate, 96000.0)
        << "prepareForHost must hand the host's sample rate to the transport, or every musical-time "
           "conversion is wrong by the rate ratio";

    engine.releaseFromHost();
    engine.shutdown();
}

// ============================================================================
// Playhead installation — once on the graph, re-applied by JUCE to every node
// ============================================================================
// Asserts the timeline integration: the playhead is installed on the graph.

TEST(AudioEngineTransportTest, PlayHeadInstalledOnEveryNode) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(kSampleRate, kBlockSize, 0, 2);

    EXPECT_EQ(engine.getGraph().getPlayHead(), &engine.getTransport());

    processHostBlocks(engine, 1);

    ASSERT_GT(engine.getGraph().getNumNodes(), 0) << "default patch must be non-empty or this proves nothing";
    EXPECT_EQ(nodesMissingPlayHead(engine), 0)
        << "every node processor must see the engine's transport after one render pass";

    engine.releaseFromHost();
    engine.shutdown();
}

TEST(AudioEngineTransportTest, PlayHeadSurvivesUndoRestore) {
    // This is the whole point of installing the playhead on the GRAPH rather than on each node: an
    // undo/redo of a structural change re-creates node processors, and nothing re-injects anything.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(kSampleRate, kBlockSize, 0, 2);

    auto& graph = engine.getGraph();
    AppUndoManager undoManager;
    const int nodesBefore = graph.getNumNodes();

    undoManager.recordStructuralChange(graph, [&graph] { graph.addNode(std::make_unique<OscillatorModule>()); });
    ASSERT_EQ(graph.getNumNodes(), nodesBefore + 1);

    ASSERT_TRUE(undoManager.undo());
    ASSERT_EQ(graph.getNumNodes(), nodesBefore);
    ASSERT_TRUE(undoManager.redo());
    ASSERT_EQ(graph.getNumNodes(), nodesBefore + 1);

    ASSERT_TRUE(engine.getTransport().play());
    processHostBlocks(engine, 2);

    EXPECT_EQ(nodesMissingPlayHead(engine), 0)
        << "nodes re-created by an undo restore must still see the transport, with no re-injection";
    EXPECT_EQ(samplePositionOf(engine), (std::int64_t)kBlockSize)
        << "the transport must still be ticking after the graph's node set was rebuilt";

    engine.releaseFromHost();
    engine.shutdown();
}

TEST(AudioEngineTransportTest, NodeSeesTransportPositionThroughPlayHead) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(kSampleRate, kBlockSize, 0, 2);

    ASSERT_TRUE(engine.getTransport().play());
    processHostBlocks(engine, 3);

    juce::AudioProcessorGraph::Node* someNode = nullptr;
    for (auto* node : engine.getGraph().getNodes())
        if (someNode == nullptr)
            someNode = node;
    ASSERT_NE(someNode, nullptr);

    auto* playHead = someNode->getProcessor()->getPlayHead();
    ASSERT_NE(playHead, nullptr);

    const auto position = playHead->getPosition();
    ASSERT_TRUE(position.hasValue());
    EXPECT_TRUE(position->getIsPlaying());

    const auto timeInSamples = position->getTimeInSamples();
    ASSERT_TRUE(timeInSamples.hasValue());
    EXPECT_EQ(*timeInSamples, (std::int64_t)2 * kBlockSize) << "a node reads the START of the block it is rendering";

    const auto ppq = position->getPpqPosition();
    ASSERT_TRUE(ppq.hasValue());
    EXPECT_NEAR(*ppq, (double)(2 * kBlockSize) * kDefaultBpm / (60.0 * kSampleRate), 1e-9);

    engine.releaseFromHost();
    engine.shutdown();
}

// ============================================================================
// Latency is reported, never compensated by us
// ============================================================================
// Not gated: getGraphLatencySamples() is a pass-through of the graph's own aggregate latency
// reporting, which has nothing to do with the transport tick or playhead installation.

TEST(AudioEngineTransportTest, GraphLatencyIsReportedNotCompensated) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(kSampleRate, kBlockSize, 0, 2);

    // getGraphLatencySamples() is a pass-through of the graph's own aggregate. JUCE's graph already
    // delay-compensates parallel paths internally; we only surface the total for display / for the
    // host, and must not add compensation of our own.
    EXPECT_EQ(engine.getGraphLatencySamples(), engine.getGraph().getLatencySamples());

    engine.releaseFromHost();
    engine.shutdown();
}

// ============================================================================
// Runtime setTransportEnabled() — freeze/resume without a rebuild
// ============================================================================
// Exercises setTransportEnabled()/isTransportEnabled(), the runtime freeze/resume switch.

TEST(AudioEngineTransportTest, SetTransportEnabledFreezesAndResumesPosition) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(kSampleRate, kBlockSize, 0, 2);

    ASSERT_TRUE(engine.getTransport().play());
    ASSERT_TRUE(engine.isTransportEnabled()) << "default must be enabled, or every existing build's "
                                                "transport would silently stop ticking";

    processHostBlocks(engine, 2);
    const auto positionBeforeDisable = samplePositionOf(engine);
    ASSERT_GT(positionBeforeDisable, 0) << "sanity: the transport must have actually advanced first";

    engine.setTransportEnabled(false);
    EXPECT_FALSE(engine.isTransportEnabled());
    processHostBlocks(engine, 4);
    EXPECT_EQ(samplePositionOf(engine), positionBeforeDisable)
        << "disabling the runtime setting mid-session must freeze the transport in place";

    engine.setTransportEnabled(true);
    EXPECT_TRUE(engine.isTransportEnabled());
    processHostBlocks(engine, 1);
    EXPECT_EQ(samplePositionOf(engine), positionBeforeDisable + kBlockSize)
        << "re-enabling must resume ticking from exactly where it was frozen";

    engine.releaseFromHost();
    engine.shutdown();
}
