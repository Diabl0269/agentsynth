// A recorded take lands where it was PLAYED.
//
// Two corrections meet here, and the tests are organised around them:
//
//   1. A SAMPLE-HONEST CAPTURE START. `RecordTapModule` records the transport sample its frame 0
//      was captured at, on the audio thread, from the block's BlockTimeInfo — not from a later poll
//      tick, which could miss up to ~100 ms of head. The tests here assert the error is ZERO.
//
//   2. ROUND-TRIP LATENCY COMPENSATION. A musician plays against what they HEAR, so audio captured
//      at timeline sample T was played at T - (input + graph + output). The acceptance test is
//      RecordedImpulseLandsWherePlayed: an impulse injected at exactly the callback sample a note
//      played on the grid would arrive at must come back out of the committed clip at exactly the
//      grid sample — off-by-zero.
//
// Three layers: PLACEMENT (synth::computeTakePlacement in isolation, no engine/device/files);
// ENGINE (a real AudioEngine driving Audio Input -> Rec Tap -> Audio Output through
// FakeAudioIODevice by hand — the only layer that sees a real transport playhead on the tap, since
// juce::AudioProcessorGraph installs it per node per render pass); FLOW (MainComponent's
// record-on/commit choreography with a count-in, same fake device). Engine and Flow layers are
// gated #if SYNTH_ENABLE_TIMELINE.
//
// Headless house rules as everywhere else: no real audio device, no sleeps.

#include "../Source/AudioEngine.h"
#include "../Source/Modules/AudioInputModule.h"
#include "../Source/Modules/RecordTapModule.h"
#include "../Source/Timeline/TakePlacement.h"
#include "../Source/UI/StatusBarComponent.h"
#include "FakeAudioIODevice.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

#if SYNTH_ENABLE_TIMELINE
#include "../Source/AI/AIProvider.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "MainComponent.h"
#endif

namespace {

using synth::test::FakeAudioIODevice;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr double kSampleRate = synth::test::kFakeDeviceSampleRate; // 48000
constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;      // 512
constexpr int kInLatency = synth::test::kFakeDeviceInputLatency;   // 64
constexpr int kOutLatency = synth::test::kFakeDeviceOutputLatency; // 128
// What the compensation covers: input device + graph (0 for this patch) + output device.
constexpr int kRoundTrip = kInLatency + kOutLatency; // 192 samples = 4.0 ms at 48 kHz

struct ScopedTempFile {
    explicit ScopedTempFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedTempFile() { file.deleteFile(); }

    juce::File file;
};

/** The take, read back with JUCE's own reader — nothing here shares code with the writer. */
struct WavContents {
    bool ok = false;
    juce::int64 lengthInSamples = 0;
    juce::AudioBuffer<float> audio;
};

WavContents readWav(const juce::File& file) {
    WavContents out;
    if (!file.existsAsFile())
        return out;
    auto input = file.createInputStream();
    if (input == nullptr)
        return out;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(input.release(), /*deleteStreamIfOpeningFails=*/true));
    if (reader == nullptr)
        return out;

    out.lengthInSamples = reader->lengthInSamples;
    out.audio.setSize((int)reader->numChannels, (int)std::max((juce::int64)1, reader->lengthInSamples));
    out.audio.clear();
    if (reader->lengthInSamples > 0)
        reader->read(&out.audio, 0, (int)reader->lengthInSamples, 0, true, true);
    out.ok = true;
    return out;
}

/** The one frame of a take that is not silent, or -1. */
juce::int64 findImpulseFrame(const WavContents& wav, float threshold) {
    juce::int64 found = -1;
    for (juce::int64 frame = 0; frame < wav.lengthInSamples; ++frame) {
        if (std::abs(wav.audio.getReadPointer(0)[frame]) > threshold) {
            if (found >= 0)
                return -2; // more than one: the caller's assertion should say so
            found = frame;
        }
    }
    return found;
}

} // namespace

// ============================================================================
// 1. Placement layer — the commit's arithmetic, in isolation
// ============================================================================

