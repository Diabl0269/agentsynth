// AudioClipPlaybackTests.cpp
//
// Disk-streaming audio clip playback — synth::AudioClipStreamer plus the "Track Audio"
// module that pulls from it.
//
// Everything here renders through synth::OfflineTransportDriver the way a host would and asserts on
// the RENDERED AUDIO, in the house style TimelineE2ETests established. Two properties make the
// assertions BIT-EXACT rather than statistical, and both are deliberate:
//
//   * the test WAV is 32-bit IEEE float carrying values that are exactly representable
//     (`n / 65536`), so a round trip through the writer, the reader, the ring and the mixer changes
//     no bits at all;
//   * the prefetch thread is PAUSED (`setPrefetchPausedForTest`) and driven by `pumpForTest()` from
//     the render loop's per-block callback, so there is no thread to race, no sleep, and no
//     "eventually the ring fills" wait anywhere in this file.
//
// Groups:
//   1. Snapshot flatten (the audio half of TimelineSnapshot).
//   2. The streamer in isolation — root resolution, the escaping-ref refusal, the RAM bound, the
//      pool cap.
//   3. Playback through a real graph — placement, source offset, gain/fades, seek, loop wrap,
//      overlap summing, mute/bypass/stop.
//   4. Registration — the internal-only checklist Track In established.
//   5. The add-audio-track FLOW through MainComponent.

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Modules/TimelineAudioSourceModule.h"
#include "../Source/Timeline/AudioClipStreamer.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Timeline/TimelineSnapshot.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "../Source/UI/CableColour.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include "MainComponent.h"
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

#if SYNTH_ENABLE_TIMELINE

using synth::AudioClipStreamer;
using synth::TimelineDoc;
using synth::TimelineSnapshot;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kBeatSamples = 24000; // 120 BPM at 48 kHz
constexpr double kSamplesPerBeat = 24000.0;

constexpr const char* kTrackAudioUuid = "c1170000-0000-0000-0000-000000000001";
constexpr const char* kAssetRef = "Audio/clip.wav";

// The source signal. `n / 65536` is exactly representable as a float, so a 32-bit float WAV
// round-trips it bit for bit and every assertion below can use plain equality. The period is long
// enough that no test window is ambiguous about WHERE in the file it came from.
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

/** Writes a 32-bit float WAV of `numFrames` frames. `generator` is called per (frame, channel).
 *  Written in chunks so even a minute-long file costs one small buffer. */
bool writeWav(const juce::File& file, juce::int64 numFrames, int numChannels, double sampleRate,
              const std::function<float(juce::int64, int)>& generator) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), sampleRate, (unsigned int)numChannels, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();

    constexpr int kChunk = 8192;
    juce::AudioBuffer<float> chunk(numChannels, kChunk);
    juce::int64 written = 0;
    while (written < numFrames) {
        const int n = (int)std::min<juce::int64>(kChunk, numFrames - written);
        for (int channel = 0; channel < numChannels; ++channel)
            for (int i = 0; i < n; ++i)
                chunk.getWritePointer(channel)[i] = generator(written + i, channel);
        if (!writer->writeFromAudioSampleBuffer(chunk, 0, n))
            return false;
        written += n;
    }
    writer.reset(); // finalises the header
    return true;
}

bool writeSourceWav(const juce::File& file, juce::int64 numFrames, int numChannels = 2,
                    double sampleRate = kSampleRate) {
    return writeWav(file, numFrames, numChannels, sampleRate,
                    [](juce::int64 frame, int channel) { return sourceSample(frame, channel); });
}

juce::String buildPatchJson() {
    return juce::String(R"({
        "nodes": [
            {"id": 1, "type": "Track Audio",  "uuid": ")") +
           kTrackAudioUuid + R"("},
            {"id": 2, "type": "Audio Output", "uuid": "c1170000-0000-0000-0000-000000000002"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
            {"src": 1, "srcPort": 1, "dst": 2, "dstPort": 1}
        ]
    })";
}

int sampleAt(double beat) { return (int)std::llround(beat * kSamplesPerBeat); }

/** The rig every playback test uses: a hosted engine whose graph is Track Audio -> Audio Output, a
 *  temp "bundle root" with one asset in it, and a one-track/one-clip document bound to the node.
 *  The streamer's prefetch thread is paused from the outset — pump() is the only thing that ever
 *  fills a ring here. */
struct Fixture {
    ScopedTempDir bundle{"agentsynth_clipplayback"};
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    TimelineDoc doc;
    synth::TrackId trackId;
    synth::ClipId clipId;

    juce::File assetFile() const { return bundle.dir.getChildFile(kAssetRef); }

    AudioClipStreamer& streamer() { return engine.getAudioClipStreamer(); }

