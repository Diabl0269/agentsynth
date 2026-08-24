// DeviceChangeTests.cpp
//
// Audio device / sample-rate change while a project is open. AudioEngine::
// handleStreamFormatChange is the one consolidated prepare-path hook (called from BOTH
// audioDeviceAboutToStart and prepareForHost) that:
//   1. re-prepares the transport (musical position preserved, verified here)
//   2. resets the metronome's voice pool (a ringing click was computed for the OLD rate)
//   3. invalidates the audio-clip streamer's rings (self-healing on a miss is not enough — a
//      coincidental hit on stale content would be WRONG audio, not silence)
//   4. flags any in-flight take (audio or MIDI) so the next 10 Hz poll finalizes it — a take must
//      never span a format change. The poll is what COMMITS; the audio side stops at the boundary
//      itself (RecordTapModule::prepareToPlay), so no new-rate block reaches the old-rate WAV.
//
// Groups, each gated the same way its engine/timeline dependencies require (mirrors
// MetronomeTests.cpp / LatencyAlignmentTests.cpp's own mixed gating within one file):
//   1. Musical position across a rate change — bare AudioEngine + TransportService, always compiled.
//   2. The streamer's explicit invalidate — needs TimelineDoc/Track Audio, gated.
//   3/4. In-flight audio/MIDI takes finalized at the change — needs MainComponent's record flow, gated.
//   5. Duplex refusal's reachable half — bare AudioEngine + AudioInputModule, always compiled.
//   6. The metronome's voice pool reset — needs the engine's renderPass (metronome click is
//      gated), gated.
//   7. The hook's step order — bare AudioEngine via the onFormatChangeStepForTest() seam, always
//      compiled.
//
// House rules as everywhere else: no real audio device (FakeAudioIODevice, driven by hand), no
// sleeps, no network.

#include "../Source/AudioEngine.h"
#include "../Source/Modules/AudioInputModule.h"
#include "../Source/Modules/RecordTapModule.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "FakeAudioIODevice.h"
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/Modules/TimelineAudioSourceModule.h"
#include "../Source/Timeline/AudioClipStreamer.h"
#include "../Source/Timeline/TakePlacement.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "MainComponent.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

using synth::test::FakeAudioIODevice;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate; // 48000
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;      // 512
constexpr int kInLatency = synth::test::kFakeDeviceInputLatency;   // 64
constexpr int kOutLatency = synth::test::kFakeDeviceOutputLatency; // 128
constexpr int kRoundTrip = kInLatency + kOutLatency;               // 192
constexpr double kNewSampleRate = 96000.0;

FakeAudioIODevice makeFakeAt(double sampleRate, int numIn = 2, int numOut = 2) {
    return FakeAudioIODevice(numIn, numOut, kInLatency, kOutLatency, sampleRate, kBlockSize);
}

} // namespace

// ============================================================================
// 1. Musical position survives a rate change (bare engine, no timeline needed)
// ============================================================================

TEST(DeviceChangeTest, MusicalPositionSurvivesRateChange_Playing) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto fake48 = makeFakeAt(kSampleRate);
    engine.audioDeviceAboutToStart(&fake48);

    auto& transport = engine.getTransport();
    ASSERT_TRUE(transport.locateBeat(4.0));
    ASSERT_TRUE(transport.play());
    transport.tick(0); // drains both queued commands without advancing the position at all

    const auto before = transport.getPositionSnapshot();
    ASSERT_DOUBLE_EQ(before.ppq, 4.0);
    ASSERT_TRUE(before.playing);
    ASSERT_DOUBLE_EQ(before.sampleRate, kSampleRate);
    const auto sampleAt48k = before.samplePosition;

    auto fake96 = makeFakeAt(kNewSampleRate);
    engine.audioDeviceAboutToStart(&fake96);

    const auto after = transport.getPositionSnapshot();
    EXPECT_DOUBLE_EQ(after.ppq, 4.0) << "the musical position must be preserved across a rate change";
    EXPECT_TRUE(after.playing);
    EXPECT_DOUBLE_EQ(after.sampleRate, kNewSampleRate);
    EXPECT_EQ(after.samplePosition, sampleAt48k * 2)
        << "the sample-domain mirror doubles with the rate; the beat does not move";

    engine.audioDeviceStopped();
}

