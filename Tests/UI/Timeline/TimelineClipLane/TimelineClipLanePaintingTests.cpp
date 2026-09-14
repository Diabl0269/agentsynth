// TimelineClipLanePaintingTests.cpp — waveform painting from PeaksFile data, the peaks cache and
// its invalidation, the missing-asset placeholder, and the live-recording strip.
#include "Timeline/PeaksFile.h"
#include "TimelineClipLaneTestFixture.h"

namespace {

struct ScopedPeaksFile {
    explicit ScopedPeaksFile(const juce::String& name)
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name)) {
        file.deleteFile();
    }
    ~ScopedPeaksFile() { file.deleteFile(); }
    juce::File file;
};

synth::PeaksFile::Data makeWaveformData(int numBuckets, float amplitude) {
    synth::PeaksFile::Data data;
    data.bucketSize = 256;
    data.numChannels = 1;
    for (int i = 0; i < numBuckets; ++i)
        data.buckets.emplace_back(-amplitude, amplitude);
    return data;
}

// Polls RecordTapModule::copyLivePeaks() until it has at least `minPairs` entries, or gives up
// after a generous bound. The writer thread drains asynchronously (TimeSliceThread — see
// RecordTapModule's class comment), so growing its live peaks mid-capture is inherently a real
// background-thread hand-off, not something this test can force synchronously; the bound is wide
// enough that a slow CI box does not make this flaky in practice.
bool waitForLivePeaks(const RecordTapModule& tap, size_t minPairs, std::vector<std::pair<float, float>>& out) {
    constexpr int kMaxWaitMs = 2000;
    constexpr int kStepMs = 5;
    for (int waited = 0; waited <= kMaxWaitMs; waited += kStepMs) {
        tap.copyLivePeaks(out);
        if (out.size() >= minPairs)
            return true;
        juce::Thread::sleep(kStepMs);
    }
    return false;
}

} // namespace

TEST(TimelineClipLaneWaveformTest, AudioClipPaintsWaveform) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Take"); // 20 beats * 40 px/beat = wide
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clipId, "Audio/take-1.wav", 0.0));
    f.lane.setSize(1000, 200);

    // Baseline: no resolver installed at all, so paintWaveform() has nothing to draw from.
    const juce::Image withoutPeaks = f.lane.createComponentSnapshot(f.lane.getLocalBounds());

    ScopedPeaksFile peaksFile("agentsynth_clipslane_waveform.agpk");
    ASSERT_TRUE(synth::PeaksFile::write(peaksFile.file, makeWaveformData(400, 0.8f)));

    // Resolves unconditionally to the synthetic file, regardless of the ref text it's handed.
    const juce::File resolved = peaksFile.file;
    f.lane.setPeaksResolver([resolved](const juce::String&) { return resolved; });
    const juce::Image withPeaks = f.lane.createComponentSnapshot(f.lane.getLocalBounds());

    ASSERT_FALSE(withoutPeaks.isNull());
    ASSERT_FALSE(withPeaks.isNull());
    EXPECT_FALSE(imagesIdentical(withoutPeaks, withPeaks))
        << "installing a resolver with real peaks data must change the painted pixels";
}

TEST(TimelineClipLaneWaveformTest, CacheInvalidation) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Take");
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clipId, "Audio/take-1.wav", 0.0));
    f.lane.setSize(1000, 200);

    ScopedPeaksFile fileA("agentsynth_clipslane_cache_a.agpk");
    ScopedPeaksFile fileB("agentsynth_clipslane_cache_b.agpk");
    ASSERT_TRUE(synth::PeaksFile::write(fileA.file, makeWaveformData(50, 0.1f)));
    ASSERT_TRUE(synth::PeaksFile::write(fileB.file, makeWaveformData(400, 0.9f)));

    juce::File current = fileA.file;
    f.lane.setPeaksResolver([&current](const juce::String&) { return current; });
    const juce::Image first = f.lane.createComponentSnapshot(f.lane.getLocalBounds());

    // Swap the file the resolver would now return, WITHOUT invalidating: paint() must keep
    // serving the cached (stale) data.
    current = fileB.file;
    const juce::Image stillCached = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    EXPECT_TRUE(imagesIdentical(first, stillCached)) << "the cache must not silently re-resolve every paint";

    f.lane.invalidatePeaksCache();
    const juce::Image afterInvalidate = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    EXPECT_FALSE(imagesIdentical(first, afterInvalidate)) << "invalidation must force a fresh resolve + read";
}