TEST(TakePlacementTest, ClampAtTimelineZero) {
    // Punch at beat 0 with a real device round trip: the take's frame 0 was PLAYED 192 samples
    // before the timeline starts, and there is nowhere to put that. The clip stays pinned at 0 and
    // sourceStartSeconds absorbs the whole shift, so the CONTENT never moves relative to itself —
    // only the window onto it does.
    synth::TakePlacementInput in;
    in.takeLengthSamples = 48000;
    in.captureStartValid = true;
    in.captureStartTimelineSample = 0;
    in.punchInBeat = 0.0;
    in.recordingLatencySamples = kRoundTrip;
    in.sampleRate = kSampleRate;
    in.bpm = 120.0;

    const auto out = synth::computeTakePlacement(in);
    ASSERT_TRUE(out.hasContent);
    EXPECT_EQ(out.clipStartSample, 0) << "a clip may not start before the timeline does";
    EXPECT_EQ(out.trimFrames, kRoundTrip) << "the shift moves into the source window instead";
    EXPECT_EQ(out.lengthFrames, 48000 - kRoundTrip);
    EXPECT_DOUBLE_EQ(out.clipStartBeat, 0.0);
    EXPECT_DOUBLE_EQ(out.sourceStartSeconds, (double)kRoundTrip / kSampleRate);
}

TEST(TakePlacementTest, ZeroLatencyPlacesTheClipAtThePunch) {
    // in = out = 0 and a capture that started exactly at the punch: nothing to correct, so the clip
    // starts at the punch with no trim at all.
    synth::TakePlacementInput in;
    in.takeLengthSamples = 24000;
    in.captureStartValid = true;
    in.captureStartTimelineSample = 48000; // beat 2 at 120 bpm / 48 kHz
    in.punchInBeat = 2.0;
    in.recordingLatencySamples = 0;
    in.sampleRate = kSampleRate;
    in.bpm = 120.0;

    const auto out = synth::computeTakePlacement(in);
    ASSERT_TRUE(out.hasContent);
    EXPECT_EQ(out.clipStartSample, 48000);
    EXPECT_EQ(out.trimFrames, 0);
    EXPECT_DOUBLE_EQ(out.clipStartBeat, 2.0);
    EXPECT_DOUBLE_EQ(out.sourceStartSeconds, 0.0);
}

TEST(TakePlacementTest, PreRollIsTrimmedNotRewritten) {
    // A count-in: the take begins at timeline 0 and the punch is a bar in. Every pre-roll frame is
    // in the FILE; the clip window simply starts after them.
    synth::TakePlacementInput in;
    in.takeLengthSamples = 120000;
    in.captureStartValid = true;
    in.captureStartTimelineSample = 0;
    in.punchInBeat = 4.0; // 96000 samples at 120 bpm / 48 kHz
    in.recordingLatencySamples = kRoundTrip;
    in.sampleRate = kSampleRate;
    in.bpm = 120.0;

    const auto out = synth::computeTakePlacement(in);
    ASSERT_TRUE(out.hasContent);
    EXPECT_EQ(out.clipStartSample, 96000) << "the clip starts at the punch, never before it";
    EXPECT_EQ(out.trimFrames, 96000 + kRoundTrip) << "pre-roll + the latency shift, all in the window";
    EXPECT_EQ(out.lengthFrames, 120000 - (96000 + kRoundTrip));
    EXPECT_DOUBLE_EQ(out.sourceStartSeconds, (double)(96000 + kRoundTrip) / kSampleRate);
}

TEST(TakePlacementTest, NoAnchorFallsBackToThePunch) {
    // No transport behind the tap (a bare unit test, a foreign host): there is no honest anchor, so
    // the placement falls back to the punch beat, untrimmed.
    synth::TakePlacementInput in;
    in.takeLengthSamples = 4096;
    in.captureStartValid = false;
    in.punchInBeat = 1.0;
    in.recordingLatencySamples = kRoundTrip;
    in.sampleRate = kSampleRate;
    in.bpm = 120.0;

    const auto out = synth::computeTakePlacement(in);
    ASSERT_TRUE(out.hasContent);
    EXPECT_EQ(out.clipStartSample, 24000);
    EXPECT_EQ(out.trimFrames, 0);
    EXPECT_EQ(out.lengthFrames, 4096);
}