    /** @param sourceFrames  length of the WAV written into the bundle (0 = write none) */
    bool build(double clipStartBeat = 0.0, double clipLengthBeats = 2.0, juce::int64 sourceFrames = 4 * kBeatSamples,
               int sourceChannels = 2, double sourceRate = kSampleRate) {
        engine.getAudioClipStreamer().setPrefetchPausedForTest(true);

        if (sourceFrames > 0 && !writeSourceWav(assetFile(), sourceFrames, sourceChannels, sourceRate))
            return false;

        engine.initialise();

        const juce::var patch = juce::JSON::parse(buildPatchJson());
        if (!patch.isObject())
            return false;
        if (!synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true,
                                                    /*trusted=*/true))
            return false;

        // After the graph is built: the ctor calls prepareForHost on the nodes just created.
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, 2);

        // The bundle root is a plain directory here (not a real .agsproj) — the streamer only ever
        // resolves refs against whatever roots it is handed, which is exactly what makes it
        // testable without a project on disk.
        engine.getAudioClipStreamer().setAssetRoots(bundle.dir, juce::File());

        trackId = doc.addTrack(TrackKind::Audio, "Audio 1");
        if (!doc.setTrackBinding(trackId, kTrackAudioUuid))
            return false;
        clipId = doc.addClip(trackId, clipStartBeat, clipLengthBeats, "Clip");
        if (!clipId.isValid())
            return false;
        return doc.setClipAsset(clipId, kAssetRef, 0.0);
    }

    /** Publishes the document (which also syncs the streamer) and fills every ring. */
    void publishAndPump() {
        engine.publishTimeline(doc);
        engine.getAudioClipStreamer().pumpForTest();
    }

    /** Renders `numBlocks` blocks, running a full prefetch pass after each one — the deterministic
     *  stand-in for the background thread. */
    juce::AudioBuffer<float> render(int numBlocks) {
        return driver->renderBlocks(numBlocks, [this](const juce::AudioBuffer<float>&, const synth::BlockTimeInfo&) {
            engine.getAudioClipStreamer().pumpForTest();
        });
    }

    /** Renders without pumping at all — for pinning what a not-yet-filled ring sounds like. */
    juce::AudioBuffer<float> renderWithoutPumping(int numBlocks) { return driver->renderBlocks(numBlocks); }

    TimelineAudioSourceModule* findTrackAudio() {
        for (auto* node : engine.getGraph().getNodes())
            if (auto* t = dynamic_cast<TimelineAudioSourceModule*>(node->getProcessor()))
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

/** Samples where `rendered` differs from `expected(i)` by more than `tolerance`, on one channel. */
int countMismatches(const juce::AudioBuffer<float>& rendered, int channel, const std::function<float(int)>& expected,
                    float tolerance, int* firstBadIndex = nullptr) {
    int bad = 0;
    const float* data = rendered.getReadPointer(channel);
    for (int i = 0; i < rendered.getNumSamples(); ++i) {
        if (std::abs(data[i] - expected(i)) > tolerance) {
            if (bad == 0 && firstBadIndex != nullptr)
                *firstBadIndex = i;
            ++bad;
        }
    }
    return bad;
}

bool isExactlySilent(const juce::AudioBuffer<float>& buffer, int channel, int start, int end) {
    const float* data = buffer.getReadPointer(channel);
    for (int i = start; i < end && i < buffer.getNumSamples(); ++i)
        if (data[i] != 0.0f)
            return false;
    return true;
}

} // namespace

// ============================================================================
// 1. The snapshot's audio half
// ============================================================================

