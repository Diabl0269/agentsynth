// Regression test for FRO1235: routed key pickers must be audible.
// Real renders (the engine's own device callback on a fake device, so the graph, the module and the output node all
// run): the instrument-track route the piano roll's Key picker and
// the Song key's chosen track share (Track In -> Pluck). Each says what is heard while a note is on and that silence
// comes back after the off. Skipped on purpose: the hover delay of the picker (KeyAuditionSoundTests covers it) and the
// 140 ms step timer, which the test seam advances.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/AuditionMonitor.h"
#include "AudioEngine/ConnectionIndex.h"
#include "KeyAuditionRenderRig.h"
#include "Modules/ChannelStripModule.h"
#include "Music/SongKey.h"
#include "Transport/TransportService.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Timeline/SongKeyField.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>
#include <set>

using keyaudition_test::kMajor;
using keyaudition_test::kQuiet;
using keyaudition_test::Render;

TEST_F(MainComponentTest, ASongKeyTracksAndAPianoRollClipsScaleAreHeardThroughTheTracksPluck) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& panel = mc.getTimelinePanel();
    panel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluckMenuId);
    const auto& tracks = mc.getTimelineDoc().getTracks();
    ASSERT_FALSE(tracks.empty());
    const auto track = tracks.back().id;
    ASSERT_TRUE(tracks.back().bindingUuid.isNotEmpty());
    const auto clip = mc.getTimelineDoc().addClip(track, 0.0, 4.0, "Riff");

    Render render(mc.getAudioEngine());
    EXPECT_LT(render.peak(20), kQuiet) << "silent before anything is hovered";

    // Song key: the picker's Preview sound chose this track.
    auto& songKey = panel.getTransportBar().getSongKeyField();
    songKey.setPreviewTrack(track);
    songKey.startAudition(kMajor);
    ASSERT_TRUE(songKey.isAuditioning());
    EXPECT_FALSE(songKey.getAuditioner().isUsingDefaultSound()) << "the chosen track, not the default sound";
    const float songPeak = render.peak(30);
    RecordProperty("song_key_track_peak", std::to_string(songPeak));
    EXPECT_GT(songPeak, 0.001f);
    songKey.stopAudition();
    EXPECT_GE(render.blocksUntilQuiet(kQuiet, 1500), 0) << "silent again after the note-off";
    EXPECT_LT(render.peak(20), kQuiet);

    // Piano roll: its Key picker plays through the open clip's track.
    panel.openPianoRoll(clip);
    ASSERT_TRUE(panel.getPianoRoll().isOpen());
    auto& rollAuditioner = panel.getPianoRoll().getScaleAssistPanel().getAuditioner();
    rollAuditioner.start(kMajor);
    ASSERT_TRUE(rollAuditioner.isAuditioning());
    EXPECT_FALSE(rollAuditioner.isUsingDefaultSound());
    const float rollPeak = render.peak(30);
    RecordProperty("piano_roll_track_peak", std::to_string(rollPeak));
    EXPECT_GT(rollPeak, 0.001f);
    rollAuditioner.stop();
    EXPECT_GE(render.blocksUntilQuiet(kQuiet, 1500), 0) << "silent again after the note-off";
    EXPECT_LT(render.peak(20), kQuiet);
}

namespace {

// The channel strip a track's instrument plays into: the first ChannelStrip reachable by following cables from the
// track's source node.
ChannelStripModule* stripOfTrack(MainComponent& mc, synth::TrackId track, juce::AudioProcessorGraph::NodeID* idOut) {
    const auto* entry = mc.getTimelineDoc().getTrack(track);
    if (entry == nullptr)
        return nullptr;
    auto& graph = mc.getAudioEngine().getGraph();
    const synth::ConnectionIndex index(graph);
    std::vector<juce::AudioProcessorGraph::NodeID> pending;
    std::set<juce::AudioProcessorGraph::NodeID> seen;
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == entry->bindingUuid)
            pending.push_back(node->nodeID);
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!seen.insert(id).second)
            continue;
        if (auto* node = graph.getNodeForId(id))
            if (auto* strip = dynamic_cast<ChannelStripModule*>(node->getProcessor())) {
                if (idOut != nullptr)
                    *idOut = id;
                return strip;
            }
        for (const auto& connection : index.outOf(id))
            pending.push_back(connection.destination.nodeID);
    }
    return nullptr;
}

} // namespace

