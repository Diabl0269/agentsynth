// Topic: an imported audio clip plays at its true pitch and length whatever the file and engine rates.

#include "../Engine/BounceExporter/BounceExporterTestHelpers.h"
#include "Timeline/AudioClipStreamer.h"
#include <cmath>
#include <tuple>

namespace {

constexpr double kToneHz = 441.0;
constexpr double kToneSeconds = 2.0;
constexpr float kToneAmplitude = 0.5f;

/** A mono sine of `kToneSeconds` at `fileRate`. */
bool writeSineWav(const juce::File& file, double fileRate) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), fileRate, 1, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();

    const int numFrames = (int)std::llround(kToneSeconds * fileRate);
    juce::AudioBuffer<float> buffer(1, numFrames);
    for (int i = 0; i < numFrames; ++i)
        buffer.setSample(
            0, i, kToneAmplitude * (float)std::sin(2.0 * juce::MathConstants<double>::pi * kToneHz * i / fileRate));
    return writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
}

/** Frequency from the spacing of the first and last positive-going zero crossings in [begin, end). */
double estimateHz(const float* data, int begin, int end, double rate) {
    double first = -1.0;
    double last = -1.0;
    int crossings = 0;
    for (int i = begin + 1; i < end; ++i) {
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

/** Seconds up to the last sample louder than -60 dBFS. */
double audibleSeconds(const float* data, int numSamples, double rate) {
    for (int i = numSamples - 1; i >= 0; --i)
        if (std::abs(data[i]) > 1.0e-3f)
            return (i + 1) / rate;
    return 0.0;
}

} // namespace

class AudioClipSampleRateTest : public ::testing::TestWithParam<std::tuple<double, double>> {};

TEST_P(AudioClipSampleRateTest, BouncedClipKeepsItsPitchAndLength) {
    // Regression test for FRO1520: a file whose rate differs from the engine's played transposed
    // and short (44.1 kHz on a 48 kHz device: +1.47 semitones, 8% short).
    const double fileRate = std::get<0>(GetParam());
    const double engineRate = std::get<1>(GetParam());

    AudioClipFixture f;
    ASSERT_TRUE(f.build(/*pausePrefetch=*/true, /*clipStartBeat=*/0.0, /*clipLengthBeats=*/6.0,
                        /*sourceFrames=*/kBeatSamples));
    // The fixture wrote its own asset; replace it with the tone before anything opens the file.
    ASSERT_TRUE(writeSineWav(f.bundle.dir.getChildFile(kClipAssetRef), fileRate));

    ScopedTempFile out("agentsynth_clip_samplerate.wav");
    auto options = defaultOptions();
    options.endBeat = 6.0; // 3 seconds: longer than the 2 s file, so a too-long playback is caught too
    options.sampleRate = engineRate;
    options.bitDepth = 32;

    const auto result = BounceExporter::bounce(f.engine, out.file, options);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.streamDropouts, 0) << result.message;

    const auto wav = readWav(out.file);
    ASSERT_TRUE(wav.ok);
    ASSERT_EQ(wav.sampleRate, engineRate);

    const int n = (int)wav.lengthInSamples;
    const float* data = wav.audio.getReadPointer(0);
    const double hz = estimateHz(data, (int)(0.2 * engineRate), (int)(1.8 * engineRate), engineRate);
    EXPECT_NEAR(hz, kToneHz, kToneHz * 0.005) << "file " << fileRate << " on engine " << engineRate;
    EXPECT_NEAR(audibleSeconds(data, n, engineRate), kToneSeconds, 0.005);
}

INSTANTIATE_TEST_SUITE_P(Rates, AudioClipSampleRateTest,
                         ::testing::Combine(::testing::Values(44100.0, 48000.0), ::testing::Values(44100.0, 48000.0)));

TEST(AudioClipSampleRateStreamerTest, AnEngineRateChangeMidLifeRemapsTheStream) {
    synth::AudioClipStreamer streamer;
    ScopedTempDir root{"agentsynth_clipstreamer_rate"};
    constexpr double kFileRate = 48000.0;
    ASSERT_TRUE(writeSineWav(root.dir.getChildFile(kClipAssetRef), kFileRate));

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    ASSERT_TRUE(doc.setClipAsset(clip, kClipAssetRef, 0.0));

    streamer.setPrefetchPausedForTest(true);
    streamer.setAssetRoots(root.dir, juce::File());
    streamer.setEngineSampleRate(44100.0);
    auto snapshot = TimelineSnapshot::buildFrom(doc);
    streamer.syncToSnapshot(*snapshot);

    for (const double engineRate : {44100.0, 48000.0, 44100.0}) {
        streamer.setEngineSampleRate(engineRate);
        streamer.pumpForTest();

        const auto* handle = streamer.acquire(clip.value);
        ASSERT_NE(handle, nullptr);

        constexpr int kFrames = 8192;
        const int start = (int)(0.25 * engineRate);
        std::vector<float> left(kFrames), right(kFrames);
        // A first read steers the stream to where playback is (a miss right after a rate change),
        // the pump refills there, and the second read is served whole.
        streamer.readFrames(handle, start, left.data(), right.data(), kFrames);
        streamer.pumpForTest();
        ASSERT_EQ(streamer.readFrames(handle, start, left.data(), right.data(), kFrames), kFrames);
        EXPECT_NEAR(estimateHz(left.data(), 0, kFrames, engineRate), kToneHz, kToneHz * 0.005)
            << "engine rate " << engineRate;
        EXPECT_NEAR(handle->playbackLengthFrames.load(), kToneSeconds * engineRate, 1.0);
    }
}