TEST(AudioClipSnapshotTest, AudioTracksCarryClipRunsAndNoNotes) {
    TimelineDoc doc;
    const auto audioTrack = doc.addTrack(TrackKind::Audio, "A");
    const auto midiTrack = doc.addTrack(TrackKind::Midi, "M");

    const auto a1 = doc.addClip(audioTrack, 4.0, 2.0, "second");
    const auto a0 = doc.addClip(audioTrack, 0.0, 2.0, "first");
    ASSERT_TRUE(doc.setClipAsset(a0, "Audio/one.wav", 1.5));
    ASSERT_TRUE(doc.setClipAsset(a1, "Audio/two.wav", 0.0));
    ASSERT_TRUE(doc.setClipGainDb(a0, -6.0));
    ASSERT_TRUE(doc.setClipFades(a0, 0.25, 0.5));

    // A note left on an audio clip is INERT: the track's kind decides which half is flattened.
    synth::MidiNote note;
    note.startBeat = 0.0;
    note.pitch = 60;
    ASSERT_TRUE(doc.addNote(a0, note).isValid());

    const auto midiClip = doc.addClip(midiTrack, 0.0, 4.0, "midi");
    ASSERT_TRUE(doc.addNote(midiClip, note).isValid());
    // ...and an assetRef on a MIDI clip is inert the same way.
    ASSERT_TRUE(doc.setClipAsset(midiClip, "Audio/ignored.wav", 0.0));

    auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->selfCheck());

    const auto& audio = snapshot->tracks[0];
    const auto& midi = snapshot->tracks[1];

    EXPECT_EQ(audio.numAudioClips, 2);
    EXPECT_EQ(audio.numNotes, 0) << "an audio track must contribute no note run";
    EXPECT_EQ(midi.numAudioClips, 0) << "a MIDI track must contribute no audio-clip run";
    EXPECT_EQ(midi.numNotes, 1);

    // Sorted by startBeat, whatever order they were added in.
    const auto& first = snapshot->audioClips[(std::size_t)audio.firstAudioClip];
    const auto& second = snapshot->audioClips[(std::size_t)audio.firstAudioClip + 1];
    EXPECT_DOUBLE_EQ(first.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(second.startBeat, 4.0);

    EXPECT_EQ(first.clipId, a0.value);
    EXPECT_STREQ(first.assetRef, "Audio/one.wav");
    EXPECT_DOUBLE_EQ(first.sourceStartSeconds, 1.5);
    EXPECT_DOUBLE_EQ(first.fadeInBeats, 0.25);
    EXPECT_DOUBLE_EQ(first.fadeOutBeats, 0.5);
    // dB converted ONCE, at flatten time.
    EXPECT_NEAR(first.gainLinear, std::pow(10.0f, -6.0f / 20.0f), 1.0e-6f);
    EXPECT_FLOAT_EQ(second.gainLinear, 1.0f);
}

TEST(AudioClipSnapshotTest, LongAssetRefIsTruncatedAndStillNulTerminated) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 1.0, "c");
    const juce::String longRef = "Audio/" + juce::String::repeatedString("x", 400) + ".wav";
    ASSERT_TRUE(doc.setClipAsset(clip, longRef, 0.0));

    auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->selfCheck());
    const auto& info = snapshot->audioClips[0];
    EXPECT_EQ(std::strlen(info.assetRef), (std::size_t)TimelineSnapshot::kMaxAssetRefBytes - 1);
    EXPECT_EQ(info.assetRef[TimelineSnapshot::kMaxAssetRefBytes - 1], '\0');
}

TEST(AudioClipSnapshotTest, ASoloedAudioTrackCountsTowardsAnySoloed) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    ASSERT_TRUE(doc.setTrackSoloed(track, true));

    auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    // Audio tracks render, so solo has to mean solo for them too.
    EXPECT_TRUE(snapshot->anySoloed);
}

// ============================================================================
// 2. The streamer in isolation
// ============================================================================

TEST(AudioClipStreamerTest, UnresolvableEscapingRefRefused) {
    ScopedTempDir root{"agentsynth_clipstreamer_roots"};
    const auto inside = root.dir.getChildFile("Audio/ok.wav");
    ASSERT_TRUE(writeSourceWav(inside, 1000));

    // A real file OUTSIDE the root, which an escaping ref would otherwise reach.
    const auto outside = root.dir.getParentDirectory().getChildFile("agentsynth_clipstreamer_escape.wav");
    outside.deleteFile();
    ASSERT_TRUE(writeSourceWav(outside, 1000));

    AudioClipStreamer streamer;
    streamer.setPrefetchPausedForTest(true);
    streamer.setAssetRoots(root.dir, juce::File());

    EXPECT_EQ(streamer.resolveAssetRef("Audio/ok.wav"), inside) << "a legitimate ref must still resolve";

    // Every shape of escape is refused, and none of them opens anything.
    EXPECT_EQ(streamer.resolveAssetRef("../agentsynth_clipstreamer_escape.wav"), juce::File());
    EXPECT_EQ(streamer.resolveAssetRef("Audio/../../agentsynth_clipstreamer_escape.wav"), juce::File());
    EXPECT_EQ(streamer.resolveAssetRef(outside.getFullPathName()), juce::File());
    EXPECT_EQ(streamer.resolveAssetRef("/etc/passwd"), juce::File());
    EXPECT_EQ(streamer.resolveAssetRef(""), juce::File());
    EXPECT_EQ(streamer.resolveAssetRef("Audio/missing.wav"), juce::File());

    outside.deleteFile();
}

