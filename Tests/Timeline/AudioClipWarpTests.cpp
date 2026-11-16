// Topic: an audio clip that follows the song's tempo (Clip::sourceBpm) keeps its musical length and its pitch at any
// song tempo, stretched live while it plays.

#include "../Engine/BounceExporter/BounceExporterTestHelpers.h"
#include "Timeline/AudioClipStreamer.h"
#include "Timeline/AudioClipWarp.h"
#include <cmath>

namespace {

constexpr double kLowHz = 441.0;
constexpr double kHighHz = 882.0;
constexpr double kHalfSeconds = 1.0; // each tone; the file is 2 s, 4 beats at its own 120 BPM
constexpr float kAmplitude = 0.5f;
constexpr double kLoopBpm = 120.0;

/** A mono file at 48 kHz: kLowHz for kHalfSeconds, then kHighHz for kHalfSeconds. Where each tone sounds in a render
 *  shows where each half of the loop landed. */
bool writeTwoToneWav(const juce::File& file) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), kSampleRate, 1, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    const int half = (int)std::llround(kHalfSeconds * kSampleRate);
    juce::AudioBuffer<float> buffer(1, half * 2);
    double phase = 0.0;
    for (int i = 0; i < half * 2; ++i) {
        buffer.setSample(0, i, kAmplitude * (float)std::sin(phase));
        phase += 2.0 * juce::MathConstants<double>::pi * (i < half ? kLowHz : kHighHz) / kSampleRate;
    }
    return writer->writeFromAudioSampleBuffer(buffer, 0, half * 2);
}

/** Frequency from the spacing of positive-going zero crossings in [begin, end). */
double estimateHz(const float* data, int begin, int end, double rate) {
    double first = -1.0;
    double last = -1.0;
    int crossings = 0;
    for (int i = std::max(1, begin + 1); i < end; ++i) {
        if (data[i - 1] < 0.0f && data[i] >= 0.0f) {
            const double t = (i - 1) + (double)(-data[i - 1]) / (double)(data[i] - data[i - 1]);
            if (crossings == 0)
                first = t;
            last = t;
            ++crossings;
        }
    }
    return crossings < 2 ? 0.0 : (crossings - 1) * rate / (last - first);
}

double rms(const float* data, int begin, int end) {
    double sum = 0.0;
    for (int i = begin; i < end; ++i)
        sum += (double)data[i] * data[i];
    return end > begin ? std::sqrt(sum / (end - begin)) : 0.0;
}

struct WarpRender {
    WavContents wav;
    int result = 0;
};

/** Bounces a 4-beat clip of the two-tone loop (sourceBpm `clipBpm`, 0 = plays at its own speed) at `songBpm`. */
WarpRender bounceLoopAt(double songBpm, double clipBpm, double clipLengthBeats = 4.0) {
    AudioClipFixture f;
    WarpRender out;
    if (!f.build(/*pausePrefetch=*/true, 0.0, clipLengthBeats, kBeatSamples) ||
        !writeTwoToneWav(f.bundle.dir.getChildFile(kClipAssetRef)) || !f.doc.setClipSourceBpm(f.clipId, clipBpm))
        return out;
    f.engine.publishTimeline(f.doc);
    f.engine.getTransport().setBpm(songBpm);

    ScopedTempFile file("agentsynth_clip_warp.wav");
    auto options = defaultOptions();
    options.endBeat = clipLengthBeats + 2.0;
    options.bitDepth = 32;
    const auto result = BounceExporter::bounce(f.engine, file.file, options);
    out.result = result.ok ? 1 : 0;
    out.wav = readWav(file.file);
    return out;
}

} // namespace

