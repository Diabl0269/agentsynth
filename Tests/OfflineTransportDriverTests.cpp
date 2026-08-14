// Tests for synth::OfflineTransportDriver — the headless render harness (TL1-4). It clocks an
// AudioEngine's graph at a fixed sample rate / block size with no audio device, which is how every
// timeline engine test (TL2-TL6) renders, and is the loop the user-facing bounce/export (TL4-6)
// will be built on.
//
// Headless/deterministic constraints (see docs/testing.md and AudioEngineTransportTests.cpp): no
// real audio device, no network, no sleeps. Every engine here is HostMode::Hosted — the driver
// requires it, and a HostMode::Standalone engine must never have initialise() called on it in
// tests, since that opens real hardware.
//
// The numbers: at 48000 Hz / 120 BPM one beat is exactly 24000 samples, so a 512-sample block is
// 512/24000 beats and two beats are 48000 samples — 93.75 blocks, i.e. 94 whole blocks.

#include "../Source/AudioEngine.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "TestAudioHelpers.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 2;
constexpr double kSamplesPerBeat = 24000.0; // 120 BPM at 48 kHz
constexpr double kBeatsPerBlock = (double)kBlockSize / kSamplesPerBeat;

/** The graph's "Audio Output" IO node, the one every audible patch terminates in. */
juce::AudioProcessorGraph::Node* findAudioOutputNode(juce::AudioProcessorGraph& graph) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    for (auto* node : graph.getNodes())
        if (auto* io = dynamic_cast<IOProcessor*>(node->getProcessor()))
            if (io->getType() == IOProcessor::audioOutputNode)
                return node;
    return nullptr;
}

/** Drops a free-running oscillator into the graph and patches it straight to both audio output
 *  channels. The Oscillator is a drone in mono mode (level 1.0, A4 from its default note), so it
 *  needs no MIDI to make sound — which is what lets these tests prove audio flows with an empty
 *  MIDI buffer. Must be called before the driver is constructed, so the node is in the graph when
 *  prepareForHost prepares it. Returns false if the graph has no audio output node. */
bool wireOscillatorToAudioOutput(AudioEngine& engine) {
    auto& graph = engine.getGraph();
    auto* outputNode = findAudioOutputNode(graph);
    if (outputNode == nullptr)
        return false;

    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    if (oscNode == nullptr)
        return false;

    bool wired = graph.addConnection({{oscNode->nodeID, 0}, {outputNode->nodeID, 0}});
    wired = graph.addConnection({{oscNode->nodeID, 0}, {outputNode->nodeID, 1}}) && wired;
    return wired;
}

double endPpqOf(const AudioEngine& engine) { return engine.getTransport().getCurrentBlockInfo().endPpq; }

} // namespace

// ============================================================================
// Block accounting
// ============================================================================

TEST(OfflineTransportDriverTest, RendersExactlyNBlocks) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    synth::OfflineTransportDriver driver(engine, kSampleRate, kBlockSize, kNumChannels);
    EXPECT_EQ(driver.getSampleRate(), kSampleRate);
    EXPECT_EQ(driver.getBlockSize(), kBlockSize);
    ASSERT_TRUE(driver.getTransport().play());

    constexpr int kBlocks = 7;
    const auto rendered = driver.renderBlocks(kBlocks);

    EXPECT_EQ(rendered.getNumSamples(), kBlocks * kBlockSize);
    EXPECT_EQ(rendered.getNumChannels(), kNumChannels);

    // The transport advanced by exactly one block per rendered block. endPpq is the end of the
    // block just rendered; the cross-thread snapshot lags it by one block by design (it describes
    // the START of the block), which the extra block below pins.
    EXPECT_NEAR(endPpqOf(engine), (double)(kBlocks * kBlockSize) / kSamplesPerBeat, 1e-12);

    driver.renderBlocks(1);
    EXPECT_EQ(driver.getTransport().getPositionSnapshot().samplePosition, (std::int64_t)kBlocks * kBlockSize)
        << "the block after an N-block render must start exactly N blocks in";

    engine.releaseFromHost();
    engine.shutdown();
}