TEST(TakePlacementTest, EmptyAndAllPreRollTakesCommitNothing) {
    synth::TakePlacementInput empty;
    empty.takeLengthSamples = 0;
    EXPECT_FALSE(synth::computeTakePlacement(empty).hasContent);

    // Record disengaged during the count-in: everything captured sits before the punch.
    synth::TakePlacementInput preRollOnly;
    preRollOnly.takeLengthSamples = 1000;
    preRollOnly.captureStartValid = true;
    preRollOnly.captureStartTimelineSample = 0;
    preRollOnly.punchInBeat = 4.0;
    preRollOnly.sampleRate = kSampleRate;
    preRollOnly.bpm = 120.0;
    EXPECT_FALSE(synth::computeTakePlacement(preRollOnly).hasContent);
}

// ============================================================================
// 2. Engine layer — the anchor, and the impulse
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

namespace {

/** Audio Input -> Rec Tap -> Audio Output, on `engine`'s own graph. The tap must be IN the graph:
 *  juce::AudioProcessorGraph installs the playhead on each node as it renders it, so a tap driven
 *  by hand has no transport to anchor against. */
RecordTapModule* buildRecordingPassthrough(AudioEngine& engine) {
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    auto in = graph.addNode(std::make_unique<AudioInputModule>());
    auto tap = graph.addNode(std::make_unique<RecordTapModule>());
    auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    for (int channel = 0; channel < 2; ++channel) {
        graph.addConnection({{in->nodeID, channel}, {tap->nodeID, channel}});
        graph.addConnection({{tap->nodeID, channel}, {out->nodeID, channel}});
    }
    return dynamic_cast<RecordTapModule*>(tap->getProcessor());
}

/** One device callback of silence, except for `impulseAmplitude` at absolute input sample
 *  `impulseSample` (negative: none). `firstSample` is where this block starts in the run. */
void driveBlock(AudioEngine& engine, juce::int64 firstSample, juce::int64 impulseSample, float impulseAmplitude) {
    std::vector<float> left((std::size_t)kBlockSize, 0.0f), right((std::size_t)kBlockSize, 0.0f);
    if (impulseSample >= firstSample && impulseSample < firstSample + kBlockSize) {
        const auto offset = (std::size_t)(impulseSample - firstSample);
        left[offset] = impulseAmplitude;
        right[offset] = impulseAmplitude;
    }
    std::vector<float> outLeft((std::size_t)kBlockSize, 0.0f), outRight((std::size_t)kBlockSize, 0.0f);
    const float* inputs[] = {left.data(), right.data()};
    float* outputs[] = {outLeft.data(), outRight.data()};
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
}

} // namespace