TEST(AudioClipWarpTest, AtTwiceItsTempoTheLoopFillsItsBeatsInHalfTheTimeAtTheSamePitch) {
    // Regression test for FRO1393: a loop placed on a track did not follow a tempo change, so a faster song cut the
    // second half of the loop off the clip.
    const auto render = bounceLoopAt(240.0, kLoopBpm);
    ASSERT_EQ(render.result, 1);
    ASSERT_TRUE(render.wav.ok);
    const float* data = render.wav.audio.getReadPointer(0);
    const double rate = render.wav.sampleRate;
    // 4 beats at 240 BPM = 1 s: the low half in [0, 0.5) s and the high half in [0.5, 1.0) s.
    EXPECT_NEAR(estimateHz(data, (int)(0.1 * rate), (int)(0.4 * rate), rate), kLowHz, kLowHz * 0.01);
    EXPECT_NEAR(estimateHz(data, (int)(0.6 * rate), (int)(0.9 * rate), rate), kHighHz, kHighHz * 0.01);
    EXPECT_GT(rms(data, (int)(0.1 * rate), (int)(0.9 * rate)), 0.25);
    EXPECT_LT(rms(data, (int)(1.05 * rate), (int)(1.4 * rate)), 1.0e-3); // nothing past the clip's end
}

TEST(AudioClipWarpTest, AtHalfItsTempoTheLoopFillsItsBeatsWithNoGapAtTheSamePitch) {
    const auto render = bounceLoopAt(60.0, kLoopBpm);
    ASSERT_EQ(render.result, 1);
    ASSERT_TRUE(render.wav.ok);
    const float* data = render.wav.audio.getReadPointer(0);
    const double rate = render.wav.sampleRate;
    // 4 beats at 60 BPM = 4 s: the low half in [0, 2) s, the high half in [2, 4) s, sound to the end.
    EXPECT_NEAR(estimateHz(data, (int)(0.3 * rate), (int)(1.7 * rate), rate), kLowHz, kLowHz * 0.01);
    EXPECT_NEAR(estimateHz(data, (int)(2.3 * rate), (int)(3.7 * rate), rate), kHighHz, kHighHz * 0.01);
    EXPECT_GT(rms(data, (int)(3.6 * rate), (int)(3.9 * rate)), 0.25);
}

TEST(AudioClipWarpTest, AtItsOwnTempoTheLoopPlaysTheFileUntouched) {
    const auto render = bounceLoopAt(kLoopBpm, kLoopBpm);
    ASSERT_EQ(render.result, 1);
    ASSERT_TRUE(render.wav.ok);
    const float* data = render.wav.audio.getReadPointer(0);
    const double rate = render.wav.sampleRate;
    EXPECT_NEAR(estimateHz(data, (int)(0.1 * rate), (int)(0.9 * rate), rate), kLowHz, kLowHz * 0.002);
    EXPECT_NEAR(estimateHz(data, (int)(1.1 * rate), (int)(1.9 * rate), rate), kHighHz, kHighHz * 0.002);
}

TEST(AudioClipWarpTest, ARecordingStillPlaysAtItsOwnSpeed) {
    // sourceBpm 0: at 240 BPM the 4-beat clip is 1 s of the file, all of it the low tone.
    const auto render = bounceLoopAt(240.0, 0.0);
    ASSERT_EQ(render.result, 1);
    ASSERT_TRUE(render.wav.ok);
    const float* data = render.wav.audio.getReadPointer(0);
    const double rate = render.wav.sampleRate;
    EXPECT_NEAR(estimateHz(data, (int)(0.1 * rate), (int)(0.9 * rate), rate), kLowHz, kLowHz * 0.002);
}

TEST(AudioClipWarpTest, TheSourceFrameOfAClipThatFollowsTheTempoDependsOnTheBeatAlone) {
    // Beat 2 of a 120 BPM loop is 1 s into the file at any song tempo; a recording's frame follows the song's.
    for (const double songBpm : {60.0, 120.0, 240.0})
        EXPECT_EQ(synth::sourceFrameForClipBeat(0.0, 0.0, kLoopBpm, 2.0, songBpm, kSampleRate), 48000);
    EXPECT_EQ(synth::sourceFrameForClipBeat(0.0, 0.0, 0.0, 2.0, 240.0, kSampleRate), 24000);
}