TEST(DeviceChangeTest, MusicalPositionSurvivesRateChange_Stopped) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto fake48 = makeFakeAt(kSampleRate);
    engine.audioDeviceAboutToStart(&fake48);

    auto& transport = engine.getTransport();
    ASSERT_TRUE(transport.locateBeat(4.0));
    transport.tick(0); // drains the locate; never played

    const auto before = transport.getPositionSnapshot();
    ASSERT_DOUBLE_EQ(before.ppq, 4.0);
    ASSERT_FALSE(before.playing);
    const auto sampleAt48k = before.samplePosition;

    auto fake96 = makeFakeAt(kNewSampleRate);
    engine.audioDeviceAboutToStart(&fake96);

    const auto after = transport.getPositionSnapshot();
    EXPECT_DOUBLE_EQ(after.ppq, 4.0);
    EXPECT_FALSE(after.playing);
    EXPECT_DOUBLE_EQ(after.sampleRate, kNewSampleRate);
    EXPECT_EQ(after.samplePosition, sampleAt48k * 2);

    engine.audioDeviceStopped();
}

// ============================================================================
// 2. The streamer's explicit invalidate
// ============================================================================

namespace {

constexpr const char* kTrackAudioUuid = "d9170000-0000-0000-0000-000000000001";
constexpr const char* kOutputUuid = "d9170000-0000-0000-0000-000000000002";
constexpr const char* kAssetRef = "Audio/clip.wav";
constexpr int kSourcePeriod = 65536; // n/65536 is exactly representable as a float — bit-exact round trip

float sourceSample(juce::int64 frame, int channel) {
    const float value = (float)(frame % kSourcePeriod) / (float)kSourcePeriod;
    return channel == 0 ? value : -value;
}

struct ScopedTempDir {
    explicit ScopedTempDir(const juce::String& name)
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        dir.deleteRecursively();
        dir.createDirectory();
    }
    ~ScopedTempDir() { dir.deleteRecursively(); }
    juce::File dir;
};

bool writeSourceWav(const juce::File& file, juce::int64 numFrames) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), kSampleRate, 2u, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();

    constexpr int kChunk = 8192;
    juce::AudioBuffer<float> chunk(2, kChunk);
    juce::int64 written = 0;
    while (written < numFrames) {
        const int n = (int)std::min<juce::int64>(kChunk, numFrames - written);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < n; ++i)
                chunk.getWritePointer(channel)[i] = sourceSample(written + i, channel);
        if (!writer->writeFromAudioSampleBuffer(chunk, 0, n))
            return false;
        written += n;
    }
    writer.reset();
    return true;
}

juce::String buildTrackAudioPatchJson() {
    return juce::String(R"({
        "nodes": [
            {"id": 1, "type": "Track Audio",  "uuid": ")") +
           kTrackAudioUuid + R"("},
            {"id": 2, "type": "Audio Output", "uuid": ")" +
           kOutputUuid + R"("}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
            {"src": 1, "srcPort": 1, "dst": 2, "dstPort": 1}
        ]
    })";
}

/** Hosted engine, Track Audio -> Audio Output, one long clip covering the whole render. The
 *  prefetch thread is paused throughout — pumpForTest() is the only thing that ever fills a ring,
 *  exactly like AudioClipPlaybackTests.cpp's own Fixture. */
struct StreamerFixture {
    ScopedTempDir bundle{"agentsynth_devicechange_streamer"};
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    synth::TimelineDoc doc;
    synth::TrackId trackId;
    synth::ClipId clipId;

    bool build(juce::int64 sourceFrames) {
        engine.getAudioClipStreamer().setPrefetchPausedForTest(true);
        if (!writeSourceWav(bundle.dir.getChildFile(kAssetRef), sourceFrames))
            return false;

        engine.initialise();
        const juce::var patch = juce::JSON::parse(buildTrackAudioPatchJson());
        if (!patch.isObject())
            return false;
        if (!synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true,
                                                    /*trusted=*/true))
            return false;

        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, 2);
        engine.getAudioClipStreamer().setAssetRoots(bundle.dir, juce::File());

        trackId = doc.addTrack(synth::TrackKind::Audio, "Audio 1");
        if (!doc.setTrackBinding(trackId, kTrackAudioUuid))
            return false;
        clipId = doc.addClip(trackId, 0.0, 1000.0, "Clip"); // long enough to outlast the whole test
        if (!clipId.isValid())
            return false;
        return doc.setClipAsset(clipId, kAssetRef, 0.0);
    }

    void publishAndPump() {
        engine.publishTimeline(doc);
        engine.getAudioClipStreamer().pumpForTest();
    }

    ~StreamerFixture() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

