// Tests for TL5-6: synth::Metronome — the click generator summed POST-graph — plus its engine
// wiring (AudioEngine::renderPass, BounceExporter's force-off guard) and the count-in pre-roll's
// MainComponent-level choreography (MidiRecorder's punch-in filter, the forced-on click).
//
// Timing arithmetic throughout: 48 kHz, 512-sample blocks, 120 BPM => 24000 samples/beat, the same
// convention MidiRecorderTests.cpp / BounceExporterTests.cpp use. 8 beats == 192000 samples == 375
// whole blocks (see BounceExporterTests.cpp's own kEightBeatBlocks derivation) — tests 1/2/4/5/6
// use a hosted AudioEngine with an EMPTY (cleared) graph, so any energy measured is the metronome's
// own click and nothing else; tests 3/9 drive synth::Metronome directly with hand-built
// BlockTimeInfo structs, which needs no engine at all.

#include "../Source/AudioEngine.h"
#include "../Source/Transport/BounceExporter.h"
#include "../Source/Transport/Metronome.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "TestAudioHelpers.h"
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

#if SYNTH_ENABLE_TIMELINE
#include "../Source/AI/AIProvider.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "MainComponent.h"
#endif

using synth::BlockTimeInfo;
using synth::Metronome;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 2;
constexpr int kSamplesPerBeat = 24000; // 120 BPM at 48 kHz — TransportService's default BPM
constexpr int kEightBeatBlocks = 375;  // 8 beats == 192000 samples == 375 * 512, exactly

// The first audible sample of any click is one sample AFTER its crossing (phase 0 at the crossing
// itself is sin(0) == 0 exactly); this is comfortably above the smaller of the two amplitudes
// (0.25) after one sample's worth of decay at any tempo/frequency this class uses, and comfortably
// below where genuine silence measures.
constexpr float kOnsetThreshold = 0.02f;
constexpr float kSilenceThreshold = 1.0e-4f;

// Hosted engine, EMPTY graph — silent by construction, so any energy in a render is the
// metronome's own click. Mirrors BounceExporterTests.cpp's Fixture exactly (same driver, same
// teardown order).
struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;

    void build(double sampleRate = kSampleRate, int blockSize = kBlockSize) {
        engine.initialise();
        engine.getGraph().clear();
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, sampleRate, blockSize, kNumChannels);
    }

    ~Fixture() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

BlockTimeInfo makeInfo(double sampleRate, double bpm, int timeSigNumerator, int timeSigDenominator, double startPpq,
                       int numSamples) {
    BlockTimeInfo info;
    info.sampleRate = sampleRate;
    info.bpm = bpm;
    info.timeSigNumerator = timeSigNumerator;
    info.timeSigDenominator = timeSigDenominator;
    info.playing = true;
    info.startPpq = startPpq;
    info.numSamples = numSamples;
    info.endPpq = startPpq + (double)numSamples * info.beatsPerSample();
    info.loopWrapSample = -1;
    return info;
}

} // namespace

// ============================================================================
// 1. ClicksAtExactBeatCrossings
// ============================================================================

// Engine-driven: these assert the flag-gated integration (renderPass only calls the metronome
// under SYNTH_ENABLE_TIMELINE), so they compile out with the flag to keep the OFF CI job green.
#if SYNTH_ENABLE_TIMELINE