TEST(AudioClipWarpTest, TheSnapshotCarriesTheClipsTempo) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(doc.setClipAsset(clip, kClipAssetRef, 0.0));
    ASSERT_TRUE(doc.setClipSourceBpm(clip, 96.0));
    const auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_EQ(snapshot->audioClips.size(), 1u);
    EXPECT_DOUBLE_EQ(snapshot->audioClips[0].sourceBpm, 96.0);
}

TEST(AudioClipWarpTest, ConsecutiveWarpedReadsContinueWithoutGaps) {
    synth::AudioClipStreamer streamer;
    ScopedTempDir root{"agentsynth_clip_warp_stream"};
    ASSERT_TRUE(writeTwoToneWav(root.dir.getChildFile(kClipAssetRef)));
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(doc.setClipAsset(clip, kClipAssetRef, 0.0));
    ASSERT_TRUE(doc.setClipSourceBpm(clip, kLoopBpm));

    streamer.setPrefetchPausedForTest(true);
    streamer.setAssetRoots(root.dir, juce::File());
    streamer.setEngineSampleRate(kSampleRate);
    streamer.syncToSnapshot(*TimelineSnapshot::buildFrom(doc));
    streamer.pumpForTest();
    const auto* handle = streamer.acquire(clip.value);
    ASSERT_NE(handle, nullptr);

    // 1.5 times faster, in blocks of 256 from the file's start: every block is served and the tone is steady.
    constexpr double kRatio = 1.5;
    constexpr int kBlock = 256;
    constexpr int kBlocks = 96;
    std::vector<float> left(kBlock * kBlocks), right(kBlock * kBlocks);
    for (int b = 0; b < kBlocks; ++b) {
        const double position = (double)b * kBlock * kRatio;
        EXPECT_EQ(streamer.readWarpedFrames(handle, position, kRatio, left.data() + b * kBlock,
                                            right.data() + b * kBlock, kBlock),
                  kBlock)
            << "block " << b;
        streamer.pumpForTest();
    }
    const int n = kBlock * kBlocks;
    EXPECT_NEAR(estimateHz(left.data(), n / 8, n - n / 8, kSampleRate), kLowHz, kLowHz * 0.01);
    // No dropout: every 256-frame block after the stretch's own onset carries the tone.
    for (int b = 2; b < kBlocks; ++b)
        EXPECT_GT(rms(left.data(), b * kBlock, (b + 1) * kBlock), 0.2) << "block " << b;
}

TEST(AudioClipWarpTest, ALocateIntoTheLoopPlaysFromThere) {
    synth::AudioClipStreamer streamer;
    ScopedTempDir root{"agentsynth_clip_warp_seek"};
    ASSERT_TRUE(writeTwoToneWav(root.dir.getChildFile(kClipAssetRef)));
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(doc.setClipAsset(clip, kClipAssetRef, 0.0));
    ASSERT_TRUE(doc.setClipSourceBpm(clip, kLoopBpm));

    streamer.setPrefetchPausedForTest(true);
    streamer.setAssetRoots(root.dir, juce::File());
    streamer.setEngineSampleRate(kSampleRate);
    streamer.syncToSnapshot(*TimelineSnapshot::buildFrom(doc));
    streamer.pumpForTest();
    const auto* handle = streamer.acquire(clip.value);
    ASSERT_NE(handle, nullptr);

    // A locate to the high half (1.2 s in): the stretch seeks there, so what plays is the high tone.
    constexpr int kBlock = 512;
    const double position = 1.2 * kSampleRate;
    std::vector<float> outL(kBlock * 16), outR(kBlock * 16);
    for (int b = 0; b < 16; ++b) {
        EXPECT_EQ(streamer.readWarpedFrames(handle, position + b * kBlock * 2.0, 2.0, outL.data() + b * kBlock,
                                            outR.data() + b * kBlock, kBlock),
                  kBlock);
        streamer.pumpForTest();
    }
    EXPECT_NEAR(estimateHz(outL.data(), kBlock * 2, kBlock * 14, kSampleRate), kHighHz, kHighHz * 0.01);
}
