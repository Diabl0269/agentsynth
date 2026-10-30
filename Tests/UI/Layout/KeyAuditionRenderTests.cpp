// Regression test for FRO1235: routed key pickers must be audible.
// Real renders (the engine's own device callback on a fake device, so the graph, the module and the output node all
// run): the instrument-track route the piano roll's Key picker and
// the Song key's chosen track share (Track In -> Pluck). Each says what is heard while a note is on and that silence
// comes back after the off. Skipped on purpose: the hover delay of the picker (KeyAuditionSoundTests covers it) and the
// 140 ms step timer, which the test seam advances.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "AudioEngine/AudioEngine.h"
#include "KeyAuditionRenderRig.h"
#include "Music/SongKey.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Timeline/SongKeyField.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>

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