bool isExactlySilent(const juce::AudioBuffer<float>& buffer) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        const float* data = buffer.getReadPointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (data[i] != 0.0f)
                return false;
    }
    return true;
}

} // namespace

TEST(DeviceChangeTest, StreamerRefillsAtNewMappingWithoutGarbage) {
    StreamerFixture f;
    // 60 seconds of source at 48 kHz: comfortably longer than anything this test reads from it, on
    // either side of the rate change.
    ASSERT_TRUE(f.build(60 * (juce::int64)kSampleRate));
    f.publishAndPump();
    ASSERT_TRUE(f.driver->getTransport().play());

    // Sanity: ordinary playback at 48 kHz is correct before the change happens at all.
    const auto before = f.driver->renderBlocks(4, [&](const juce::AudioBuffer<float>&, const synth::BlockTimeInfo&) {
        f.engine.getAudioClipStreamer().pumpForTest();
    });
    for (int i = 0; i < before.getNumSamples(); ++i)
        ASSERT_FLOAT_EQ(before.getSample(0, i), sourceSample(i, 0)) << "sample " << i;

    // THE CHANGE. Hosted mode's counterpart to audioDeviceAboutToStart — bypass the driver (whose
    // own sampleRate member would go stale) and call the SAME production hook directly; the block
    // size is unchanged so the driver's scratch sizing stays valid for the renders below.
    f.engine.prepareForHost(kNewSampleRate, kBlockSize, 0, 2);

    // Render ONE block WITHOUT pumping: invalidateAllStreams() must force a miss (silence), not a
    // coincidental hit on content filled under the OLD mapping.
    juce::AudioBuffer<float> noPump(2, kBlockSize);
    noPump.clear();
    juce::MidiBuffer midiNoPump;
    f.engine.processHostBlock(noPump, midiNoPump);
    EXPECT_TRUE(isExactlySilent(noPump)) << "a format change must force silence until the streamer refills, "
                                            "never a coincidental hit on stale-mapping content";

    // Now let the (paused, synchronously-driven) prefetch thread catch up.
    const int slices = f.engine.getAudioClipStreamer().pumpForTest();
    EXPECT_GT(slices, 0) << "the invalidate must have triggered real collapse+refill work, not a no-op";

    // The first block AFTER the refill must match the file at the NEW mapping, reconstructed
    // independently from this block's own BlockTimeInfo (bpm/sampleRate), exactly the formula
    // TimelineAudioSourceModule::renderClip uses.
    juce::AudioBuffer<float> afterPump(2, kBlockSize);
    afterPump.clear();
    juce::MidiBuffer midiAfterPump;
    f.engine.processHostBlock(afterPump, midiAfterPump);
    const auto& info = f.engine.getTransport().getCurrentBlockInfo();
    ASSERT_GT(info.bpm, 0.0);
    ASSERT_GT(info.sampleRate, 0.0);
    const double secondsPerBeat = 60.0 / info.bpm;
    const juce::int64 expectedStartFrame = (juce::int64)std::llround(info.startPpq * secondsPerBeat * info.sampleRate);

    int firstBad = -1;
    int bad = 0;
    for (int i = 0; i < afterPump.getNumSamples(); ++i) {
        if (std::abs(afterPump.getSample(0, i) - sourceSample(expectedStartFrame + i, 0)) > 0.0f) {
            if (bad == 0)
                firstBad = i;
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0) << "first mismatch at sample " << firstBad
                      << " — the refilled ring must serve the NEW rate's mapping exactly";
}

// ============================================================================
// 3/4. In-flight takes finalized at the change (MainComponent's record flow)
// ============================================================================

namespace {

class MinimalProviderDC : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "DeviceChangeMock"; }
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

/** Audio Input -> Audio Output. Record-on splices the master tap in front of the output, exactly as
 *  it would in a real session — same shape as LatencyAlignmentTests.cpp's own buildInputPatch. */
void buildInputPatch(MainComponent& mc) {
    auto& graph = mc.getAudioEngine().getGraph();
    // UI first, modules second — the ordering contract every product path honours (see
    // PluginProcessor::setStateInformation): a ModuleComponent detached AFTER its module died
    // dereferences a destroyed processor (glibc hangs on the dead keyboard-state mutex).
    mc.getGraphEditor().detachAllModuleComponents();
    graph.clear();
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    auto in = graph.addNode(std::make_unique<AudioInputModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    for (int channel = 0; channel < 2; ++channel)
        graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});
}