// THE ACCEPTANCE TEST.
//
// The musician hears the click for grid sample G `kOutLatency` samples after the graph rendered it,
// plays exactly on it, and their note reaches our callback `kInLatency` samples later still. So an
// impulse injected at callback sample G + 64 + 128 IS "played exactly on the grid". After the take
// commits, that impulse must sit at timeline sample G — exactly, not nearly.
TEST(LatencyAlignmentTest, RecordedImpulseLandsWherePlayed) {
    ScopedTempFile wav("agentsynth_tl68_impulse.wav");
    ScopedTempFile peaks("agentsynth_tl68_impulse.agpk");

    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto* tap = buildRecordingPassthrough(engine);
    ASSERT_NE(tap, nullptr);

    FakeAudioIODevice fake(2, 2); // in = 64, out = 128
    engine.audioDeviceAboutToStart(&fake);
    engine.setInputMonitoringEnabled(true);
    ASSERT_EQ(engine.getRecordingLatencySamples(), kRoundTrip)
        << "the sum under test is input + graph + output; this patch's graph reports 0";

    auto& transport = engine.getTransport();
    transport.play(); // takes effect at sample 0 of the first callback below

    ASSERT_TRUE(tap->startCapture(wav.file, peaks.file, kSampleRate, 2));

    constexpr juce::int64 kGridSample = 24000; // beat 1 at 120 bpm / 48 kHz
    constexpr juce::int64 kInjectAt = kGridSample + kInLatency + kOutLatency;
    constexpr int kBlocks = 60; // comfortably past the injection point

    for (int block = 0; block < kBlocks; ++block)
        driveBlock(engine, (juce::int64)block * kBlockSize, kInjectAt, 0.5f);

    const auto take = tap->stopCapture();
    ASSERT_TRUE(take.ok);
    EXPECT_FALSE(take.overran);
    ASSERT_TRUE(take.captureStartValid) << "the tap must have anchored itself against the transport";
    EXPECT_EQ(take.captureStartTimelineSample, 0) << "capture armed before the first block: frame 0 is timeline 0";
    EXPECT_EQ(take.captureStartBlockOffset, 0);

    // The commit's own arithmetic — the SAME function MainComponent::commitAudioRecording calls.
    synth::TakePlacementInput placementInput;
    placementInput.takeLengthSamples = take.lengthSamples;
    placementInput.captureStartValid = take.captureStartValid;
    placementInput.captureStartTimelineSample = take.captureStartTimelineSample;
    placementInput.captureStartBlockOffset = take.captureStartBlockOffset;
    placementInput.punchInBeat = 0.0; // record-on at beat 0, no count-in
    placementInput.recordingLatencySamples = engine.getRecordingLatencySamples();
    placementInput.sampleRate = kSampleRate;
    placementInput.bpm = 120.0;
    const auto placement = synth::computeTakePlacement(placementInput);
    ASSERT_TRUE(placement.hasContent);

    const auto contents = readWav(wav.file);
    ASSERT_TRUE(contents.ok);
    const juce::int64 impulseFrame = findImpulseFrame(contents, 0.25f);
    ASSERT_GE(impulseFrame, 0) << "expected exactly one non-silent frame in the take";

    // Where the committed clip puts that frame, derived the way anything reading the document would:
    // clipStartBeat -> samples through the tempo, plus the frame's offset past sourceStartSeconds.
    const double samplesPerBeat = 60.0 * kSampleRate / 120.0;
    const juce::int64 clipStartSample = (juce::int64)std::llround(placement.clipStartBeat * samplesPerBeat);
    const juce::int64 sourceStartFrames = (juce::int64)std::llround(placement.sourceStartSeconds * kSampleRate);
    const juce::int64 impulseTimelineSample = clipStartSample + (impulseFrame - sourceStartFrames);

    EXPECT_EQ(impulseTimelineSample, kGridSample)
        << "a note played exactly on the grid must land exactly on the grid — off-by-zero, not near";
    // And the same answer through the sample-domain fields, so a beat-conversion bug can't hide.
    EXPECT_EQ(placement.clipStartSample + (impulseFrame - placement.trimFrames), kGridSample);

    engine.audioDeviceStopped();
}

// The v1 slop, gone: the take's frame 0 is exactly the transport sample the capture engaged at, not
// wherever a 10 Hz poll happened to notice.
TEST(LatencyAlignmentTest, CaptureStartIsSampleHonest) {
    ScopedTempFile wav("agentsynth_tl68_honest.wav");
    ScopedTempFile peaks("agentsynth_tl68_honest.agpk");

    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto* tap = buildRecordingPassthrough(engine);
    ASSERT_NE(tap, nullptr);

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    engine.setInputMonitoringEnabled(true);

    auto& transport = engine.getTransport();
    transport.play();

    // Roll for a while UNARMED, then engage mid-run — the "record while already playing" case.
    constexpr int kBlocksBeforeArming = 7;
    for (int block = 0; block < kBlocksBeforeArming; ++block)
        driveBlock(engine, (juce::int64)block * kBlockSize, -1, 0.0f);

    // The capture will engage on the NEXT block, which starts here.
    const juce::int64 engagementSample = (juce::int64)kBlocksBeforeArming * kBlockSize;
    // Note what the message thread can see at this moment: the snapshot carries the start of the
    // block just RENDERED, one block behind where the take is about to begin. A poll observing this
    // is already stale before it acts on it — which is the whole reason the anchor is taken on the
    // audio thread instead.
    ASSERT_EQ(transport.getPositionSnapshot().samplePosition, engagementSample - kBlockSize);
    ASSERT_TRUE(tap->startCapture(wav.file, peaks.file, kSampleRate, 2));

    // Mid-take the anchor is already readable — the live strip wants it before the take ends.
    for (int block = kBlocksBeforeArming; block < kBlocksBeforeArming + 4; ++block)
        driveBlock(engine, (juce::int64)block * kBlockSize, -1, 0.0f);
    juce::int64 liveAnchor = -1;
    EXPECT_TRUE(tap->getCaptureStartTimelineSample(liveAnchor));
    EXPECT_EQ(liveAnchor, engagementSample);

    const auto take = tap->stopCapture();
    ASSERT_TRUE(take.ok);
    ASSERT_TRUE(take.captureStartValid);
    EXPECT_EQ(take.captureStartTimelineSample - engagementSample, 0)
        << "the anchor error must be ZERO — the legacy poll could be a whole 10 Hz tick (~4800 "
           "samples at 48 kHz) late";

    engine.audioDeviceStopped();
}

