#pragma once

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

#include "../../TestAudioHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ExternalMidiModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include "Transport/BounceExporter.h"
#include "Transport/OfflineTransportDriver.h"
#include <cmath>
#include <gtest/gtest.h>
#include <initializer_list>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

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

// ---------------------------------------------------------------------------
// The streamed-clip rig. A bounce renders orders of magnitude faster than AudioClipStreamer's
// prefetch thread refills a ring — and constructing the exporter's driver re-prepares the engine,
// which invalidates every ring first — so this is the fixture that pins "a bounced clip is whole,
// from its very first frame".
//
// The asset is 32-bit float carrying exactly-representable values (n / 65536), the same trick
// AudioClipPlaybackTests uses, so a bounce written at bitDepth 32 is a bit-exact copy of it and
// every assertion below is plain equality rather than an RMS window.
// ---------------------------------------------------------------------------
constexpr const char* kTrackAudioUuid = "c0000000-0000-0000-0000-000000000001";
constexpr const char* kClipAssetRef = "Audio/clip.wav";
constexpr int kSourcePeriod = 65536;

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

/** Writes the stereo 32-bit float asset, in chunks, so even a long take costs one small buffer. */
bool writeSourceWav(const juce::File& file, juce::int64 numFrames) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), kSampleRate, 2, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();

    constexpr int kChunk = 8192;
    juce::AudioBuffer<float> chunk(2, kChunk);
    juce::int64 written = 0;
    while (written < numFrames) {
        const int n = (int)juce::jmin<juce::int64>(kChunk, numFrames - written);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < n; ++i)
                chunk.getWritePointer(channel)[i] = sourceSample(written + i, channel);
        if (!writer->writeFromAudioSampleBuffer(chunk, 0, n))
            return false;
        written += n;
    }
    return true;
}

juce::String buildAudioClipPatchJson() {
    return juce::String(R"({
        "nodes": [
            {"id": 1, "type": "Track Audio",  "uuid": ")") +
           kTrackAudioUuid + R"("},
            {"id": 2, "type": "Audio Output", "uuid": "c0000000-0000-0000-0000-000000000002"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
            {"src": 1, "srcPort": 1, "dst": 2, "dstPort": 1}
        ]
    })";
}

struct AudioClipFixture {
    ScopedTempDir bundle{"agentsynth_bounce_clip"};
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    TimelineDoc doc;
    synth::ClipId clipId;

    /** @param pausePrefetch  true == the bounce's own priming call is the only thing that ever
     *                        fills a ring, which makes the render deterministic and sleep-free;
     *                        false == the real background thread, i.e. the shipping path. */
    bool build(bool pausePrefetch, double clipStartBeat, double clipLengthBeats, juce::int64 sourceFrames) {
        if (pausePrefetch)
            engine.getAudioClipStreamer().setPrefetchPausedForTest(true);

        if (!writeSourceWav(bundle.dir.getChildFile(kClipAssetRef), sourceFrames))
            return false;

        engine.initialise();

        const juce::var patch = juce::JSON::parse(buildAudioClipPatchJson());
        if (!patch.isObject())
            return false;
        if (!synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true,
                                                    /*trusted=*/true))
            return false;

        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, kNumChannels);
        engine.getAudioClipStreamer().setAssetRoots(bundle.dir, juce::File());

        const auto trackId = doc.addTrack(TrackKind::Audio, "Audio 1");
        if (!doc.setTrackBinding(trackId, kTrackAudioUuid))
            return false;
        clipId = doc.addClip(trackId, clipStartBeat, clipLengthBeats, "Clip");
        if (!clipId.isValid())
            return false;
        if (!doc.setClipAsset(clipId, kClipAssetRef, 0.0))
            return false;

        engine.publishTimeline(doc); // also syncs the streamer to this snapshot
        return true;
    }

    ~AudioClipFixture() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

/** Samples of `channel` in [0, count) that differ from the asset frame `sourceOffset + i`. */
int countClipMismatches(const juce::AudioBuffer<float>& audio, int channel, juce::int64 sourceOffset, int count,
                        int* firstBadIndex) {
    int bad = 0;
    const float* data = audio.getReadPointer(channel);
    for (int i = 0; i < count && i < audio.getNumSamples(); ++i) {
        if (data[i] != sourceSample(sourceOffset + i, channel)) {
            if (bad == 0 && firstBadIndex != nullptr)
                *firstBadIndex = i;
            ++bad;
        }
    }
    return bad;
}

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

/** Same contract as readWav, opened with JUCE's AIFF reader instead. */
WavContents readAiff(const juce::File& file) {
    WavContents out;
    if (!file.existsAsFile())
        return out;

    auto input = file.createInputStream();
    if (input == nullptr)
        return out;

    juce::AiffAudioFormat aiffFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        aiffFormat.createReaderFor(input.release(), /*deleteStreamIfOpeningFails=*/true));
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