TEST(MetronomeTest, ClicksAtExactBeatCrossings) {
    Fixture f;
    f.build();
    f.engine.getMetronome().setEnabled(true);
    ASSERT_TRUE(f.engine.getTransport().play());

    const auto audio = f.driver->renderBlocks(kEightBeatBlocks);
    ASSERT_EQ(audio.getNumSamples(), kEightBeatBlocks * kBlockSize);

    for (int beat = 0; beat < 8; ++beat) {
        const int crossing = beat * kSamplesPerBeat;
        EXPECT_NEAR(audio.getSample(0, crossing), 0.0f, 1.0e-6f)
            << "beat " << beat << ": phase 0 at the exact crossing sample must be silent";
        ASSERT_LT(crossing + 1, audio.getNumSamples());
        EXPECT_GT(std::abs(audio.getSample(0, crossing + 1)), kOnsetThreshold)
            << "beat " << beat << ": the click must be audible one sample after the crossing";

        const int nextCrossing = crossing + kSamplesPerBeat;
        if (nextCrossing <= audio.getNumSamples()) {
            const int silentProbe = nextCrossing - 1000; // well past the ~192-sample decay
            EXPECT_LT(std::abs(audio.getSample(0, silentProbe)), kSilenceThreshold)
                << "beat " << beat << ": must be silent well before the next crossing";
        }
    }
}

// ============================================================================
// 2. DownbeatAccented
// ============================================================================

TEST(MetronomeTest, DownbeatAccented) {
    auto peakNear = [](const juce::AudioBuffer<float>& audio, int beat) {
        const int crossing = beat * kSamplesPerBeat;
        return TestAudioHelpers::computeRMSInRange(audio, crossing, crossing + 40, 0);
    };

    {
        // Default 4/4: downbeats at beats 0 and 4 (within an 8-beat render).
        Fixture f;
        f.build();
        f.engine.getMetronome().setEnabled(true);
        ASSERT_TRUE(f.engine.getTransport().play());

        const auto audio = f.driver->renderBlocks(kEightBeatBlocks);
        for (int downbeat : {0, 4}) {
            const float downbeatPeak = peakNear(audio, downbeat);
            for (int beat = 0; beat < 8; ++beat) {
                if (beat == 0 || beat == 4)
                    continue;
                EXPECT_GT(downbeatPeak, peakNear(audio, beat))
                    << "downbeat " << downbeat << " must measure louder than beat " << beat;
            }
        }
    }
    {
        // 3/4: downbeats every 3 beats — 0, 3, 6.
        Fixture f;
        f.build();
        f.engine.getMetronome().setEnabled(true);
        ASSERT_TRUE(f.engine.getTransport().setTimeSignature(3, 4));
        ASSERT_TRUE(f.engine.getTransport().play());

        const auto audio = f.driver->renderBlocks(kEightBeatBlocks);
        for (int downbeat : {0, 3, 6}) {
            const float downbeatPeak = peakNear(audio, downbeat);
            for (int beat = downbeat + 1; beat < downbeat + 3 && beat < 8; ++beat)
                EXPECT_GT(downbeatPeak, peakNear(audio, beat))
                    << "downbeat " << downbeat << " must measure louder than beat " << beat;
        }
    }
}

// ============================================================================
// 3. ClickSpansBlockBoundary
// ============================================================================

#endif // SYNTH_ENABLE_TIMELINE (engine-driven crossing/accent tests)