/** One silent device callback of exactly one block — advances the transport (and, if a tap is
 *  armed, its capture) by kBlockSize samples. No impulse is needed for these tests. */
void driveSilentBlock(AudioEngine& engine) {
    std::vector<float> left((std::size_t)kBlockSize, 0.0f), right((std::size_t)kBlockSize, 0.0f);
    std::vector<float> outLeft((std::size_t)kBlockSize, 0.0f), outRight((std::size_t)kBlockSize, 0.0f);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
}

/** driveSilentBlock's counterpart for the take-content tests: both input channels carry `value` for
 *  the whole block, so a committed take's samples say which rate era they were captured in. */
void driveBlockWithInput(AudioEngine& engine, float value) {
    std::vector<float> left((std::size_t)kBlockSize, value), right((std::size_t)kBlockSize, value);
    std::vector<float> outLeft((std::size_t)kBlockSize, 0.0f), outRight((std::size_t)kBlockSize, 0.0f);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
}

RecordTapModule* findRecordTap(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* tap = dynamic_cast<RecordTapModule*>(node->getProcessor()))
                return tap;
    return nullptr;
}

struct WavInfo {
    bool ok = false;
    juce::int64 lengthInSamples = 0;
    double sampleRate = 0.0;
    juce::AudioBuffer<float> audio;
};

WavInfo readWavInfo(const juce::File& file) {
    WavInfo out;
    auto input = file.createInputStream();
    if (input == nullptr)
        return out;
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(input.release(), /*deleteStreamIfOpeningFails=*/true));
    if (reader == nullptr)
        return out;
    out.ok = true;
    out.lengthInSamples = reader->lengthInSamples;
    out.sampleRate = reader->sampleRate;
    out.audio.setSize((int)reader->numChannels, (int)std::max<juce::int64>(reader->lengthInSamples, 1));
    out.audio.clear();
    if (reader->lengthInSamples > 0)
        reader->read(&out.audio, 0, (int)reader->lengthInSamples, 0, true, true);
    return out;
}

} // namespace

class DeviceChangeFlowTest : public ::testing::Test {
protected:
    // The delegating MainComponent ctor reads/writes the shared on-disk "Agent Synth" settings —
    // same hygiene as LatencyAlignmentTests.cpp's LatencyFlowTest / MetronomeTests.cpp's
    // MetronomeCountInTest. No count-in, so a take's punch is exactly its record-on beat.
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
};

