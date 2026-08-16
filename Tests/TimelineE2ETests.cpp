// The headless end-to-end regression net for the whole timeline scheduling path — a MIDI
// clip on a track, through Track In, through Poly MIDI, into an audible chain, out of the graph's
// Audio Output node. Every later scheduling change (loop wrap, tempo change, clip editing, …) has
// to keep tripping this file's assertions, so it renders through synth::OfflineTransportDriver
// exactly the way a real host would, and asserts on the RENDERED AUDIO — never on anything inside
// a module.
//
// Wiring (one voice's worth of PresetManager::getPresetJSON's "Poly Pad" preset, case 6):
//   Track In --MIDI--> Poly MIDI --pitch ch0--> Oscillator (poly, voice 0 pitch input)
//                       Poly MIDI --gate  ch8--> VCA (poly, voice 0 CV input)
//                       Oscillator audio ch0   --> VCA (poly, voice 0 audio input)
//                       VCA ch0/ch1 (poly sums all voices to stereo) --> Audio Output
// No Filter, no Amp Env: this net exists to catch a SCHEDULING regression (a note firing on the
// wrong sample), and an ADSR's own release tail would be a second envelope on top of PolyMidi's 5
// ms gate smoothing, blurring exactly the edge this file has to pin. The Oscillator and VCA are
// both put in poly mode so their per-voice pitch/CV inputs are live (mono mode ignores pitch CV
// entirely — see OscillatorModule::processMonoMode); only voice 0 is ever driven, and the poly fan
// itself is PolyMidiModuleTest's concern, not this file's.
//
// Timing arithmetic used throughout: 48 kHz, 512-sample blocks, 120 BPM => 24000 samples/beat, so
// 8 beats is exactly 192000 samples = 375 whole blocks (no renderToBeat overshoot to account for).
//
// Headless/deterministic house rules apply: HostMode::Hosted only, no audio device, no sleeps.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/TimelineMidiSourceModule.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "TestAudioHelpers.h"
#include "Timeline/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include <cmath>
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>

#if SYNTH_ENABLE_TIMELINE

using synth::MidiNote;
using synth::TimelineDoc;
using synth::TimelineSnapshot;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kSamplesPerBeat = 24000.0; // 120 BPM at 48 kHz
constexpr int kBeatSamples = 24000;
constexpr int kEightBeatBlocks = 375; // 8 beats == 192000 samples == 375 * 512, exactly
static_assert(kEightBeatBlocks * kBlockSize == 8 * kBeatSamples, "8 beats must be a whole number of blocks");

constexpr const char* kTrackInUuid = "e2e00000-0000-0000-0000-000000000001";

// The guard bands the task pins: do not loosen these. A one-block (512-sample) scheduling
// regression must still be caught inside either margin.
constexpr int kNoteGuard = 2400;     // both edges of a note window
constexpr int kGapStartGuard = 4800; // generous — this is where a release tail would land
constexpr int kGapEndGuard = 2400;   // tight — nothing has attacked yet at this edge

// Empirically measured: during a held note, VCA poly mode computes tanh(audio * gain * cv / 8)
// with gain == cv == 1 and a unity sine, so RMS settles near 1/(8*sqrt2) ~= 0.088 (measured:
// 0.0880); true silence (gate fully closed by the guard band) measured bit-exact 0.0f. Both
// margins below leave roughly an order of magnitude either side of the measured values.
constexpr float kEnergyThreshold = 0.02f;
constexpr float kSilenceThreshold = 1.0e-3f;

int sampleAt(double beat) { return (int)std::llround(beat * kSamplesPerBeat); }

MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0, int velocity = 100, int channel = 1) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

juce::String buildPatchJson(const juce::String& trackInUuid) {
    return juce::String(R"({
        "nodes": [
            {"id": 1, "type": "Track In",     "uuid": ")") +
           trackInUuid + R"("},
            {"id": 2, "type": "Poly MIDI",    "uuid": "e2e00000-0000-0000-0000-000000000002"},
            {"id": 3, "type": "Oscillator",   "uuid": "e2e00000-0000-0000-0000-000000000003",
             "params": {"poly": true, "waveform": "Sine", "level": 1.0}},
            {"id": 4, "type": "VCA",          "uuid": "e2e00000-0000-0000-0000-000000000004",
             "params": {"poly": true, "gain": 1.0}},
            {"id": 5, "type": "Audio Output", "uuid": "e2e00000-0000-0000-0000-000000000005"}
        ],
        "connections": [
            {"src": 1, "srcPort": -1, "dst": 2, "dstPort": -1},
            {"src": 2, "srcPort": 0,  "dst": 3, "dstPort": 0},
            {"src": 2, "srcPort": 8,  "dst": 4, "dstPort": 8},
            {"src": 3, "srcPort": 0,  "dst": 4, "dstPort": 0},
            {"src": 4, "srcPort": 0,  "dst": 5, "dstPort": 0},
            {"src": 4, "srcPort": 1,  "dst": 5, "dstPort": 1}
        ]
    })";
}