// A downbeat click 5 samples before a 512-sample block boundary must continue into the next call
// with no discontinuity — proven by comparing a two-call (block-split) render against a single
// uninterrupted render of the identical click, driving synth::Metronome directly (no engine needed:
// this is a pure function of the BlockTimeInfo sequence handed to it).
TEST(MetronomeTest, ClickSpansBlockBoundary) {
    constexpr double kBpm = 120.0;
    const double beatsPerSample = kBpm / (60.0 * kSampleRate);
    constexpr int kCrossingOffset = 512 - 5; // beat 0 lands 5 samples before the block ends

    const double startPpq = -(double)kCrossingOffset * beatsPerSample; // so beat 0 falls at offset 507

    // Two-call (split) render.
    Metronome split;
    split.setEnabled(true);
    juce::AudioBuffer<float> blockA(2, 512);
    blockA.clear();
    split.renderClicks(blockA, makeInfo(kSampleRate, kBpm, 4, 4, startPpq, 512));

    const double secondBlockStartPpq = startPpq + 512.0 * beatsPerSample;
    juce::AudioBuffer<float> blockB(2, 512);
    blockB.clear();
    split.renderClicks(blockB, makeInfo(kSampleRate, kBpm, 4, 4, secondBlockStartPpq, 512));

    // Single-call (uninterrupted) reference render of the SAME click, spanning both blocks.
    Metronome reference;
    reference.setEnabled(true);
    juce::AudioBuffer<float> refBlock(2, 1024);
    refBlock.clear();
    reference.renderClicks(refBlock, makeInfo(kSampleRate, kBpm, 4, 4, startPpq, 1024));

    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(blockA.getSample(0, i), refBlock.getSample(0, i)) << "first block, sample " << i;
        EXPECT_FLOAT_EQ(blockA.getSample(1, i), refBlock.getSample(1, i)) << "first block (ch1), sample " << i;
    }
    for (int i = 0; i < 512; ++i) {
        EXPECT_FLOAT_EQ(blockB.getSample(0, i), refBlock.getSample(0, 512 + i)) << "second block, sample " << i;
        EXPECT_FLOAT_EQ(blockB.getSample(1, i), refBlock.getSample(1, 512 + i)) << "second block (ch1), sample " << i;
    }

    // Sanity: the click must actually have fired (not a false-positive all-zero comparison).
    EXPECT_GT(std::abs(blockA.getSample(0, kCrossingOffset + 1)), kOnsetThreshold);
}

// ============================================================================
// 4. LoopWrapClicks
// ============================================================================

// Engine-driven like sections 1-2: compiled out with the flag.
#if SYNTH_ENABLE_TIMELINE

TEST(MetronomeTest, LoopWrapClicks) {
    Fixture f;
    f.build();
    f.engine.getMetronome().setEnabled(true);

    ASSERT_TRUE(f.engine.getTransport().setLoop(0.0, 2.0, true));
    ASSERT_TRUE(f.engine.getTransport().play());

    struct WrapCapture {
        int offset;
        juce::AudioBuffer<float> block;
    };
    std::vector<WrapCapture> wraps;

    f.driver->streamBlocks(400, [&](const juce::AudioBuffer<float>& block, const BlockTimeInfo& info) {
        if (info.loopWrapSample >= 0 && wraps.size() < 3)
            wraps.push_back({info.loopWrapSample, juce::AudioBuffer<float>(block)});
    });

    ASSERT_GE(wraps.size(), 2u) << "loop [0,2) must wrap more than once in 400 blocks at 120 BPM/48 kHz";
    for (const auto& wrap : wraps) {
        ASSERT_GE(wrap.offset, 0);
        ASSERT_LT(wrap.offset + 1, wrap.block.getNumSamples());
        EXPECT_NEAR(wrap.block.getSample(0, wrap.offset), 0.0f, 1.0e-6f)
            << "the wrapped range's beat-0 click starts at phase 0, exactly at the wrap offset";
        EXPECT_GT(std::abs(wrap.block.getSample(0, wrap.offset + 1)), kOnsetThreshold)
            << "the wrapped range's beat-0 click must fire right at the wrap offset, every pass";
    }
}

#endif // SYNTH_ENABLE_TIMELINE (LoopWrapClicks)

// ============================================================================
// 5. NeverInABounce
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

namespace {

struct ScopedTempFile {
    explicit ScopedTempFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedTempFile() { file.deleteFile(); }
    juce::File file;
};

/** Peak absolute magnitude across the whole file, or -1.0f if it could not be opened/read. */
float peakMagnitudeInWav(const juce::File& file) {
    auto input = file.createInputStream();
    if (input == nullptr)
        return -1.0f;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(input.release(), /*deleteStreamIfOpeningFails=*/true));
    if (reader == nullptr)
        return -1.0f;