TEST_F(DeviceChangeFlowTest, AudioTakeCommittedAtFormatChange) {
    MainComponent mc(std::make_unique<MinimalProviderDC>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    buildInputPatch(mc);

    auto& engine = mc.getAudioEngine();
    auto& doc = mc.getTimelineDoc();
    auto& bar = mc.getTimelinePanel().getTransportBar();

    auto fake48 = makeFakeAt(kSampleRate);
    engine.audioDeviceAboutToStart(&fake48);
    ASSERT_EQ(engine.getRecordingLatencySamples(), kRoundTrip);
    const int frozenLatency = engine.getRecordingLatencySamples(); // what the commit must have used

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(doc.setTrackArmed(track, true));
    mc.timerCallback(); // arms input monitoring so the input reaches the tap

    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());
    auto* tap = findRecordTap(engine.getGraph());
    ASSERT_NE(tap, nullptr);
    ASSERT_TRUE(tap->isCapturing());

    // Roll a few blocks at 48 kHz so the take has real content and a sample-honest, zero anchor
    // (record-on happens before any block is driven, so frame 0 of the capture IS timeline sample 0).
    constexpr int kBlocksBeforeChange = 20;
    for (int block = 0; block < kBlocksBeforeChange; ++block)
        driveSilentBlock(engine);
    ASSERT_TRUE(tap->isCapturing());

    // THE CHANGE: the device switches to 96 kHz mid-take.
    auto fake96 = makeFakeAt(kNewSampleRate);
    engine.audioDeviceAboutToStart(&fake96);

    // The next 10 Hz poll finalizes the take through the SAME choke point a manual Record-off uses.
    mc.timerCallback();

    EXPECT_FALSE(tap->isCapturing()) << "a format change must finalize the take, not let it keep rolling";
    ASSERT_NE(doc.getTrack(track), nullptr);
    ASSERT_EQ(doc.getTrack(track)->clips.size(), 1u) << "the take must be committed exactly once";
    EXPECT_EQ(mc.getStatusBar().getTransientMessageForTest(), "Recording stopped: audio device changed");

    const auto& clip = doc.getTrack(track)->clips[0];
    ASSERT_TRUE(clip.assetRef.startsWith("Recordings/")) << "unsaved project ref prefix";
    const auto takeFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("Agent Synth")
                              .getChildFile("Recordings")
                              .getChildFile(clip.assetRef.fromLastOccurrenceOf("/", false, false));
    const auto wav = readWavInfo(takeFile);
    ASSERT_TRUE(wav.ok);
    // The WAV keeps its OWN (pre-change) rate — it was recorded at it, and a format change does not touch it.
    EXPECT_DOUBLE_EQ(wav.sampleRate, kSampleRate);

    // The commit's placement math must be exactly what computeTakePlacement gives from the FROZEN
    // (pre-change, 48 kHz) rate/bpm/latency — reproduced independently here and compared against
    // what was actually committed. Using the LIVE (post-change, 96 kHz) values here would fail this
    // comparison, which is exactly the rate-mixing bug the frozen AudioTake fields fix.
    synth::TakePlacementInput expected;
    expected.takeLengthSamples = wav.lengthInSamples;
    expected.captureStartValid = true;
    expected.captureStartTimelineSample = 0;
    expected.captureStartBlockOffset = 0;
    expected.punchInBeat = 0.0;
    expected.recordingLatencySamples = frozenLatency;
    expected.sampleRate = kSampleRate;
    expected.bpm = 120.0;
    const auto expectedPlacement = synth::computeTakePlacement(expected);
    ASSERT_TRUE(expectedPlacement.hasContent);
    EXPECT_DOUBLE_EQ(clip.startBeat, expectedPlacement.clipStartBeat);
    EXPECT_DOUBLE_EQ(clip.sourceStartSeconds, expectedPlacement.sourceStartSeconds);

    takeFile.deleteFile();
    takeFile.withFileExtension("agpk").deleteFile();
    engine.audioDeviceStopped();
}

TEST_F(DeviceChangeFlowTest, AudioTakeCapturesNothingAfterTheRateBoundary) {
    // The commit is a 10 Hz POLL, so blocks keep being rendered between the format change and the
    // finalize. Those blocks are at the NEW rate while the take's WAV header says the OLD one, so
    // the tap has to stop pushing at the boundary itself — not when the poll gets round to it.
    MainComponent mc(std::make_unique<MinimalProviderDC>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    buildInputPatch(mc);

    auto& engine = mc.getAudioEngine();
    auto& doc = mc.getTimelineDoc();
    auto& bar = mc.getTimelinePanel().getTransportBar();

    auto fake48 = makeFakeAt(kSampleRate);
    engine.audioDeviceAboutToStart(&fake48);

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(doc.setTrackArmed(track, true));
    mc.timerCallback(); // arms input monitoring so the input reaches the tap

    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());
    auto* tap = findRecordTap(engine.getGraph());
    ASSERT_NE(tap, nullptr);
    ASSERT_TRUE(tap->isCapturing());

    // Positive before the change, negative after: a single negative sample in the committed file is
    // a block that was recorded at 96 kHz into a 48 kHz take.
    constexpr int kBlocksBeforeChange = 8;
    constexpr int kBlocksAfterChange = 8;
    constexpr float kOldRateInput = 0.25f;  // well under the feedback guard's threshold
    constexpr float kNewRateInput = -0.75f; // ditto, and unmistakably from the other era
    for (int block = 0; block < kBlocksBeforeChange; ++block)
        driveBlockWithInput(engine, kOldRateInput);
    ASSERT_EQ(tap->getCapturedSamples(), (juce::int64)kBlocksBeforeChange * kBlockSize);

    // THE CHANGE, with the poll deliberately NOT run yet — this is the window under test.
    auto fake96 = makeFakeAt(kNewSampleRate);
    engine.audioDeviceAboutToStart(&fake96);
    EXPECT_TRUE(tap->wasCaptureHaltedByFormatChange());
    ASSERT_TRUE(tap->isCapturing()) << "the halt must leave the commit to the message thread";

    for (int block = 0; block < kBlocksAfterChange; ++block)
        driveBlockWithInput(engine, kNewRateInput);
    EXPECT_EQ(tap->getCapturedSamples(), (juce::int64)kBlocksBeforeChange * kBlockSize)
        << "blocks rendered at the new rate must never reach a take armed at the old one";

    // The existing early-commit flow, unchanged.
    mc.timerCallback();
    EXPECT_FALSE(tap->isCapturing());
    ASSERT_NE(doc.getTrack(track), nullptr);
    ASSERT_EQ(doc.getTrack(track)->clips.size(), 1u);

    const auto& clip = doc.getTrack(track)->clips[0];
    const auto takeFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("Agent Synth")
                              .getChildFile("Recordings")
                              .getChildFile(clip.assetRef.fromLastOccurrenceOf("/", false, false));
    const auto wav = readWavInfo(takeFile);
    ASSERT_TRUE(wav.ok);
    EXPECT_DOUBLE_EQ(wav.sampleRate, kSampleRate);
    EXPECT_EQ(wav.lengthInSamples, (juce::int64)kBlocksBeforeChange * kBlockSize)
        << "the file must end at the format change, not at the poll that committed it";

    bool anyNonZero = false;
    for (int channel = 0; channel < wav.audio.getNumChannels(); ++channel) {
        const float* data = wav.audio.getReadPointer(channel);
        for (int i = 0; i < (int)wav.lengthInSamples; ++i) {
            ASSERT_GE(data[i], 0.0f) << "a post-change sample landed in the take: channel " << channel << ", frame "
                                     << i;
            anyNonZero = anyNonZero || data[i] != 0.0f;
        }
    }
    EXPECT_TRUE(anyNonZero) << "the input never reached the tap, so the sample check proved nothing";

    takeFile.deleteFile();
    takeFile.withFileExtension("agpk").deleteFile();
    engine.audioDeviceStopped();
}

