// The sound preview, measured in rendered audio: heard at the slider's gain after a muted master, a cut without a
// click, a loop that starts on the exact sample of the next bar (with automation slicing on and off), the plugin's
// processBlock, no allocation on the audio thread, and nothing of it in a bounce.
#include "../Modules/Filter/AllocationCounter.h"
#include "AudioEngine/AudioEngine.h"
#include "Plugin/PluginProcessor.h"
#include "SoundBrowser/SoundPreviewPlayer.h"
#include "SoundBrowserTestKit.h"
#include "Transport/BounceExporter.h"
#include "Transport/OfflineTransportDriver.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>

using synth::soundbrowser::SoundPreviewPlayer;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Clips {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("preview-clips-" + juce::Uuid().toDashedString());
    ~Clips() { dir.deleteRecursively(); }
    juce::File make(const juce::String& name, double seconds, float amplitude, double hz) const {
        const auto f = dir.getChildFile(name + ".wav");
        sound_browser_test::writeSignal(f, seconds, kRate, amplitude, hz, 2);
        return f;
    }
};

struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    Fixture() {
        engine.initialise();
        engine.getGraph().clear();
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kRate, kBlock, 2);
    }
    ~Fixture() {
        engine.releaseFromHost();
        engine.shutdown();
    }
    SoundPreviewPlayer& preview() { return engine.getSoundPreview(); }
    /** Starts `file` and waits until it is decoded and queued for the audio thread. */
    void start(const juce::File& file, bool alignToBar = false) {
        preview().play(file, alignToBar);
        ASSERT_TRUE(preview().waitForDecodeForTests(10000));
    }
};

float peak(const juce::AudioBuffer<float>& b, int from, int to, int channel = 0) {
    float p = 0.0f;
    for (int i = from; i < to; ++i)
        p = std::max(p, std::abs(b.getSample(channel, i)));
    return p;
}

float maxStep(const juce::AudioBuffer<float>& b, int from, int to) {
    float m = 0.0f;
    for (int i = from + 1; i < to; ++i)
        m = std::max(m, std::abs(b.getSample(0, i) - b.getSample(0, i - 1)));
    return m;
}

int firstNonZero(const juce::AudioBuffer<float>& b) {
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (b.getSample(0, i) != 0.0f)
            return i;
    return -1;
}

float db(float a, float b) { return 20.0f * std::log10(a / b); }

} // namespace

TEST(SoundPreviewPlayerTest, IsHeardAtTheSliderGainAfterAMutedMaster) {
    Clips clips;
    Fixture f;
    f.engine.setMasterMute(true);
    f.preview().setGainDb(-6.0f);
    f.start(clips.make("tone", 1.0, 0.5f, 1000.0));
    const auto audio = f.driver->renderBlocks(40);
    const float expected = 0.5f * std::pow(10.0f, -6.0f / 20.0f);
    const float measured = peak(audio, 2000, audio.getNumSamples());
    RecordProperty("preview_peak_at_minus_6_db", std::to_string(measured));
    EXPECT_NEAR(db(measured, expected), 0.0f, 0.5f) << "peak " << measured << " expected " << expected;
    EXPECT_NEAR(db(peak(audio, 2000, audio.getNumSamples(), 1), expected), 0.0f, 0.5f);

    f.preview().setGainDb(0.0f);
    const auto louder = f.driver->renderBlocks(20);
    EXPECT_NEAR(db(peak(louder, 4000, louder.getNumSamples()), 0.5f), 0.0f, 0.5f);

    f.preview().setGainDb(-60.0f);
    f.driver->renderBlocks(4); // the level glides to silence
    const auto silent = f.driver->renderBlocks(4);
    EXPECT_EQ(peak(silent, 0, silent.getNumSamples()), 0.0f);
}

TEST(SoundPreviewPlayerTest, TheNextSelectionCutsTheOldSoundWithoutAClick) {
    Clips clips;
    Fixture f;
    f.preview().setGainDb(0.0f);
    f.start(clips.make("loud", 3.0, 0.25f, 1000.0));
    auto audio = f.driver->renderBlocks(8);
    f.start(clips.make("quiet", 3.0, 0.05f, 1000.0));
    audio.setSize(2, audio.getNumSamples() + 20 * kBlock, true);
    const auto next = f.driver->renderBlocks(20);
    for (int ch = 0; ch < 2; ++ch)
        audio.copyFrom(ch, 8 * kBlock, next, ch, 0, next.getNumSamples());

    const float step = maxStep(audio, 0, audio.getNumSamples());
    const float sineStep = 0.25f * 2.0f * juce::MathConstants<float>::pi * 1000.0f / (float)kRate;
    RecordProperty("max_sample_step_over_cut", std::to_string(step));
    EXPECT_LT(step, sineStep * 1.3f) << "a click would show as a step larger than the loud sine's own";
    // The old sound is gone: later audio carries only the quiet clip.
    EXPECT_NEAR(db(peak(audio, audio.getNumSamples() - 4000, audio.getNumSamples()), 0.05f), 0.0f, 0.5f);
}

TEST(SoundPreviewPlayerTest, StopFadesOutAndSilences) {
    Clips clips;
    Fixture f;
    f.preview().setGainDb(0.0f);
    f.start(clips.make("loud", 3.0, 0.25f, 1000.0));
    f.driver->renderBlocks(8);
    EXPECT_TRUE(f.preview().isPlaying());
    f.preview().stop();
    const auto audio = f.driver->renderBlocks(4);
    EXPECT_LT(maxStep(audio, 0, audio.getNumSamples()), 0.05f);
    EXPECT_EQ(peak(audio, 2 * kBlock, 4 * kBlock), 0.0f);
    EXPECT_FALSE(f.preview().isPlaying());
}