TEST(OfflineTransportDriverTest, RenderToBeatCoversTheBeat) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    synth::OfflineTransportDriver driver(engine, kSampleRate, kBlockSize, kNumChannels);
    ASSERT_TRUE(driver.getTransport().play());

    const auto rendered = driver.renderToBeat(2.0);

    // Two beats are 48000 samples; renders are whole blocks, so 93.75 blocks rounds up to 94.
    const int expectedBlocks = (int)std::ceil(2.0 * kSamplesPerBeat / (double)kBlockSize);
    EXPECT_EQ(expectedBlocks, 94);
    EXPECT_EQ(rendered.getNumSamples(), expectedBlocks * kBlockSize);
    EXPECT_EQ(rendered.getNumSamples(), 48128);
    EXPECT_EQ(rendered.getNumChannels(), kNumChannels);

    // Position is checked after each block, so the render covers the target beat and overshoots it
    // by less than one block.
    EXPECT_GE(endPpqOf(engine), 2.0);
    EXPECT_LT(endPpqOf(engine), 2.0 + kBeatsPerBlock);

    engine.releaseFromHost();
    engine.shutdown();
}

TEST(OfflineTransportDriverTest, RenderToBeatWhileStoppedReturnsEmpty) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    synth::OfflineTransportDriver driver(engine, kSampleRate, kBlockSize, kNumChannels);
    // No play(): a stopped transport never reaches any beat, so this must give up rather than spin
    // until the safety cap.
    const auto rendered = driver.renderToBeat(2.0);

    EXPECT_EQ(rendered.getNumSamples(), 0);
    EXPECT_EQ(driver.getTransport().getPositionSnapshot().ppq, 0.0);
    EXPECT_FALSE(driver.getTransport().getPositionSnapshot().playing);

    engine.releaseFromHost();
    engine.shutdown();
}

TEST(OfflineTransportDriverTest, RenderToBeatBehindPositionReturnsEmpty) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    synth::OfflineTransportDriver driver(engine, kSampleRate, kBlockSize, kNumChannels);
    ASSERT_TRUE(driver.getTransport().play());
    ASSERT_TRUE(driver.getTransport().locateBeat(5.0));

    // Commands are drained by the next tick, so one block is what makes the locate real.
    driver.renderBlocks(1);
    ASSERT_GT(driver.getTransport().getPositionSnapshot().ppq, 2.0);
    const double ppqBefore = endPpqOf(engine);

    const auto rendered = driver.renderToBeat(2.0);

    EXPECT_EQ(rendered.getNumSamples(), 0) << "a target behind the playhead cannot be rendered forwards into";
    EXPECT_EQ(endPpqOf(engine), ppqBefore) << "the bail-out must not render (or advance) anything";

    engine.releaseFromHost();
    engine.shutdown();
}

// ============================================================================
// The per-block observer seam (what bounce/export streams from)
// ============================================================================

TEST(OfflineTransportDriverTest, BlockCallbackSeesConsecutiveBlockTimeInfo) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    synth::OfflineTransportDriver driver(engine, kSampleRate, kBlockSize, kNumChannels);
    ASSERT_TRUE(driver.getTransport().play());

    constexpr int kBlocks = 5;
    std::vector<synth::BlockTimeInfo> seen;
    std::vector<int> callbackBufferSamples;
    const auto rendered =
        driver.renderBlocks(kBlocks, [&](const juce::AudioBuffer<float>& block, const synth::BlockTimeInfo& info) {
            seen.push_back(info);
            callbackBufferSamples.push_back(block.getNumSamples());
        });

    ASSERT_EQ((int)seen.size(), kBlocks) << "the observer must fire once per rendered block";
    EXPECT_EQ(rendered.getNumSamples(), kBlocks * kBlockSize);

    for (int i = 0; i < kBlocks; ++i) {
        EXPECT_EQ(seen[(size_t)i].blockStartSample, (std::int64_t)i * kBlockSize) << "block " << i << " start sample";
        EXPECT_EQ(seen[(size_t)i].numSamples, kBlockSize) << "block " << i << " length";
        EXPECT_TRUE(seen[(size_t)i].playing) << "block " << i << " play state (play() lands on the first tick)";
        EXPECT_EQ(seen[(size_t)i].sampleRate, kSampleRate);
        EXPECT_NEAR(seen[(size_t)i].startPpq, (double)(i * kBlockSize) / kSamplesPerBeat, 1e-12);
        EXPECT_EQ(callbackBufferSamples[(size_t)i], kBlockSize) << "the observer sees the block, not the accumulation";
    }

    engine.releaseFromHost();
    engine.shutdown();
}