TEST(AudioClipStreamerTest, RecordingsRefResolvesAgainstTheRecordingsRoot) {
    ScopedTempDir appData{"agentsynth_clipstreamer_appdata"};
    const auto recordings = appData.dir.getChildFile("Recordings");
    const auto take = recordings.getChildFile("take-1.wav");
    ASSERT_TRUE(writeSourceWav(take, 1000));

    AudioClipStreamer streamer;
    streamer.setPrefetchPausedForTest(true);
    streamer.setAssetRoots(juce::File(), recordings);

    // Exactly the form MainComponent::chooseTakeFiles stores for an unsaved project: the
    // ref INCLUDES the "Recordings/" segment and resolves against the folder containing it.
    EXPECT_EQ(streamer.resolveAssetRef("Recordings/take-1.wav"), take);
    // With no bundle root, a bundle-relative ref resolves to nothing rather than falling back.
    EXPECT_EQ(streamer.resolveAssetRef("Audio/take-1.wav"), juce::File());
    // ...and the reserved prefix is not a way out of its own root.
    EXPECT_EQ(streamer.resolveAssetRef("Recordings/../../elsewhere.wav"), juce::File());
}

TEST(AudioClipStreamerTest, RamStaysBounded) {
    // A full minute of stereo audio: 2.88M frames, ~23 MB on disk. The whole point of this test is
    // that NONE of that is resident — the streamer holds one ring, whatever the file's length.
    ScopedTempDir root{"agentsynth_clipstreamer_ram"};
    constexpr juce::int64 kMinuteFrames = 60 * (juce::int64)kSampleRate;
    ASSERT_TRUE(writeSourceWav(root.dir.getChildFile(kAssetRef), kMinuteFrames));

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 120.0, "long");
    ASSERT_TRUE(doc.setClipAsset(clip, kAssetRef, 0.0));

    AudioClipStreamer streamer;
    streamer.setPrefetchPausedForTest(true);
    streamer.setAssetRoots(root.dir, juce::File());

    auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    streamer.syncToSnapshot(*snapshot);
    streamer.pumpForTest();

    ASSERT_TRUE(streamer.isClipReady(clip.value));
    EXPECT_EQ(streamer.getResidentFramesForClip(clip.value), AudioClipStreamer::kRingFrames)
        << "resident capacity is the ring, never the file";
    EXPECT_LT((juce::int64)streamer.getResidentFramesForClip(clip.value), kMinuteFrames);

    const std::size_t expectedBytes = (std::size_t)AudioClipStreamer::kRingFrames * 2 * sizeof(float);
    EXPECT_EQ(streamer.getTotalResidentBytes(), expectedBytes);

    // Play through the whole minute in one-second hops. The resident figure must not move.
    const auto* handle = streamer.acquire(clip.value);
    ASSERT_NE(handle, nullptr);
    std::vector<float> left(512), right(512);
    for (juce::int64 frame = 0; frame < kMinuteFrames; frame += (juce::int64)kSampleRate) {
        streamer.readFrames(handle, frame, left.data(), right.data(), 512);
        streamer.pumpForTest();
    }
    EXPECT_EQ(streamer.getTotalResidentBytes(), expectedBytes);
}

TEST(AudioClipStreamerTest, PoolCapDropsExcessClipsGracefully) {
    ScopedTempDir root{"agentsynth_clipstreamer_cap"};
    ASSERT_TRUE(writeSourceWav(root.dir.getChildFile(kAssetRef), 4800));

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");

    constexpr int kClips = AudioClipStreamer::kMaxStreams + 1; // 33
    std::vector<synth::ClipId> clips;
    for (int i = 0; i < kClips; ++i) {
        const auto clip = doc.addClip(track, (double)i, 1.0, "c" + juce::String(i));
        ASSERT_TRUE(clip.isValid());
        ASSERT_TRUE(doc.setClipAsset(clip, kAssetRef, 0.0));
        clips.push_back(clip);
    }

    AudioClipStreamer streamer;
    streamer.setPrefetchPausedForTest(true);
    streamer.setAssetRoots(root.dir, juce::File());

    auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    streamer.syncToSnapshot(*snapshot);
    streamer.pumpForTest();

    EXPECT_EQ(streamer.getActiveStreamCount(), AudioClipStreamer::kMaxStreams);

    int streamed = 0;
    for (const auto& clip : clips)
        if (streamer.acquire(clip.value) != nullptr)
            ++streamed;
    EXPECT_EQ(streamed, AudioClipStreamer::kMaxStreams) << "exactly one clip is over the cap";

    // The documented policy: document order wins, so it is the LAST clip that goes silent.
    EXPECT_NE(streamer.acquire(clips.front().value), nullptr);
    EXPECT_EQ(streamer.acquire(clips.back().value), nullptr);

    // ...and reading through the dropped clip's (absent) handle is silence, not a crash.
    std::vector<float> left(64, 1.0f), right(64, 1.0f);
    EXPECT_EQ(streamer.readFrames(nullptr, 0, left.data(), right.data(), 64), 0);
    for (int i = 0; i < 64; ++i) {
        EXPECT_FLOAT_EQ(left[(std::size_t)i], 0.0f);
        EXPECT_FLOAT_EQ(right[(std::size_t)i], 0.0f);
    }
}

