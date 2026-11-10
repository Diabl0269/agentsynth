// What a Track Audio node plays while its track's Monitor hears the live input: the input INSTEAD of the track's own
// clips, never the two summed. Driven through a Hosted engine (prepareForHost + processHostBlock) with a track that
// holds a clip of constant 0.5 and an input that is a small distinct level, so every block is exactly one of: the
// input only, the clip only, silence, or the (forbidden) sum. The streamer's prefetch thread is paused and pumped
// by hand after each block, so nothing races.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/TimelineAudioSourceModule.h"
#include "Timeline/AudioClipStreamer.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kInputs = 2;
constexpr int kBeatSamples = 24000; // 120 BPM at 48 kHz
constexpr float kClipLevel = 0.5f;
constexpr float kInputLevel = 0.125f; // exactly representable; kClipLevel + kInputLevel is the forbidden sum
constexpr const char* kUuid = "c1215000-0000-0000-0000-000000000001";
constexpr const char* kAssetRef = "Audio/take.wav";

enum class Heard { Silent, InputOnly, ClipOnly, Sum, Other };

const char* name(Heard heard) {
    switch (heard) {
    case Heard::Silent:
        return "silent";
    case Heard::InputOnly:
        return "input only";
    case Heard::ClipOnly:
        return "clip only";
    case Heard::Sum:
        return "clip + input";
    case Heard::Other:
        return "something else";
    }
    return "?";
}

bool writeClipWav(const juce::File& file, int frames) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), kRate, 2, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    juce::AudioBuffer<float> chunk(2, 8192);
    for (int written = 0; written < frames; written += 8192) {
        const int n = std::min(8192, frames - written);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < n; ++i)
                chunk.setSample(channel, i, kClipLevel);
        if (!writer->writeFromAudioSampleBuffer(chunk, 0, n))
            return false;
    }
    return true;
}

struct ClipMonitorRig {
    juce::File bundle = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("agentsynth_monitorclip_" + juce::Uuid().toString());
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    synth::TimelineDoc doc;
    synth::TrackId track;
    bool ready = false;

    ClipMonitorRig() {
        bundle.createDirectory();
        engine.getAudioClipStreamer().setPrefetchPausedForTest(true);
        engine.initialise();
        const juce::String patch = juce::String(R"({
            "nodes": [
                {"id": 1, "type": "Track Audio",  "uuid": ")") +
                                   kUuid + R"("},
                {"id": 2, "type": "Audio Output", "uuid": "c1215000-0000-0000-0000-000000000002"}
            ],
            "connections": [
                {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
                {"src": 1, "srcPort": 1, "dst": 2, "dstPort": 1}
            ]
        })";
        if (!synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(patch), engine.getGraph(), true, true))
            return;
        engine.prepareForHost(kRate, kBlock, kInputs, 2);
        engine.getAudioClipStreamer().setAssetRoots(bundle, juce::File());
        if (!writeClipWav(bundle.getChildFile(kAssetRef), 8 * kBeatSamples))
            return;
        track = doc.addTrack(synth::TrackKind::Audio, "Vox");
        const auto clip = doc.addClip(track, 0.0, 8.0, "Take");
        synth::AudioInputChoice choice;
        choice.kind = synth::AudioInputChoice::Kind::Device;
        choice.firstChannel = 0;
        choice.numChannels = 1;
        ready = doc.setTrackBinding(track, kUuid) && doc.setClipAsset(clip, kAssetRef, 0.0) &&
                doc.setTrackAudioInput(track, choice);
        engine.publishTimeline(doc);
        engine.getAudioClipStreamer().pumpForTest();
    }
    ~ClipMonitorRig() {
        engine.releaseFromHost();
        engine.shutdown();
        bundle.deleteRecursively();
    }

    void set(synth::MonitorMode mode, bool armed) {
        ASSERT_TRUE(doc.setTrackMonitorMode(track, mode));
        ASSERT_TRUE(doc.setTrackArmed(track, armed));
        engine.publishTimeline(doc);
        engine.getAudioClipStreamer().pumpForTest();
    }

    // One block; the transport takes a block to act on play()/stop(), so tests render a warm-up block after changing
    // it.
    juce::AudioBuffer<float> render(float inputLevel = kInputLevel) {
        juce::AudioBuffer<float> buffer(kInputs, kBlock);
        for (int channel = 0; channel < kInputs; ++channel)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(channel, i, inputLevel);
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
        engine.getAudioClipStreamer().pumpForTest();
        return buffer;
    }

    void play() {
        ASSERT_TRUE(engine.getTransport().play());
        render();
        render(); // the first block after play() may still be the stopped one
    }

    // Classifies both channels of a block against the four things it may be.
    static Heard classify(const juce::AudioBuffer<float>& out, float inputLevel = kInputLevel) {
        const auto is = [&](float value) {
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < kBlock; ++i)
                    if (std::abs(out.getSample(channel, i) - value) > 1.0e-5f)
                        return false;
            return true;
        };
        if (is(0.0f))
            return Heard::Silent;
        if (is(inputLevel))
            return Heard::InputOnly;
        if (is(kClipLevel))
            return Heard::ClipOnly;
        if (is(kClipLevel + inputLevel))
            return Heard::Sum;
        return Heard::Other;
    }
};

#define EXPECT_HEARD(expected, actual)                                                                                 \
    EXPECT_EQ((int)(expected), (int)(actual)) << "expected " << name(expected) << ", heard " << name(actual)

} // namespace

