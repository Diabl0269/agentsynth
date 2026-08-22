// Audio device INPUT reaching the graph, and the device setup that survives a restart.
//
// This file introduced the repo's FakeAudioIODevice, which is the pattern for driving AudioEngine's
// juce::AudioIODeviceCallback half headlessly — see Tests/FakeAudioIODevice.h, where it now lives
// so other tests can drive the same device.
//
// Headless/deterministic house rules (docs/testing.md, and the header of AudioEngineTransportTests):
// no real audio device, no network, no sleeps. A HostMode::Standalone engine must never have
// initialise() called on it in a test, because that opens real hardware — the two tests here that
// DO exercise initialise() go through a subclass that overrides the AudioEngine::initialiseDevices
// seam, which is the only part of initialise() that touches a device.

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIProviderRegistry.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/OscillatorModule.h"
#include "FakeAudioIODevice.h"
#include "MainComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

using synth::test::FakeAudioIODevice;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate;
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;
constexpr int kFakeInputLatency = synth::test::kFakeDeviceInputLatency;

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

/** AudioEngine with the one hardware-touching step of initialise() intercepted, so a test can
 *  assert WHICH branch the saved-state decision took without opening a device or grabbing a MIDI
 *  input. This is the seam added for exactly this purpose. */
class SeamEngine : public AudioEngine {
public:
    using AudioEngine::AudioEngine;

    int initialiseDeviceCalls = 0;
    bool sawSavedState = false;

protected:
    void initialiseDevices(const juce::XmlElement* savedDeviceState) override {
        ++initialiseDeviceCalls;
        sawSavedState = savedDeviceState != nullptr;
    }
};

/** A ramp, so a passthrough assertion fails loudly on an off-by-one or a swapped channel. */
std::vector<float> makeRamp(int numSamples) {
    std::vector<float> data((std::size_t)numSamples);
    for (int i = 0; i < numSamples; ++i)
        data[(std::size_t)i] = (float)i / (float)numSamples;
    return data;
}

std::vector<float> makeSine(int numSamples) {
    std::vector<float> data((std::size_t)numSamples);
    for (int i = 0; i < numSamples; ++i)
        data[(std::size_t)i] = std::sin(juce::MathConstants<float>::twoPi * 3.0f * (float)i / (float)numSamples);
    return data;
}

/** Every output sample starts as this, so "the graph wrote here" is distinguishable from "nobody
 *  touched this". */
constexpr float kSentinel = -9999.0f;

std::vector<float> makeSentinelBuffer(int numSamples) { return std::vector<float>((std::size_t)numSamples, kSentinel); }

bool isSilent(const std::vector<float>& data) {
    for (float sample : data)
        if (std::abs(sample) > 1.0e-7f)
            return false;
    return true;
}

/** in ch0 -> out ch0 (and optionally in ch1 -> out ch0), built directly on the engine's graph.
 *  The play config must be set BEFORE the IO nodes are added: juce::AudioGraphIOProcessor snapshots
 *  the graph's channel counts the moment it gets a parent, so a node added to a 0-channel graph
 *  stays a 0-channel node however the graph is prepared afterwards. */
struct PassthroughPatch {
    juce::AudioProcessorGraph::NodeID inputNode, outputNode;
};

PassthroughPatch buildPassthrough(AudioEngine& engine, int numInputChannels, int numOutputChannels,
                                  int inputChannelToConnect) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(numInputChannels, numOutputChannels, kSampleRate, kBlockSize);

    auto in = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode));
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    graph.addConnection({{in->nodeID, inputChannelToConnect}, {out->nodeID, 0}});

    return {in->nodeID, out->nodeID};
}

/** A free-running 440 Hz oscillator into Audio Output — a patch that renders without any input or
 *  any MIDI, so "what the graph produces on its own" is well defined. */
void buildOscillatorPatch(AudioEngine& engine, int numInputChannels, int numOutputChannels) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(numInputChannels, numOutputChannels, kSampleRate, kBlockSize);

    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    graph.addConnection({{osc->nodeID, 0}, {out->nodeID, 0}});
}

class MinimalProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "AudioInputTestsMock"; }
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
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "mock-model";
    int requestTimeoutMs = 240000;
};

} // namespace

// ============================================================================
// 1. Device input reaches the graph
// ============================================================================