// Zero-latency devices: nothing to compensate, so the clip starts exactly at the punch.
TEST(LatencyAlignmentTest, ZeroLatencyDevicesNoShift) {
    ScopedTempFile wav("agentsynth_tl68_zerolat.wav");
    ScopedTempFile peaks("agentsynth_tl68_zerolat.agpk");

    AudioEngine engine(AudioEngine::HostMode::Standalone);
    auto* tap = buildRecordingPassthrough(engine);
    ASSERT_NE(tap, nullptr);

    FakeAudioIODevice fake(2, 2, /*inputLatency=*/0, /*outputLatency=*/0);
    engine.audioDeviceAboutToStart(&fake);
    engine.setInputMonitoringEnabled(true);
    EXPECT_EQ(engine.getInputLatencySamples(), 0);
    EXPECT_EQ(engine.getOutputLatencySamples(), 0);
    ASSERT_EQ(engine.getRecordingLatencySamples(), 0);

    auto& transport = engine.getTransport();
    transport.locateBeat(2.0);
    transport.play();
    ASSERT_TRUE(tap->startCapture(wav.file, peaks.file, kSampleRate, 2));

    for (int block = 0; block < 8; ++block)
        driveBlock(engine, (juce::int64)block * kBlockSize, -1, 0.0f);

    const auto take = tap->stopCapture();
    ASSERT_TRUE(take.ok);
    ASSERT_TRUE(take.captureStartValid);
    EXPECT_EQ(take.captureStartTimelineSample, 48000) << "beat 2 at 120 bpm / 48 kHz";

    synth::TakePlacementInput placementInput;
    placementInput.takeLengthSamples = take.lengthSamples;
    placementInput.captureStartValid = take.captureStartValid;
    placementInput.captureStartTimelineSample = take.captureStartTimelineSample;
    placementInput.punchInBeat = 2.0; // record-on at the position the capture engaged at
    placementInput.recordingLatencySamples = engine.getRecordingLatencySamples();
    placementInput.sampleRate = kSampleRate;
    placementInput.bpm = 120.0;

    const auto placement = synth::computeTakePlacement(placementInput);
    ASSERT_TRUE(placement.hasContent);
    EXPECT_DOUBLE_EQ(placement.clipStartBeat, 2.0) << "no latency, no shift: the clip starts at the punch";
    EXPECT_EQ(placement.trimFrames, 0);
    EXPECT_DOUBLE_EQ(placement.sourceStartSeconds, 0.0);

    engine.audioDeviceStopped();
}

#endif // SYNTH_ENABLE_TIMELINE

// ============================================================================
// 3. Flow layer — MainComponent's count-in commit
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

namespace {

// Same minimal pattern as RecordTapTests.cpp's MockProviderRT.
class MinimalProviderLA : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "LatencyAlignmentMock"; }
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

} // namespace