// ============================================================================
// Audio actually flows through the graph
// ============================================================================

TEST(OfflineTransportDriverTest, RenderedAudioIsFiniteAndGraphAudioFlows) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise(); // builds the default patch

    synth::OfflineTransportDriver driver(engine, kSampleRate, kBlockSize, kNumChannels);
    ASSERT_TRUE(driver.getTransport().play());

    const auto rendered = driver.renderBlocks(10);
    ASSERT_EQ(rendered.getNumSamples(), 10 * kBlockSize);

    // The default patch may legitimately be silent with no MIDI input; what must never happen is
    // NaN/Inf or out-of-range garbage reaching the output.
    for (int ch = 0; ch < rendered.getNumChannels(); ++ch) {
        const float* data = rendered.getReadPointer(ch);
        for (int i = 0; i < rendered.getNumSamples(); ++i) {
            ASSERT_TRUE(std::isfinite(data[i])) << "non-finite sample at channel " << ch << ", sample " << i;
            ASSERT_LE(std::abs(data[i]), 8.0f) << "wildly out-of-range sample at channel " << ch << ", sample " << i;
        }
    }

    engine.releaseFromHost();
    engine.shutdown();

    // Non-silence needs a source that runs without MIDI: a free-running oscillator patched to the
    // audio output. This is the shape every TL2+ engine test uses to hear its patch.
    AudioEngine oscEngine(AudioEngine::HostMode::Hosted);
    oscEngine.initialise();
    ASSERT_TRUE(wireOscillatorToAudioOutput(oscEngine));

    synth::OfflineTransportDriver oscDriver(oscEngine, kSampleRate, kBlockSize, kNumChannels);
    ASSERT_TRUE(oscDriver.getTransport().play());

    const auto oscRendered = oscDriver.renderBlocks(10);
    EXPECT_GT(TestAudioHelpers::computeRMS(oscRendered, 0), 1e-3f)
        << "the graph's audio must reach the driver's output";
    EXPECT_FALSE(TestAudioHelpers::isSilent(oscRendered, 1));

    oscEngine.releaseFromHost();
    oscEngine.shutdown();
}

TEST(OfflineTransportDriverTest, StoppedTransportStillRendersGraphAudio) {
    // The transport is a conductor, not the engine: stopping it must not mute a free-running patch.
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    ASSERT_TRUE(wireOscillatorToAudioOutput(engine));

    synth::OfflineTransportDriver driver(engine, kSampleRate, kBlockSize, kNumChannels);
    ASSERT_FALSE(driver.getTransport().getPositionSnapshot().playing);

    const auto rendered = driver.renderBlocks(10);

    ASSERT_EQ(rendered.getNumSamples(), 10 * kBlockSize);
    EXPECT_GT(TestAudioHelpers::computeRMS(rendered, 0), 1e-3f)
        << "a stopped transport must not silence the graph — the blocks still render";
    EXPECT_FALSE(driver.getTransport().getPositionSnapshot().playing);
    EXPECT_EQ(driver.getTransport().getPositionSnapshot().samplePosition, 0)
        << "…and the position must not advance while stopped";

    engine.releaseFromHost();
    engine.shutdown();
}