TEST(AudioInputTest, InputReachesTheGraph) {
    // Standalone, but never initialise()d: the device callback is called by hand below, so no real
    // device is ever opened.
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildPassthrough(engine, 2, 2, /*inputChannelToConnect*/ 0);

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    auto inLeft = makeRamp(kBlockSize);
    auto inRight = makeSine(kBlockSize);
    auto outLeft = makeSentinelBuffer(kBlockSize);
    auto outRight = makeSentinelBuffer(kBlockSize);

    const float* inputs[] = {inLeft.data(), inRight.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};

    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});

    for (int i = 0; i < kBlockSize; ++i) {
        ASSERT_NEAR(outLeft[(std::size_t)i], inLeft[(std::size_t)i], 1.0e-6f)
            << "device input channel 0 must arrive at the graph's Audio Input node and pass through "
               "to Audio Output unchanged (sample "
            << i << ")";
    }

    EXPECT_TRUE(isSilent(outRight)) << "an unconnected output channel must render silent, not carry the input "
                                       "channel that happens to alias the same buffer channel";

    engine.audioDeviceStopped();
}

// ============================================================================
// 2. Zero inputs — byte-identical to the legacy callback
// ============================================================================

TEST(AudioInputTest, InputCountZeroBehavesAsToday) {
    // The reference: the same patch clocked through processHostBlock, which shares renderNextBlock
    // with the device callback and differs ONLY in how the render buffer is built.
    AudioEngine reference(AudioEngine::HostMode::Hosted);
    buildOscillatorPatch(reference, 0, 2);
    reference.prepareForHost(kSampleRate, kBlockSize, 0, 2);

    juce::AudioBuffer<float> referenceBuffer(2, kBlockSize);
    referenceBuffer.clear();
    juce::MidiBuffer midi;
    reference.processHostBlock(referenceBuffer, midi);

    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildOscillatorPatch(engine, 0, 2);

    FakeAudioIODevice fake(0, 2);
    engine.audioDeviceAboutToStart(&fake);

    auto outLeft = makeSentinelBuffer(kBlockSize);
    auto outRight = makeSentinelBuffer(kBlockSize);
    float* outputs[] = {outLeft.data(), outRight.data()};

    // Explicitly null input array + 0 channels: a zero-input device is what every install has until
    // the user opts in, and the callback must not dereference anything to serve it.
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, kBlockSize, {});

    EXPECT_FALSE(isSilent(outLeft)) << "the patch must still render with no input device";

    for (int i = 0; i < kBlockSize; ++i) {
        ASSERT_NEAR(outLeft[(std::size_t)i], referenceBuffer.getSample(0, i), 1.0e-6f)
            << "with 0 input channels the device callback must produce the pure graph render, "
               "sample-for-sample (sample "
            << i << ")";
        ASSERT_NEAR(outRight[(std::size_t)i], referenceBuffer.getSample(1, i), 1.0e-6f);
    }

    engine.audioDeviceStopped();
    reference.releaseFromHost();
}

// ============================================================================
// 3. More inputs than outputs — the scratch path
// ============================================================================

TEST(AudioInputTest, MoreInputsThanOutputs) {
    // 2 in / 1 out: the render buffer needs 2 channels but the device only gives us 1 writable
    // output pointer, so input channel 1 has to live in the preallocated scratch. Connecting input
    // ch1 (not ch0) to the single output is what proves the scratch channel actually carried it.
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildPassthrough(engine, 2, 1, /*inputChannelToConnect*/ 1);

    FakeAudioIODevice fake(2, 1);
    engine.audioDeviceAboutToStart(&fake);

    auto inLeft = makeRamp(kBlockSize);
    auto inRight = makeSine(kBlockSize);
    auto outLeft = makeSentinelBuffer(kBlockSize);

    const float* inputs[] = {inLeft.data(), inRight.data()};
    float* outputs[] = {outLeft.data()};

    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 1, kBlockSize, {});

    for (int i = 0; i < kBlockSize; ++i) {
        ASSERT_NEAR(outLeft[(std::size_t)i], inRight[(std::size_t)i], 1.0e-6f)
            << "input channel 1 must survive the scratch round trip (sample " << i << ")";
    }

    engine.audioDeviceStopped();
}

// ============================================================================
// 4. The callback never allocates — the scratch is sized in the prepare call
// ============================================================================

