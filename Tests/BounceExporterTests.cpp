// synth::BounceExporter — offline bounce/export of a beat range to a WAV file, rendered
// faster than realtime through the same graph the app plays through.
//
// Everything here asserts on the FILE, not on anything inside the exporter: it is opened with
// juce::WavAudioFormat's own reader, and the audio is measured with the same RMS windows and guard
// bands TimelineE2ETests uses on the in-memory render. If a bounce and a playback of the same range
// ever stop agreeing, one of those two files goes red.
//
// Headless/deterministic house rules apply: HostMode::Hosted only, no audio device, no sleeps.
// Timing arithmetic: 48 kHz, 512-sample blocks, 120 BPM => 24000 samples/beat, so 8 beats is
// exactly 192000 samples = 375 whole blocks and the range render lands on a block boundary with no
// overshoot to account for.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Transport/BounceExporter.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "TestAudioHelpers.h"
#include "Timeline/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include <cmath>
#include <gtest/gtest.h>
#include <initializer_list>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

#if SYNTH_ENABLE_TIMELINE

using synth::BounceExporter;
using synth::BounceOptions;
using synth::BounceResult;
using synth::MidiNote;
using synth::TimelineDoc;
using synth::TimelineSnapshot;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 2;
constexpr double kSamplesPerBeat = 24000.0; // 120 BPM at 48 kHz
constexpr int kBeatSamples = 24000;
constexpr int kEightBeatBlocks = 375; // 8 beats == 192000 samples == 375 * 512, exactly
constexpr int kEightBeatSamples = kEightBeatBlocks * kBlockSize;
static_assert(kEightBeatSamples == 8 * kBeatSamples, "8 beats must be a whole number of blocks");

// Same guard bands and thresholds as TimelineE2ETests — a bounce that measures differently from a
// playback of the same range is the regression this file exists to catch.
constexpr int kNoteGuard = 2400;
constexpr int kGapStartGuard = 4800;
constexpr int kGapEndGuard = 2400;
constexpr float kEnergyThreshold = 0.02f;
constexpr float kSilenceThreshold = 1.0e-3f;

constexpr std::initializer_list<double> kNoteBeats = {0.0, 2.0, 4.0, 6.0};
constexpr std::initializer_list<double> kGapBeats = {1.0, 3.0, 5.0, 7.0};

constexpr const char* kTrackInUuid = "b0000000-0000-0000-0000-000000000001";

int sampleAt(double beat) { return (int)std::llround(beat * kSamplesPerBeat); }

// ---------------------------------------------------------------------------
// The render patch. Deliberately the same chain TimelineE2ETests renders — clip -> Track In ->
// Poly MIDI -> Oscillator(poly)/VCA(poly) -> Audio Output — duplicated rather than shared: that
// file is the scheduling regression net and its fixture has to stay readable on its own, while
// this one needs an optional FX insert the net does not want. The one difference is `withDelayTail`,
// which splices a fully-wet Delay between the VCA and the output so there is something to ring out
// past the end of the range.
// ---------------------------------------------------------------------------
juce::String buildPatchJson(bool withDelayTail) {
    const juce::String delayNode = withDelayTail ? juce::String(R"(,
            {"id": 6, "type": "Delay",        "uuid": "b0000000-0000-0000-0000-000000000006",
             "params": {"time": 125.0, "feedback": 0.75, "mix": 1.0}})")
                                                 : juce::String();

    // 125 ms taps at 0.75 feedback: the tail is unmistakably present 0.5 s after the last note-off
    // (0.75^4 ~= 0.32 of the note) and unmistakably quieter a second later (0.75^12 ~= 0.03).
    const juce::String outputWiring = withDelayTail ? juce::String(R"(
            {"src": 4, "srcPort": 0,  "dst": 6, "dstPort": 0},
            {"src": 4, "srcPort": 1,  "dst": 6, "dstPort": 1},
            {"src": 6, "srcPort": 0,  "dst": 5, "dstPort": 0},
            {"src": 6, "srcPort": 1,  "dst": 5, "dstPort": 1})")
                                                    : juce::String(R"(
            {"src": 4, "srcPort": 0,  "dst": 5, "dstPort": 0},
            {"src": 4, "srcPort": 1,  "dst": 5, "dstPort": 1})");

    return juce::String(R"({
        "nodes": [
            {"id": 1, "type": "Track In",     "uuid": ")") +
           kTrackInUuid + R"("},
            {"id": 2, "type": "Poly MIDI",    "uuid": "b0000000-0000-0000-0000-000000000002"},
            {"id": 3, "type": "Oscillator",   "uuid": "b0000000-0000-0000-0000-000000000003",
             "params": {"poly": true, "waveform": "Sine", "level": 1.0}},
            {"id": 4, "type": "VCA",          "uuid": "b0000000-0000-0000-0000-000000000004",
             "params": {"poly": true, "gain": 1.0}},
            {"id": 5, "type": "Audio Output", "uuid": "b0000000-0000-0000-0000-000000000005"})" +
           delayNode + R"(
        ],
        "connections": [
            {"src": 1, "srcPort": -1, "dst": 2, "dstPort": -1},
            {"src": 2, "srcPort": 0,  "dst": 3, "dstPort": 0},
            {"src": 2, "srcPort": 8,  "dst": 4, "dstPort": 8},
            {"src": 3, "srcPort": 0,  "dst": 4, "dstPort": 0},)" +
           outputWiring + R"(
        ]
    })";
}

MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0, int velocity = 100, int channel = 1) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    TimelineDoc doc;
    synth::TrackId trackId;
    synth::ClipId clipId;

    // The fixture's own driver is what makes the engine live at 48 kHz / 512 before any bounce
    // happens — which is exactly the prepare state a bounce has to hand back afterwards.
    bool build(bool withDelayTail = false) {
        engine.initialise();

        const juce::var patch = juce::JSON::parse(buildPatchJson(withDelayTail));
        if (!patch.isObject())
            return false;
        if (!synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true,
                                                    /*trusted=*/true))
            return false;

        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, kNumChannels);

        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        if (!doc.setTrackBinding(trackId, kTrackInUuid))
            return false;
        clipId = doc.addClip(trackId, 0.0, 8.0, "Clip");
        return clipId.isValid();
    }

    // One note per even beat: [0,1), [2,3), [4,5), [6,7). Pitch 69 == A4.
    bool addStandardNotes() {
        bool ok = true;
        for (double start : kNoteBeats)
            ok = doc.addNote(clipId, makeNote(start, 69, 1.0, 100)).isValid() && ok;
        return ok;
    }

    void publish() { engine.getTimelineSnapshots().publish(TimelineSnapshot::buildFrom(doc)); }

    ~Fixture() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

/** A file in the temp directory that is gone before and after the test that owns it. */
struct ScopedTempFile {
    explicit ScopedTempFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedTempFile() { file.deleteFile(); }

    juce::File file;
};

BounceOptions defaultOptions() {
    BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 8.0;
    options.tailSeconds = 0.0;
    options.sampleRate = kSampleRate;
    options.blockSize = kBlockSize;
    options.bitDepth = 24;
    options.numChannels = kNumChannels;
    return options;
}

struct WavContents {
    bool ok = false;
    double sampleRate = 0.0;
    int bitsPerSample = 0;
    int numChannels = 0;
    bool usesFloatingPointData = false;
    juce::int64 lengthInSamples = 0;
    juce::AudioBuffer<float> audio;
};

/** Opens a bounce with JUCE's own WAV reader — the point of the assertion is that some other tool
 *  can read what we wrote, so nothing here shares code with the writer. */
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

    out.sampleRate = reader->sampleRate;
    out.bitsPerSample = (int)reader->bitsPerSample;
    out.numChannels = (int)reader->numChannels;
    out.usesFloatingPointData = reader->usesFloatingPointData;
    out.lengthInSamples = reader->lengthInSamples;

    out.audio.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
    out.audio.clear();
    reader->read(&out.audio, 0, (int)reader->lengthInSamples, 0, true, true);
    out.ok = true;
    return out;
}

} // namespace

// ============================================================================
// 1. The file exists, opens, and is exactly as long as the block arithmetic says
// ============================================================================