TEST(SoundPreviewPlayerTest, ALoopStartsOnTheExactSampleOfTheNextBar) {
    Clips clips;
    const auto loop = clips.make("loop", 4.0, 0.5f, 0.0); // a constant, so its first sample is measurable
    for (const bool slicing : {false, true}) {
        Fixture f;
        f.engine.setAutomationSlicingEnabled(slicing);
        f.preview().setGainDb(0.0f);
        ASSERT_TRUE(f.engine.getTransport().setBpm(120.0));
        ASSERT_TRUE(f.engine.getTransport().locateBeat(0.5));
        ASSERT_TRUE(f.engine.getTransport().play());
        f.start(loop, true);
        const auto audio = f.driver->renderBlocks(200);
        // 120 BPM at 48 kHz is 24000 samples a beat; beat 0.5 to the bar line at beat 4 is 3.5 beats.
        EXPECT_EQ(firstNonZero(audio), 84000) << "slicing " << slicing;
    }
}

TEST(SoundPreviewPlayerTest, ALoopStartsAtOnceWhenTheSongIsStopped) {
    Clips clips;
    Fixture f;
    f.preview().setGainDb(0.0f);
    f.start(clips.make("loop", 1.0, 0.5f, 0.0), true);
    const auto audio = f.driver->renderBlocks(4);
    EXPECT_EQ(firstNonZero(audio), 0);
}

TEST(SoundPreviewPlayerTest, PluginProcessBlockContainsThePreview) {
    Clips clips;
    synth::AgentSynthAudioProcessor processor;
    processor.getAudioEngine().getGraph().clear();
    processor.prepareToPlay(kRate, kBlock);
    auto& preview = processor.getAudioEngine().getSoundPreview();
    preview.setGainDb(-6.0f);
    preview.play(clips.make("tone", 1.0, 0.5f, 1000.0), false);
    ASSERT_TRUE(preview.waitForDecodeForTests(10000));

    float measured = 0.0f;
    juce::MidiBuffer midi;
    for (int i = 0; i < 20; ++i) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        processor.processBlock(buffer, midi);
        if (i >= 4)
            measured = std::max(measured, buffer.getMagnitude(0, 0, kBlock));
    }
    const float expected = 0.5f * std::pow(10.0f, -6.0f / 20.0f);
    EXPECT_NEAR(db(measured, expected), 0.0f, 0.5f) << measured;
    processor.releaseResources();
}

TEST(SoundPreviewPlayerTest, TheAudioThreadPathAllocatesNothing) {
    Clips clips;
    SoundPreviewPlayer player;
    player.prepare(kRate, kBlock);
    player.setGainDb(-3.0f);
    const auto a = clips.make("a", 1.0, 0.3f, 440.0);
    const auto b = clips.make("b", 0.2, 0.3f, 880.0);
    player.play(a, true);
    ASSERT_TRUE(player.waitForDecodeForTests(10000));

    synth::BlockTimeInfo info;
    info.sampleRate = kRate;
    info.bpm = 120.0;
    info.timeSigNumerator = 4;
    info.timeSigDenominator = 4;
    info.playing = true;
    info.numSamples = kBlock;
    info.loopWrapSample = -1;
    juce::AudioBuffer<float> buffer(2, kBlock);
    int allocations = 0;
    for (int block = 0; block < 200; ++block) {
        if (block == 20) {
            player.play(b, false); // decode runs on its own thread, then queues a start for the audio thread
            ASSERT_TRUE(player.waitForDecodeForTests(10000));
        }
        if (block == 60)
            player.stop();
        if (block == 80) {
            player.play(a, false);
            ASSERT_TRUE(player.waitForDecodeForTests(10000));
        }
        info.startPpq = (double)block * kBlock * 120.0 / 60.0 / kRate;
        info.endPpq = info.startPpq + kBlock * info.beatsPerSample();
        buffer.clear();
        synth::test::AllocationScope scope;
        player.process(buffer, 2, info);
        allocations += scope.count();
    }
    EXPECT_EQ(allocations, 0);
}

TEST(SoundPreviewPlayerTest, ABounceNeverContainsThePreview) {
    Clips clips;
    Fixture f;
    f.preview().setGainDb(0.0f);
    const auto tone = clips.make("tone", 6.0, 0.5f, 1000.0);
    f.start(tone);
    f.driver->renderBlocks(4);
    EXPECT_TRUE(f.preview().isPlaying());

    const auto out = clips.dir.getChildFile("bounce.wav");
    synth::BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 4.0;
    options.tailSeconds = 0.0;
    options.sampleRate = kRate;
    options.blockSize = kBlock;
    options.bitDepth = 24;
    const auto result = synth::BounceExporter::bounce(f.engine, out, options);
    ASSERT_TRUE(result.ok) << result.message;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(out.createInputStream().release(), true));
    ASSERT_NE(reader, nullptr);
    juce::AudioBuffer<float> audio((int)reader->numChannels, (int)reader->lengthInSamples);
    reader->read(&audio, 0, (int)reader->lengthInSamples, 0, true, true);
    EXPECT_EQ(audio.getMagnitude(0, audio.getNumSamples()), 0.0f);

    // The preview is not left suspended: the next one is heard again.
    f.start(tone);
    const auto after = f.driver->renderBlocks(20);
    EXPECT_GT(peak(after, 4000, after.getNumSamples()), 0.4f);
}