    juce::AudioBuffer<float> audio((int)reader->numChannels, (int)reader->lengthInSamples);
    audio.clear();
    reader->read(&audio, 0, (int)reader->lengthInSamples, 0, true, true);

    float peak = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        peak = std::max(peak, audio.getMagnitude(channel, 0, audio.getNumSamples()));
    return peak;
}

synth::BounceOptions defaultBounceOptions() {
    synth::BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 4.0;
    options.tailSeconds = 0.0;
    options.sampleRate = kSampleRate;
    options.blockSize = kBlockSize;
    options.bitDepth = 24;
    options.numChannels = kNumChannels;
    return options;
}

} // namespace

TEST(MetronomeTest, NeverInABounce) {
    Fixture f;
    f.build();
    f.engine.getMetronome().setEnabled(true);

    ScopedTempFile out("agentsynth_metronome_bounce.wav");
    const auto options = defaultBounceOptions();

    const auto result = synth::BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;

    const float peak = peakMagnitudeInWav(out.file);
    ASSERT_GE(peak, 0.0f) << "the bounce must have opened and read back";
    EXPECT_LT(peak, kSilenceThreshold)
        << "a bounce of a silent graph must be silent even with the metronome enabled, peak=" << peak;

    EXPECT_TRUE(f.engine.getMetronome().isEnabled())
        << "the force-off guard must restore the user's setting after the bounce";
    EXPECT_FALSE(f.engine.getMetronome().isForcedOn());

    // A cancelled bounce restores too — the guard is RAII, unwound on every return path.
    f.engine.getMetronome().setForcedOn(true);
    int calls = 0;
    const auto cancelled = synth::BounceExporter::bounce(f.engine, out.file, options, [&calls](double) {
        ++calls;
        return false; // cancel on the very first progress report
    });
    EXPECT_FALSE(cancelled.ok);
    EXPECT_TRUE(f.engine.getMetronome().isEnabled());
    EXPECT_TRUE(f.engine.getMetronome().isForcedOn());
}

#endif // SYNTH_ENABLE_TIMELINE

// ============================================================================
// 6. MasterMuteKillsClick
// ============================================================================

TEST(MetronomeTest, MasterMuteKillsClick) {
    Fixture f;
    f.build();
    f.engine.getMetronome().setEnabled(true);
    f.engine.setMasterMute(true);
    ASSERT_TRUE(f.engine.getTransport().play());

    // Spans two beat crossings (0 and 1) — master mute must kill both.
    const auto audio = f.driver->renderBlocks(60);
    EXPECT_TRUE(TestAudioHelpers::isSilent(audio, 0)) << "master mute must clear the click along with everything else";
    EXPECT_TRUE(TestAudioHelpers::isSilent(audio, 1));
}

// ============================================================================
// 9. DisabledMetronomeIsSilentAndFree
// ============================================================================

TEST(MetronomeTest, DisabledMetronomeIsSilentAndFree) {
    Metronome metronome; // disabled by default; setForcedOn is also never called here
    EXPECT_EQ(metronome.getActiveVoiceCountForTest(), 0);

    juce::AudioBuffer<float> buffer(2, 512);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(channel, i, 0.1234f); // a known non-zero pattern

    const juce::AudioBuffer<float> before(buffer); // deep copy for comparison

    const auto info = makeInfo(kSampleRate, 120.0, 4, 4, 0.0, 512);
    metronome.renderClicks(buffer, info);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 512; ++i)
            EXPECT_FLOAT_EQ(buffer.getSample(channel, i), before.getSample(channel, i))
                << "a disabled metronome must not touch the buffer at all, sample " << i;

    EXPECT_EQ(metronome.getActiveVoiceCountForTest(), 0) << "the crossing scan must not run while disabled";
}

// ============================================================================
// 7/8. Count-in choreography — MainComponent-level.
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

namespace {

class MockProviderMetronome : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMetronome"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }

private:
    juce::String model = "MockModel";
};

} // namespace

