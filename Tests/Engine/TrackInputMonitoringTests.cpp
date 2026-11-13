// Track input monitoring through a real engine: an audio track's Track Audio node plays the track's own input live
// when its Monitor mode says so, the per-track feedback guard mutes it on sustained clipping and reports it once,
// and the hand-patched Audio Input module passes input only while it is wired. Driven through a Hosted engine
// (prepareForHost + processHostBlock) with a distinct signal on every input channel, so the output names the
// channel it came from.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/AudioInputModule.h"
#include "Modules/TimelineAudioSourceModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kInputs = 4;
constexpr const char* kTrackAudioUuid = "c1180000-0000-0000-0000-000000000001";

float inputSample(int channel, int frame) { return (float)(channel + 1) / 16.0f + (float)(frame % 256) / 8192.0f; }

juce::String patchJson() {
    return juce::String(R"({
        "nodes": [
            {"id": 1, "type": "Track Audio",  "uuid": ")") +
           kTrackAudioUuid + R"("},
            {"id": 2, "type": "Audio Output", "uuid": "c1180000-0000-0000-0000-000000000002"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
            {"src": 1, "srcPort": 1, "dst": 2, "dstPort": 1}
        ]
    })";
}

// A hosted engine whose graph is Track Audio -> Audio Output, and a one-audio-track document bound to the node.
struct MonitorRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    synth::TimelineDoc doc;
    synth::TrackId track;
    bool ready = false;

    MonitorRig() {
        engine.initialise();
        if (!synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(patchJson()), engine.getGraph(), true, true))
            return;
        engine.prepareForHost(kRate, kBlock, kInputs, 2);
        track = doc.addTrack(synth::TrackKind::Audio, "Vox");
        ready = doc.setTrackBinding(track, kTrackAudioUuid);
    }
    ~MonitorRig() {
        engine.releaseFromHost();
        engine.shutdown();
    }

    void setInput(int first, int count) {
        synth::AudioInputChoice choice;
        choice.kind = synth::AudioInputChoice::Kind::Device;
        choice.firstChannel = first;
        choice.numChannels = count;
        ASSERT_TRUE(doc.setTrackAudioInput(track, choice));
        engine.publishTimeline(doc);
    }
    void set(synth::MonitorMode mode, bool armed) {
        ASSERT_TRUE(doc.setTrackMonitorMode(track, mode));
        ASSERT_TRUE(doc.setTrackArmed(track, armed));
        engine.publishTimeline(doc);
    }

    // One host block; `level` overrides the input with a constant (for the clipping guard).
    juce::AudioBuffer<float> render(float level = -1.0f) {
        juce::AudioBuffer<float> buffer(kInputs, kBlock);
        for (int channel = 0; channel < kInputs; ++channel)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(channel, i, level >= 0.0f ? level : inputSample(channel, i));
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
        return buffer;
    }
};

bool outputIsChannel(const juce::AudioBuffer<float>& out, int outChannel, int inputChannel) {
    for (int i = 0; i < kBlock; ++i)
        if (std::abs(out.getSample(outChannel, i) - inputSample(inputChannel, i)) > 1.0e-6f)
            return false;
    return true;
}

bool outputIsSilent(const juce::AudioBuffer<float>& out) {
    for (int channel = 0; channel < 2; ++channel)
        if (out.getMagnitude(channel, 0, kBlock) > 1.0e-7f)
            return false;
    return true;
}

} // namespace

TEST(TrackInputMonitoringTest, AutoArmedAndStoppedHearsTheTracksOwnInputOnBothSides) {
    MonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.setInput(2, 1);
    rig.set(synth::MonitorMode::Auto, true);
    const auto out = rig.render();
    EXPECT_TRUE(outputIsChannel(out, 0, 2)) << "In 3 (mono) on the left";
    EXPECT_TRUE(outputIsChannel(out, 1, 2)) << "and on the right";
}

TEST(TrackInputMonitoringTest, AStereoPairKeepsItsSides) {
    MonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.setInput(2, 2);
    rig.set(synth::MonitorMode::On, false);
    const auto out = rig.render();
    EXPECT_TRUE(outputIsChannel(out, 0, 2));
    EXPECT_TRUE(outputIsChannel(out, 1, 3));
}