TEST(TrackInputMonitorClipTest, MonitorOnPlaysTheInputInsteadOfTheClipWhetherPlayingOrStopped) {
    ClipMonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.set(synth::MonitorMode::On, false);
    EXPECT_HEARD(Heard::InputOnly, ClipMonitorRig::classify(rig.render())); // stopped
    rig.play();
    for (int block = 0; block < 8; ++block)
        EXPECT_HEARD(Heard::InputOnly, ClipMonitorRig::classify(rig.render())); // never the sum
}

TEST(TrackInputMonitorClipTest, OffPlaysTheClipAndNeverTheInput) {
    ClipMonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.set(synth::MonitorMode::Off, true);
    EXPECT_HEARD(Heard::Silent, ClipMonitorRig::classify(rig.render())) << "stopped: no clips, no input";
    rig.play();
    EXPECT_HEARD(Heard::ClipOnly, ClipMonitorRig::classify(rig.render()));
}

TEST(TrackInputMonitorClipTest, AutoHearsTheInputWhenStoppedAndRecordingAndTheClipDuringPlainPlayback) {
    ClipMonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.set(synth::MonitorMode::Auto, true);
    EXPECT_HEARD(Heard::InputOnly, ClipMonitorRig::classify(rig.render())) << "armed and stopped";

    rig.play();
    EXPECT_HEARD(Heard::ClipOnly, ClipMonitorRig::classify(rig.render())) << "plain playback";

    rig.engine.setRecordingEngaged(true);
    for (int block = 0; block < 4; ++block)
        EXPECT_HEARD(Heard::InputOnly, ClipMonitorRig::classify(rig.render())) << "recording: the old take is silent";

    rig.engine.setRecordingEngaged(false);
    EXPECT_HEARD(Heard::ClipOnly, ClipMonitorRig::classify(rig.render())) << "back to plain playback";
}

TEST(TrackInputMonitorClipTest, AutoWhileNotArmedPlaysTheClipAndNotTheInput) {
    ClipMonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.set(synth::MonitorMode::Auto, false);
    rig.play();
    rig.engine.setRecordingEngaged(true);
    EXPECT_HEARD(Heard::ClipOnly, ClipMonitorRig::classify(rig.render()));
}

TEST(TrackInputMonitorClipTest, ATrackWithNoInputKeepsPlayingItsClipUnderMonitorOn) {
    ClipMonitorRig rig;
    ASSERT_TRUE(rig.ready);
    synth::AudioInputChoice none; // No input
    ASSERT_TRUE(rig.doc.setTrackAudioInput(rig.track, none));
    rig.set(synth::MonitorMode::On, true);
    rig.play();
    EXPECT_HEARD(Heard::ClipOnly, ClipMonitorRig::classify(rig.render())) << "nothing to hear, so nothing replaces it";
}

TEST(TrackInputMonitorClipTest, ATrippedFeedbackGuardFallsBackToTheClipNotSilence) {
    ClipMonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.set(synth::MonitorMode::On, true);
    rig.play();

    const int blocksToTrip = (int)std::ceil(synth::kMonitorGuardSustainSeconds * kRate / kBlock);
    for (int block = 0; block < blocksToTrip - 1; ++block)
        EXPECT_HEARD(Heard::InputOnly, ClipMonitorRig::classify(rig.render(1.0f), 1.0f)) << "block " << block;
    // The loud input is the only thing heard until the guard trips: 1.0, not 1.5.
    EXPECT_HEARD(Heard::ClipOnly, ClipMonitorRig::classify(rig.render(1.0f))) << "the tripping block plays the clip";
    EXPECT_HEARD(Heard::ClipOnly, ClipMonitorRig::classify(rig.render())) << "latched: the clip plays, not silence";
}

TEST(TrackInputMonitorClipTest, WhileTheInputIsHeardTheLoudInputIsNeverSummedWithTheClip) {
    ClipMonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.set(synth::MonitorMode::On, true);
    rig.play();
    const auto out = rig.render(0.25f);
    for (int i = 0; i < kBlock; ++i)
        ASSERT_NEAR(out.getSample(0, i), 0.25f, 1.0e-5f);
}

TEST(TrackInputMonitorClipTest, TheSecondPassOfALoopRecordingDoesNotPlayTheFirstPass) {
    ClipMonitorRig rig;
    ASSERT_TRUE(rig.ready);
    ASSERT_TRUE(rig.engine.getTransport().setLoop(0.0, 1.0, true));
    rig.set(synth::MonitorMode::Auto, true);
    rig.engine.setRecordingEngaged(true);
    rig.play();
    // 1 beat is 24000 samples (about 47 blocks): render past the wrap twice over.
    for (int block = 0; block < 110; ++block)
        ASSERT_EQ((int)Heard::InputOnly, (int)ClipMonitorRig::classify(rig.render())) << "block " << block;
}

TEST(TrackInputMonitorClipTest, TheClipComesBackAtOnceAfterALongStretchOfBeingMonitored) {
    ClipMonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.set(synth::MonitorMode::On, true);
    rig.play();
    // 5 beats is further than one ring (kRingFrames) ahead of where reading started: only a prefetch that
    // kept following the playhead has the clip ready.
    for (int block = 0; block < 5 * kBeatSamples / kBlock; ++block)
        rig.render();
    rig.set(synth::MonitorMode::Off, false);
    EXPECT_HEARD(Heard::ClipOnly, ClipMonitorRig::classify(rig.render()));
}