// Regression test for FRO1410: the scale was silent when the chosen sound was muted or another sound was soloed,
// because the notes went through the same mute and solo gates as playback. Rendered through the engine's real device
// callback with the strips muted / soloed for real; the first version of this guard only ever used an unmuted track.
TEST_F(MainComponentTest, AScaleIsHeardThroughAMutedSoundAndWhileAnotherSoundIsSoloedAndMuteComesBack) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& panel = mc.getTimelinePanel();
    panel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluckMenuId);
    panel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluckMenuId);
    panel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kCreateChannelsMenuId);
    const auto& tracks = mc.getTimelineDoc().getTracks();
    ASSERT_GE(tracks.size(), 2u);
    const auto target = tracks[tracks.size() - 2].id;
    const auto other = tracks[tracks.size() - 1].id;
    juce::AudioProcessorGraph::NodeID targetStripId, otherStripId;
    auto* targetStrip = stripOfTrack(mc, target, &targetStripId);
    auto* otherStrip = stripOfTrack(mc, other, &otherStripId);
    ASSERT_NE(targetStrip, nullptr);
    ASSERT_NE(otherStrip, nullptr);
    ASSERT_NE(targetStrip, otherStrip);

    auto& engine = mc.getAudioEngine();
    auto& songKey = panel.getTransportBar().getSongKeyField();
    songKey.setPreviewTrack(target);
    Render render(engine);

    const auto heard = [&] {
        songKey.startAudition(kMajor);
        EXPECT_TRUE(songKey.isAuditioning());
        const float peak = render.peakAfter(10, 20);
        songKey.stopAudition();
        return peak;
    };
    const auto settle = [&] {
        engine.getAuditionMonitor().clear(); // the release tail is over
        EXPECT_GE(render.blocksUntilQuiet(kQuiet, 3000), 0);
    };

    // Muted target.
    targetStrip->setMuted(true);
    const float mutedPeak = heard();
    RecordProperty("muted_target_peak", std::to_string(mutedPeak));
    EXPECT_GT(mutedPeak, 0.001f) << "a muted sound still plays the scale";
    EXPECT_TRUE(engine.getAuditionMonitor().isActive()) << "the release tail keeps the bypass on after the last note";
    settle();
    EXPECT_TRUE(targetStrip->isMuted()) << "the mixer still shows the real mute";
    EXPECT_LT(render.peak(20), kQuiet);
    targetStrip->setMuted(false);

    // Solo elsewhere.
    ASSERT_TRUE(engine.setChannelStripSoloed(otherStripId, true));
    const float soloPeak = heard();
    RecordProperty("solo_elsewhere_peak", std::to_string(soloPeak));
    EXPECT_GT(soloPeak, 0.001f) << "another sound on solo does not silence the scale";
    settle();
    EXPECT_TRUE(otherStrip->isSoloed());
    ASSERT_TRUE(engine.setChannelStripSoloed(otherStripId, false));

    // The tail does not leak into the song: audition a muted target, press Play, and the target is silent again.
    targetStrip->setMuted(true);
    songKey.startAudition(kMajor);
    ASSERT_GT(render.peakAfter(10, 10), 0.001f);
    ASSERT_TRUE(engine.getTransport().play());
    render.block();
    engine.getAuditionMonitor().pollForTest();
    EXPECT_FALSE(engine.getAuditionMonitor().isActive()) << "playback drops the bypass at once";
    songKey.stopAudition();
    engine.getTransport().stop();
    render.block();
    EXPECT_GE(render.blocksUntilQuiet(kQuiet, 3000), 0)
        << "the muted target is silent again, not carried into playback";
    targetStrip->setMuted(false);

    // Nothing was left bypassed: with the target muted again and no audition, it is silent.
    targetStrip->setMuted(true);
    EXPECT_LT(render.peak(30), kQuiet);
    targetStrip->setMuted(false);
}

// Regression test for FRO1410: while the transport plays the scale is held back, so it never sounds on top of the song.
TEST_F(MainComponentTest, NoScaleIsPlayedWhileTheTransportIsPlaying) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& panel = mc.getTimelinePanel();
    panel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluckMenuId);
    const auto track = mc.getTimelineDoc().getTracks().back().id;
    auto& songKey = panel.getTransportBar().getSongKeyField();
    songKey.setPreviewTrack(track);
    Render render(mc.getAudioEngine());

    ASSERT_TRUE(mc.getAudioEngine().getTransport().play());
    render.block();
    ASSERT_TRUE(mc.getAudioEngine().getTransport().getPositionSnapshot().playing);
    songKey.startAudition(kMajor);
    EXPECT_FALSE(songKey.isAuditioning());
    EXPECT_LT(render.peak(30), kQuiet);

    mc.getAudioEngine().getTransport().stop();
    render.block();
    songKey.startAudition(kMajor);
    EXPECT_TRUE(songKey.isAuditioning()) << "plays again once the transport has stopped";
    songKey.stopAudition();
}
