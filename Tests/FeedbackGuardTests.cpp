// The input monitoring gate + feedback guard.
//
// Three layers:
//   * MODULE/ENGINE layer — an AudioEngine driving a passthrough patch (AudioInputModule -> Audio
//     Output) through FakeAudioIODevice, exactly like Tests/AudioInputTests.cpp /
//     Tests/AudioInputModuleTests.cpp: audioDeviceAboutToStart once, then
//     audioDeviceIOCallbackWithContext called BY HAND, block by block. This needs the transport the
//     ENGINE installs as playhead (SYNTH_ENABLE_TIMELINE only — AudioInputModuleTest's "Engine
//     layer" note applies here too), so those tests are gated.
//   * GUARD-ONLY layer — the guard itself does not care about AudioInputModule at all; it reads
//     whatever is in the render buffer post-graph. LoudSynthWithoutMonitoringNeverTrips uses a raw
//     juce::AudioGraphIOProcessor passthrough (nothing to do with the module's own gate) to prove
//     the guard never evaluates while monitoring is disabled, and needs no flag at all.
//   * MAINCOMPONENT layer — the poll wiring: armed-Audio-track derivation, the trip-latches-until-
//     re-arm rule, and the status-bar message. Gated (SYNTH_ENABLE_TIMELINE only — there is no
//     TimelineDoc-driven poll without it).
//
// Headless house rules as everywhere else: no real audio device, no sleeps.

#include "../Source/AudioEngine.h"
#include "../Source/Modules/AudioInputModule.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "FakeAudioIODevice.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

#if SYNTH_ENABLE_TIMELINE
#include "../Source/AI/AIProvider.h"
#include "MainComponent.h"
#endif

namespace {

using synth::test::FakeAudioIODevice;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate;
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr float kSentinel = -9999.0f;

std::vector<float> makeSine(int numSamples, float amplitude) {
    std::vector<float> data((std::size_t)numSamples);
    for (int i = 0; i < numSamples; ++i)
        data[(std::size_t)i] =
            amplitude * std::sin(juce::MathConstants<float>::twoPi * 3.0f * (float)i / (float)numSamples);
    return data;
}

bool isSilent(const std::vector<float>& data) {
    for (float sample : data)
        if (std::abs(sample) > 1.0e-7f)
            return false;
    return true;
}

/** Builds an AudioInputModule -> Audio Output passthrough, channel-for-channel, on `engine`'s own
 *  graph — the harness AudioInputModuleTests.cpp's engine-layer tests use. play config must be set
 *  BEFORE the IO node is added (it snapshots the graph's channel count the moment it gets a parent). */
void buildAudioInputPassthrough(AudioEngine& engine, int numChannels) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(numChannels, numChannels, kSampleRate, kBlockSize);
    auto in = graph.addNode(std::make_unique<AudioInputModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    for (int channel = 0; channel < numChannels; ++channel)
        graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});
}

/** A raw IO-node passthrough — nothing to do with AudioInputModule's own gate. Stands in for "any
 *  loud graph output" (a synth patch, or a legacy raw input node) that the feedback guard must
 *  still watch regardless of what's wired upstream. */
void buildRawPassthrough(AudioEngine& engine, int numChannels) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(numChannels, numChannels, kSampleRate, kBlockSize);
    auto in = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode));
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    for (int channel = 0; channel < numChannels; ++channel)
        graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});
}

} // namespace

// ============================================================================
// Engine layer (needs the transport as playhead — see the file header)
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

TEST(FeedbackGuardTest, MonitoringGateSilencesWhenDisabled) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildAudioInputPassthrough(engine, 2);

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    auto loud = makeSine(kBlockSize, 0.8f);
    std::vector<float> outLeft((std::size_t)kBlockSize, kSentinel), outRight((std::size_t)kBlockSize, kSentinel);
    const float* inputs[] = {loud.data(), loud.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    ASSERT_FALSE(engine.isInputMonitoringEnabled()) << "monitoring defaults to disabled";
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
    EXPECT_TRUE(isSilent(outLeft)) << "disabled: the module's own graph output must be silent";
    EXPECT_TRUE(isSilent(outRight));

    engine.setInputMonitoringEnabled(true);
    std::fill(outLeft.begin(), outLeft.end(), kSentinel);
    std::fill(outRight.begin(), outRight.end(), kSentinel);
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
    for (int i = 0; i < kBlockSize; ++i) {
        ASSERT_NEAR(outLeft[(std::size_t)i], loud[(std::size_t)i], 1.0e-6f) << "enabled: passthrough, sample " << i;
        ASSERT_NEAR(outRight[(std::size_t)i], loud[(std::size_t)i], 1.0e-6f);
    }

    engine.audioDeviceStopped();
}