class LatencyFlowTest : public ::testing::Test {
protected:
    // The delegating MainComponent ctor reads/writes the shared on-disk "Agent Synth" settings —
    // same hygiene as RecordTapTests.cpp's RecordFlowTest, plus the count-in this file needs.
    void writeKeys(int countInBars) {
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
            s->setValue("timelineCountInBars", countInBars);
            s->saveIfNeeded();
        }
    }

    void SetUp() override { writeKeys(0); }
    void TearDown() override { writeKeys(0); }

    static void quiesceEngine(MainComponent& mc) { mc.getAudioEngine().suspendDeviceCallback(); }

    /** Replaces the default patch with Audio Input -> Audio Output. Record-on splices the master tap
     *  in front of the output, exactly as it would in a real session. */
    static void buildInputPatch(MainComponent& mc) {
        auto& graph = mc.getAudioEngine().getGraph();
        graph.clear();
        graph.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
        auto in = graph.addNode(std::make_unique<AudioInputModule>());
        // AudioGraphIOProcessor::getName() is already "Audio Output", which is how
        // ensureMasterRecordTap() identifies the master bus to splice in front of.
        auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
        for (int channel = 0; channel < 2; ++channel)
            graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});
    }
};

// A count-in's pre-roll is RECORDED (that is what makes the start sample-honest) and excluded from
// the clip by the window, not by rewriting the file.
TEST_F(LatencyFlowTest, CountInPunchTrimsViaSourceStart) {
    writeKeys(1); // one bar of count-in

    MainComponent mc(std::make_unique<MinimalProviderLA>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    buildInputPatch(mc);

    auto& engine = mc.getAudioEngine();
    auto& doc = mc.getTimelineDoc();
    auto& bar = mc.getTimelinePanel().getTransportBar();
    auto& transport = engine.getTransport();

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    ASSERT_EQ(engine.getRecordingLatencySamples(), kRoundTrip);

    // 480 bpm keeps the pre-roll short in wall-clock blocks: one 4/4 bar is 0.5 s = 24000 samples.
    ASSERT_TRUE(transport.setBpm(480.0));
    ASSERT_TRUE(transport.locateBeat(4.0)); // record from bar 2, so bar 1 is genuine pre-roll
    driveBlock(engine, 0, -1, 0.0f);        // drains both commands
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().bpm, 480.0);

    const auto track = doc.addTrack(synth::TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(doc.setTrackArmed(track, true));
    mc.timerCallback(); // arms input monitoring, so the input reaches the tap

    // ---- Record on: the transport jumps back a bar and the capture starts NOW ----
    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());
    auto* tap = [&]() -> RecordTapModule* {
        for (auto* node : engine.getGraph().getNodes())
            if (node != nullptr)
                if (auto* t = dynamic_cast<RecordTapModule*>(node->getProcessor()))
                    return t;
        return nullptr;
    }();
    ASSERT_NE(tap, nullptr);
    EXPECT_TRUE(tap->isCapturing()) << "the capture starts at the click, pre-roll included";

    // Roll through the pre-roll bar (24000 samples) and a little past the punch.
    constexpr int kBlocks = 56; // 28672 samples > 24000
    for (int block = 0; block < kBlocks; ++block)
        driveBlock(engine, (juce::int64)block * kBlockSize, -1, 0.0f);
    ASSERT_GT(transport.getPositionSnapshot().ppq, 4.0) << "the transport must have crossed the punch";

    // ---- Record off ----
    bar.getRecordButton().onClick();
    EXPECT_FALSE(tap->isCapturing());

    ASSERT_NE(doc.getTrack(track), nullptr);
    ASSERT_EQ(doc.getTrack(track)->clips.size(), 1u);
    const auto& clip = doc.getTrack(track)->clips[0];

    // The clip starts at the punch — never in the pre-roll — and the trim is "pre-roll + round trip".
    EXPECT_DOUBLE_EQ(clip.startBeat, 4.0);
    const double samplesPerBeat = 60.0 * kSampleRate / 480.0; // 6000
    const juce::int64 punchSample = (juce::int64)std::llround(4.0 * samplesPerBeat);
    const juce::int64 trimFrames = (juce::int64)std::llround(clip.sourceStartSeconds * kSampleRate);
    EXPECT_EQ(trimFrames, punchSample + kRoundTrip)
        << "the pre-roll AND the latency shift are both expressed as a source offset";

    // ...and every one of those trimmed frames is still in the FILE. An unsaved project records
    // into app data under the reserved "Recordings/" ref prefix, and the take number is whichever
    // was free — so the file is resolved from the clip's own assetRef, never guessed.
    ASSERT_TRUE(clip.assetRef.startsWith("Recordings/")) << "unsaved project ref prefix";
    const auto takeFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("Agent Synth")
                              .getChildFile("Recordings")
                              .getChildFile(clip.assetRef.fromLastOccurrenceOf("/", false, false));
    ASSERT_TRUE(takeFile.existsAsFile()) << "an unsaved project records into app data";
    const auto contents = readWav(takeFile);
    ASSERT_TRUE(contents.ok);
    EXPECT_GT(contents.lengthInSamples, trimFrames)
        << "the WAV keeps the pre-roll: the clip window excludes it, nothing is rewritten";

    takeFile.deleteFile();
    takeFile.withFileExtension("agpk").deleteFile();
    engine.audioDeviceStopped();
}