// Builds the graph (clearing whatever initialise() left), then the driver (which prepareForHost's
// it), then an empty one-track/one-clip TimelineDoc bound to the Track In node. Each test adds its
// own notes to `doc` and calls publish() before playing.
struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    TimelineDoc doc;
    synth::TrackId trackId;
    synth::ClipId clipId;

    bool build(double clipLengthBeats = 8.0) {
        engine.initialise();

        const juce::var patch = juce::JSON::parse(buildPatchJson(kTrackInUuid));
        if (!patch.isObject())
            return false;
        if (!synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true,
                                                    /*trusted=*/true))
            return false;

        // Must be constructed after the graph is built: its constructor calls prepareForHost,
        // which prepares the nodes just created.
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, 2);

        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        if (!doc.setTrackBinding(trackId, kTrackInUuid))
            return false;
        clipId = doc.addClip(trackId, 0.0, clipLengthBeats, "Clip");
        return clipId.isValid();
    }

    void publish() { engine.getTimelineSnapshots().publish(TimelineSnapshot::buildFrom(doc)); }

    TimelineMidiSourceModule* findTrackIn() {
        for (auto* node : engine.getGraph().getNodes())
            if (auto* t = dynamic_cast<TimelineMidiSourceModule*>(node->getProcessor()))
                return t;
        return nullptr;
    }

    ~Fixture() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

// One note per even beat across an 8-beat clip: [0,1), [2,3), [4,5), [6,7). Pitch 69 == A4 ==
// 440 Hz. Gaps are the complementary beats: [1,2), [3,4), [5,6), [7,8).
bool addStandardNotes(Fixture& f) {
    bool ok = true;
    for (double start : {0.0, 2.0, 4.0, 6.0})
        ok = f.doc.addNote(f.clipId, makeNote(start, 69, 1.0, 100)).isValid() && ok;
    return ok;
}

constexpr std::initializer_list<double> kNoteBeats = {0.0, 2.0, 4.0, 6.0};
constexpr std::initializer_list<double> kGapBeats = {1.0, 3.0, 5.0, 7.0};

} // namespace

// ============================================================================
// 1. Energy exactly where the notes are, silence exactly where they aren't
// ============================================================================

TEST(TimelineE2ETest, EnergyExactlyWhereNotesAre) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(addStandardNotes(f));
    f.publish();
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.driver->renderToBeat(8.0);
    ASSERT_EQ(rendered.getNumSamples(), kEightBeatBlocks * kBlockSize);

    for (double beat : kNoteBeats) {
        const int start = sampleAt(beat);
        const int end = start + kBeatSamples;
        const float rms = TestAudioHelpers::computeRMSInRange(rendered, start + kNoteGuard, end - kNoteGuard, 0);
        EXPECT_GT(rms, kEnergyThreshold) << "note at beat " << beat << " measured rms=" << rms;
    }
    for (double beat : kGapBeats) {
        const int start = sampleAt(beat);
        const int end = start + kBeatSamples;
        const float rms = TestAudioHelpers::computeRMSInRange(rendered, start + kGapStartGuard, end - kGapEndGuard, 0);
        EXPECT_LT(rms, kSilenceThreshold) << "gap at beat " << beat << " measured rms=" << rms;
    }
}

// ============================================================================
// 2. A transport that never plays produces nothing, ever
// ============================================================================

TEST(TimelineE2ETest, SilenceWhenStopped) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(addStandardNotes(f));
    f.publish();
    // No play(): renderToBeat would bail out immediately (a stopped transport reaches no beat), so
    // this uses renderBlocks the way OfflineTransportDriverTest::StoppedTransportStillRendersGraphAudio
    // does — the graph still runs every block, it just never gets a note.
    ASSERT_FALSE(f.driver->getTransport().getPositionSnapshot().playing);

    const auto rendered = f.driver->renderBlocks(kEightBeatBlocks);
    ASSERT_EQ(rendered.getNumSamples(), kEightBeatBlocks * kBlockSize);
    EXPECT_TRUE(TestAudioHelpers::isSilent(rendered, 0, kSilenceThreshold));
    EXPECT_TRUE(TestAudioHelpers::isSilent(rendered, 1, kSilenceThreshold));
}

// ============================================================================
// 3. Looping repeats the pattern every pass
// ============================================================================