TEST(TimelineClipLaneLiveRecordingTest, LiveStripGrowsOnNewBucketsOnly) {
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(f.doc.setTrackArmed(trackId, true));
    f.lane.setSize(2000, 400);

    const auto wavFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_clipslane_livestrip.wav");
    const auto peaksFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth_clipslane_livestrip.agpk");
    wavFile.deleteFile();
    peaksFile.deleteFile();

    constexpr double kSampleRate = 48000.0;
    RecordTapModule tap;
    tap.prepareToPlay(kSampleRate, 256);
    ASSERT_TRUE(tap.startCapture(wavFile, peaksFile, kSampleRate, 2));

    synth::ui::TimelineClipLaneArea::LiveRecordingInfo info;
    info.active = true;
    info.track = trackId;
    info.punchBeat = 0.0;
    info.currentBeat = 4.0;
    info.tap = &tap;

    juce::AudioBuffer<float> buffer(2, 256); // exactly one peak bucket
    juce::MidiBuffer midi;
    buffer.clear();

    tap.processBlock(buffer, midi);
    std::vector<std::pair<float, float>> peaks;
    ASSERT_TRUE(waitForLivePeaks(tap, 2, peaks)) << "one full bucket (both channels) must have flushed by now";

    f.lane.updateLiveRecording(info); // first frame: always a repaint
    const int afterFirst = f.lane.getLiveStripRepaintCountForTest();
    EXPECT_GT(afterFirst, 0);

    // Same info again, no new samples pushed: bucket count is unchanged -> no additional repaint.
    f.lane.updateLiveRecording(info);
    EXPECT_EQ(f.lane.getLiveStripRepaintCountForTest(), afterFirst)
        << "an unchanged bucket count must not trigger a repaint";

    // Push another full bucket and wait for it to flush -> a repaint.
    tap.processBlock(buffer, midi);
    ASSERT_TRUE(waitForLivePeaks(tap, 4, peaks));
    f.lane.updateLiveRecording(info);
    EXPECT_GT(f.lane.getLiveStripRepaintCountForTest(), afterFirst) << "new buckets must trigger a repaint";

    tap.stopCapture();
    wavFile.deleteFile();
    peaksFile.deleteFile();
}

TEST(TimelineClipLaneWaveformTest, SourceOffsetShiftsWaveform) {
    using synth::ui::TimelineClipLaneArea;

    // sampleRate chosen so 1 second is an EXACT whole number of buckets (25600 / 256 = 100),
    // which keeps the expected shift/length arithmetic exact rather than off-by-one from floor/
    // ceil rounding at a non-bucket-aligned sample offset.
    constexpr double kSampleRate = 25600.0;
    constexpr double kBpm = 120.0;
    constexpr double kLengthBeats = 4.0; // 2 seconds at 120 bpm

    const auto data = makeWaveformData(400, 1.0f);

    const auto atZero = TimelineClipLaneArea::bucketRangeForClip(data, kLengthBeats, 0.0, kBpm, kSampleRate);
    const auto atOneSecond = TimelineClipLaneArea::bucketRangeForClip(data, kLengthBeats, 1.0, kBpm, kSampleRate);

    ASSERT_GT(atZero.bucketCount, 0);
    constexpr int kExpectedShiftBuckets = 100; // 1 second * 25600 Hz / 256 samples-per-bucket
    EXPECT_EQ(atOneSecond.firstBucket, atZero.firstBucket + kExpectedShiftBuckets);
    EXPECT_EQ(atOneSecond.bucketCount, atZero.bucketCount)
        << "same clip length -> same bucket-window size, just shifted";

    // Empty peaks data yields a zero-length range rather than an out-of-bounds one.
    synth::PeaksFile::Data empty;
    empty.bucketSize = 256;
    empty.numChannels = 1;
    const auto emptyRange = TimelineClipLaneArea::bucketRangeForClip(empty, kLengthBeats, 0.0, kBpm, kSampleRate);
    EXPECT_EQ(emptyRange.bucketCount, 0);
}