TEST(AudioInputTest, CallbackNeverAllocatesBecauseScratchIsSizedInPrepare) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);

    const auto beforePrepare = engine.getDeviceScratchInfo();
    EXPECT_EQ(beforePrepare.numChannelPointers, 0);
    EXPECT_EQ(beforePrepare.numScratchChannels, 0);
    EXPECT_EQ(beforePrepare.numScratchSamples, 0);

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    const auto afterPrepare = engine.getDeviceScratchInfo();
    EXPECT_GE(afterPrepare.numChannelPointers, 2) << "the render buffer's channel-pointer array must cover "
                                                     "max(in, out) before the first block arrives";
    EXPECT_GE(afterPrepare.numScratchChannels, 2);
    EXPECT_GE(afterPrepare.numScratchSamples, kBlockSize)
        << "the scratch must already hold a whole device block — growing it from the callback would allocate";

    // A device with more inputs than outputs must be sized on max(in, out), not on the output count.
    AudioEngine lopsided(AudioEngine::HostMode::Standalone);
    FakeAudioIODevice lopsidedDevice(2, 1);
    lopsided.audioDeviceAboutToStart(&lopsidedDevice);

    const auto lopsidedScratch = lopsided.getDeviceScratchInfo();
    EXPECT_GE(lopsidedScratch.numChannelPointers, 2);
    EXPECT_GE(lopsidedScratch.numScratchChannels, 2);
    EXPECT_GE(lopsidedScratch.numScratchSamples, kBlockSize);
}

TEST(AudioInputTest, CallbackBeforePrepareStillSilencesTheOutput) {
    // juce::AudioDeviceManager always calls audioDeviceAboutToStart before a callback can receive a
    // block, so this is the belt-and-braces path: with no scratch to render through, the callback
    // must still leave the speakers silent rather than replay whatever the device left in the block.
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    buildOscillatorPatch(engine, 0, 2);

    auto outLeft = makeSentinelBuffer(kBlockSize);
    auto outRight = makeSentinelBuffer(kBlockSize);
    float* outputs[] = {outLeft.data(), outRight.data()};

    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, kBlockSize, {});

    EXPECT_TRUE(isSilent(outLeft));
    EXPECT_TRUE(isSilent(outRight));
}

// ============================================================================
// 5. Device state — the persist callback out, the saved state back in
// ============================================================================

TEST(AudioInputTest, DeviceStateChangeReachesTheOwnerCallback) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);

    int calls = 0;
    engine.onDeviceStateChanged = [&calls](std::unique_ptr<juce::XmlElement>) { ++calls; };

    // Exactly what juce::AudioDeviceManager does when the user changes anything in the Audio tab.
    // The payload is null here and that is correct: JUCE only produces a DEVICESETUP element once a
    // setup has been chosen explicitly, and no device was ever opened in this test. What the engine
    // owes its owner is the notification; MainComponentPersistsDeviceState below covers what the
    // owner does with a real payload.
    engine.changeListenerCallback(&engine.getDeviceManager());
    EXPECT_EQ(calls, 1);

    juce::ChangeBroadcaster somethingElse;
    engine.changeListenerCallback(&somethingElse);
    EXPECT_EQ(calls, 1) << "only the engine's own device manager may drive device-state persistence";

    // Hosted: the host owns the device, so there is no state of ours to persist.
    AudioEngine hosted(AudioEngine::HostMode::Hosted);
    int hostedCalls = 0;
    hosted.onDeviceStateChanged = [&hostedCalls](std::unique_ptr<juce::XmlElement>) { ++hostedCalls; };
    hosted.changeListenerCallback(&hosted.getDeviceManager());
    EXPECT_EQ(hostedCalls, 0);
}

TEST(AudioInputTest, SavedDeviceStateSelectsTheRestorePath) {
    // No saved state -> the legacy defaults path, which is the whole "migration" for existing
    // users: inputs stay off until they opt in.
    SeamEngine fresh(AudioEngine::HostMode::Standalone);
    EXPECT_FALSE(fresh.hasSavedDeviceState());
    fresh.initialise();
    EXPECT_EQ(fresh.initialiseDeviceCalls, 1);
    EXPECT_FALSE(fresh.sawSavedState);
    EXPECT_FALSE(fresh.isReceivingDeviceCallbacks()) << "the seam must leave the engine unattached — a test that "
                                                        "opens a device is a test that fights the machine's speakers";
    fresh.shutdown();

    // A saved state -> the restore path.
    SeamEngine restored(AudioEngine::HostMode::Standalone);
    restored.setSavedDeviceState(juce::parseXML(R"(<DEVICESETUP deviceType="CoreAudio"
                                                                audioOutputDeviceName="Nowhere"
                                                                audioInputDeviceName="Nowhere"
                                                                audioDeviceInChans="11"/>)"));
    EXPECT_TRUE(restored.hasSavedDeviceState());
    restored.initialise();
    EXPECT_EQ(restored.initialiseDeviceCalls, 1);
    EXPECT_TRUE(restored.sawSavedState);
    restored.shutdown();

    // Hosted engines never acquire a device at all, saved state or not.
    SeamEngine hosted(AudioEngine::HostMode::Hosted);
    hosted.setSavedDeviceState(juce::parseXML(R"(<DEVICESETUP deviceType="CoreAudio"/>)"));
    hosted.initialise();
    EXPECT_EQ(hosted.initialiseDeviceCalls, 0);
    hosted.shutdown();
}

// ============================================================================
// 6. Input latency accessor
// ============================================================================

TEST(AudioInputTest, InputLatencyAccessor) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    EXPECT_EQ(engine.getInputLatencySamples(), 0) << "no device started yet — there is nothing to report";

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    EXPECT_EQ(engine.getInputLatencySamples(), kFakeInputLatency);

    engine.audioDeviceStopped();
    EXPECT_EQ(engine.getInputLatencySamples(), 0) << "a stopped device reports no latency";

    AudioEngine hosted(AudioEngine::HostMode::Hosted);
    hosted.audioDeviceAboutToStart(&fake);
    EXPECT_EQ(hosted.getInputLatencySamples(), 0) << "the host owns the device in Hosted mode";
    EXPECT_EQ(hosted.getOutputLatencySamples(), 0);
}