// The drawn playhead answers a DIFFERENT question from the recording alignment, and must keep
// answering it: "where is the audio the user is hearing right now?" — which is the OUTPUT latency
// alone. This pins MainComponent's wiring, not the overlay's arithmetic (TimelinePlayhead
// Tests.cpp's LatencyOffsetShiftsTheLine already owns that): a fake device with musically large,
// UNEQUAL latencies makes "output only" and "input + graph + output" land on different pixels.
TEST_F(LatencyFlowTest, PlayheadUsesOutputLatencyOnlyNotTheRecordingSum) {
    MainComponent mc(std::make_unique<MinimalProviderLA>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    buildInputPatch(mc); // no FX, so the graph term is 0 and the two candidates differ by input only

    auto& engine = mc.getAudioEngine();
    // 0.5 s out, 0.25 s in at 48 kHz = 1.0 and 0.5 beats at 120 bpm. Absurd for a real device,
    // deliberately: a realistic 128-sample output latency is half a pixel at default zoom.
    FakeAudioIODevice fake(2, 2, /*inputLatency=*/12000, /*outputLatency=*/24000);
    engine.audioDeviceAboutToStart(&fake);
    ASSERT_EQ(engine.getGraphLatencySamples(), 0);
    ASSERT_EQ(engine.getRecordingLatencySamples(), 36000);

    mc.simulateToggleTimelineClick();
    auto& panel = mc.getTimelinePanel();
    ASSERT_TRUE(panel.isVisible());

    auto& transport = engine.getTransport();
    ASSERT_TRUE(transport.locateBeat(4.0));
    driveBlock(engine, 0, -1, 0.0f); // drains the locate; the transport is stopped, so it stays put
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 4.0);

    mc.timerCallback();

    auto& view = panel.getViewState();
    const int outputOnlyX = (int)std::llround(view.beatToX(4.0 - 1.0));
    const int recordingSumX = (int)std::llround(view.beatToX(4.0 - 1.5));
    EXPECT_EQ(panel.getPlayhead().getLineX(), outputOnlyX) << "the playhead shows what is HEARD: output latency only";
    EXPECT_NE(panel.getPlayhead().getLineX(), recordingSumX)
        << "the recording sum belongs to take placement, not to the drawn line";

    mc.simulateToggleTimelineClick();
    engine.audioDeviceStopped();
}

#endif // SYNTH_ENABLE_TIMELINE

// ============================================================================
// 4. The status bar's round-trip readout
// ============================================================================