TEST(BounceExporterTest, BounceProducesParseableWavOfExactLength) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_length.wav");

    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions());
    ASSERT_TRUE(result.ok) << result.message;

    // The driver renders whole blocks and stops on the first one whose end position reaches the
    // target, so the length is ceil(sampleFromBeat(8) / blockSize) * blockSize. Eight beats divide
    // evenly here, so that is 375 * 512 with no overshoot.
    EXPECT_EQ(result.samplesWritten, (juce::int64)kEightBeatSamples);
    ASSERT_TRUE(out.file.existsAsFile());

    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok) << "the bounce must open with JUCE's own WAV reader";
    EXPECT_EQ(wav.lengthInSamples, (juce::int64)kEightBeatSamples);
    EXPECT_EQ(wav.sampleRate, kSampleRate);
    EXPECT_EQ(wav.bitsPerSample, 24);
    EXPECT_EQ(wav.numChannels, kNumChannels);
    EXPECT_FALSE(wav.usesFloatingPointData) << "24-bit must be integer PCM";
}

TEST(BounceExporterTest, FloatBitDepthWritesAFloatWav) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_float.wav");

    auto options = defaultOptions();
    options.bitDepth = 32;
    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;

    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok);
    EXPECT_EQ(wav.bitsPerSample, 32);
    EXPECT_TRUE(wav.usesFloatingPointData) << "bitDepth 32 means an IEEE-float WAV, not 32-bit int";
    EXPECT_EQ(wav.lengthInSamples, (juce::int64)kEightBeatSamples);
}

// ============================================================================
// 2. Energy exactly where the notes are, silence exactly where they aren't
// ============================================================================

TEST(BounceExporterTest, EnergyWhereNotesAre) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_energy.wav");

    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions());
    ASSERT_TRUE(result.ok) << result.message;

    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok);
    ASSERT_EQ(wav.lengthInSamples, (juce::int64)kEightBeatSamples);

    for (double beat : kNoteBeats) {
        const int start = sampleAt(beat);
        const int end = start + kBeatSamples;
        const float rms = TestAudioHelpers::computeRMSInRange(wav.audio, start + kNoteGuard, end - kNoteGuard, 0);
        EXPECT_GT(rms, kEnergyThreshold) << "bounced note at beat " << beat << " measured rms=" << rms;
    }
    for (double beat : kGapBeats) {
        const int start = sampleAt(beat);
        const int end = start + kBeatSamples;
        const float rms = TestAudioHelpers::computeRMSInRange(wav.audio, start + kGapStartGuard, end - kGapEndGuard, 0);
        EXPECT_LT(rms, kSilenceThreshold) << "bounced gap at beat " << beat << " measured rms=" << rms;
    }
}

// ============================================================================
// 3. The tail: FX ring out past the range, and only when asked for
// ============================================================================

TEST(BounceExporterTest, TailRingsOut) {
    Fixture f;
    ASSERT_TRUE(f.build(/*withDelayTail=*/true));
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile tailed("agentsynth_bounce_tail.wav");

    auto options = defaultOptions();
    options.tailSeconds = 1.0;
    const auto result = BounceExporter::bounce(f.engine, tailed.file, options);
    ASSERT_TRUE(result.ok) << result.message;

    // One second of tail is 48000 samples == 93.75 blocks, rounded up to 94 whole blocks.
    constexpr int kTailBlocks = 94;
    constexpr int kTailSamples = kTailBlocks * kBlockSize;
    ASSERT_EQ((int)std::ceil(1.0 * kSampleRate / (double)kBlockSize), kTailBlocks);

    const auto wav = readWav(tailed.file);
    ASSERT_TRUE(wav.ok);
    ASSERT_EQ(wav.lengthInSamples, (juce::int64)(kEightBeatSamples + kTailSamples));

    constexpr int kWindow = 4800; // 100 ms
    const float tailStartRms =
        TestAudioHelpers::computeRMSInRange(wav.audio, kEightBeatSamples, kEightBeatSamples + kWindow, 0);
    const float tailEndRms = TestAudioHelpers::computeRMSInRange(wav.audio, kEightBeatSamples + kTailSamples - kWindow,
                                                                 kEightBeatSamples + kTailSamples, 0);

    EXPECT_GT(tailStartRms, kSilenceThreshold)
        << "the tail must still be ringing where the range ended, rms=" << tailStartRms;
    EXPECT_GT(tailStartRms, tailEndRms) << "a tail decays: start rms=" << tailStartRms << ", end rms=" << tailEndRms;

    // …and with no tail asked for, the file stops dead at the range even though the same Delay is
    // still ringing at that instant.
    ScopedTempFile untailed("agentsynth_bounce_no_tail.wav");
    const auto noTail = BounceExporter::bounce(f.engine, untailed.file, defaultOptions());
    ASSERT_TRUE(noTail.ok) << noTail.message;
    EXPECT_EQ(noTail.samplesWritten, (juce::int64)kEightBeatSamples);

    const auto untailedWav = readWav(untailed.file);
    ASSERT_TRUE(untailedWav.ok);
    EXPECT_EQ(untailedWav.lengthInSamples, (juce::int64)kEightBeatSamples);
}