TEST(FeedbackGuardTest, GuardTripsOnSustainedNearClip) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildAudioInputPassthrough(engine, 2);

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    engine.setInputMonitoringEnabled(true);

    auto loud = makeSine(kBlockSize, 0.99f);
    std::vector<float> outLeft((std::size_t)kBlockSize), outRight((std::size_t)kBlockSize);
    const float* inputs[] = {loud.data(), loud.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    // kFeedbackSustainSeconds (0.25 s) at 48 kHz is 12000 samples = ceil(12000 / 512) = 24 blocks of
    // sustained near-clip content before the guard may trip.
    constexpr int kBlocksToTrip = 24;
    for (int i = 0; i < kBlocksToTrip - 1; ++i) {
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
        ASSERT_TRUE(engine.isInputMonitoringEnabled())
            << "must not trip before the sustain threshold (block " << i << ")";
    }

    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
    EXPECT_FALSE(engine.isInputMonitoringEnabled()) << "the guard must have tripped by block " << kBlocksToTrip;
    EXPECT_TRUE(isSilent(outLeft));
    EXPECT_TRUE(isSilent(outRight));

    EXPECT_TRUE(engine.consumeFeedbackGuardTripped()) << "the trip must be reported exactly once";
    EXPECT_FALSE(engine.consumeFeedbackGuardTripped()) << "a second consume without a second trip reads false";

    engine.audioDeviceStopped();
}

TEST(FeedbackGuardTest, BriefTransientDoesNotTrip) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildAudioInputPassthrough(engine, 2);

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    engine.setInputMonitoringEnabled(true);

    auto loud = makeSine(kBlockSize, 0.99f);
    std::vector<float> quiet((std::size_t)kBlockSize, 0.0f);
    std::vector<float> outLeft((std::size_t)kBlockSize), outRight((std::size_t)kBlockSize);
    float* outputs[] = {outLeft.data(), outRight.data()};

    const float* loudInputs[] = {loud.data(), loud.data()};
    for (int i = 0; i < 3; ++i)
        engine.audioDeviceIOCallbackWithContext(loudInputs, 2, outputs, 2, kBlockSize, {});
    ASSERT_TRUE(engine.isInputMonitoringEnabled()) << "3 blocks (~32 ms) is far short of the 0.25 s sustain";

    const float* quietInputs[] = {quiet.data(), quiet.data()};
    engine.audioDeviceIOCallbackWithContext(quietInputs, 2, outputs, 2, kBlockSize, {});
    EXPECT_TRUE(engine.isInputMonitoringEnabled()) << "the transient must not have tripped the guard";
    EXPECT_FALSE(engine.consumeFeedbackGuardTripped());

    engine.audioDeviceStopped();
}

TEST(FeedbackGuardTest, ImmediateBlockZeroing) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildAudioInputPassthrough(engine, 2);

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    engine.setInputMonitoringEnabled(true);

    auto loud = makeSine(kBlockSize, 0.99f);
    std::vector<float> outLeft((std::size_t)kBlockSize), outRight((std::size_t)kBlockSize);
    const float* inputs[] = {loud.data(), loud.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    bool tripped = false;
    for (int i = 0; i < 40 && !tripped; ++i) {
        std::fill(outLeft.begin(), outLeft.end(), kSentinel);
        std::fill(outRight.begin(), outRight.end(), kSentinel);
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
        tripped = !engine.isInputMonitoringEnabled();
    }
    ASSERT_TRUE(tripped) << "the guard must trip within 40 blocks of 0.99-amplitude input";

    // Pin the implementation: the guard measures peak over the WHOLE block (it cannot know an
    // in-block trip sample), so it zeroes the WHOLE tripping block, not just from some offset.
    EXPECT_TRUE(isSilent(outLeft)) << "the tripping block's output must be silenced immediately, same block";
    EXPECT_TRUE(isSilent(outRight));

    engine.audioDeviceStopped();
}