class MetronomeCountInTest : public ::testing::Test {
protected:
    // Same hermetic reset as TimelineTransportBarTests.cpp/TimelinePanelTests.cpp: MainComponent's
    // delegating ctor reads the shared on-disk "Agent Synth" properties.
    void resetPanelKeys() {
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

    void SetUp() override { resetPanelKeys(); }
    void TearDown() override { resetPanelKeys(); }

    // Nothing here wants a real device clocking the graph: the transport is driven by hand (like
    // MidiRecorderTests.cpp's Harness / TimelineTransportBarTests.cpp's app-wiring tests) after
    // being re-prepared at this file's own fixed sample rate/block size.
    static void quiesceEngine(MainComponent& mc) {
        mc.getAudioEngine().suspendDeviceCallback();
        mc.getAudioEngine().getTransport().prepare(kSampleRate, kBlockSize);
    }
};

// ---- 7. CountInPreRollNotRecorded ----

TEST_F(MetronomeCountInTest, CountInPreRollNotRecorded) {
    MainComponent mc(std::make_unique<MockProviderMetronome>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& transport = mc.getAudioEngine().getTransport();
    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    ASSERT_TRUE(doc.setTrackArmed(trackId, true));

    auto& bar = mc.getTimelinePanel().getTransportBar();
    auto& recorder = mc.getMidiRecorderForTest();
    auto& metronome = mc.getAudioEngine().getMetronome();

    bar.getCountInCombo().setSelectedId(2, juce::sendNotificationSync); // "1 bar"
    ASSERT_EQ(bar.getCountInBars(), 1);

    ASSERT_TRUE(transport.locateBeat(4.0));
    transport.tick(kBlockSize); // drains the locate

    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());
    EXPECT_TRUE(metronome.isForcedOn()) << "the pre-roll must force the click on regardless of the user toggle";

    transport.tick(kBlockSize); // drains the pre-roll's locateBeat(0) + play() — this IS block [0, 512)
    {
        const auto snap = transport.getPositionSnapshot();
        EXPECT_NEAR(snap.ppq, 0.0, 1.0e-9) << "1 bar of 4/4 count-in from beat 4 must locate to beat 0 exactly";
        ASSERT_TRUE(snap.playing);
    }
    EXPECT_NEAR(recorder.getPunchInBeat(), 4.0, 1.0e-9) << "the punch-in must be exactly where record was engaged";

    struct Injection {
        std::int64_t sample;
        int pitch;
        bool expectRecorded;
    };
    const std::vector<Injection> injections = {
        {1 * (std::int64_t)kSamplesPerBeat, 61, false},
        {2 * (std::int64_t)kSamplesPerBeat, 62, false},
        {3 * (std::int64_t)kSamplesPerBeat, 63, false},
        {4 * (std::int64_t)kSamplesPerBeat - 500, 64, false}, // just before the punch: still pre-roll
        {4 * (std::int64_t)kSamplesPerBeat + 500, 70, true},  // just after the punch: recorded
        {5 * (std::int64_t)kSamplesPerBeat, 75, true},
    };

    // Block [0, 512) was already consumed by the drain tick above; the next tick() call renders
    // block [512, 1024), so the loop's own bookkeeping starts there.
    std::int64_t absoluteSample = kBlockSize;
    const std::int64_t totalSamples = 6 * (std::int64_t)kSamplesPerBeat;
    std::size_t nextInjection = 0;
    bool sawForcedOnDuringPreRoll = false;
    bool clearedForcedOnPastPunch = false;

    while (absoluteSample < totalSamples) {
        juce::MidiBuffer midi;
        while (nextInjection < injections.size() && injections[nextInjection].sample >= absoluteSample &&
               injections[nextInjection].sample < absoluteSample + kBlockSize) {
            midi.addEvent(juce::MidiMessage::noteOn(1, injections[nextInjection].pitch, (juce::uint8)100),
                          (int)(injections[nextInjection].sample - absoluteSample));
            ++nextInjection;
        }

        const auto& info = transport.tick(kBlockSize);
        recorder.captureBlock(midi, info);

        if (!sawForcedOnDuringPreRoll && absoluteSample >= 2 * (std::int64_t)kSamplesPerBeat) {
            mc.timerCallback();
            EXPECT_TRUE(metronome.isForcedOn()) << "must stay forced on for the whole pre-roll";
            sawForcedOnDuringPreRoll = true;
        }
        if (!clearedForcedOnPastPunch && absoluteSample >= 4 * (std::int64_t)kSamplesPerBeat + 1000) {
            mc.timerCallback();
            EXPECT_FALSE(metronome.isForcedOn()) << "the poll must clear forced-on once past the punch-in";
            clearedForcedOnPastPunch = true;
        }

        absoluteSample += kBlockSize;
    }
    ASSERT_EQ(nextInjection, injections.size()) << "test setup: every injection must land inside a rendered block";
    EXPECT_TRUE(sawForcedOnDuringPreRoll);
    EXPECT_TRUE(clearedForcedOnPastPunch);

    bar.getRecordButton().onClick(); // stop + commit
    EXPECT_FALSE(bar.isRecordingForTest());
    EXPECT_FALSE(metronome.isForcedOn()) << "commit must clear forced-on unconditionally";

    const auto* trackPtr = doc.getTrack(trackId);
    ASSERT_NE(trackPtr, nullptr);
    ASSERT_EQ(trackPtr->clips.size(), 1u);
    const auto& notes = trackPtr->clips[0].notes;

    auto hasPitch = [&](int pitch) {
        for (const auto& note : notes)
            if (note.pitch == pitch)
                return true;
        return false;
    };

    for (const auto& injection : injections) {
        EXPECT_EQ(hasPitch(injection.pitch), injection.expectRecorded)
            << "pitch " << injection.pitch << ": recorded=" << hasPitch(injection.pitch)
            << " expected=" << injection.expectRecorded;
    }
}

// ---- 8. RecordWhilePlayingSkipsCountIn ----

TEST_F(MetronomeCountInTest, RecordWhilePlayingSkipsCountIn) {
    MainComponent mc(std::make_unique<MockProviderMetronome>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& transport = mc.getAudioEngine().getTransport();
    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    ASSERT_TRUE(doc.setTrackArmed(trackId, true));

    auto& bar = mc.getTimelinePanel().getTransportBar();
    auto& recorder = mc.getMidiRecorderForTest();
    auto& metronome = mc.getAudioEngine().getMetronome();

    bar.getCountInCombo().setSelectedId(2, juce::sendNotificationSync); // "1 bar" — must still be skipped
    ASSERT_EQ(bar.getCountInBars(), 1);

    ASSERT_TRUE(transport.locateBeat(4.0));
    ASSERT_TRUE(transport.play());
    transport.tick(kBlockSize); // drains the locate + play; genuinely playing from here on

    const auto beforeRecord = transport.getPositionSnapshot();
    ASSERT_TRUE(beforeRecord.playing);

    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());
    EXPECT_FALSE(metronome.isForcedOn()) << "record engaged while already playing must not force the click on";

    const auto afterRecord = transport.getPositionSnapshot();
    EXPECT_NEAR(afterRecord.ppq, beforeRecord.ppq, 1.0e-9) << "no pre-roll: the transport must not be relocated";
    EXPECT_NEAR(recorder.getPunchInBeat(), beforeRecord.ppq, 1.0e-6)
        << "punch-in must be the CURRENT position, not a pre-roll target";

    bar.getRecordButton().onClick(); // stop + commit
    EXPECT_FALSE(bar.isRecordingForTest());
    EXPECT_FALSE(metronome.isForcedOn());
}

#endif // SYNTH_ENABLE_TIMELINE