// ============================================================================
// 7. MainComponent's half: the ApplicationProperties round trip
// ============================================================================

class MainComponentDeviceStateTest : public ::testing::Test {
protected:
    // MainComponent reads and writes the app's REAL settings file, so this harness snapshots the
    // one key it touches and puts it back — a developer's actual device choice must survive a test
    // run, and the tests must not inherit it either.
    // juce::ApplicationProperties is non-copyable, so each accessor opens its own against the same
    // on-disk location MainComponent uses.
    template <typename Fn>
    static void withSettings(Fn&& fn) {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* settings = props.getUserSettings())
            fn(*settings);
    }

    static void setStoredState(const juce::String& xml) {
        withSettings([&xml](juce::PropertiesFile& settings) {
            if (xml.isEmpty())
                settings.removeValue(kKey);
            else
                settings.setValue(kKey, xml);
            settings.saveIfNeeded();
        });
    }

    static juce::String getStoredState() {
        juce::String value;
        withSettings([&value](juce::PropertiesFile& settings) { value = settings.getValue(kKey, juce::String()); });
        return value;
    }

    void SetUp() override {
        original = getStoredState();
        setStoredState({});
    }

    void TearDown() override { setStoredState(original); }

    static constexpr const char* kKey = "audioDeviceState";
    juce::String original;
};

TEST_F(MainComponentDeviceStateTest, MainComponentPersistsDeviceState) {
    MainComponent component(std::make_unique<MinimalProvider>(), synth::AIProviderRegistry::createDefault());
    auto& engine = component.getAudioEngine();

    ASSERT_TRUE(engine.onDeviceStateChanged) << "the owner must have installed the persist callback";

    // Null payload (nothing explicit chosen yet) must persist nothing — that absence is what keeps
    // the next launch on the inputs-off defaults.
    engine.onDeviceStateChanged(nullptr);
    EXPECT_TRUE(getStoredState().isEmpty());

    auto state = juce::parseXML(R"(<DEVICESETUP deviceType="CoreAudio" audioDeviceInChans="11"/>)");
    ASSERT_NE(state, nullptr);
    engine.onDeviceStateChanged(std::move(state));

    const juce::String stored = getStoredState();
    EXPECT_TRUE(stored.contains("DEVICESETUP"));
    EXPECT_TRUE(stored.contains("audioDeviceInChans")) << "the input channel mask is the part that has to survive — "
                                                          "it is the whole record of the user's opt-in";
}

TEST_F(MainComponentDeviceStateTest, MainComponentRestoresStoredDeviceState) {
    // A device that cannot resolve, on purpose: MainComponent's job here is to parse the stored
    // string and hand it to the engine, and juce::AudioDeviceManager's documented fallback for an
    // unopenable saved device is the default device with the input count the engine asked for —
    // which is pinned at 0, so this test can never turn a real microphone on.
    setStoredState(R"(<DEVICESETUP deviceType="NoSuchType" audioOutputDeviceName="AgentSynth Test No Such Device" )"
                   R"(audioInputDeviceName=""/>)");

    MainComponent component(std::make_unique<MinimalProvider>(), synth::AIProviderRegistry::createDefault());
    EXPECT_TRUE(component.getAudioEngine().hasSavedDeviceState())
        << "a stored device state must be parsed and handed to the engine BEFORE initialise()";
}

TEST_F(MainComponentDeviceStateTest, NoStoredStateMeansNoSavedState) {
    MainComponent component(std::make_unique<MinimalProvider>(), synth::AIProviderRegistry::createDefault());
    EXPECT_FALSE(component.getAudioEngine().hasSavedDeviceState())
        << "an install that has never chosen a device must take the legacy defaults path";
}