TEST(AudioClipStreamerTest, MonoFileIsUpmixedToStereo) {
    ScopedTempDir root{"agentsynth_clipstreamer_mono"};
    ASSERT_TRUE(writeWav(root.dir.getChildFile(kAssetRef), 4800, 1, kSampleRate,
                         [](juce::int64 frame, int) { return sourceSample(frame, 0); }));

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 1.0, "c");
    ASSERT_TRUE(doc.setClipAsset(clip, kAssetRef, 0.0));

    AudioClipStreamer streamer;
    streamer.setPrefetchPausedForTest(true);
    streamer.setAssetRoots(root.dir, juce::File());
    auto snapshot = TimelineSnapshot::buildFrom(doc);
    streamer.syncToSnapshot(*snapshot);
    streamer.pumpForTest();

    const auto* handle = streamer.acquire(clip.value);
    ASSERT_NE(handle, nullptr);
    std::vector<float> left(256), right(256);
    EXPECT_EQ(streamer.readFrames(handle, 0, left.data(), right.data(), 256), 256);
    for (int i = 0; i < 256; ++i) {
        EXPECT_FLOAT_EQ(left[(std::size_t)i], sourceSample(i, 0));
        EXPECT_FLOAT_EQ(right[(std::size_t)i], sourceSample(i, 0)) << "mono must be duplicated, not zeroed";
    }
}

// ============================================================================
// 3. Playback through a real graph
// ============================================================================

TEST(AudioClipPlaybackTest, ClipPlaysWhereItSits) {
    Fixture f;
    // Clip on beats [2, 4) of an 8-beat render: samples [48000, 96000).
    ASSERT_TRUE(f.build(/*clipStartBeat=*/2.0, /*clipLengthBeats=*/2.0));
    f.publishAndPump();
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.render(375); // 8 beats == 192000 samples == 375 * 512 exactly
    ASSERT_EQ(rendered.getNumSamples(), 375 * kBlockSize);

    const int clipStart = sampleAt(2.0);
    const int clipEnd = sampleAt(4.0);

    int firstBad = -1;
    const int bad = countMismatches(
        rendered, 0, [&](int i) { return (i >= clipStart && i < clipEnd) ? sourceSample(i - clipStart, 0) : 0.0f; },
        0.0f, &firstBad);
    EXPECT_EQ(bad, 0) << "first mismatch at sample " << firstBad;

    // Silence OUTSIDE the clip is exact, not merely quiet.
    EXPECT_TRUE(isExactlySilent(rendered, 0, 0, clipStart));
    EXPECT_TRUE(isExactlySilent(rendered, 0, clipEnd, rendered.getNumSamples()));
    // ...and the right channel carries the file's right channel, not a copy of the left.
    EXPECT_FLOAT_EQ(rendered.getReadPointer(1)[clipStart + 100], sourceSample(100, 1));
}

TEST(AudioClipPlaybackTest, SourceOffsetHonoured) {
    Fixture f;
    ASSERT_TRUE(f.build(/*clipStartBeat=*/0.0, /*clipLengthBeats=*/2.0,
                        /*sourceFrames=*/6 * kBeatSamples));
    // One second into the file: at 48 kHz, source frame 48000.
    ASSERT_TRUE(f.doc.setClipAsset(f.clipId, kAssetRef, 1.0));
    f.publishAndPump();
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.render(120); // > 2 beats
    const int clipEnd = sampleAt(2.0);
    constexpr int kOffsetFrames = 48000;

    int firstBad = -1;
    const int bad = countMismatches(
        rendered, 0, [&](int i) { return i < clipEnd ? sourceSample(i + kOffsetFrames, 0) : 0.0f; }, 0.0f, &firstBad);
    EXPECT_EQ(bad, 0) << "first mismatch at sample " << firstBad;
}