// ============================================================================
// 4. Two bounces of the same project are byte-identical
// ============================================================================
// Two fixtures, one bounce each: "the same project" means the same starting state, and a second
// bounce from a live engine inherits whatever DSP state the first left behind (free-running
// oscillator phase, an un-decayed delay line) because prepareToPlay resets rates and ramps, not
// phase. Nothing on the render path reads the wall clock, so same input in, same bytes out.

TEST(BounceExporterTest, DeterministicByteIdentical) {
    ScopedTempFile first("agentsynth_bounce_det_a.wav");
    ScopedTempFile second("agentsynth_bounce_det_b.wav");

    {
        Fixture f;
        ASSERT_TRUE(f.build());
        ASSERT_TRUE(f.addStandardNotes());
        f.publish();
        const auto result = BounceExporter::bounce(f.engine, first.file, defaultOptions());
        ASSERT_TRUE(result.ok) << result.message;
    }
    {
        Fixture f;
        ASSERT_TRUE(f.build());
        ASSERT_TRUE(f.addStandardNotes());
        f.publish();
        const auto result = BounceExporter::bounce(f.engine, second.file, defaultOptions());
        ASSERT_TRUE(result.ok) << result.message;
    }

    juce::MemoryBlock firstBytes, secondBytes;
    ASSERT_TRUE(first.file.loadFileAsData(firstBytes));
    ASSERT_TRUE(second.file.loadFileAsData(secondBytes));
    EXPECT_GT(firstBytes.getSize(), (size_t)0);
    EXPECT_TRUE(firstBytes == secondBytes) << "two bounces of the same project must be byte-identical";
}

// ============================================================================
// 5. The transport (and the engine) are handed back exactly as they were found
// ============================================================================

TEST(BounceExporterTest, TransportRestoredAfterBounce) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    auto& transport = f.engine.getTransport();
    ASSERT_TRUE(transport.locateBeat(3.0));
    ASSERT_TRUE(transport.play());
    f.driver->renderBlocks(1); // one tick is what makes the queued locate/play real

    const auto beforeBounce = transport.getPositionSnapshot();
    ASSERT_NEAR(beforeBounce.ppq, 3.0, 1e-12);
    ASSERT_TRUE(beforeBounce.playing);

    ScopedTempFile out("agentsynth_bounce_restore.wav");
    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions());
    ASSERT_TRUE(result.ok) << result.message;

    const auto afterBounce = transport.getPositionSnapshot();
    EXPECT_NEAR(afterBounce.ppq, 3.0, 1e-12) << "a bounce leaves the playhead where it found it";
    EXPECT_FALSE(afterBounce.playing) << "…and leaves it stopped, the way every DAW does";
    EXPECT_EQ(afterBounce.sampleRate, kSampleRate) << "the engine must be back on its previous render format";

    // The graph, the published snapshot and the Track In binding all survived the round trip:
    // playing the clip again still makes the note at beat 0 audible.
    ASSERT_TRUE(transport.locateBeat(0.0));
    ASSERT_TRUE(transport.play());
    const auto live = f.driver->renderBlocks(kBeatSamples / kBlockSize);
    EXPECT_GT(TestAudioHelpers::computeRMS(live, 0), kEnergyThreshold)
        << "Track In must still emit after the engine has been round-tripped through a bounce";
}

// ============================================================================
// 6. Cancelling leaves nothing behind
// ============================================================================