TEST(FeedbackGuardTest, FalsePositiveCorpus) {
    // Margins under test: kFeedbackPeakThreshold = 0.97f, kFeedbackSustainSeconds = 0.25 s. Both
    // signals below sit under the PEAK margin (0.9 and 0.95 are both < 0.97), which alone is enough
    // to keep the guard from ever accumulating a qualifying run — continuous or not.
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildAudioInputPassthrough(engine, 2);

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    engine.setInputMonitoringEnabled(true);

    std::vector<float> outLeft((std::size_t)kBlockSize), outRight((std::size_t)kBlockSize);
    float* outputs[] = {outLeft.data(), outRight.data()};

    // (a) A continuous 0.9-amplitude sine, well past the 0.25 s sustain window.
    auto sine09 = makeSine(kBlockSize, 0.9f);
    const float* sineInputs[] = {sine09.data(), sine09.data()};
    for (int i = 0; i < 40; ++i)
        engine.audioDeviceIOCallbackWithContext(sineInputs, 2, outputs, 2, kBlockSize, {});
    EXPECT_TRUE(engine.isInputMonitoringEnabled()) << "0.9 peak stays under the 0.97 threshold — must never trip";
    EXPECT_FALSE(engine.consumeFeedbackGuardTripped());

    // (b) A 0.95-amplitude square wave (also under threshold), with periodic silence gaps so a
    // signal that "feels" loud and bursty stays clear on both margins at once — under the peak
    // threshold AND never sustained long enough even if it weren't.
    std::vector<float> square095((std::size_t)kBlockSize);
    for (int i = 0; i < kBlockSize; ++i)
        square095[(std::size_t)i] = (i % 32 < 16) ? 0.95f : -0.95f;
    std::vector<float> silence((std::size_t)kBlockSize, 0.0f);
    const float* squareInputs[] = {square095.data(), square095.data()};
    const float* silenceInputs[] = {silence.data(), silence.data()};
    for (int cycle = 0; cycle < 6; ++cycle) {
        for (int i = 0; i < 5; ++i)
            engine.audioDeviceIOCallbackWithContext(squareInputs, 2, outputs, 2, kBlockSize, {});
        engine.audioDeviceIOCallbackWithContext(silenceInputs, 2, outputs, 2, kBlockSize, {});
    }
    EXPECT_TRUE(engine.isInputMonitoringEnabled()) << "0.95 peak stays under threshold too, gaps or not";
    EXPECT_FALSE(engine.consumeFeedbackGuardTripped());

    engine.audioDeviceStopped();
}

#endif // SYNTH_ENABLE_TIMELINE

// ============================================================================
// Guard-only layer — no AudioInputModule, no transport dependency
// ============================================================================

TEST(FeedbackGuardTest, LoudSynthWithoutMonitoringNeverTrips) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildRawPassthrough(engine, 2); // any loud graph output — unrelated to AudioInputModule's gate

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    ASSERT_FALSE(engine.isInputMonitoringEnabled()) << "monitoring defaults to disabled";

    auto loud = makeSine(kBlockSize, 0.99f);
    std::vector<float> outLeft((std::size_t)kBlockSize), outRight((std::size_t)kBlockSize);
    const float* inputs[] = {loud.data(), loud.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    // Comfortably past the 0.25 s sustain threshold, with genuinely near-clip output every block.
    for (int i = 0; i < 40; ++i)
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});

    EXPECT_FALSE(engine.consumeFeedbackGuardTripped()) << "the guard must never evaluate while monitoring is disabled";
    for (int i = 0; i < kBlockSize; ++i)
        ASSERT_NEAR(outLeft[(std::size_t)i], loud[(std::size_t)i], 1.0e-6f)
            << "output must be untouched by the guard, sample " << i;

    engine.audioDeviceStopped();
}

// ============================================================================
// MainComponent layer — the poll wiring
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

namespace {

// Same minimal pattern as AudioInputTests.cpp's MinimalProvider / RecordTapTests.cpp's MockProviderRT.
class MinimalProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "FeedbackGuardTestsMock"; }
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