TEST_F(DeviceChangeFlowTest, MidiTakeCommittedAtFormatChange) {
    MainComponent mc(std::make_unique<MinimalProviderDC>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& engine = mc.getAudioEngine();
    auto& doc = mc.getTimelineDoc();
    auto& bar = mc.getTimelinePanel().getTransportBar();
    auto& recorder = mc.getMidiRecorderForTest();

    // MIDI recording needs nothing from the graph — clear it so handleIncomingMidiMessage(nullptr,
    // ...) below has no ExternalMidiModule to (safely, but needlessly) dereference the null source
    // against.
    mc.getGraphEditor().detachAllModuleComponents(); // UI lets go BEFORE the modules die
    engine.getGraph().clear();

    auto fake48 = makeFakeAt(kSampleRate);
    engine.audioDeviceAboutToStart(&fake48);

    const auto track = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    ASSERT_TRUE(doc.setTrackArmed(track, true));

    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());
    ASSERT_TRUE(recorder.isRecording());

    // A note landing inside the take — via the same public path a real MIDI input device uses
    // (AudioEngine::handleIncomingMidiMessage feeds the collector the device callback drains).
    engine.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    driveSilentBlock(engine); // drains the collector into captureBlock via renderPass

    for (int block = 0; block < 4; ++block)
        driveSilentBlock(engine);
    ASSERT_TRUE(recorder.isRecording());

    // THE CHANGE.
    auto fake96 = makeFakeAt(kNewSampleRate);
    engine.audioDeviceAboutToStart(&fake96);

    mc.timerCallback();

    EXPECT_FALSE(recorder.isRecording()) << "a format change must finalize the MIDI take too";
    ASSERT_NE(doc.getTrack(track), nullptr);
    ASSERT_EQ(doc.getTrack(track)->clips.size(), 1u) << "the MIDI take must be committed exactly once";
    EXPECT_FALSE(doc.getTrack(track)->clips[0].notes.empty()) << "the captured note must survive the commit";
    EXPECT_EQ(mc.getStatusBar().getTransientMessageForTest(), "Recording stopped: audio device changed");

    engine.audioDeviceStopped();
}

// ============================================================================
// 5. Duplex refusal's reachable half: zero active input channels survive
// ============================================================================