TEST(BounceExporterTest, CancellationDeletesPartialFile) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_cancel.wav");

    int calls = 0;
    const auto result = BounceExporter::bounce(f.engine, out.file, defaultOptions(), [&calls](double) {
        ++calls;
        return calls < 5; // cancel a handful of blocks in, long before the range ends
    });

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.containsIgnoreCase("cancel")) << result.message;
    EXPECT_FALSE(out.file.existsAsFile()) << "a cancelled bounce must not leave a truncated file";
    EXPECT_LT(result.samplesWritten, (juce::int64)kEightBeatSamples);

    // …and no temp file either: the sibling juce::TemporaryFile goes with the exporter's scope.
    const auto strays = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .findChildFiles(juce::File::findFiles, false, "agentsynth_bounce_cancel_temp*");
    EXPECT_TRUE(strays.isEmpty()) << "a cancelled bounce left " << strays.size() << " temp file(s) behind";

    // The engine is still usable and the transport is back where it started.
    const auto after = f.engine.getTransport().getPositionSnapshot();
    EXPECT_NEAR(after.ppq, 0.0, 1e-12);
    EXPECT_FALSE(after.playing);
}

// ============================================================================
// 7. An impossible destination fails before anything is rendered, and restores
// ============================================================================

TEST(BounceExporterTest, UnwritablePathFailsCleanly) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    const auto missingDirectory =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_bounce_no_such_directory");
    ASSERT_FALSE(missingDirectory.exists()) << "this test needs a directory that genuinely isn't there";
    const auto target = missingDirectory.getChildFile("nope.wav");

    const auto result = BounceExporter::bounce(f.engine, target, defaultOptions());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.isNotEmpty()) << "a failure must say why";
    EXPECT_EQ(result.samplesWritten, (juce::int64)0);
    EXPECT_FALSE(target.existsAsFile());
    EXPECT_FALSE(missingDirectory.exists()) << "a failed bounce must not create directories on its way out";

    // The engine survived the failure: same prepare format, still renders.
    const auto after = f.engine.getTransport().getPositionSnapshot();
    EXPECT_FALSE(after.playing);
    EXPECT_EQ(after.sampleRate, kSampleRate);

    auto& transport = f.engine.getTransport();
    ASSERT_TRUE(transport.locateBeat(0.0));
    ASSERT_TRUE(transport.play());
    const auto live = f.driver->renderBlocks(kBeatSamples / kBlockSize);
    EXPECT_GT(TestAudioHelpers::computeRMS(live, 0), kEnergyThreshold);
}

TEST(BounceExporterTest, InvalidOptionsAreRejectedBeforeAnythingIsWritten) {
    Fixture f;
    ASSERT_TRUE(f.build());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_invalid.wav");

    auto backwards = defaultOptions();
    backwards.endBeat = backwards.startBeat;
    EXPECT_FALSE(BounceExporter::bounce(f.engine, out.file, backwards).ok);

    auto badDepth = defaultOptions();
    badDepth.bitDepth = 20;
    EXPECT_FALSE(BounceExporter::bounce(f.engine, out.file, badDepth).ok);

    auto badRate = defaultOptions();
    badRate.sampleRate = 0.0;
    EXPECT_FALSE(BounceExporter::bounce(f.engine, out.file, badRate).ok);

    EXPECT_FALSE(out.file.existsAsFile());
}

// ============================================================================
// 8. Progress is monotonic and finishes at 1.0
// ============================================================================

TEST(BounceExporterTest, ProgressReachesOne) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.addStandardNotes());
    f.publish();

    ScopedTempFile out("agentsynth_bounce_progress.wav");

    auto options = defaultOptions();
    options.tailSeconds = 0.5; // so the fraction has to account for both phases

    std::vector<double> reported;
    const auto result = BounceExporter::bounce(f.engine, out.file, options, [&reported](double fraction) {
        reported.push_back(fraction);
        return true;
    });
    ASSERT_TRUE(result.ok) << result.message;

    ASSERT_FALSE(reported.empty());
    EXPECT_GT(reported.front(), 0.0);
    for (std::size_t i = 1; i < reported.size(); ++i)
        EXPECT_GE(reported[i], reported[i - 1]) << "progress went backwards at report " << i;
    for (double fraction : reported)
        EXPECT_LE(fraction, 1.0);
    EXPECT_NEAR(reported.back(), 1.0, 1e-9);

    // 8 beats (375 blocks) + 0.5 s of tail (ceil(24000/512) == 47 blocks).
    EXPECT_EQ(result.samplesWritten, (juce::int64)(kEightBeatBlocks + 47) * kBlockSize);
}

#endif // SYNTH_ENABLE_TIMELINE