TEST(AudioClipPlaybackTest, GainAndFadesShapeTheEnvelope) {
    Fixture f;
    // A DC source: the rendered sample IS the envelope, so the fade shape can be asserted directly
    // rather than inferred from an RMS window.
    ASSERT_TRUE(f.build(/*clipStartBeat=*/0.0, /*clipLengthBeats=*/4.0, /*sourceFrames=*/0));
    ASSERT_TRUE(writeWav(f.assetFile(), 6 * kBeatSamples, 2, kSampleRate, [](juce::int64, int) { return 1.0f; }));

    ASSERT_TRUE(f.doc.setClipGainDb(f.clipId, -6.0));
    ASSERT_TRUE(f.doc.setClipFades(f.clipId, /*fadeIn=*/1.0, /*fadeOut=*/2.0));
    f.publishAndPump();
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.render(200); // > 4 beats
    const float gain = std::pow(10.0f, -6.0f / 20.0f);

    const auto envelopeAt = [&](int sample) {
        const double beat = (double)sample / kSamplesPerBeat;
        if (beat >= 4.0)
            return 0.0f;
        const float fadeIn = (float)juce::jlimit(0.0, 1.0, beat / 1.0);
        const float fadeOut = (float)juce::jlimit(0.0, 1.0, (4.0 - beat) / 2.0);
        return gain * fadeIn * fadeOut;
    };

    int firstBad = -1;
    // One sample of slack in the beat->sample rounding is worth 1/24000 of the ramp, so the
    // tolerance is two of those plus float noise.
    const int bad = countMismatches(rendered, 0, envelopeAt, 1.0e-4f, &firstBad);
    EXPECT_EQ(bad, 0) << "first mismatch at sample " << firstBad;

    // The named landmarks, spelled out so a regression says WHICH part of the envelope broke.
    EXPECT_NEAR(rendered.getReadPointer(0)[sampleAt(0.5)], gain * 0.5f * 1.0f, 1.0e-4f); // mid fade-in
    EXPECT_NEAR(rendered.getReadPointer(0)[sampleAt(1.5)], gain * 1.0f * 1.0f, 1.0e-4f); // full, both open
    EXPECT_NEAR(rendered.getReadPointer(0)[sampleAt(3.0)], gain * 1.0f * 0.5f, 1.0e-4f); // mid fade-out
    // -6 dB really is about half.
    EXPECT_NEAR(gain, 0.5f, 0.01f);
    EXPECT_TRUE(isExactlySilent(rendered, 0, sampleAt(4.0) + 2, rendered.getNumSamples()));
}

TEST(AudioClipPlaybackTest, SeekIntoTheMiddlePlaysFromTheRightFrame) {
    Fixture f;
    // A clip long enough that a locate past the pre-filled window is a genuine seek: the ring holds
    // kPrefetchAheadFrames (98304) from the start, so beat 6 == frame 144000 is outside it.
    ASSERT_TRUE(f.build(/*clipStartBeat=*/0.0, /*clipLengthBeats=*/16.0,
                        /*sourceFrames=*/16 * kBeatSamples));
    f.publishAndPump();

    ASSERT_TRUE(f.driver->getTransport().locateBeat(6.0));
    ASSERT_TRUE(f.driver->getTransport().play());

    // BEFORE any pump: the ring cannot serve frame 144000 yet, and what comes out is SILENCE — not
    // whatever bytes happen to sit in those slots.
    const auto beforePump = f.renderWithoutPumping(1);
    ASSERT_EQ(beforePump.getNumSamples(), kBlockSize);
    EXPECT_TRUE(isExactlySilent(beforePump, 0, 0, kBlockSize)) << "a seek gap is silence, never garbage";
    EXPECT_TRUE(isExactlySilent(beforePump, 1, 0, kBlockSize));

    // One prefetch pass later the stream is repositioned and playback resumes at the right frame.
    f.streamer().pumpForTest();
    const auto afterPump = f.render(8);
    const int startFrame = sampleAt(6.0) + kBlockSize; // the block already consumed above

    int firstBad = -1;
    const int bad =
        countMismatches(afterPump, 0, [&](int i) { return sourceSample(startFrame + i, 0); }, 0.0f, &firstBad);
    EXPECT_EQ(bad, 0) << "first mismatch at sample " << firstBad;
}

TEST(AudioClipPlaybackTest, LoopWrapReplaysTheClip) {
    Fixture f;
    // The clip fills the loop exactly, and the whole file fits in one ring — so each pass replays
    // it with no re-seek at all.
    ASSERT_TRUE(f.build(/*clipStartBeat=*/0.0, /*clipLengthBeats=*/2.0,
                        /*sourceFrames=*/2 * kBeatSamples));
    f.publishAndPump();

    ASSERT_TRUE(f.driver->getTransport().setLoop(0.0, 2.0, true));
    ASSERT_TRUE(f.driver->getTransport().play());

    constexpr int kLoopSamples = 2 * kBeatSamples; // 48000
    constexpr int kPasses = 4;
    constexpr int kBlocks = kPasses * kLoopSamples / kBlockSize; // 375
    static_assert(kBlocks * kBlockSize == kPasses * kLoopSamples, "whole passes only");

    const auto rendered = f.render(kBlocks);
    ASSERT_EQ(rendered.getNumSamples(), kBlocks * kBlockSize);

    int firstBad = -1;
    const int bad =
        countMismatches(rendered, 0, [&](int i) { return sourceSample(i % kLoopSamples, 0); }, 0.0f, &firstBad);
    EXPECT_EQ(bad, 0) << "first mismatch at sample " << firstBad;
}