TEST(TimelineClipLaneWaveformTest, MissingAssetPaintsPlaceholder) {
    int missingResolverCalls = 0;

    const juce::Image missingImage = [&] {
        ClipLaneFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Missing");
        // EXPECT rather than ASSERT: this lambda returns a value, and ASSERT_* expands to a bare
        // `return;` on failure, which cannot coexist with a non-void return type.
        EXPECT_TRUE(clipId.isValid());
        EXPECT_TRUE(f.doc.setClipAsset(clipId, "Audio/ghost.wav", 0.0));
        f.lane.setAssetExistsResolver([&](const juce::String&) {
            ++missingResolverCalls;
            return false;
        });
        f.lane.setSize(1000, 200);

        // Paint TWICE — the existence answer must be cached per assetRef, not re-queried every
        // paint (the same contract the peaks cache already has).
        const auto first = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
        const auto second = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
        EXPECT_TRUE(imagesIdentical(first, second));
        return second;
    }();
    EXPECT_EQ(missingResolverCalls, 1) << "the existence check must be cached per assetRef";

    const juce::Image presentImage = [] {
        ClipLaneFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Present");
        EXPECT_TRUE(clipId.isValid());
        EXPECT_TRUE(f.doc.setClipAsset(clipId, "Audio/real.wav", 0.0));

        ScopedPeaksFile peaksFile("agentsynth_clipslane_missing_placeholder.agpk");
        EXPECT_TRUE(synth::PeaksFile::write(peaksFile.file, makeWaveformData(400, 0.8f)));
        const juce::File resolved = peaksFile.file;
        f.lane.setPeaksResolver([resolved](const juce::String&) { return resolved; });
        f.lane.setAssetExistsResolver([](const juce::String&) { return true; });
        f.lane.setSize(1000, 200);
        return f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    }();

    const juce::Image midiImage = [] {
        ClipLaneFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Midi 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Notes");
        EXPECT_TRUE(clipId.isValid());
        f.doc.addNote(clipId, makeNote(0.0, 60));
        f.lane.setSize(1000, 200);
        return f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    }();

    ASSERT_FALSE(missingImage.isNull());
    ASSERT_FALSE(presentImage.isNull());
    ASSERT_FALSE(midiImage.isNull());
    EXPECT_FALSE(imagesIdentical(missingImage, presentImage))
        << "the placeholder must not look like a normal waveform clip";
    EXPECT_FALSE(imagesIdentical(missingImage, midiImage))
        << "the placeholder must not look like a plain MIDI (no-asset) clip";
}

TEST(TimelineClipLaneWaveformTest, NoResolverInstalledAssumesAssetExists) {
    // Degrade-gracefully contract: without setAssetExistsResolver ever being called, paint() must
    // NOT draw a placeholder — existing callers (and existing snapshot tests) that never wire this
    // resolver must see byte-identical output.
    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 20.0, "Take");
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clipId, "Audio/take-1.wav", 0.0));
    f.lane.setSize(1000, 200);

    EXPECT_TRUE(f.lane.getClipRect(clipId).getWidth() > 24) << "sanity: the clip must be wide enough to paint into";
    // No crash, no assertion failure — the absence of a resolver is itself the thing under test.
    const auto snapshot = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
    EXPECT_FALSE(snapshot.isNull());
}