juce::AudioProcessorGraph::Node* findNodeNamed(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            return node;
    return nullptr;
}

} // namespace

class MainComponentFeedbackGuardTest : public ::testing::Test {
protected:
    // The delegating MainComponent ctor reads/writes the shared on-disk "Agent Synth" settings —
    // same hygiene as RecordTapTests.cpp's RecordFlowTest.
    void resetKeys() {
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
            s->saveIfNeeded();
        }
    }

    void SetUp() override { resetKeys(); }
    void TearDown() override { resetKeys(); }

    // Nothing here wants a real device clocking the graph — the fake device is driven by hand.
    static void quiesceEngine(MainComponent& mc) { mc.getAudioEngine().suspendDeviceCallback(); }

    // Replaces whatever default patch initialise() built with a clean, predictable AudioInputModule
    // -> Audio Output passthrough, then re-prepares the engine against a FakeAudioIODevice — the
    // same passthrough harness the engine-layer tests above use, just reached through MainComponent.
    static void buildPassthroughPatch(MainComponent& mc) {
        auto& engine = mc.getAudioEngine();
        auto& graph = engine.getGraph();
        graph.clear();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
        auto in = graph.addNode(std::make_unique<AudioInputModule>());
        auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
        graph.addConnection({{in->nodeID, 0}, {out->nodeID, 0}});
        graph.addConnection({{in->nodeID, 1}, {out->nodeID, 1}});
    }
};

TEST_F(MainComponentFeedbackGuardTest, ArmEnablesDisarmDisables) {
    MainComponent mc(std::make_unique<MinimalProvider>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& doc = mc.getTimelineDoc();
    auto& engine = mc.getAudioEngine();

    ASSERT_FALSE(engine.isInputMonitoringEnabled()) << "nothing armed yet";

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(doc.setTrackArmed(track, true));
    mc.timerCallback();
    EXPECT_TRUE(engine.isInputMonitoringEnabled()) << "an armed Audio track must enable monitoring";

    ASSERT_TRUE(doc.setTrackArmed(track, false));
    mc.timerCallback();
    EXPECT_FALSE(engine.isInputMonitoringEnabled()) << "disarming must disable monitoring again";
}

TEST_F(MainComponentFeedbackGuardTest, StaysOffAfterTripWhileArmedThenDisarmRearmReenables) {
    MainComponent mc(std::make_unique<MinimalProvider>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    buildPassthroughPatch(mc);

    auto& doc = mc.getTimelineDoc();
    auto& engine = mc.getAudioEngine();

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(doc.setTrackArmed(track, true));
    mc.timerCallback();
    ASSERT_TRUE(engine.isInputMonitoringEnabled());

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    auto loud = makeSine(kBlockSize, 0.99f);
    std::vector<float> outLeft((std::size_t)kBlockSize), outRight((std::size_t)kBlockSize);
    const float* inputs[] = {loud.data(), loud.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    // Drive real blocks through the full engine pipeline until the guard trips for real.
    for (int i = 0; i < 40 && engine.isInputMonitoringEnabled(); ++i)
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
    ASSERT_FALSE(engine.isInputMonitoringEnabled()) << "the guard must have tripped within 40 blocks";

    // The poll consumes the trip: shows the message, and must NOT re-enable monitoring even though
    // the Audio track is still armed.
    mc.timerCallback();
    EXPECT_FALSE(engine.isInputMonitoringEnabled());
    EXPECT_EQ(mc.getStatusBar().getTransientMessageForTest(),
              juce::String("Input muted — sustained clipping (feedback protection)"));

    // Still armed, still latched, across a further poll.
    mc.timerCallback();
    EXPECT_FALSE(engine.isInputMonitoringEnabled()) << "staying armed must not re-enable monitoring after a trip";

    // Disarm then re-arm — the explicit reset gesture.
    ASSERT_TRUE(doc.setTrackArmed(track, false));
    mc.timerCallback();
    EXPECT_FALSE(engine.isInputMonitoringEnabled());
    ASSERT_TRUE(doc.setTrackArmed(track, true));
    mc.timerCallback();
    EXPECT_TRUE(engine.isInputMonitoringEnabled()) << "disarm + re-arm must clear the latch";

    engine.audioDeviceStopped();
}

#endif // SYNTH_ENABLE_TIMELINE