TEST(AudioClipPlaybackTest, OverlappingClipsSum) {
    Fixture f;
    ASSERT_TRUE(f.build(/*clipStartBeat=*/0.0, /*clipLengthBeats=*/2.0,
                        /*sourceFrames=*/4 * kBeatSamples));

    // A second clip on the same track, at the same beat, from the same asset: distinct clip ids, so
    // two independent streams, and their output SUMS.
    const auto second = f.doc.addClip(f.trackId, 0.0, 2.0, "Clip 2");
    ASSERT_TRUE(second.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(second, kAssetRef, 0.0));

    f.publishAndPump();
    EXPECT_EQ(f.streamer().getActiveStreamCount(), 2);
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.render(90);
    const int clipEnd = sampleAt(2.0);

    int firstBad = -1;
    const int bad = countMismatches(
        rendered, 0, [&](int i) { return i < clipEnd ? 2.0f * sourceSample(i, 0) : 0.0f; }, 1.0e-6f, &firstBad);
    EXPECT_EQ(bad, 0) << "first mismatch at sample " << firstBad;
}

TEST(AudioClipPlaybackTest, MissingAssetIsSilentNoCrash) {
    Fixture f;
    ASSERT_TRUE(f.build());
    // A well-formed but non-existent ref: accepted by the document (the form is legal), refused by
    // the streamer (there is no such file), and therefore simply silent.
    ASSERT_TRUE(f.doc.setClipAsset(f.clipId, "Audio/does-not-exist.wav", 0.0));
    f.publishAndPump();

    EXPECT_EQ(f.streamer().getActiveStreamCount(), 0);
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.render(90);
    EXPECT_TRUE(isExactlySilent(rendered, 0, 0, rendered.getNumSamples()));
    EXPECT_TRUE(isExactlySilent(rendered, 1, 0, rendered.getNumSamples()));
}

TEST(AudioClipPlaybackTest, MutedTrackSilent) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.doc.setTrackMuted(f.trackId, true));
    f.publishAndPump();
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.render(90);
    EXPECT_TRUE(isExactlySilent(rendered, 0, 0, rendered.getNumSamples()));
    EXPECT_TRUE(isExactlySilent(rendered, 1, 0, rendered.getNumSamples()));
}

TEST(AudioClipPlaybackTest, SoloElsewhereSilencesThisTrack) {
    Fixture f;
    ASSERT_TRUE(f.build());
    // A second, soloed audio track that this node is not bound to.
    const auto other = f.doc.addTrack(TrackKind::Audio, "Other");
    ASSERT_TRUE(f.doc.setTrackSoloed(other, true));
    f.publishAndPump();
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.render(90);
    EXPECT_TRUE(isExactlySilent(rendered, 0, 0, rendered.getNumSamples()));
}

TEST(AudioClipPlaybackTest, BypassClears) {
    Fixture f;
    ASSERT_TRUE(f.build());
    f.publishAndPump();

    auto* module = f.findTrackAudio();
    ASSERT_NE(module, nullptr);
    module->setBypassed(true);

    ASSERT_TRUE(f.driver->getTransport().play());
    const auto rendered = f.render(90);
    EXPECT_TRUE(isExactlySilent(rendered, 0, 0, rendered.getNumSamples()));
    EXPECT_TRUE(isExactlySilent(rendered, 1, 0, rendered.getNumSamples()));
}

TEST(AudioClipPlaybackTest, StoppedSilent) {
    Fixture f;
    ASSERT_TRUE(f.build());
    f.publishAndPump();
    ASSERT_FALSE(f.driver->getTransport().getPositionSnapshot().playing);

    // No play(): the graph still runs every block, it just never reaches a beat.
    const auto rendered = f.render(90);
    EXPECT_TRUE(isExactlySilent(rendered, 0, 0, rendered.getNumSamples()));
    EXPECT_TRUE(isExactlySilent(rendered, 1, 0, rendered.getNumSamples()));
}

TEST(AudioClipPlaybackTest, UnboundNodePlaysNothing) {
    Fixture f;
    ASSERT_TRUE(f.build());
    ASSERT_TRUE(f.doc.setTrackBinding(f.trackId, {}));
    f.publishAndPump();
    ASSERT_TRUE(f.driver->getTransport().play());

    const auto rendered = f.render(90);
    EXPECT_TRUE(isExactlySilent(rendered, 0, 0, rendered.getNumSamples()));
}

// ============================================================================
// 4. Registration — the internal-only checklist Track In established
// ============================================================================