TEST(DeviceChangeTest, ZeroInputDeviceSurvives) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    auto in = graph.addNode(std::make_unique<AudioInputModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    for (int channel = 0; channel < 2; ++channel)
        graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});

    // The reachable half of duplex refusal: a saved state asked for inputs, but this device (as if
    // the saved one could not be opened and JUCE fell back to a default with no input channels)
    // reports ZERO active inputs while the graph/AudioInputModule were built expecting 2.
    FakeAudioIODevice fake(0, 2, kInLatency, kOutLatency, kSampleRate, kBlockSize);
    engine.audioDeviceAboutToStart(&fake);
    engine.setInputMonitoringEnabled(true); // the monitoring gate must stay safe with zero inputs too

    auto* module = dynamic_cast<AudioInputModule*>(in->getProcessor());
    ASSERT_NE(module, nullptr);

    std::vector<float> outLeft((std::size_t)kBlockSize, -9999.0f), outRight((std::size_t)kBlockSize, -9999.0f);
    float* outputs[] = {outLeft.data(), outRight.data()};

    // No crash with a null input array and 0 input channels.
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, kBlockSize, {});

    for (int i = 0; i < kBlockSize; ++i) {
        EXPECT_FLOAT_EQ(outLeft[(std::size_t)i], 0.0f)
            << "no input channels: Audio Input must render clean silence, sample " << i;
        EXPECT_FLOAT_EQ(outRight[(std::size_t)i], 0.0f);
    }

    EXPECT_EQ(module->getVisibleOutputPortCount(), 1) << "visible ports must floor at 1 even with zero device inputs";

    engine.audioDeviceStopped();
}

// ============================================================================
// 6. Metronome voice pool reset on rate change
// ============================================================================

TEST(DeviceChangeTest, MetronomeVoicesResetOnRateChange) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.getGraph().clear(); // silent by construction: any energy measured would be the click alone
    auto driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, 2);

    engine.getMetronome().setEnabled(true);
    ASSERT_TRUE(engine.getTransport().play());

    // 120 BPM (default) at 48 kHz: beat 1 crosses at absolute sample 24000, which lands at local
    // offset 448 within block 46 ([23552, 24064)) — 64 of the click's 192-sample decay render in
    // that block, leaving 128 samples still ringing when the render stops.
    driver->renderBlocks(47);
    ASSERT_GT(engine.getMetronome().getActiveVoiceCountForTest(), 0)
        << "test setup: a click must still be ringing across the block boundary";

    // THE CHANGE, mid-ring. Hosted mode's counterpart to audioDeviceAboutToStart.
    engine.prepareForHost(kNewSampleRate, kBlockSize, 0, 2);

    EXPECT_EQ(engine.getMetronome().getActiveVoiceCountForTest(), 0)
        << "a voice ringing across a format change must be silenced, never continue at the wrong pitch";

    engine.releaseFromHost();
    engine.shutdown();
}

// ============================================================================
// 7. Hook order pinned
// ============================================================================

namespace {

/** TEST SEAM user: records the step sequence handleStreamFormatChange() runs, and what the
 *  transport's sample rate reads at each one — see AudioEngine::onFormatChangeStepForTest(). */
class OrderSeamEngine : public AudioEngine {
public:
    using AudioEngine::AudioEngine;
    std::vector<int> stepsObserved;
    std::vector<double> rateAtStep;

protected:
    void onFormatChangeStepForTest(int step) override {
        stepsObserved.push_back(step);
        rateAtStep.push_back(getTransport().getSampleRate());
    }
};

} // namespace

TEST(DeviceChangeTest, HookOrderPinned) {
    OrderSeamEngine engine(AudioEngine::HostMode::Standalone);
    auto fake48 = makeFakeAt(kSampleRate);
    engine.audioDeviceAboutToStart(&fake48);
    engine.stepsObserved.clear();
    engine.rateAtStep.clear();

    auto fake96 = makeFakeAt(kNewSampleRate);
    engine.audioDeviceAboutToStart(&fake96);

    ASSERT_EQ(engine.stepsObserved.size(), 4u);
    EXPECT_EQ(engine.stepsObserved, (std::vector<int>{1, 2, 3, 4}))
        << "transport (1), metronome (2), streamer (3), in-flight-take check (4), in that order";

    // Step 1 IS the transport prepare, so it already reports the new rate: this is what proves every
    // later step — the streamer's invalidate (3) included — never sees the stale one.
    ASSERT_GE(engine.rateAtStep.size(), 3u);
    for (std::size_t i = 0; i < engine.rateAtStep.size(); ++i)
        EXPECT_DOUBLE_EQ(engine.rateAtStep[i], kNewSampleRate) << "step " << engine.stepsObserved[i];

    engine.audioDeviceStopped();
}