TEST(StatusBarRoundTripTest, FormatsAndGatesOnTheStringItWouldDraw) {
    EXPECT_EQ(StatusBarComponent::formatRoundTrip(4.0, true), "RT 4.0 ms");
    EXPECT_EQ(StatusBarComponent::formatRoundTrip(12.34, true), "RT 12.3 ms");
    EXPECT_EQ(StatusBarComponent::formatRoundTrip(-1.0, true), "RT 0.0 ms") << "never print a negative latency";
    EXPECT_EQ(StatusBarComponent::formatRoundTrip(4.0, false), juce::String::fromUTF8("RT \xe2\x80\x94"))
        << "hosted / no device: a placeholder, not a made-up number";

    StatusBarComponent bar;
    bar.setSize(800, 24);
    EXPECT_EQ(bar.getRoundTripRepaintCountForTest(), 0);
    EXPECT_TRUE(bar.getRoundTripTextForTest().isEmpty());

    bar.updateRoundTripLatency(4.0, true);
    EXPECT_EQ(bar.getRoundTripTextForTest(), "RT 4.0 ms");
    EXPECT_EQ(bar.getRoundTripRepaintCountForTest(), 1);

    // Same value again: gated, exactly one repaint so far.
    bar.updateRoundTripLatency(4.0, true);
    EXPECT_EQ(bar.getRoundTripRepaintCountForTest(), 1) << "an unchanged reading must not repaint";
    // A change below the printed resolution is also gated — the diff is on the drawn string.
    bar.updateRoundTripLatency(4.02, true);
    EXPECT_EQ(bar.getRoundTripRepaintCountForTest(), 1);

    bar.updateRoundTripLatency(5.5, true);
    EXPECT_EQ(bar.getRoundTripTextForTest(), "RT 5.5 ms");
    EXPECT_EQ(bar.getRoundTripRepaintCountForTest(), 2);
}

TEST(StatusBarRoundTripTest, HostedEngineHasNoRoundTripToReport) {
    AudioEngine hosted(AudioEngine::HostMode::Hosted);
    FakeAudioIODevice fake(2, 2);
    hosted.audioDeviceAboutToStart(&fake); // a host would never do this; prove it changes nothing
    EXPECT_EQ(hosted.getInputLatencySamples(), 0);
    EXPECT_EQ(hosted.getOutputLatencySamples(), 0);
    EXPECT_EQ(hosted.getRecordingLatencySamples(), 0) << "the host owns both ends of the round trip";
}

TEST(StatusBarRoundTripTest, EngineSumsInputGraphAndOutput) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    EXPECT_EQ(engine.getRecordingLatencySamples(), 0) << "no device started: nothing to report";

    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    EXPECT_EQ(engine.getInputLatencySamples(), kInLatency);
    EXPECT_EQ(engine.getOutputLatencySamples(), kOutLatency) << "cached at start, like its input sibling";
    EXPECT_EQ(engine.getRecordingLatencySamples(),
              engine.getInputLatencySamples() + engine.getGraphLatencySamples() + engine.getOutputLatencySamples());

    engine.audioDeviceStopped();
    EXPECT_EQ(engine.getOutputLatencySamples(), 0) << "a stopped device reports no latency";
    EXPECT_EQ(engine.getRecordingLatencySamples(), 0);
}

#if SYNTH_ENABLE_TIMELINE

TEST_F(LatencyFlowTest, StatusBarShowsRoundTrip) {
    MainComponent mc(std::make_unique<MinimalProviderLA>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& engine = mc.getAudioEngine();
    // A latency-free patch, so the displayed number is exactly the device round trip. (The default
    // patch's FX report latency of their own — correctly included in the sum, which is why this test
    // replaces the patch rather than hard-coding what that sum happens to be today.)
    buildInputPatch(mc);
    FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);
    ASSERT_EQ(engine.getGraphLatencySamples(), 0);
    ASSERT_EQ(engine.getRecordingLatencySamples(),
              engine.getInputLatencySamples() + engine.getGraphLatencySamples() + engine.getOutputLatencySamples());

    // The status bar is fed on every SECOND 10 Hz tick (5 Hz), so two calls to reach the first one.
    mc.timerCallback();
    mc.timerCallback();

    // 64 + 0 + 128 = 192 samples at 48 kHz is exactly 4.0 ms.
    EXPECT_EQ(mc.getStatusBar().getRoundTripTextForTest(), "RT 4.0 ms");
    const int repaints = mc.getStatusBar().getRoundTripRepaintCountForTest();
    EXPECT_EQ(repaints, 1);

    // Two more polls with nothing changed: still one repaint.
    mc.timerCallback();
    mc.timerCallback();
    mc.timerCallback();
    mc.timerCallback();
    EXPECT_EQ(mc.getStatusBar().getRoundTripRepaintCountForTest(), repaints)
        << "an unchanged latency must not repaint the status bar";

    engine.audioDeviceStopped();
}

#endif // SYNTH_ENABLE_TIMELINE