TEST(TimelineE2ETest, LoopedRenderRepeatsEnergyPattern) {
    Fixture f;
    ASSERT_TRUE(f.build());
    // Only the note at beat 0 is ever reachable once the transport is confined to [0, 2) — notes at
    // 2/4/6 are outside the loop and this document does not need them, but adding the standard set
    // exercises the same clip a non-looped test would use.
    ASSERT_TRUE(addStandardNotes(f));
    f.publish();

    ASSERT_TRUE(f.driver->getTransport().setLoop(0.0, 2.0, true));
    ASSERT_TRUE(f.driver->getTransport().play());

    // setLoop wraps ppq back toward 0 every pass, so renderToBeat's "endPpq >= target" never fires —
    // renderBlocks is the right tool here, same as the loop-wrap tests in TimelineMidiSourceTests.
    const auto rendered = f.driver->renderBlocks(kEightBeatBlocks);
    ASSERT_EQ(rendered.getNumSamples(), kEightBeatBlocks * kBlockSize);

    constexpr int kLoopSamples = 2 * kBeatSamples; // 48000: one pass of the [0, 2) loop
    constexpr int kNumPasses = 4;                  // 192000 / 48000
    static_assert(kNumPasses * kLoopSamples == kEightBeatBlocks * kBlockSize, "four whole passes expected");

    for (int pass = 0; pass < kNumPasses; ++pass) {
        const int passStart = pass * kLoopSamples;
        // The note: loop-relative beat [0, 1) of this pass.
        const float noteRms = TestAudioHelpers::computeRMSInRange(rendered, passStart + kNoteGuard,
                                                                  passStart + kBeatSamples - kNoteGuard, 0);
        EXPECT_GT(noteRms, kEnergyThreshold) << "pass " << pass << " note window, rms=" << noteRms;

        // The gap: loop-relative beat [1, 2) of this pass.
        const float gapRms = TestAudioHelpers::computeRMSInRange(rendered, passStart + kBeatSamples + kGapStartGuard,
                                                                 passStart + kLoopSamples - kGapEndGuard, 0);
        EXPECT_LT(gapRms, kSilenceThreshold) << "pass " << pass << " gap window, rms=" << gapRms;
    }
}

// ============================================================================
// 4. The pitch CV path, not just the gate: the note's frequency is really 440 Hz
// ============================================================================

TEST(TimelineE2ETest, FrequencyIsThePitchCv) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(addStandardNotes(f));
    f.publish();
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.driver->renderToBeat(8.0);
    ASSERT_EQ(rendered.getNumSamples(), kEightBeatBlocks * kBlockSize);

    for (double beat : kNoteBeats) {
        const int start = sampleAt(beat) + kNoteGuard;
        const int end = sampleAt(beat) + kBeatSamples - kNoteGuard;
        juce::AudioBuffer<float> window(1, end - start);
        window.copyFrom(0, 0, rendered, 0, start, end - start);
        const float freq = TestAudioHelpers::estimateFrequencyByZeroCrossings(window, kSampleRate, 0);
        EXPECT_NEAR(freq, 440.0f, 5.0f) << "note at beat " << beat << " measured freq=" << freq;
    }
}

// ============================================================================
// 5. A muted track renders silence even though the transport is genuinely playing
// ============================================================================

TEST(TimelineE2ETest, MutedTrackRendersSilence) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(addStandardNotes(f));
    ASSERT_TRUE(f.doc.setTrackMuted(f.trackId, true));
    f.publish();
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.driver->renderToBeat(8.0);
    ASSERT_EQ(rendered.getNumSamples(), kEightBeatBlocks * kBlockSize);
    EXPECT_TRUE(TestAudioHelpers::isSilent(rendered, 0, kSilenceThreshold));
    EXPECT_TRUE(TestAudioHelpers::isSilent(rendered, 1, kSilenceThreshold));
}

// ============================================================================
// 6. Bypassing the Track In node silences it too
// ============================================================================

TEST(TimelineE2ETest, BypassedTrackInRendersSilence) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(addStandardNotes(f));
    f.publish();

    TimelineMidiSourceModule* trackIn = f.findTrackIn();
    ASSERT_NE(trackIn, nullptr);
    trackIn->setBypassed(true);

    ASSERT_TRUE(f.driver->getTransport().play());
    const auto rendered = f.driver->renderToBeat(8.0);
    ASSERT_EQ(rendered.getNumSamples(), kEightBeatBlocks * kBlockSize);
    EXPECT_TRUE(TestAudioHelpers::isSilent(rendered, 0, kSilenceThreshold));
    EXPECT_TRUE(TestAudioHelpers::isSilent(rendered, 1, kSilenceThreshold));
}

#endif // SYNTH_ENABLE_TIMELINE