TEST(TrackInputMonitoringTest, AutoFollowsArmPlaybackAndRecording) {
    MonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.setInput(0, 1);
    rig.set(synth::MonitorMode::Auto, false);
    EXPECT_TRUE(outputIsSilent(rig.render())) << "unarmed";

    rig.set(synth::MonitorMode::Auto, true);
    ASSERT_TRUE(rig.engine.getTransport().play());
    EXPECT_TRUE(outputIsSilent(rig.render())) << "armed but plain playback: the clips play, not the input";

    rig.engine.setRecordingEngaged(true);
    EXPECT_TRUE(outputIsChannel(rig.render(), 0, 0)) << "Record engaged: the take is heard as it is made";
    rig.engine.setRecordingEngaged(false);
    EXPECT_TRUE(outputIsSilent(rig.render()));
}

TEST(TrackInputMonitoringTest, OnAndOffIgnoreTheArmAndTheTransport) {
    MonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.setInput(1, 1);
    rig.set(synth::MonitorMode::Off, true);
    EXPECT_TRUE(outputIsSilent(rig.render()));
    rig.set(synth::MonitorMode::On, false);
    ASSERT_TRUE(rig.engine.getTransport().play());
    EXPECT_TRUE(outputIsChannel(rig.render(), 0, 1));
}

TEST(TrackInputMonitoringTest, NoInputOrAMutedTrackHearsNothing) {
    MonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.set(synth::MonitorMode::On, true);
    EXPECT_TRUE(outputIsSilent(rig.render())) << "No input";
    rig.setInput(0, 1);
    ASSERT_TRUE(rig.doc.setTrackMuted(rig.track, true));
    rig.engine.publishTimeline(rig.doc);
    EXPECT_TRUE(outputIsSilent(rig.render())) << "muted";
}

TEST(TrackInputMonitoringTest, SustainedClippingMutesTheTrackReportsOnceAndReArmingClearsIt) {
    MonitorRig rig;
    ASSERT_TRUE(rig.ready);
    rig.setInput(0, 1);
    rig.set(synth::MonitorMode::Auto, true);

    // 0.25 s at 48 kHz is 12000 samples: 24 blocks of 512 at full scale reach it.
    const int blocksToTrip = (int)std::ceil(synth::kMonitorGuardSustainSeconds * kRate / kBlock);
    for (int block = 0; block < blocksToTrip - 1; ++block)
        EXPECT_FALSE(outputIsSilent(rig.render(1.0f))) << "not yet sustained, block " << block;
    EXPECT_TRUE(outputIsSilent(rig.render(1.0f))) << "the tripping block is not played";
    EXPECT_TRUE(outputIsSilent(rig.render())) << "latched: quiet input stays muted";

    std::vector<std::int64_t> reported;
    rig.engine.consumeMonitorGuardTrips([&](std::int64_t id) { reported.push_back(id); });
    EXPECT_EQ(reported, (std::vector<std::int64_t>{(std::int64_t)rig.track.value}));
    for (int block = 0; block < 4; ++block)
        rig.render(1.0f);
    reported.clear();
    rig.engine.consumeMonitorGuardTrips([&](std::int64_t id) { reported.push_back(id); });
    EXPECT_TRUE(reported.empty()) << "a latched track is not reported again";

    rig.set(synth::MonitorMode::Auto, false);
    rig.render();
    rig.set(synth::MonitorMode::Auto, true);
    EXPECT_TRUE(outputIsChannel(rig.render(), 0, 0)) << "re-arming clears the latch";
}

TEST(TrackInputMonitoringTest, TheAudioInputModulePassesInputOnlyWhileWired) {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(2, 2, kRate, kBlock);
    auto in = graph.addNode(std::make_unique<AudioInputModule>());
    auto out = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    engine.prepareForHost(kRate, kBlock, 2, 2);

    engine.refreshAudioInputGate();
    EXPECT_FALSE(engine.isInputMonitoringEnabled()) << "an unwired Audio Input keeps the mic out of the speakers";
    graph.addConnection({{in->nodeID, 0}, {out->nodeID, 0}});
    engine.refreshAudioInputGate();
    EXPECT_TRUE(engine.isInputMonitoringEnabled());
    graph.removeConnection({{in->nodeID, 0}, {out->nodeID, 0}});
    engine.refreshAudioInputGate();
    EXPECT_FALSE(engine.isInputMonitoringEnabled());
    engine.releaseFromHost();
}