TEST(AudioClipPlaybackTest, RegisteredButInternalOnly) {
    auto processor = synth::AIStateMapper::createModule("Track Audio");
    ASSERT_NE(processor, nullptr) << "Track Audio must be in the factory so a saved patch round-trips it";
    auto* module = dynamic_cast<TimelineAudioSourceModule*>(processor.get());
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->getModuleType(), ModuleType::TimelineAudioSource);
    EXPECT_EQ(module->getTotalNumInputChannels(), 0);
    EXPECT_EQ(module->getTotalNumOutputChannels(), TimelineAudioSourceModule::kNumChannels);
    EXPECT_FALSE(module->acceptsMidi());
    EXPECT_FALSE(module->producesMidi());
    EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(module), "Track Audio");

    // Never offered to a model. (The full golden lives in AIStateMapperTests.)
    EXPECT_FALSE(synth::AIStateMapper::authorableModuleTypes().contains("Track Audio"));

    // An audio-emitting source, so it shares the Sampler's bucket rather than Track In's.
    EXPECT_EQ(synth::ui::categoryFor(ModuleType::TimelineAudioSource), synth::ui::ModuleCategory::Sources);
}

TEST(AudioClipPlaybackTest, AbsentFromTheLibraryWithAPinnedSizeEstimate) {
    ModuleLibraryComponent library;
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Track Audio"))
        << "Track Audio is internal-only and must stay out of the module library";

    auto processor = synth::AIStateMapper::createModule("Track Audio");
    ASSERT_NE(processor, nullptr);

    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleComponent comp(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);

    const auto estimate = GraphEditor::estimateModuleSize("Track Audio");
    EXPECT_EQ(estimate.x, comp.getWidth());
    EXPECT_EQ(estimate.y, comp.getHeight());
}

// ============================================================================
// 5. The add-audio-track flow (MainComponent)
// ============================================================================

namespace {

// Same minimal pattern as MainComponentTests.cpp's MockProvider.
class MockProviderACP : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockACP"; }
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

int countNodesOfType(juce::AudioProcessorGraph& graph, ModuleType type) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type)
                    ++count;
    return count;
}

juce::AudioProcessorGraph::Node* findNodeNamedACP(juce::AudioProcessorGraph& graph, const juce::String& name) {
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->getProcessor() != nullptr && node->getProcessor()->getName() == name)
            return node;
    return nullptr;
}

} // namespace

class AddAudioTrackFlowTest : public ::testing::Test {
protected:
    // The delegating MainComponent ctor reads/writes the shared on-disk "Agent Synth" settings, so
    // the keys this flow depends on are pinned before AND after every test — same hygiene as
    // RecordTapTests.cpp / TimelinePanelTests.cpp.
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
};

TEST_F(AddAudioTrackFlowTest, AddAudioTrackFlow) {
    MainComponent mc(std::make_unique<MockProviderACP>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& panel = mc.getTimelinePanel();

    const int tracksBefore = (int)doc.getTracks().size();
    const int nodesBefore = countNodesOfType(graph, ModuleType::TimelineAudioSource);

    // The menu hook, not the async PopupMenu: the same headless seam the binding chip uses.
    panel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);

    ASSERT_EQ((int)doc.getTracks().size(), tracksBefore + 1);
    const auto& track = doc.getTracks().back();
    EXPECT_EQ(track.kind, TrackKind::Audio);
    EXPECT_TRUE(track.bindingUuid.isNotEmpty());

    ASSERT_EQ(countNodesOfType(graph, ModuleType::TimelineAudioSource), nodesBefore + 1);

    // The node the track is bound to is the one that was created, and it is wired into the master
    // bus on both channels.
    juce::AudioProcessorGraph::Node* created = nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == track.bindingUuid)
            created = node;
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->getProcessor()->getName(), "Track Audio");

    auto* output = findNodeNamedACP(graph, "Audio Output");
    ASSERT_NE(output, nullptr);
    for (int channel = 0; channel < TimelineAudioSourceModule::kNumChannels; ++channel)
        EXPECT_TRUE(graph.isConnected({{created->nodeID, channel}, {output->nodeID, channel}}))
            << "channel " << channel;

    // ONE undo step removes the node AND the track together.
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    mc.getUndoManager().undo();
    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore);
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineAudioSource), nodesBefore);
}

TEST_F(AddAudioTrackFlowTest, AddMidiTrackFromTheSameMenuIsUnchanged) {
    MainComponent mc(std::make_unique<MockProviderACP>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    auto& doc = mc.getTimelineDoc();
    const int tracksBefore = (int)doc.getTracks().size();

    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);

    ASSERT_EQ((int)doc.getTracks().size(), tracksBefore + 1);
    EXPECT_EQ(doc.getTracks().back().kind, TrackKind::Midi);
    EXPECT_EQ(countNodesOfType(mc.getAudioEngine().getGraph(), ModuleType::TimelineMidiSource), 1);
}

#endif // SYNTH_ENABLE_TIMELINE
